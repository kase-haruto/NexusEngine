#include "ModelLoader.h"

// c++
#include <charconv>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

// engine
#include "Foundation/Math/Vector3.h"

namespace NexusEngine {
	namespace {
		constexpr int32_t kFileOpenFailed = 1;
		constexpr int32_t kInvalidObj = 2;
		constexpr int32_t kModelTooLarge = 3;

		struct ObjVertexKey {
			std::size_t position = 0;
			int64_t texcoord = -1;
			int64_t normal = -1;

			[[nodiscard]] friend bool operator==(const ObjVertexKey&, const ObjVertexKey&) noexcept = default;
		};

		struct ObjVertexKeyHash {
			[[nodiscard]] std::size_t operator()(const ObjVertexKey& key) const noexcept {
				std::size_t hash = key.position;
				hash ^= static_cast<std::size_t>(key.texcoord + 1) + 0x9e3779b9U + (hash << 6) + (hash >> 2);
				hash ^= static_cast<std::size_t>(key.normal + 1) + 0x9e3779b9U + (hash << 6) + (hash >> 2);
				return hash;
			}
		};

		struct MeshBuilder {
			std::string name = "Mesh";
			std::vector<ModelVertex> vertices;
			std::vector<uint32_t> indices;
			std::unordered_map<ObjVertexKey, uint32_t, ObjVertexKeyHash> vertexBySource;
		};

		[[nodiscard]] Error MakeObjError(
			const std::filesystem::path& path,
			const std::size_t lineNumber,
			const std::string_view message) {
			std::ostringstream stream;
			stream << "OBJ load failed: " << path.string();
			if(lineNumber != 0) stream << " (line " << lineNumber << ')';
			stream << ": " << message;
			return Error(ErrorCategory::Resource, kInvalidObj, stream.str());
		}

		[[nodiscard]] Result<int64_t> ParseIndex(
			const std::string_view token,
			const std::size_t elementCount,
			const std::filesystem::path& path,
			const std::size_t lineNumber,
			const std::string_view semantic) {
			int64_t rawIndex = 0;
			const auto parse = std::from_chars(
				token.data(), token.data() + token.size(), rawIndex);
			if(token.empty() || parse.ec != std::errc {} ||
			   parse.ptr != token.data() + token.size() || rawIndex == 0) {
				return std::unexpected(MakeObjError(path, lineNumber,
					std::string("invalid face ") + std::string(semantic) + " index"));
			}

			const int64_t resolved = rawIndex > 0
				? rawIndex - 1
				: static_cast<int64_t>(elementCount) + rawIndex;
			if(resolved < 0 || resolved >= static_cast<int64_t>(elementCount)) {
				return std::unexpected(MakeObjError(path, lineNumber,
					std::string("face ") + std::string(semantic) + " index is out of range"));
			}
			return resolved;
		}

		// OBJはposition/texcoord/normalを別々にindex化するため、三要素を1つの頂点keyへ変換する。
		[[nodiscard]] Result<ObjVertexKey> ParseVertexKey(
			const std::string_view token,
			const std::size_t positionCount,
			const std::size_t texcoordCount,
			const std::size_t normalCount,
			const std::filesystem::path& path,
			const std::size_t lineNumber) {
			std::array<std::string_view, 3> parts;
			std::size_t partIndex = 0;
			std::size_t begin = 0;
			while(partIndex < parts.size()) {
				const std::size_t separator = token.find('/', begin);
				parts[partIndex++] = token.substr(begin, separator - begin);
				if(separator == std::string_view::npos) break;
				if(partIndex == parts.size()) {
					return std::unexpected(MakeObjError(path, lineNumber, "face vertex has too many indices"));
				}
				begin = separator + 1;
			}

			auto position = ParseIndex(parts[0], positionCount, path, lineNumber, "position");
			if(!position) return std::unexpected(std::move(position.error()));
			ObjVertexKey key { static_cast<std::size_t>(*position), -1, -1 };
			if(!parts[1].empty()) {
				auto texcoord = ParseIndex(parts[1], texcoordCount, path, lineNumber, "texcoord");
				if(!texcoord) return std::unexpected(std::move(texcoord.error()));
				key.texcoord = *texcoord;
			}
			if(!parts[2].empty()) {
				auto normal = ParseIndex(parts[2], normalCount, path, lineNumber, "normal");
				if(!normal) return std::unexpected(std::move(normal.error()));
				key.normal = *normal;
			}
			return key;
		}

		[[nodiscard]] Result<uint32_t> AddVertex(
			MeshBuilder& builder,
			const ObjVertexKey& key,
			const std::vector<Vector3>& positions,
			const std::vector<std::array<float, 2>>& texcoords,
			const std::vector<Vector3>& normals) {
			if(const auto found = builder.vertexBySource.find(key); found != builder.vertexBySource.end()) {
				return found->second;
			}
			if(builder.vertices.size() >= (std::numeric_limits<uint32_t>::max)()) {
				return std::unexpected(Error(ErrorCategory::Resource, kModelTooLarge,
					"OBJ mesh exceeds the UInt32 vertex index limit."));
			}
			const Vector3& position = positions[key.position];
			const Vector3 normal = key.normal >= 0 ? normals[static_cast<std::size_t>(key.normal)] : Vector3::Up();
			const std::array<float, 2> texcoord = key.texcoord >= 0
				? texcoords[static_cast<std::size_t>(key.texcoord)] : std::array<float, 2> {};
			const uint32_t index = static_cast<uint32_t>(builder.vertices.size());
			builder.vertices.push_back({
				{ position.x, position.y, position.z },
				{ normal.x, normal.y, normal.z },
				{ texcoord[0], texcoord[1] },
				{ 1.0f, 1.0f, 1.0f, 1.0f }
			});
			builder.vertexBySource.emplace(key, index);
			return index;
		}

