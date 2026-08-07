#include "PipelineLayout.h"

#include <algorithm>
#include <string>
#include <utility>

namespace NexusEngine {
	namespace { constexpr int32_t kBindingConflict = 1; constexpr int32_t kBindingNotFound = 2; }
	namespace {
		uint8_t GetRegisterNamespace(const ShaderResourceType type) noexcept {
			switch(type) {
			case ShaderResourceType::ConstantBuffer: return 0;
			case ShaderResourceType::Sampler: return 3;
			case ShaderResourceType::RwTexture:
			case ShaderResourceType::RwStructuredBuffer: return 2;
			default: return 1;
			}
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////
	// VS/PS Reflection MetadataをPipeline単位のBinding Layoutへ統合する
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<void> PipelineLayout::Build(const ShaderMetadata& vertexMetadata, const ShaderMetadata& pixelMetadata) {
		bindings_.clear();
		resourceDescriptorCount_ = 0;
		samplerDescriptorCount_ = 0;
		const ShaderMetadata* stages[] = { &vertexMetadata, &pixelMetadata };
		for(const ShaderMetadata* metadata : stages) {
			for(const auto& resource : metadata->resources) {
				// 同名ResourceはStageを跨いで一つへ統合する。型・register・配列数の差異は
				// 同じBindingを別解釈することになるためPipeline生成前に拒否する。
				auto existing = std::find_if(bindings_.begin(), bindings_.end(), [&](const PipelineBinding& binding) {
					return binding.resource.name == resource.name;
				});
				if(existing != bindings_.end()) {
					if(existing->resource.type != resource.type || existing->resource.bindPoint != resource.bindPoint ||
					   existing->resource.registerSpace != resource.registerSpace || existing->resource.bindCount != resource.bindCount) {
						return std::unexpected(Error(ErrorCategory::Graphics, kBindingConflict, "Shader stage binding conflict: " + resource.name));
					}
					existing->resource.stageMask |= resource.stageMask;
					continue;
				}

				// 名前が異なっていても同一register namespace・spaceの範囲が重なる場合は衝突である。
				// b/t/u/sは独立namespaceなので、例えばb0とt0は正しく共存できる。
				const auto conflict = std::find_if(bindings_.begin(), bindings_.end(), [&](const PipelineBinding& binding) {
					if(GetRegisterNamespace(binding.resource.type) != GetRegisterNamespace(resource.type) ||
					   binding.resource.registerSpace != resource.registerSpace) {
						return false;
					}
					const uint64_t existingEnd = static_cast<uint64_t>(binding.resource.bindPoint) + binding.resource.bindCount;
					const uint64_t incomingEnd = static_cast<uint64_t>(resource.bindPoint) + resource.bindCount;
					return binding.resource.bindPoint < incomingEnd && resource.bindPoint < existingEnd;
				});
				if(conflict != bindings_.end()) {
					return std::unexpected(Error(ErrorCategory::Graphics, kBindingConflict, "Shader register collision: " + resource.name));
				}

				// Descriptor offsetはHeap種別ごとに連続配置し、RootSignature生成規則と共有する。
				PipelineBinding binding { resource, 0 };
				if(resource.type == ShaderResourceType::Sampler) {
					binding.descriptorOffset = samplerDescriptorCount_;
					samplerDescriptorCount_ += resource.bindCount;
				} else {
					binding.descriptorOffset = resourceDescriptorCount_;
					resourceDescriptorCount_ += resource.bindCount;
				}
				bindings_.push_back(std::move(binding));
			}
		}
		return {};
	}

	Result<ShaderBindingHandle> PipelineLayout::ResolveBinding(const std::string_view name) const {
		// 文字列検索はMaterial/Pipeline初期化時だけ使用し、Draw時は返したindexで直接参照する。
		for(uint32_t index = 0; index < bindings_.size(); ++index) {
			if(bindings_[index].resource.name == name) {
				return ShaderBindingHandle { index };
			}
		}
		return std::unexpected(Error(ErrorCategory::Graphics, kBindingNotFound, "Shader binding was not found: " + std::string(name)));
	}

	const std::vector<PipelineBinding>& PipelineLayout::GetBindings() const noexcept { return bindings_; }
	uint32_t PipelineLayout::GetResourceDescriptorCount() const noexcept { return resourceDescriptorCount_; }
	uint32_t PipelineLayout::GetSamplerDescriptorCount() const noexcept { return samplerDescriptorCount_; }

} // namespace NexusEngine
