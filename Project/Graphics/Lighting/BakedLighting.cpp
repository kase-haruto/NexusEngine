#include "BakedLighting.h"
#include "Foundation/Logging/Logger.h"
#include "Graphics/Renderer/GraphicsResourceFactory.h"
#include "Graphics/Renderer/RenderScene.h"
#include "Graphics/Resource/SamplerDesc.h"
#include "Graphics/Resource/TextureResource.h"
#include "LightBaker.h"
#include <span>
#include <unordered_map>
namespace NexusEngine {
	class BakedLighting::Impl {
	public:
		struct Resources {
			GraphicsResourceFactory*		 factory = nullptr;
			std::array<TextureResource, 3>	 textures;
			std::array<ShaderResourceRef, 4> refs{};
			~Resources() {
				for(auto ref : refs)
					if(ref.IsValid()) {
						auto result = factory->RetirePersistentShaderResource(ref);
						if(!result) NEXUS_LOG_ERROR("BakedGI", result.error().GetMessageText());
					}
			}
		};
		GraphicsResourceFactory*   factory = nullptr;
		std::unique_ptr<Resources> gpu;
		BakedLightingAsset		   asset;
		struct Key {
			uint64_t object;
			uint32_t node, mesh;
			bool	 operator==(const Key&) const = default;
		};
		struct Hash {
			size_t operator()(const Key& k) const noexcept { return static_cast<size_t>(k.object ^ (uint64_t(k.node) << 32) ^ k.mesh); }
		};
		std::unordered_map<Key, size_t, Hash> bindings; //< 読み込み時に一度だけ作る描画照合表
		std::filesystem::path				  path;
		uint64_t							  revision	= 0;
		bool								  attempted = false;
		std::string							  status	= "Not loaded";
	};
	BakedLighting::BakedLighting() noexcept = default;
	BakedLighting::~BakedLighting() noexcept { Shutdown(); }
	void BakedLighting::Initialize(GraphicsResourceFactory& factory) {
		impl_		   = std::make_unique<Impl>();
		impl_->factory = &factory;
	}
	void			   BakedLighting::Shutdown() noexcept { impl_.reset(); }
	const std::string& BakedLighting::GetStatus() const noexcept {
		static const std::string empty = "Not initialized";
		return impl_ ? impl_->status : empty;
	}
	Result<void> BakedLighting::Prepare(const ModelAssetData& model, const ModelInstance& pose, const RenderScene& scene) {
		if(!impl_) return std::unexpected(Error(ErrorCategory::Graphics, 91, "BakedLighting not initialized."));
		auto&		i		 = *impl_;
		const auto& settings = scene.bakedLighting;
		if(!settings.enabled) return {};
		if(i.attempted && i.path == settings.cachePath && i.revision == settings.revision) return {};
		i.attempted = true;
		i.path		= settings.cachePath;
		i.revision	= settings.revision;
		auto fail	= [&](Error error) -> Result<void> { i.status=error.GetMessageText();return std::unexpected(std::move(error)); };
		if(!settings.IsValid()) return fail(Error(ErrorCategory::Resource, 91, "Invalid baked lighting settings."));
		auto asset = LoadBakedLighting(settings.cachePath);
		if(!asset) return fail(std::move(asset.error()));
		if(asset->signature != LightBaker::Signature(model, pose, scene)) return fail(Error(ErrorCategory::Resource, 91, "Baked lighting is stale; bake the static scene again."));
		auto gpu															 = std::make_unique<Impl::Resources>();
		gpu->factory														 = i.factory;
		const uint32_t												  probes = asset->probeCounts[0] * asset->probeCounts[1] * asset->probeCounts[2];
		const std::array<uint32_t, 3>								  width{asset->atlasSize, 10, 96}, height{asset->atlasSize, probes, probes};
		const std::array<const std::vector<std::array<float, 4>>*, 3> data{&asset->lightmap, &asset->probes, &asset->distances};
		// 転送は共有Queue上で同期完了する。新旧を同時に保持し、成功するまで旧資源を破棄しない。
		for(size_t n = 0; n < 3; ++n) {
			auto bytes	= std::as_bytes(std::span(*data[n]));
			auto result = i.factory->CreateTexture2D(gpu->textures[n], {width[n], height[n], TextureFormat::Rgba32Float},
													 {reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()});
			if(!result) return fail(std::move(result.error()));
			auto ref = i.factory->CreatePersistentTextureShaderResource(gpu->textures[n]);
			if(!ref) return fail(std::move(ref.error()));
			gpu->refs[n] = *ref;
		}
		SamplerDesc sampler;
		sampler.addressU = sampler.addressV = sampler.addressW = TextureAddressMode::Clamp;
		auto ref											   = i.factory->CreatePersistentSampler(sampler);
		if(!ref) return fail(std::move(ref.error()));
		gpu->refs[3] = *ref;
		auto idle	 = i.factory->WaitForIdle();
		if(!idle) return fail(std::move(idle.error()));
		i.status = "Loaded: " + std::to_string(asset->bindings.size()) + " lightmaps / " + std::to_string(probes) + " probes";
		// CPU側には照合メタデータだけ残す。GPUテクスチャの複製保持を避ける。
		for(auto* values : {&asset->lightmap, &asset->probes, &asset->distances}) std::vector<std::array<float, 4>>{}.swap(*values);
		i.bindings.clear();
		for(size_t n = 0; n < asset->bindings.size(); ++n) {
			const auto& binding = asset->bindings[n];
			i.bindings.emplace(Impl::Key{binding.object, binding.node, binding.mesh}, n);
		}
		i.asset = std::move(*asset);
		i.gpu	= std::move(gpu);
		return {};
	}
	BakedLightingDrawData BakedLighting::GetDrawData(const BakedLightingSettings& settings) const noexcept {
		BakedLightingDrawData data;
		if(!impl_ || !impl_->gpu || !settings.enabled || !settings.IsValid()) return data;
		const auto& a		 = impl_->asset;
		const auto& refs	 = impl_->gpu->refs;
		data.textures		 = {refs[0].index, refs[1].index, refs[2].index, refs[3].index};
		data.originIntensity = {a.origin[0], a.origin[1], a.origin[2], settings.intensity};
		data.spacingEnabled	 = {a.spacing[0], a.spacing[1], a.spacing[2], 1};
		data.counts			 = {a.probeCounts[0], a.probeCounts[1], a.probeCounts[2], 8};
		return data;
	}
	BakedLightingDrawData BakedLighting::GetDrawData(const BakedLightingSettings& settings, uint64_t object, uint32_t node, uint32_t mesh, const Matrix4x4& world, bool staticLighting) const noexcept {
		auto data = GetDrawData(settings);
		if(data.spacingEnabled[3] == 0 || !staticLighting) return data;
		if(auto found = impl_->bindings.find({object, node, mesh}); found != impl_->bindings.end()) {
			const auto& binding = impl_->asset.bindings[found->second];
			bool		matches = true;
			for(size_t n = 0; n < 16; ++n)
				if(std::abs(binding.world.elements[n] - world.elements[n]) > 1e-5f) matches = false;
			if(matches) data.lightmapTransform = binding.scaleBias;
		}
		return data;
	}
} // namespace NexusEngine