		void CommitMesh(MeshBuilder& builder, ModelAssetData& model) {
			if(builder.indices.empty()) return;

			MeshAssetData mesh;
			mesh.vertexStride = sizeof(ModelVertex);
			mesh.vertexData.resize(builder.vertices.size() * sizeof(ModelVertex));
			std::memcpy(mesh.vertexData.data(), builder.vertices.data(), mesh.vertexData.size());
			mesh.indexFormat = IndexFormat::UInt32;
			mesh.indexData.resize(builder.indices.size() * sizeof(uint32_t));
			std::memcpy(mesh.indexData.data(), builder.indices.data(), mesh.indexData.size());
			mesh.submeshes.push_back({ 0, static_cast<uint32_t>(builder.indices.size()), 0, 0 });

			const uint32_t meshIndex = static_cast<uint32_t>(model.meshes.size());
			model.meshes.push_back(std::move(mesh));
			model.nodes.push_back({ builder.name, ModelNodeAssetData::kNoParent,
				ModelNodeTransform {}, { meshIndex } });
		}
	} // namespace

	/////////////////////////////////////////////////////////////////////////////////////////
	// OBJをCPU側ModelAssetDataへ読み込む
	/////////////////////////////////////////////////////////////////////////////////////////
	Result<ModelAssetData> ModelLoader::LoadObj(const std::filesystem::path& filePath) const {
		std::ifstream input(filePath);
		if(!input) {
			return std::unexpected(Error(ErrorCategory::FileSystem, kFileOpenFailed,
				"Failed to open model file: " + filePath.string()));
		}

		std::vector<Vector3> positions;
		std::vector<std::array<float, 2>> texcoords;
		std::vector<Vector3> normals;
		ModelAssetData model;
		MeshBuilder builder;
		std::string line;
		std::size_t lineNumber = 0;
		while(std::getline(input, line)) {
			++lineNumber;
			std::istringstream tokens(line);
			std::string command;
			tokens >> command;
			if(command.empty() || command.starts_with('#')) continue;

			if(command == "v") {
				Vector3 position;
				if(!(tokens >> position.x >> position.y >> position.z)) {
					return std::unexpected(MakeObjError(filePath, lineNumber, "invalid vertex position"));
				}
				positions.push_back(position);
				continue;
			}
			if(command == "vt") {
				std::array<float, 2> texcoord = {};
				if(!(tokens >> texcoord[0] >> texcoord[1])) {
					return std::unexpected(MakeObjError(filePath, lineNumber, "invalid texture coordinate"));
				}
				texcoords.push_back(texcoord);
				continue;
			}
			if(command == "vn") {
				Vector3 normal;
				if(!(tokens >> normal.x >> normal.y >> normal.z) || normal.LengthSquared() <= 0.0f) {
					return std::unexpected(MakeObjError(filePath, lineNumber, "invalid vertex normal"));
				}
				normals.push_back(normal.Normalized());
				continue;
			}

			if(command == "o" || command == "g") {
				CommitMesh(builder, model);
				builder = {};
				if(!(tokens >> builder.name)) builder.name = "Mesh";
				continue;
			}

			if(command != "f") continue;

			std::vector<uint32_t> polygon;
			std::string faceToken;
			while(tokens >> faceToken) {
				auto vertexKey = ParseVertexKey(
					faceToken, positions.size(), texcoords.size(), normals.size(), filePath, lineNumber);
				if(!vertexKey) return std::unexpected(std::move(vertexKey.error()));
				auto vertexIndex = AddVertex(builder, *vertexKey, positions, texcoords, normals);
				if(!vertexIndex) return std::unexpected(std::move(vertexIndex.error()));
				polygon.push_back(*vertexIndex);
			}
			if(polygon.size() < 3) {
				return std::unexpected(MakeObjError(filePath, lineNumber, "face has fewer than three vertices"));
			}

			// 凸polygonをfan triangulationし、元ファイルの巻き順を維持する。
			for(std::size_t index = 1; index + 1 < polygon.size(); ++index) {
				builder.indices.push_back(polygon[0]);
				builder.indices.push_back(polygon[index]);
				builder.indices.push_back(polygon[index + 1]);
			}
		}
		if(input.bad()) {
			return std::unexpected(Error(ErrorCategory::FileSystem, kFileOpenFailed,
				"Failed while reading model file: " + filePath.string()));
		}
		CommitMesh(builder, model);
		if(model.meshes.empty()) {
			return std::unexpected(MakeObjError(filePath, 0, "file contains no drawable faces"));
		}
		return model;
	}
} // namespace NexusEngine
