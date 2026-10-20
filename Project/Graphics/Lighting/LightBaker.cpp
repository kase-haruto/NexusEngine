#include "LightBaker.h"
#include "Graphics/Color/ColorSpace.h"
#include "Graphics/Model/ModelInstance.h"
#include "Graphics/Renderer/RenderScene.h"
#include "Graphics/Resource/HdrImage.h"
#include "Graphics/Resource/WicImageDecoder.h"
#include "LightmapGeometry.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <fstream>
#include <numeric>
#include <thread>
namespace NexusEngine {
	namespace {
		using V			   = Vector3;
		constexpr float pi = 3.14159265358979323846f, bias = .0001f;
		V				Product(V a, V b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
		float			Axis(V a, uint32_t i) { return i == 0 ? a.x : i == 1 ? a.y
																			 : a.z; }
		V				From(const float* p) { return {p[0], p[1], p[2]}; }
		V				Transform(V p, const Matrix4x4& m, float w) {
			return {p.x * m.At(0, 0) + p.y * m.At(1, 0) + p.z * m.At(2, 0) + w * m.At(3, 0),
					p.x * m.At(0, 1) + p.y * m.At(1, 1) + p.z * m.At(2, 1) + w * m.At(3, 1),
					p.x * m.At(0, 2) + p.y * m.At(1, 2) + p.z * m.At(2, 2) + w * m.At(3, 2)};
		}
		uint64_t ObjectKey(const PrimitiveRenderItem& item) { return item.lightingId ? item.lightingId : item.sourceObject.value; }
		struct Triangle {
			std::array<ModelVertex, 3> vertices;
			std::array<V, 3>		   p, n;
			V						   geometricNormal;
			uint32_t				   material = 0;
			std::array<float, 4>	   tint{1, 1, 1, 1};
		};
		struct Node {
			V		 lo, hi;
			uint32_t first = 0, count = 0, left = 0, right = 0;
		};
		struct Hit {
			uint32_t triangle = UINT32_MAX;
			float	 t = 10000, u = 0, v = 0;
		};
		/* CPU専用BVH。三角形本体の順序を維持し、ライトマップchartと対応させる。 */
		struct Geometry {
			std::vector<Triangle> triangles;
			std::vector<uint32_t> order;
			std::vector<Node>	  nodes;
			uint32_t			  Build(uint32_t first, uint32_t count) {
				Node node;
				node.first = first;
				node.count = count;
				node.lo	   = {1e30f, 1e30f, 1e30f};
				node.hi	   = {-1e30f, -1e30f, -1e30f};
				for(uint32_t i = first; i < first + count; ++i)
					for(V p : triangles[order[i]].p) {
						node.lo = {std::min(node.lo.x, p.x), std::min(node.lo.y, p.y), std::min(node.lo.z, p.z)};
						node.hi = {std::max(node.hi.x, p.x), std::max(node.hi.y, p.y), std::max(node.hi.z, p.z)};
					}
				uint32_t index = static_cast<uint32_t>(nodes.size());
				nodes.push_back(node);
				if(count > 8) {
					V		 extent = node.hi - node.lo;
					uint32_t axis	= extent.x > extent.y ? (extent.x > extent.z ? 0U : 2U) : (extent.y > extent.z ? 1U : 2U);
					uint32_t mid	= first + count / 2;
					std::nth_element(order.begin() + first, order.begin() + mid, order.begin() + first + count, [&](uint32_t a, uint32_t b) {
						const auto& ta = triangles[a];
						const auto& tb = triangles[b];
						return Axis(ta.p[0] + ta.p[1] + ta.p[2], axis) < Axis(tb.p[0] + tb.p[1] + tb.p[2], axis);
					});
					nodes[index].count = 0;
					uint32_t left = Build(first, mid - first), right = Build(mid, first + count - mid);
					nodes[index].left  = left;
					nodes[index].right = right;
				}
				return index;
			}
			Hit Trace(V origin, V direction, float maximum = 10000) const {
				Hit hit;
				hit.t = maximum;
				if(nodes.empty()) return hit;
				std::array<uint32_t, 64> stack{};
				uint32_t				 size = 1;
				while(size) {
					const auto& node  = nodes[stack[--size]];
					float		nearT = 0, farT = hit.t;
					for(uint32_t a = 0; a < 3; ++a) {
						float d = Axis(direction, a), o = Axis(origin, a), low = Axis(node.lo, a), high = Axis(node.hi, a);
						if(std::abs(d) < 1e-12f) {
							if(o < low || o > high) {
								farT = -1;
								break;
							}
						} else {
							float t0 = (low - o) / d, t1 = (high - o) / d;
							if(t0 > t1) std::swap(t0, t1);
							nearT = std::max(nearT, t0);
							farT  = std::min(farT, t1);
						}
					}
					if(nearT > farT) continue;
					if(!node.count) {
						stack[size++] = node.left;
						stack[size++] = node.right;
						continue;
					}
					for(uint32_t i = node.first; i < node.first + node.count; ++i) {
						uint32_t	id	= order[i];
						const auto& tri = triangles[id];
						V			e1 = tri.p[1] - tri.p[0], e2 = tri.p[2] - tri.p[0];
						V			cross		= V::Cross(direction, e2);
						float		determinant = V::Dot(e1, cross);
						if(std::abs(determinant) < 1e-10f) continue;
						V	  delta = origin - tri.p[0];
						float u		= V::Dot(delta, cross) / determinant;
						if(u < 0 || u > 1) continue;
						V	  q = V::Cross(delta, e1);
						float v = V::Dot(direction, q) / determinant;
						if(v < 0 || u + v > 1) continue;
						float t = V::Dot(e2, q) / determinant;
						if(t > 0 && t < hit.t) hit = {id, t, u, v};
					}
				}
				return hit;
			}
		};
		struct Material {
			const ModelMaterialAssetData* source = nullptr;
			DecodedImage				  base, emission, mr, ao;
		};
		float Address(float value, TextureAddressMode mode) {
			if(mode == TextureAddressMode::Clamp) return std::clamp(value, 0.0f, 1.0f);
			if(mode == TextureAddressMode::Mirror) {
				value = std::fmod(std::abs(value), 2.0f);
				return value > 1 ? 2 - value : value;
			}
			return value - std::floor(value);
		}
		V Sample(const DecodedImage& image, float u, float v, const SamplerDesc& sampler, bool color) {
			if(image.rgbaPixels.empty()) return V::One();
			u		= Address(u, sampler.addressU);
			v		= Address(v, sampler.addressV);
			float x = u * image.width - .5f, y = v * image.height - .5f;
			auto  texel = [&](int ix, int iy) {
				auto coordinate = [](int i, uint32_t n, TextureAddressMode mode) {
					if(mode == TextureAddressMode::Clamp) return std::clamp(i, 0, static_cast<int>(n) - 1);
					int size = static_cast<int>(n), period = mode == TextureAddressMode::Mirror ? size * 2 : size;
					int p = (i % period + period) % period;
					return p >= size ? period - 1 - p : p;
				};
				size_t offset = (size_t(coordinate(iy, image.height, sampler.addressV)) * image.width + coordinate(ix, image.width, sampler.addressU)) * 4;
				V	   c{image.rgbaPixels[offset] / 255.0f, image.rgbaPixels[offset + 1] / 255.0f, image.rgbaPixels[offset + 2] / 255.0f};
				return color ? V{ColorSpace::SrgbToLinear(c.x), ColorSpace::SrgbToLinear(c.y), ColorSpace::SrgbToLinear(c.z)} : c;
			};
			if(sampler.filter == TextureFilter::Nearest) return texel(static_cast<int>(std::floor(x + .5f)), static_cast<int>(std::floor(y + .5f)));
			int	  ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
			float tx = x - std::floor(x), ty = y - std::floor(y);
			return (texel(ix, iy) * (1 - tx) + texel(ix + 1, iy) * tx) * (1 - ty) + (texel(ix, iy + 1) * (1 - tx) + texel(ix + 1, iy + 1) * tx) * ty;
		}
		float Radical(uint32_t bits) {
			bits = (bits << 16) | (bits >> 16);
			bits = ((bits & 0x55555555u) << 1) | ((bits & 0xaaaaaaaau) >> 1);
			bits = ((bits & 0x33333333u) << 2) | ((bits & 0xccccccccu) >> 2);
			bits = ((bits & 0x0f0f0f0fu) << 4) | ((bits & 0xf0f0f0f0u) >> 4);
			bits = ((bits & 0x00ff00ffu) << 8) | ((bits & 0xff00ff00u) >> 8);
			return bits * 2.3283064365386963e-10f;
		}
		V Cosine(uint32_t i, uint32_t count, uint32_t bounce, V n) {
			float a = (float(i) + .5f) / count, b = Radical(i);
			if(bounce > 0) {
				// 各反射で独立したHalton次元を使う。同じ乱数対の反復による積分の偏りを防ぐ。
				constexpr uint32_t primes[16] = {3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59};
				auto			   radical	  = [&](uint32_t base) { uint32_t value=i+1;float sum=0,weight=1.0f/base;while(value) {sum+=(value%base)*weight;value/=base;weight/=base;}return sum; };
				a							  = radical(primes[(bounce - 1) * 2]);
				b							  = radical(primes[(bounce - 1) * 2 + 1]);
			}
			float r = std::sqrt(a), phi = 2 * pi * b;
			V	  tangent = V::Cross(std::abs(n.z) < .999f ? V{0, 0, 1} : V{1, 0, 0}, n).Normalized();
			return tangent * (r * std::cos(phi)) + V::Cross(n, tangent) * (r * std::sin(phi)) + n * std::sqrt(1 - a);
		}
		std::array<float, 9> SH(V d) {
			return {.2820947918f, .4886025119f * d.y, .4886025119f * d.z, .4886025119f * d.x,
					1.0925484306f * d.x * d.y, 1.0925484306f * d.y * d.z, .3153915653f * (3 * d.z * d.z - 1), 1.0925484306f * d.x * d.z, .5462742153f * (d.x * d.x - d.y * d.y)};
		}
		V CubeDirection(uint32_t face, float x, float y) {
			switch(face) {
			case 0:
				return V{1, -y, -x}.Normalized();
			case 1:
				return V{-1, -y, x}.Normalized();
			case 2:
				return V{x, 1, y}.Normalized();
			case 3:
				return V{x, -1, -y}.Normalized();
			case 4:
				return V{x, -y, 1}.Normalized();
			default:
				return V{-x, -y, -1}.Normalized();
			}
		}
		struct Integrator {
			Geometry					 geometry;
			std::vector<Material>		 materials;
			HdrImage					 environment;
			const RenderScene*			 scene	  = nullptr;
			const BakedLightingSettings* settings = nullptr;
			V							 Environment(V d) const {
				const auto& e = scene->environment;
				if(!environment.pixels.empty() && e.enabled) {
					float s = std::sin(e.rotation), c = std::cos(e.rotation);
					d		   = {c * d.x - s * d.z, d.y, s * d.x + c * d.z};
					float	 u = std::atan2(d.z, d.x) / (2 * pi) + .5f, v = std::acos(std::clamp(d.y, -1.0f, 1.0f)) / pi;
					uint32_t x = std::min(static_cast<uint32_t>(u * environment.width), environment.width - 1), y = std::min(static_cast<uint32_t>(v * environment.height), environment.height - 1);
					return From(environment.pixels[size_t(y) * environment.width + x].data()) * e.intensity;
				}
				return From(scene->ambientIrradiance.data()) / pi;
			}
			V Radiance(V origin, V direction, uint32_t sample) const {
				V sum{}, throughput = V::One();
				for(uint32_t bounce = 0; bounce < settings->maxBounces; ++bounce) {
					Hit hit = geometry.Trace(origin, direction);
					if(hit.triangle == UINT32_MAX) return sum + Product(throughput, Environment(direction));
					const auto& tri		   = geometry.triangles[hit.triangle];
					const auto& material   = materials[tri.material];
					const auto& source	   = *material.source;
					float		weights[3] = {1 - hit.u - hit.v, hit.u, hit.v};
					float		u = 0, v = 0;
					V			color{}, normal{};
					for(size_t k = 0; k < 3; ++k) {
						u += tri.vertices[k].texcoord[0] * weights[k];
						v += tri.vertices[k].texcoord[1] * weights[k];
						color  = color + From(tri.vertices[k].color) * weights[k];
						normal = normal + tri.n[k] * weights[k];
					}
					color		   = Product(Product(Product(color, From(source.baseColorFactor.data())), From(tri.tint.data())), Sample(material.base, u, v, source.sampler, true));
					color		   = {std::clamp(color.x, 0.0f, 1.0f), std::clamp(color.y, 0.0f, 1.0f), std::clamp(color.z, 0.0f, 1.0f)};
					float metal	   = std::clamp(source.metallicFactor * Sample(material.mr, u, v, source.metallicRoughnessSampler, false).z, 0.0f, 1.0f);
					float ao	   = 1 - source.occlusionStrength * (1 - Sample(material.ao, u, v, source.occlusionSampler, false).x);
					V	  emission = Product(From(source.emissiveFactor.data()), Sample(material.emission, u, v, source.emissiveSampler, true));
					normal		   = normal.Normalized();
					V ng		   = tri.geometricNormal;
					if(V::Dot(ng, direction) > 0) {
						ng	   = ng * -1;
						normal = normal * -1;
					}
					if(V::Dot(normal, ng) < 0) normal = ng;
					V			p		 = origin + direction * hit.t;
					float		diffuse	 = (1 - metal) * (1 - source.optics.reflectivity) * (1 - source.optics.transmission) * .96f;
					V			incoming = emission;
					const auto& light	 = scene->directionalLight;
					V			l		 = From(light.direction.data()).Normalized();
					float		nl		 = std::max(V::Dot(normal, l), 0.0f);
					if(light.intensity > 0 && nl > 0 && V::Dot(ng, l) > 0 && geometry.Trace(p + ng * bias, l).triangle == UINT32_MAX)
						incoming = incoming + Product(color, From(light.color.data())) * (diffuse * nl * light.intensity / pi);
					sum	   = sum + Product(throughput, incoming);
					V next = Cosine(sample, settings->samples, bounce + 1, normal);
					if(V::Dot(ng, next) <= 0) break;
					throughput = Product(throughput, color) * (diffuse * ao);
					if(std::max({throughput.x, throughput.y, throughput.z}) < 1e-5f) break;
					origin	  = p + ng * bias;
					direction = next;
				}
				return sum;
			}
			std::array<float, 4> Irradiance(V p, V n, V ng) const {
				V	  sum{};
				float visibility = 0;
				for(uint32_t i = 0; i < settings->samples; ++i) {
					V direction = Cosine(i, settings->samples, 0, n);
					if(V::Dot(ng, direction) <= 0) continue;
					V origin = p + ng * bias;
					if(geometry.Trace(origin, direction).triangle == UINT32_MAX) visibility += 1;
					sum = sum + Radiance(origin, direction, i);
				}
				sum = sum / static_cast<float>(settings->samples);
				return {sum.x, sum.y, sum.z, visibility / settings->samples};
			}
		};
		template <class Function>
		void Parallel(uint32_t count, Function function) {
			std::atomic<uint32_t>	  next{0};
			std::vector<std::jthread> workers;
			uint32_t				  threads = std::min(8U, std::max(1U, std::thread::hardware_concurrency()));
			for(uint32_t t = 0; t < threads; ++t) workers.emplace_back([&] { for(;;) { auto i=next.fetch_add(1,std::memory_order_relaxed); if(i>=count) break; function(i); } });
		}
	} // namespace
	uint64_t LightBaker::Signature(const ModelAssetData& asset, const ModelInstance& pose, const RenderScene& scene) {
		uint64_t hash  = 1469598103934665603ull;
		auto	 add   = [&](const auto& v) { for(auto b:std::as_bytes(std::span(&v,1))) { hash^=std::to_integer<uint8_t>(b); hash*=1099511628211ull; } };
		auto	 bytes = [&](const auto& v) { for(auto b:v) { hash^=b; hash*=1099511628211ull; } };
		for(const auto& mesh : asset.meshes) {
			bytes(mesh.vertexData);
			bytes(mesh.indexData);
			add(mesh.skinIndex);
			for(const auto& s : mesh.submeshes) add(s);
		}
		for(const auto& m : asset.materials) {
			add(m.baseColorFactor);
			add(m.emissiveFactor);
			add(m.metallicFactor);
			add(m.roughnessFactor);
			add(m.occlusionStrength);
			add(m.optics.reflectivity);
			add(m.optics.transmission);
			for(const auto* s : {&m.sampler, &m.emissiveSampler, &m.metallicRoughnessSampler, &m.occlusionSampler}) {
				add(s->filter);
				add(s->addressU);
				add(s->addressV);
				add(s->addressW);
			}
			bytes(m.baseColorImageData);
			bytes(m.emissiveImageData);
			bytes(m.metallicRoughnessImageData);
			bytes(m.occlusionImageData);
		}
		for(const auto& item : scene.primitives)
			if(item.staticLighting) {
				auto id = ObjectKey(item);
				add(id);
				add(item.worldMatrix);
				add(item.tint);
				add(item.nodeIndex);
			}
		for(const auto& item : scene.primitives)
			if(item.staticLighting)
				for(uint32_t node = 0; node < asset.nodes.size(); ++node) {
					if(item.nodeIndex != PrimitiveRenderItem::kAllNodes && item.nodeIndex != node) continue;
					bool hasStaticMesh = false;
					for(uint32_t mesh : asset.nodes[node].meshIndices)
						if(mesh < asset.meshes.size() && asset.meshes[mesh].skinIndex == MeshAssetData::kNoSkin) hasStaticMesh = true;
					if(hasStaticMesh) {
						add(node);
						add(pose.GetNodeWorldTransforms()[node]);
						for(uint32_t mesh : asset.nodes[node].meshIndices) add(mesh);
					}
				}
		add(scene.directionalLight.direction);
		add(scene.directionalLight.color);
		add(scene.directionalLight.intensity);
		add(scene.ambientIrradiance);
		add(scene.environment.enabled);
		add(scene.environment.intensity);
		add(scene.environment.rotation);
		auto path = scene.environment.imagePath.u8string();
		for(auto c : path) {
			hash ^= static_cast<uint8_t>(c);
			hash *= 1099511628211ull;
		}
		// HDRの同名差し替えも検知する。照合は読み込み要求時だけで毎フレームは行わない。
		std::ifstream environment(scene.environment.imagePath, std::ios::binary);
		char		  buffer[4096];
		while(environment.read(buffer, sizeof(buffer)) || environment.gcount())
			for(std::streamsize i = 0; i < environment.gcount(); ++i) {
				hash ^= static_cast<uint8_t>(buffer[i]);
				hash *= 1099511628211ull;
			}
		return hash;
	}
	Result<BakedLightingAsset> LightBaker::Bake(const ModelAssetData& asset, const ModelInstance& pose, const RenderScene& scene, const BakedLightingSettings& settings) const {
		auto fail = [](const char* message) -> Result<BakedLightingAsset> { return std::unexpected(Error(ErrorCategory::Resource, 91, message)); };
		if(!settings.IsValid() || pose.GetAsset() != &asset) return fail("Invalid light bake settings/model pose.");
		Integrator integrator;
		integrator.scene	= &scene;
		integrator.settings = &settings;
		if(scene.environment.enabled && !scene.environment.imagePath.empty()) {
			auto image = LoadHdrImage(scene.environment.imagePath);
			if(!image) return std::unexpected(std::move(image.error()));
			integrator.environment = std::move(*image);
		}
		integrator.materials.resize(asset.materials.size());
		for(size_t i = 0; i < asset.materials.size(); ++i) {
			auto& m		= integrator.materials[i];
			m.source	= &asset.materials[i];
			auto decode = [&](const std::vector<uint8_t>& data, DecodedImage& target) -> Result<void> { if(data.empty()) return {}; auto result=WicImageDecoder{}.Decode(data); if(!result) return std::unexpected(std::move(result.error())); target=std::move(*result); return {}; };
			for(auto pair : {std::pair{&m.source->baseColorImageData, &m.base}, std::pair{&m.source->emissiveImageData, &m.emission}, std::pair{&m.source->metallicRoughnessImageData, &m.mr}, std::pair{&m.source->occlusionImageData, &m.ao}}) {
				auto status = decode(*pair.first, *pair.second);
				if(!status) return std::unexpected(std::move(status.error()));
			}
		}
		struct Rect {
			uint32_t first, count, width, height, x = 0, y = 0, binding;
		};
		std::vector<Rect>  rectangles;
		BakedLightingAsset result;
		result.signature	   = Signature(asset, pose, scene);
		const auto& transforms = pose.GetNodeWorldTransforms();
		for(const auto& item : scene.primitives)
			if(item.staticLighting)
				for(uint32_t node = 0; node < asset.nodes.size(); ++node) {
					if(item.nodeIndex != PrimitiveRenderItem::kAllNodes && item.nodeIndex != node) continue;
					auto world	 = Multiply(transforms[node], item.worldMatrix);
					auto inverse = TryInverse(world);
					if(!inverse) return fail("Singular static bake transform.");
					auto normalMatrix = Transpose(*inverse);
					for(uint32_t meshIndex : asset.nodes[node].meshIndices) {
						if(meshIndex >= asset.meshes.size()) return fail("Invalid static bake mesh index.");
						const auto& mesh = asset.meshes[meshIndex];
						if(mesh.skinIndex != MeshAssetData::kNoSkin) continue;
						if(mesh.vertexStride != sizeof(ModelVertex)) return fail("Light bake requires ModelVertex.");
						uint32_t first = static_cast<uint32_t>(integrator.geometry.triangles.size()), indexSize = mesh.indexFormat == IndexFormat::UInt16 ? 2 : 4;
						for(const auto& sub : mesh.submeshes) {
							if(sub.materialSlot >= asset.materials.size() || sub.indexCount % 3) return fail("Invalid static bake submesh.");
							for(uint32_t i = 0; i < sub.indexCount; i += 3) {
								Triangle tri;
								tri.material = sub.materialSlot;
								tri.tint	 = item.tint;
								for(uint32_t k = 0; k < 3; ++k) {
									uint32_t index	 = 0;
									size_t	 address = size_t(sub.firstIndex + i + k) * indexSize;
									if(address + indexSize > mesh.indexData.size()) return fail("Bake index range invalid.");
									std::memcpy(&index, mesh.indexData.data() + address, indexSize);
									int64_t v = int64_t(index) + sub.vertexOffset;
									if(v < 0 || uint64_t(v + 1) * sizeof(ModelVertex) > mesh.vertexData.size()) return fail("Bake vertex range invalid.");
									std::memcpy(&tri.vertices[k], mesh.vertexData.data() + size_t(v) * sizeof(ModelVertex), sizeof(ModelVertex));
									tri.p[k] = Transform(From(tri.vertices[k].position), world, 1);
									tri.n[k] = Transform(From(tri.vertices[k].normal), normalMatrix, 0).Normalized();
								}
								tri.geometricNormal = V::Cross(tri.p[1] - tri.p[0], tri.p[2] - tri.p[0]).Normalized();
								if(V::Dot(tri.geometricNormal, tri.n[0]) < 0) tri.geometricNormal = tri.geometricNormal * -1;
								integrator.geometry.triangles.push_back(tri);
							}
						}
						uint32_t count = static_cast<uint32_t>(integrator.geometry.triangles.size()) - first;
						if(!count) continue;
						auto	 grid	 = LightmapChartGrid(count);
						uint32_t binding = static_cast<uint32_t>(result.bindings.size());
						result.bindings.push_back({ObjectKey(item), node, meshIndex, world, {}});
						rectangles.push_back({first, count, grid[0] * settings.chartSize, grid[1] * settings.chartSize, 0, 0, binding});
					}
				}
		if(integrator.geometry.triangles.empty()) return fail("No static geometry to bake.");
		if(integrator.geometry.triangles.size() > 2000000) return fail("Static bake triangle budget exceeded.");
		integrator.geometry.order.resize(integrator.geometry.triangles.size());
		std::iota(integrator.geometry.order.begin(), integrator.geometry.order.end(), 0U);
		integrator.geometry.nodes.reserve(integrator.geometry.triangles.size() * 2);
		integrator.geometry.Build(0, static_cast<uint32_t>(integrator.geometry.triangles.size()));
		std::stable_sort(rectangles.begin(), rectangles.end(), [](const auto& a, const auto& b) { return a.height > b.height; });
		for(result.atlasSize = 32; result.atlasSize <= 4096; result.atlasSize *= 2) {
			uint32_t x = 0, y = 0, row = 0;
			bool	 fits = true;
			for(auto& rect : rectangles) {
				if(rect.width > result.atlasSize) {
					fits = false;
					break;
				}
				if(x + rect.width > result.atlasSize) {
					x = 0;
					y += row;
					row = 0;
				}
				if(y + rect.height > result.atlasSize) {
					fits = false;
					break;
				}
				rect.x = x;
				rect.y = y;
				x += rect.width;
				row = std::max(row, rect.height);
			}
			if(fits) break;
		}
		if(result.atlasSize > 4096) return fail("Lightmap atlas exceeds 4096; reduce chart size or split scene.");
		result.lightmap.resize(size_t(result.atlasSize) * result.atlasSize);
		struct Job {
			uint32_t triangle, x, y;
		};
		std::vector<Job> jobs;
		for(const auto& rect : rectangles) {
			auto& binding	  = result.bindings[rect.binding];
			float scale		  = 1.0f / result.atlasSize;
			binding.scaleBias = {rect.width * scale, rect.height * scale, rect.x * scale, rect.y * scale};
			uint32_t columns  = rect.width / settings.chartSize;
			for(uint32_t i = 0; i < rect.count; ++i) jobs.push_back({rect.first + i, rect.x + (i % columns) * settings.chartSize, rect.y + (i / columns) * settings.chartSize});
		}
		Parallel(static_cast<uint32_t>(jobs.size()), [&](uint32_t jobIndex) {
			const auto& job	 = jobs[jobIndex];
			const auto& tri	 = integrator.geometry.triangles[job.triangle];
			uint32_t	tile = settings.chartSize, padding = tile / 4;
			for(uint32_t y = padding; y < tile - padding; ++y)
				for(uint32_t x = padding; x < tile - padding; ++x) {
					float u = (float(x) + .5f - padding) / (tile * .5f), v = (float(y) + .5f - padding) / (tile * .5f);
					if(u + v > 1) {
						float projected = std::clamp((u - v + 1) * .5f, 0.0f, 1.0f);
						u				= projected;
						v				= 1 - u;
					}
					// chartの外を埋める際も三角形の内側へ僅かに戻す。壁の交線上ではt=0の隣接面を見逃すため。
					constexpr float edgeGuard = 1e-4f;
					u						  = u * (1 - 3 * edgeGuard) + edgeGuard;
					v						  = v * (1 - 3 * edgeGuard) + edgeGuard;
					V p = tri.p[0] * (1 - u - v) + tri.p[1] * u + tri.p[2] * v, n = (tri.n[0] * (1 - u - v) + tri.n[1] * u + tri.n[2] * v).Normalized();
					result.lightmap[size_t(job.y + y) * result.atlasSize + job.x + x] = integrator.Irradiance(p, n, tri.geometricNormal);
				}
			// 余白は同じchart内の端を複製し、隣の三角形とのbilinear混入を防ぐ。
			for(uint32_t y = 0; y < tile; ++y)
				for(uint32_t x = 0; x < tile; ++x)
					if(x < padding || y < padding || x >= tile - padding || y >= tile - padding)
						result.lightmap[size_t(job.y + y) * result.atlasSize + job.x + x] = result.lightmap[size_t(job.y + std::clamp(y, padding, tile - padding - 1)) * result.atlasSize + job.x + std::clamp(x, padding, tile - padding - 1)];
		});
		result.probeCounts = settings.probeCounts;
		V low = From(settings.probeMin.data()), high = From(settings.probeMax.data());
		if(settings.automaticBounds) {
			low	 = integrator.geometry.nodes[0].lo - V{.5f, .5f, .5f};
			high = integrator.geometry.nodes[0].hi + V{.5f, .5f, .5f};
		}
		result.origin  = {low.x, low.y, low.z};
		V extent	   = high - low;
		result.spacing = {extent.x / (settings.probeCounts[0] - 1), extent.y / (settings.probeCounts[1] - 1), extent.z / (settings.probeCounts[2] - 1)};
		uint32_t count = settings.probeCounts[0] * settings.probeCounts[1] * settings.probeCounts[2];
		result.probes.resize(size_t(count) * 10);
		result.distances.resize(size_t(count) * 96);
		Parallel(count, [&](uint32_t id) {
			uint32_t x = id % settings.probeCounts[0], y = (id / settings.probeCounts[0]) % settings.probeCounts[1], z = id / (settings.probeCounts[0] * settings.probeCounts[1]);
			V		 position = low + Product({float(x), float(y), float(z)}, From(result.spacing.data()));
			uint32_t front = 0, back = 0;
			for(uint32_t face = 0; face < 6; ++face) {
				for(uint32_t v = 0; v < 8; ++v)
					for(uint32_t u = 0; u < 8; ++u) {
						V		 direction = CubeDirection(face, (u + .5f) / 4 - 1, (v + .5f) / 4 - 1);
						auto	 h		   = integrator.geometry.Trace(position, direction);
						uint32_t offset	   = face * 64 + v * 8 + u;
						// 軸方向6本だけでは室外の角を見逃すため、距離ベイクの全方向で裏面率を調べる。
						if(h.triangle != UINT32_MAX) {
							if(V::Dot(integrator.geometry.triangles[h.triangle].geometricNormal, direction) > 0)
								++back;
							else
								++front;
						}
						result.distances[size_t(id) * 96 + offset / 4][offset % 4] = h.t;
					}
			}
			bool valid							  = back <= front;
			result.probes[size_t(id) * 10 + 9][0] = valid ? 1.0f : 0.0f;
			if(!valid) return;
			for(uint32_t sample = 0; sample < settings.samples; ++sample) {
				float phi = 2 * pi * Radical(sample), height = 1 - 2 * (sample + .5f) / settings.samples, radius = std::sqrt(std::max(0.0f, 1 - height * height));
				V	  direction{radius * std::cos(phi), height, radius * std::sin(phi)};
				auto  coefficients = SH(direction);
				auto  light		   = integrator.Radiance(position, direction, sample);
				float sky		   = integrator.geometry.Trace(position, direction).triangle == UINT32_MAX ? 1.0f : 0.0f;
				for(size_t l = 0; l < 9; ++l) {
					float kernel	  = l == 0 ? 1.0f : l < 4 ? 2.0f / 3
															  : 1.0f / 4,
						  weight	  = 4 * pi * coefficients[l] * kernel / settings.samples;
					auto& coefficient = result.probes[size_t(id) * 10 + l];
					coefficient[0] += light.x * weight;
					coefficient[1] += light.y * weight;
					coefficient[2] += light.z * weight;
					coefficient[3] += sky * weight;
				}
			}
		});
		return result;
	}
} // namespace NexusEngine
