#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Foundation/Error/Result.h"

namespace NexusEngine {

	struct ShaderCompileDesc {
		std::filesystem::path sourcePath; //< Compile対象HLSLのパス
		std::wstring entryPoint;          //< Shader entry point名
		std::wstring targetProfile;       //< 例: vs_6_0、ps_6_0
	};

	/*-----------------------------------------------------------------------------------------
	 * ShaderCompiler
	 * - DXCを使用してHLSLソースをDXILバイナリへ変換する
	 * - Reflection、RootSignature、Pipeline生成は担当しない
	 *---------------------------------------------------------------------------------------*/
	class ShaderCompiler final {
	public:
		/**
		 * \brief HLSLをDXCでCompileしDXIL Binaryへ変換する
		 * \param desc Source、entry point、target profile
		 * \return Compile済みDXIL。警告または失敗時は診断をLogger/Errorへ渡す
		 * \note ReflectionやShader cacheは担当しない
		 */
		[[nodiscard]] Result<std::vector<uint8_t>> Compile(const ShaderCompileDesc& desc) const;
	};

} // namespace NexusEngine
