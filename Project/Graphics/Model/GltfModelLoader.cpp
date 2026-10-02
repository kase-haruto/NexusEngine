#define _CRT_SECURE_NO_WARNINGS
#include "ModelLoader.h"

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <numeric>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// third party
#pragma warning(push, 0)
#define CGLTF_IMPLEMENTATION
#include "ThirdParty/cgltf/cgltf.h"
#pragma warning(pop)

namespace NexusEngine {
	namespace {
		constexpr int32_t kGltfParseFailed = 10;
		constexpr int32_t kGltfUnsupported = 11;
		constexpr int32_t kGltfInvalidData = 12;

		struct CgltfDeleter {
			void operator()(cgltf_data* data) const noexcept { cgltf_free(data); }
		};
		using CgltfDataPtr = std::unique_ptr<cgltf_data, CgltfDeleter>;

		[[nodiscard]] Error MakeGltfError(
			const std::filesystem::path& path,
			const int32_t code,
			const std::string_view message) {
			return Error(ErrorCategory::Resource, code,
				"glTF load failed: " + path.string() + ": " + std::string(message));
		}

		[[nodiscard]] Result<std::vector<uint8_t>> DecodeBase64Image(
			const std::string_view uri,
			const std::filesystem::path& path) {
			const std::size_t comma = uri.find(',');
			if(comma == std::string_view::npos || uri.substr(0, comma).find(";base64") == std::string_view::npos) {
				return std::unexpected(MakeGltfError(path, kGltfUnsupported,
					"only base64 image data URIs are supported"));
			}
			const std::string_view encoded = uri.substr(comma + 1);
			std::array<int8_t, 256> table;
			table.fill(-1);
			constexpr std::string_view alphabet =
				"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			for(std::size_t index = 0; index < alphabet.size(); ++index) {
				table[static_cast<uint8_t>(alphabet[index])] = static_cast<int8_t>(index);
			}
			std::vector<uint8_t> decoded;
			decoded.reserve(encoded.size() * 3 / 4);
			uint32_t accumulator = 0;
			int bitCount = 0;
			for(const char character : encoded) {
				if(character == '=') break;
				const int8_t value = table[static_cast<uint8_t>(character)];
				if(value < 0) {
					return std::unexpected(MakeGltfError(path, kGltfInvalidData,
						"image data URI contains invalid base64"));
				}
				accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
				bitCount += 6;
				if(bitCount >= 8) {
					bitCount -= 8;
					decoded.push_back(static_cast<uint8_t>((accumulator >> bitCount) & 0xffU));
				}
			}
			return decoded;
		}

		[[nodiscard]] Result<std::vector<uint8_t>> ReadImageData(
			const cgltf_image& image,
			const std::filesystem::path& modelPath) {
			if(image.buffer_view != nullptr && image.buffer_view->buffer != nullptr &&
			   image.buffer_view->buffer->data != nullptr) {
				const auto* begin = static_cast<const uint8_t*>(image.buffer_view->buffer->data) +
					image.buffer_view->offset;
				return std::vector<uint8_t>(begin, begin + image.buffer_view->size);
			}
			if(image.uri == nullptr) {
				return std::unexpected(MakeGltfError(modelPath, kGltfInvalidData,
					"material image has neither URI nor buffer view"));
			}
			const std::string_view uri = image.uri;
			if(uri.starts_with("data:")) return DecodeBase64Image(uri, modelPath);

			const std::filesystem::path imagePath = modelPath.parent_path() /
				std::filesystem::path(std::string(uri));
			std::ifstream input(imagePath, std::ios::binary | std::ios::ate);
			if(!input) {
				return std::unexpected(MakeGltfError(modelPath, kGltfParseFailed,
					"failed to open material image: " + imagePath.string()));
			}
			const std::streamsize size = input.tellg();
			if(size <= 0) {
				return std::unexpected(MakeGltfError(modelPath, kGltfInvalidData,
					"material image is empty"));
			}
			input.seekg(0, std::ios::beg);
			std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
			if(!input.read(reinterpret_cast<char*>(bytes.data()), size)) {
				return std::unexpected(MakeGltfError(modelPath, kGltfParseFailed,
					"failed to read material image"));
			}
			return bytes;
		}

