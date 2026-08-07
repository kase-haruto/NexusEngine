#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace NexusEngine {

	enum class ShaderStage : uint8_t { Vertex = 1, Pixel = 2 };
	enum class ShaderResourceType : uint8_t {
		ConstantBuffer,
		Texture,
		StructuredBuffer,
		RwTexture,
		RwStructuredBuffer,
		Sampler
	};

	struct ShaderResourceBinding {
		std::string name;
		ShaderResourceType type = ShaderResourceType::ConstantBuffer;
		uint32_t bindPoint = 0;
		uint32_t registerSpace = 0;
		uint32_t bindCount = 1;
		uint32_t constantBufferSize = 0;
		uint8_t stageMask = 0;
	};

	struct ShaderInputSemantic {
		std::string name;
		uint32_t semanticIndex = 0;
		uint32_t componentCount = 0;
	};

	struct ShaderMetadata {
		std::vector<ShaderResourceBinding> resources;
		std::vector<ShaderInputSemantic> inputs;
	};

	[[nodiscard]] constexpr uint8_t ToStageMask(const ShaderStage stage) noexcept {
		return static_cast<uint8_t>(stage);
	}

} // namespace NexusEngine
