#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "Foundation/Error/Result.h"
#include "ShaderCompiler.h"
#include "ShaderTypes.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * Shader
	 * - Compile済みDXILと初期化時に生成したReflection Metadataを所有する
	 *---------------------------------------------------------------------------------------*/
	class Shader final {
	public:
		/**
		 * \brief CompilerとReflectorを順に使用してShaderを初期化する
		 * \param desc HLSL Compile設定
		 * \param stage Shader Stage
		 * \return CompileとReflectionの結果
		 * \note Reflectionはこの初期化中に一度だけ実行する
		 */
		[[nodiscard]] Result<void> Initialize(const ShaderCompileDesc& desc, ShaderStage stage);
		/** \brief PSO生成用DXILを非所有viewとして取得する */
		[[nodiscard]] std::span<const uint8_t> GetBytecode() const noexcept;
		/** \brief Pipeline Layout生成用Metadataを取得する */
		[[nodiscard]] const ShaderMetadata& GetMetadata() const noexcept;
		[[nodiscard]] ShaderStage GetStage() const noexcept;

	private:
		std::vector<uint8_t> bytecode_; //< 所有するDXIL Binary
		ShaderMetadata metadata_;       //< Compile時に一度だけ取得するBinding情報
		ShaderStage stage_ = ShaderStage::Vertex; //< Shader Stage
	};

} // namespace NexusEngine