		[[nodiscard]] TextureAddressMode ConvertWrapMode(const cgltf_wrap_mode mode) noexcept {
			if(mode == cgltf_wrap_mode_clamp_to_edge) return TextureAddressMode::Clamp;
			if(mode == cgltf_wrap_mode_mirrored_repeat) return TextureAddressMode::Mirror;
			return TextureAddressMode::Repeat;
		}

		[[nodiscard]] uint32_t PointerIndex(
			const cgltf_node* pointer,
			const cgltf_node* first) noexcept {
			return static_cast<uint32_t>(pointer - first);
		}

		[[nodiscard]] Matrix4x4 ConvertMatrix(const cgltf_float* source) noexcept {
			Matrix4x4 result;
			// glTFのcolumn-major配列を同じ連続順でrow-majorへ解釈すると数学的に転置され、
			// column-vector式 M*v と等価なrow-vector式 v*M^Tになる。
			for(std::size_t row = 0; row < 4; ++row) {
				for(std::size_t column = 0; column < 4; ++column) {
					result.At(row, column) = source[row * 4 + column];
				}
			}
			// Z反転を入出力の両側へ適用し、右手系glTFからEngineの左手系へ移す。
			for(std::size_t index = 0; index < 4; ++index) {
				result.At(2, index) = -result.At(2, index);
				result.At(index, 2) = -result.At(index, 2);
			}
			return result;
		}

		[[nodiscard]] Quaternion QuaternionFromRotationMatrix(const Matrix4x4& matrix) noexcept {
			const float trace = matrix.At(0, 0) + matrix.At(1, 1) + matrix.At(2, 2);
			Quaternion result;
			if(trace > 0.0f) {
				const float scale = std::sqrt(trace + 1.0f) * 2.0f;
				result.w = 0.25f * scale;
				result.x = (matrix.At(1, 2) - matrix.At(2, 1)) / scale;
				result.y = (matrix.At(2, 0) - matrix.At(0, 2)) / scale;
				result.z = (matrix.At(0, 1) - matrix.At(1, 0)) / scale;
			} else if(matrix.At(0, 0) > matrix.At(1, 1) && matrix.At(0, 0) > matrix.At(2, 2)) {
				const float scale = std::sqrt(1.0f + matrix.At(0, 0) - matrix.At(1, 1) - matrix.At(2, 2)) * 2.0f;
				result.w = (matrix.At(1, 2) - matrix.At(2, 1)) / scale;
				result.x = 0.25f * scale;
				result.y = (matrix.At(1, 0) + matrix.At(0, 1)) / scale;
				result.z = (matrix.At(2, 0) + matrix.At(0, 2)) / scale;
			} else if(matrix.At(1, 1) > matrix.At(2, 2)) {
				const float scale = std::sqrt(1.0f + matrix.At(1, 1) - matrix.At(0, 0) - matrix.At(2, 2)) * 2.0f;
				result.w = (matrix.At(2, 0) - matrix.At(0, 2)) / scale;
				result.x = (matrix.At(1, 0) + matrix.At(0, 1)) / scale;
				result.y = 0.25f * scale;
				result.z = (matrix.At(2, 1) + matrix.At(1, 2)) / scale;
			} else {
				const float scale = std::sqrt(1.0f + matrix.At(2, 2) - matrix.At(0, 0) - matrix.At(1, 1)) * 2.0f;
				result.w = (matrix.At(0, 1) - matrix.At(1, 0)) / scale;
				result.x = (matrix.At(2, 0) + matrix.At(0, 2)) / scale;
				result.y = (matrix.At(2, 1) + matrix.At(1, 2)) / scale;
				result.z = 0.25f * scale;
			}
			return result.Normalized();
		}

