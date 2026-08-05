#include "Shader.h"

#include <utility>

#include "ShaderReflector.h"

namespace NexusEngine {

	Result<void> Shader::Initialize(const ShaderCompileDesc& desc, const ShaderStage stage) {
		ShaderCompiler compiler;
		auto compileResult = compiler.Compile(desc);
		if(!compileResult) {
			return std::unexpected(std::move(compileResult.error()));
		}

		// ReflectionはCompile直後に一度だけ実行し、描画中は保存済みMetadataだけを参照する。
		ShaderReflector reflector;
		auto reflectionResult = reflector.Reflect(*compileResult, stage);
		if(!reflectionResult) {
			return std::unexpected(std::move(reflectionResult.error()));
		}
		bytecode_ = std::move(*compileResult);
		metadata_ = std::move(*reflectionResult);
		stage_ = stage;
		return {};
	}

	std::span<const uint8_t> Shader::GetBytecode() const noexcept { return bytecode_; }
	const ShaderMetadata& Shader::GetMetadata() const noexcept { return metadata_; }
	ShaderStage Shader::GetStage() const noexcept { return stage_; }

} // namespace NexusEngine
