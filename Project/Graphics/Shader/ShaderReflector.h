#pragma once

#include <cstdint>
#include <span>

#include "Foundation/Error/Result.h"
#include "ShaderTypes.h"

namespace NexusEngine {

	/*-----------------------------------------------------------------------------------------
	 * ShaderReflector
	 * - DXIL ReflectionをDirectX非依存のShaderMetadataへ変換する
	 *---------------------------------------------------------------------------------------*/
	class ShaderReflector final {
	public:
		/**
		 * \brief Compile済みDXILを解析してDirectX非依存Metadataへ変換する
		 * \param bytecode ShaderCompilerが生成したDXIL
		 * \param stage Metadataへ記録するShader Stage
		 * \return Resource Binding、ConstantBuffer size、Vertex入力semantic
		 * \note Reflection COM Interfaceを戻り値へ公開しない
		 */
		[[nodiscard]] Result<ShaderMetadata> Reflect(std::span<const uint8_t> bytecode, ShaderStage stage) const;
	};

} // namespace NexusEngine