		[[nodiscard]] Result<ModelNodeTransform> ReadNodeTransform(
			const cgltf_node& node,
			const std::filesystem::path& path) {
			if(!node.has_matrix) {
				ModelNodeTransform transform;
				if(node.has_translation) {
					transform.translation = { node.translation[0], node.translation[1], -node.translation[2] };
				}
				if(node.has_rotation) {
					transform.rotation = Quaternion {
						-node.rotation[0], -node.rotation[1], node.rotation[2], node.rotation[3] }.Normalized();
				}
				if(node.has_scale) transform.scale = { node.scale[0], node.scale[1], node.scale[2] };
				return transform;
			}

			const Matrix4x4 matrix = ConvertMatrix(node.matrix);
			ModelNodeTransform transform;
			transform.translation = { matrix.At(3, 0), matrix.At(3, 1), matrix.At(3, 2) };
			transform.scale = {
				Vector3 { matrix.At(0, 0), matrix.At(0, 1), matrix.At(0, 2) }.Length(),
				Vector3 { matrix.At(1, 0), matrix.At(1, 1), matrix.At(1, 2) }.Length(),
				Vector3 { matrix.At(2, 0), matrix.At(2, 1), matrix.At(2, 2) }.Length()
			};
			if(transform.scale.x <= 0.0f || transform.scale.y <= 0.0f || transform.scale.z <= 0.0f) {
				return std::unexpected(MakeGltfError(path, kGltfInvalidData,
					"node matrix contains a singular scale"));
			}
			Matrix4x4 rotation = matrix;
			for(std::size_t column = 0; column < 3; ++column) {
				rotation.At(0, column) /= transform.scale.x;
				rotation.At(1, column) /= transform.scale.y;
				rotation.At(2, column) /= transform.scale.z;
			}
			rotation.At(3, 0) = rotation.At(3, 1) = rotation.At(3, 2) = 0.0f;
			transform.rotation = QuaternionFromRotationMatrix(rotation);
			return transform;
		}

		[[nodiscard]] const cgltf_accessor* FindAttribute(
			const cgltf_primitive& primitive,
			const cgltf_attribute_type type,
			const cgltf_int index = 0) noexcept {
			for(cgltf_size attributeIndex = 0; attributeIndex < primitive.attributes_count; ++attributeIndex) {
				const cgltf_attribute& attribute = primitive.attributes[attributeIndex];
				if(attribute.type == type && attribute.index == index) return attribute.data;
			}
			return nullptr;
		}

