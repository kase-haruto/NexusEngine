#include "BakedLightingData.h"
#include <Windows.h>
#include <cstring>
#include <fstream>
#include <span>
namespace NexusEngine {
	namespace {
		struct Header {
			char					magic[8]{'N', 'X', 'G', 'I', '0', '0', '0', '1'};
			uint64_t				signature = 0;
			uint32_t				atlas	  = 0;
			std::array<uint32_t, 3> counts{};
			std::array<float, 3>	origin{}, spacing{};
			uint32_t				bindings = 0, reserved = 0;
			uint64_t				checksum = 0;
		};
		static_assert(sizeof(Header) == 72 && sizeof(BakedLightmapBinding) == 96);
		uint64_t Checksum(const BakedLightingAsset& a) {
			uint64_t hash = 1469598103934665603ull;
			auto	 add  = [&](const auto& values) { for(auto b:std::as_bytes(std::span(values))) { hash^=std::to_integer<uint8_t>(b); hash*=1099511628211ull; } };
			add(std::array{a.signature});
			add(std::array{a.atlasSize});
			add(a.probeCounts);
			add(a.origin);
			add(a.spacing);
			add(a.lightmap);
			add(a.probes);
			add(a.distances);
			add(a.bindings);
			return hash;
		}
		bool Valid(const BakedLightingAsset& a) {
			if(!a.atlasSize || a.atlasSize > 4096 || a.bindings.size() > 65536) return false;
			uint64_t count = 1;
			for(size_t i = 0; i < 3; ++i) {
				if(a.probeCounts[i] < 2 || a.probeCounts[i] > 32 || !std::isfinite(a.origin[i]) || !std::isfinite(a.spacing[i]) || a.spacing[i] <= 0) return false;
				count *= a.probeCounts[i];
			}
			if(count > 4096 || a.lightmap.size() != size_t(a.atlasSize) * a.atlasSize || a.probes.size() != count * 10 || a.distances.size() != count * 96) return false;
			for(const auto* values : {&a.lightmap, &a.probes, &a.distances})
				for(const auto& value : *values)
					for(float v : value)
						if(!std::isfinite(v)) return false;
			for(const auto& binding : a.bindings) {
				for(float v : binding.world.elements)
					if(!std::isfinite(v)) return false;
				for(float v : binding.scaleBias)
					if(!std::isfinite(v)) return false;
			}
			return true;
		}
	} // namespace
	Result<BakedLightingAsset> LoadBakedLighting(const std::filesystem::path& path) {
		auto		  fail = [&]() -> Result<BakedLightingAsset> { return std::unexpected(Error(ErrorCategory::Resource, 91, "Invalid baked lighting cache: " + path.string())); };
		std::ifstream file(path, std::ios::binary);
		Header		  h;
		if(!file.read(reinterpret_cast<char*>(&h), sizeof(h)) || std::memcmp(h.magic, "NXGI0001", 8) || !h.atlas || h.atlas > 4096 || h.bindings > 65536) return fail();
		uint64_t count = 1;
		for(uint32_t c : h.counts) {
			if(c < 2 || c > 32) return fail();
			count *= c;
		}
		if(count > 4096) return fail();
		// 寸法だけを偽装した短いファイルで巨大な配列を確保しない。
		const uint64_t expected = sizeof(Header) + (uint64_t(h.atlas) * h.atlas + count * 106) * 16 + uint64_t(h.bindings) * sizeof(BakedLightmapBinding);
		file.seekg(0, std::ios::end);
		if(file.tellg() != static_cast<std::streamoff>(expected)) return fail();
		file.seekg(sizeof(Header));
		BakedLightingAsset a;
		a.signature	  = h.signature;
		a.atlasSize	  = h.atlas;
		a.probeCounts = h.counts;
		a.origin	  = h.origin;
		a.spacing	  = h.spacing;
		a.lightmap.resize(size_t(h.atlas) * h.atlas);
		a.probes.resize(size_t(count) * 10);
		a.distances.resize(size_t(count) * 96);
		a.bindings.resize(h.bindings);
		auto read = [&](auto& values) { return bool(file.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(values[0])))); };
		if(!read(a.lightmap) || !read(a.probes) || !read(a.distances) || !read(a.bindings) || file.peek() != std::char_traits<char>::eof() || !Valid(a) || Checksum(a) != h.checksum) return fail();
		return a;
	}
	Result<void> SaveBakedLighting(const BakedLightingAsset& a, const std::filesystem::path& path) {
		auto fail = [&]() -> Result<void> { return std::unexpected(Error(ErrorCategory::Resource, 91, "Failed to save baked lighting: " + path.string())); };
		if(!Valid(a) || path.empty()) return fail();
		std::error_code ec;
		if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
		if(ec) return fail();
		auto temporary = path;
		temporary += L".tmp";
		Header h;
		h.signature = a.signature;
		h.atlas		= a.atlasSize;
		h.counts	= a.probeCounts;
		h.origin	= a.origin;
		h.spacing	= a.spacing;
		h.bindings	= static_cast<uint32_t>(a.bindings.size());
		h.checksum	= Checksum(a);
		std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
		file.write(reinterpret_cast<const char*>(&h), sizeof(h));
		auto write = [&](const auto& values) { file.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(values[0]))); };
		write(a.lightmap);
		write(a.probes);
		write(a.distances);
		write(a.bindings);
		file.close();
		if(!file) return fail();
		// 成果物を最後まで書けた場合だけ旧キャッシュを置換する。型は公開ヘッダーへ漏らさない。
		if(!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return fail();
		return {};
	}
} // namespace NexusEngine
