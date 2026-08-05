#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "Foundation/Error/Result.h"
#include "Graphics/Shader/ShaderTypes.h"

namespace NexusEngine {

	struct ShaderBindingHandle { uint32_t index = UINT32_MAX; }; //< 名前から一度だけ解決する軽量Index
	struct PipelineBinding {
		ShaderResourceBinding resource; //< Stage統合済みResource情報
		uint32_t descriptorOffset = 0;  //< Descriptor Table内のOffset
	};

	/*-----------------------------------------------------------------------------------------
	 * PipelineLayout
	 * - Shader Stage間のBinding統合、衝突検出、名前から軽量Handleへの解決を担当する
	 *---------------------------------------------------------------------------------------*/
	class PipelineLayout final {
	public:
		/** \brief VS/PS Metadataを統合しregister衝突を検出する */
		[[nodiscard]] Result<void> Build(const ShaderMetadata& vertexMetadata, const ShaderMetadata& pixelMetadata);
		/** \brief 初期化時にResource名をDraw用Handleへ解決する */
		[[nodiscard]] Result<ShaderBindingHandle> ResolveBinding(std::string_view name) const;
		/** \brief RootSignature生成用の統合済みBinding一覧を取得する */
		[[nodiscard]] const std::vector<PipelineBinding>& GetBindings() const noexcept;
		[[nodiscard]] uint32_t GetResourceDescriptorCount() const noexcept;
		[[nodiscard]] uint32_t GetSamplerDescriptorCount() const noexcept;

	private:
		std::vector<PipelineBinding> bindings_;
		uint32_t resourceDescriptorCount_ = 0;
		uint32_t samplerDescriptorCount_ = 0;
	};

} // namespace NexusEngine