		[[nodiscard]] Result<MeshAssetData> ReadPrimitive(
			const cgltf_data& data,
			const cgltf_primitive& primitive,
			const uint32_t skinIndex,
			const std::filesystem::path& path) {
			if(primitive.type != cgltf_primitive_type_triangles) {
				return std::unexpected(MakeGltfError(path, kGltfUnsupported,
					"only triangle-list primitives are supported"));
			}
			const cgltf_accessor* positions = FindAttribute(primitive, cgltf_attribute_type_position);
			if(positions == nullptr || positions->type != cgltf_type_vec3 || positions->count == 0) {
				return std::unexpected(MakeGltfError(path, kGltfInvalidData,
					"primitive has no valid POSITION accessor"));
			}
			const cgltf_accessor* normals = FindAttribute(primitive, cgltf_attribute_type_normal);
			const cgltf_accessor* texcoords = FindAttribute(primitive, cgltf_attribute_type_texcoord);
			const cgltf_accessor* colors = FindAttribute(primitive, cgltf_attribute_type_color);
			const cgltf_accessor* joints = FindAttribute(primitive, cgltf_attribute_type_joints);
			const cgltf_accessor* weights = FindAttribute(primitive, cgltf_attribute_type_weights);
			if((joints == nullptr) != (weights == nullptr)) {
				return std::unexpected(MakeGltfError(path, kGltfInvalidData,
					"JOINTS_0 and WEIGHTS_0 must be supplied together"));
			}
			auto hasInvalidCount = [&](const cgltf_accessor* accessor) {
				return accessor != nullptr && accessor->count != positions->count;
			};
			if(hasInvalidCount(normals) || hasInvalidCount(texcoords) || hasInvalidCount(colors) ||
			   hasInvalidCount(joints) || hasInvalidCount(weights)) {
				return std::unexpected(MakeGltfError(path, kGltfInvalidData,
					"vertex attribute accessor counts do not match POSITION"));
			}
			if((normals != nullptr && normals->type != cgltf_type_vec3) ||
			   (texcoords != nullptr && texcoords->type != cgltf_type_vec2) ||
			   (colors != nullptr && colors->type != cgltf_type_vec3 && colors->type != cgltf_type_vec4) ||
			   (joints != nullptr && joints->type != cgltf_type_vec4) ||
			   (weights != nullptr && weights->type != cgltf_type_vec4)) {
				return std::unexpected(MakeGltfError(path, kGltfInvalidData,
					"vertex attribute accessor has an unsupported element type"));
			}

			std::vector<ModelVertex> vertices(positions->count);
			for(cgltf_size vertexIndex = 0; vertexIndex < positions->count; ++vertexIndex) {
				ModelVertex& vertex = vertices[vertexIndex];
				cgltf_accessor_read_float(positions, vertexIndex, vertex.position, 3);
				vertex.position[2] = -vertex.position[2];
				if(normals != nullptr) {
					cgltf_accessor_read_float(normals, vertexIndex, vertex.normal, 3);
					vertex.normal[2] = -vertex.normal[2];
				}
				if(texcoords != nullptr) cgltf_accessor_read_float(texcoords, vertexIndex, vertex.texcoord, 2);
				if(colors != nullptr) {
					const cgltf_size componentCount = colors->type == cgltf_type_vec3 ? 3 : 4;
					cgltf_accessor_read_float(colors, vertexIndex, vertex.color, componentCount);
				}
				if(joints != nullptr) {
					cgltf_uint jointValues[4] = {};
					cgltf_accessor_read_uint(joints, vertexIndex, jointValues, 4);
					cgltf_accessor_read_float(weights, vertexIndex, vertex.jointWeights, 4);
					float weightSum = 0.0f;
					for(std::size_t influence = 0; influence < 4; ++influence) {
						if(skinIndex != MeshAssetData::kNoSkin &&
						   jointValues[influence] >= data.skins[skinIndex].joints_count) {
							return std::unexpected(MakeGltfError(path, kGltfInvalidData,
								"vertex joint index is outside the referenced skin"));
						}
						vertex.jointIndices[influence] = static_cast<float>(jointValues[influence]);
						weightSum += vertex.jointWeights[influence];
					}
					if(weightSum > 0.0f) {
						for(float& weight : vertex.jointWeights) weight /= weightSum;
					}
				}
			}

			std::vector<uint32_t> indices;
			if(primitive.indices != nullptr) {
				indices.resize(primitive.indices->count);
				for(cgltf_size index = 0; index < primitive.indices->count; ++index) {
					indices[index] = static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, index));
				}
			} else {
				indices.resize(vertices.size());
				std::iota(indices.begin(), indices.end(), 0U);
			}
			if(indices.size() % 3 != 0) {
				return std::unexpected(MakeGltfError(path, kGltfInvalidData,
					"triangle index count is not divisible by three"));
			}
			for(std::size_t index = 0; index < indices.size(); index += 3) std::swap(indices[index + 1], indices[index + 2]);

			MeshAssetData mesh;
			mesh.vertexStride = sizeof(ModelVertex);
			mesh.vertexData.resize(vertices.size() * sizeof(ModelVertex));
			std::memcpy(mesh.vertexData.data(), vertices.data(), mesh.vertexData.size());
			mesh.indexFormat = IndexFormat::UInt32;
			mesh.indexData.resize(indices.size() * sizeof(uint32_t));
			std::memcpy(mesh.indexData.data(), indices.data(), mesh.indexData.size());
			const uint32_t materialSlot = primitive.material != nullptr
				? static_cast<uint32_t>(primitive.material - data.materials)
				: static_cast<uint32_t>(data.materials_count);
			mesh.submeshes.push_back({ 0, static_cast<uint32_t>(indices.size()), 0, materialSlot });
			mesh.skinIndex = skinIndex;
			return mesh;
		}
	} // namespace

	/////////////////////////////////////////////////////////////////////////////////////////
	// glTF 2.0をModelAssetDataへ変換する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<ModelAssetData> ModelLoader::LoadGltf(const std::filesystem::path& filePath) const {
		const std::string nativePath = filePath.string();
		cgltf_options options = {};
		cgltf_data* parsedData = nullptr;
		if(cgltf_parse_file(&options, nativePath.c_str(), &parsedData) != cgltf_result_success) {
			return std::unexpected(MakeGltfError(filePath, kGltfParseFailed, "failed to parse document"));
		}
		CgltfDataPtr data(parsedData);
		if(cgltf_load_buffers(&options, data.get(), nativePath.c_str()) != cgltf_result_success) {
			return std::unexpected(MakeGltfError(filePath, kGltfParseFailed, "failed to load buffers"));
		}
		if(cgltf_validate(data.get()) != cgltf_result_success) {
			return std::unexpected(MakeGltfError(filePath, kGltfInvalidData, "document validation failed"));
		}

		ModelAssetData model;
		model.materials.reserve(data->materials_count + 1);
		for(cgltf_size materialIndex = 0; materialIndex < data->materials_count; ++materialIndex) {
			const cgltf_material& source = data->materials[materialIndex];
			ModelMaterialAssetData material;
			material.name = source.name != nullptr ? source.name : "Material";
			std::copy_n(source.pbr_metallic_roughness.base_color_factor, 4,
				material.baseColorFactor.begin());

			const cgltf_texture_view& textureView = source.pbr_metallic_roughness.base_color_texture;
			if(textureView.texture != nullptr) {
				if(textureView.texcoord != 0 || textureView.has_transform) {
					return std::unexpected(MakeGltfError(filePath, kGltfUnsupported,
						"base-color TEXCOORD sets other than 0 and texture transforms are not supported"));
				}
				const cgltf_image* image = textureView.texture->image;
				if(image == nullptr) {
					return std::unexpected(MakeGltfError(filePath, kGltfUnsupported,
						"base-color texture has no core glTF image"));
				}
				auto imageData = ReadImageData(*image, filePath);
				if(!imageData) return std::unexpected(std::move(imageData.error()));
				material.baseColorImageData = std::move(*imageData);
				if(const cgltf_sampler* sampler = textureView.texture->sampler; sampler != nullptr) {
					material.sampler.filter =
						sampler->mag_filter == cgltf_filter_type_nearest ||
						sampler->min_filter == cgltf_filter_type_nearest ||
						sampler->min_filter == cgltf_filter_type_nearest_mipmap_nearest ||
						sampler->min_filter == cgltf_filter_type_nearest_mipmap_linear
						? TextureFilter::Nearest : TextureFilter::Linear;
					material.sampler.addressU = ConvertWrapMode(sampler->wrap_s);
					material.sampler.addressV = ConvertWrapMode(sampler->wrap_t);
				}
			}
			model.materials.push_back(std::move(material));
		}
		// material未指定Primitive専用slot。既存Material 0を暗黙Materialとして誤用しない。
		model.materials.push_back({ "Default" });

		std::vector<uint32_t> nodeRemap(data->nodes_count, UINT32_MAX);
		std::vector<const cgltf_node*> orderedNodes;
		orderedNodes.reserve(data->nodes_count);
		auto addNode = [&](auto&& self, const cgltf_node* node) -> void {
			const uint32_t sourceIndex = PointerIndex(node, data->nodes);
			if(nodeRemap[sourceIndex] != UINT32_MAX) return;
			if(node->parent != nullptr) self(self, node->parent);
			nodeRemap[sourceIndex] = static_cast<uint32_t>(orderedNodes.size());
			orderedNodes.push_back(node);
		};
		for(cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; ++nodeIndex) addNode(addNode, &data->nodes[nodeIndex]);

		// Skinを先に変換し、Nodeが参照するSkin indexをMeshへ設定できるようにする。
		model.skins.reserve(data->skins_count);
		for(cgltf_size skinIndex = 0; skinIndex < data->skins_count; ++skinIndex) {
			const cgltf_skin& source = data->skins[skinIndex];
			if(source.joints_count == 0 || source.joints_count > kMaxSkinJoints) {
				return std::unexpected(MakeGltfError(filePath, kGltfUnsupported,
					"skin joint count is outside the supported range"));
			}
			ModelSkin skin;
			skin.name = source.name != nullptr ? source.name : "Skin";
			for(cgltf_size jointIndex = 0; jointIndex < source.joints_count; ++jointIndex) {
				skin.joints.push_back(nodeRemap[PointerIndex(source.joints[jointIndex], data->nodes)]);
				if(source.inverse_bind_matrices != nullptr) {
					std::array<cgltf_float, 16> matrix = {};
					cgltf_accessor_read_float(source.inverse_bind_matrices, jointIndex, matrix.data(), matrix.size());
					skin.inverseBindMatrices.push_back(ConvertMatrix(matrix.data()));
				} else {
					skin.inverseBindMatrices.push_back(Matrix4x4::Identity());
				}
			}
			model.skins.push_back(std::move(skin));
		}

		model.nodes.reserve(orderedNodes.size());
		for(const cgltf_node* source : orderedNodes) {
			auto transform = ReadNodeTransform(*source, filePath);
			if(!transform) return std::unexpected(std::move(transform.error()));
			ModelNodeAssetData node;
			node.name = source->name != nullptr ? source->name : "Node";
			node.parentIndex = source->parent != nullptr
				? nodeRemap[PointerIndex(source->parent, data->nodes)] : ModelNodeAssetData::kNoParent;
			node.localTransform = *transform;
			if(source->mesh != nullptr) {
				const uint32_t skinIndex = source->skin != nullptr
					? static_cast<uint32_t>(source->skin - data->skins) : MeshAssetData::kNoSkin;
				for(cgltf_size primitiveIndex = 0; primitiveIndex < source->mesh->primitives_count; ++primitiveIndex) {
					auto mesh = ReadPrimitive(*data, source->mesh->primitives[primitiveIndex], skinIndex, filePath);
					if(!mesh) return std::unexpected(std::move(mesh.error()));
					node.meshIndices.push_back(static_cast<uint32_t>(model.meshes.size()));
					model.meshes.push_back(std::move(*mesh));
				}
			}
			model.nodes.push_back(std::move(node));
		}

		model.animations.reserve(data->animations_count);
		for(cgltf_size animationIndex = 0; animationIndex < data->animations_count; ++animationIndex) {
			const cgltf_animation& sourceAnimation = data->animations[animationIndex];
			AnimationClip clip;
			clip.name = sourceAnimation.name != nullptr ? sourceAnimation.name : "Animation";
			for(cgltf_size channelIndex = 0; channelIndex < sourceAnimation.channels_count; ++channelIndex) {
				const cgltf_animation_channel& sourceChannel = sourceAnimation.channels[channelIndex];
				if(sourceChannel.target_node == nullptr || sourceChannel.sampler == nullptr) continue;
				if(sourceChannel.target_path == cgltf_animation_path_type_weights) {
					return std::unexpected(MakeGltfError(filePath, kGltfUnsupported,
						"morph target weight animation is not currently supported"));
				}
				NodeAnimationChannel* channel = nullptr;
				const uint32_t targetNode = nodeRemap[PointerIndex(sourceChannel.target_node, data->nodes)];
				for(NodeAnimationChannel& candidate : clip.channels) {
					if(candidate.nodeIndex == targetNode) { channel = &candidate; break; }
				}
				if(channel == nullptr) {
					clip.channels.push_back({ targetNode });
					channel = &clip.channels.back();
				}

				const cgltf_accessor* times = sourceChannel.sampler->input;
				const cgltf_accessor* values = sourceChannel.sampler->output;
				const bool isCubic = sourceChannel.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
				const cgltf_size expectedValueCount = times != nullptr
					? times->count * (isCubic ? 3U : 1U) : 0U;
				if(times == nullptr || values == nullptr || values->count != expectedValueCount) {
					return std::unexpected(MakeGltfError(filePath, kGltfInvalidData,
						"animation sampler input/output counts do not match"));
				}
				const AnimationInterpolation interpolation = isCubic
					? AnimationInterpolation::CubicSpline
					: (sourceChannel.sampler->interpolation == cgltf_interpolation_type_step
						? AnimationInterpolation::Step : AnimationInterpolation::Linear);
				if(sourceChannel.target_path == cgltf_animation_path_type_rotation) {
					channel->rotationInterpolation = interpolation;
				} else if(sourceChannel.target_path == cgltf_animation_path_type_translation) {
					channel->translationInterpolation = interpolation;
				} else if(sourceChannel.target_path == cgltf_animation_path_type_scale) {
					channel->scaleInterpolation = interpolation;
				}

				auto readVector = [&](const cgltf_size accessorIndex, const bool flipZ) {
					Vector3 value;
					cgltf_accessor_read_float(values, accessorIndex, &value.x, 3);
					if(flipZ) value.z = -value.z;
					return value;
				};
				auto readQuaternion = [&](const cgltf_size accessorIndex, const bool normalize) {
					std::array<float, 4> value = {};
					cgltf_accessor_read_float(values, accessorIndex, value.data(), value.size());
					Quaternion quaternion { -value[0], -value[1], value[2], value[3] };
					return normalize ? quaternion.Normalized() : quaternion;
				};
				for(cgltf_size keyIndex = 0; keyIndex < times->count; ++keyIndex) {
					float time = 0.0f;
					cgltf_accessor_read_float(times, keyIndex, &time, 1);
					clip.duration = (std::max)(clip.duration, time);
					const cgltf_size valueIndex = isCubic ? keyIndex * 3 + 1 : keyIndex;
					if(sourceChannel.target_path == cgltf_animation_path_type_rotation) {
						QuaternionKeyframe keyframe;
						keyframe.time = time;
						keyframe.value = readQuaternion(valueIndex, true);
						if(isCubic) {
							keyframe.inTangent = readQuaternion(keyIndex * 3, false);
							keyframe.outTangent = readQuaternion(keyIndex * 3 + 2, false);
						}
						channel->rotations.push_back(keyframe);
					} else if(sourceChannel.target_path == cgltf_animation_path_type_translation) {
						VectorKeyframe keyframe;
						keyframe.time = time;
						keyframe.value = readVector(valueIndex, true);
						if(isCubic) {
							keyframe.inTangent = readVector(keyIndex * 3, true);
							keyframe.outTangent = readVector(keyIndex * 3 + 2, true);
						}
						channel->translations.push_back(keyframe);
					} else if(sourceChannel.target_path == cgltf_animation_path_type_scale) {
						VectorKeyframe keyframe;
						keyframe.time = time;
						keyframe.value = readVector(valueIndex, false);
						if(isCubic) {
							keyframe.inTangent = readVector(keyIndex * 3, false);
							keyframe.outTangent = readVector(keyIndex * 3 + 2, false);
						}
						channel->scales.push_back(keyframe);
					}
				}
			}
			model.animations.push_back(std::move(clip));
		}

		if(model.meshes.empty()) {
			return std::unexpected(MakeGltfError(filePath, kGltfInvalidData, "document has no drawable meshes"));
		}
		return model;
	}
} // namespace NexusEngine
