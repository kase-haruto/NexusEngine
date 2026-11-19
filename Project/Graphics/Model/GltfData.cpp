#define _CRT_SECURE_NO_WARNINGS
#include "GltfAnimationMetadata.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <cstdlib>
#include <fstream>
#pragma warning(push, 0)
#define CGLTF_IMPLEMENTATION
#include "ThirdParty/cgltf/cgltf.h"
#pragma warning(pop)
#undef CGLTF_IMPLEMENTATION
#include "GltfFileAccess.h"

namespace NexusEngine {
namespace {
cgltf_result ReadGltfFile(const cgltf_memory_options* memory, const cgltf_file_options*, const char* path, cgltf_size* size, void** data) {
    try {
        auto file = std::filesystem::path(reinterpret_cast<const char8_t*>(path));
        std::error_code error;
        auto native = std::filesystem::absolute(file, error).lexically_normal().make_preferred().wstring();
        if(error) {
            return cgltf_result_file_not_found;
        }
        if(!native.starts_with(L"\\\\?\\")) {
            native = native.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + native.substr(2) : L"\\\\?\\" + native;
        }
        std::ifstream input(std::filesystem::path(native), std::ios::binary | std::ios::ate);
        if(!input) {
            return cgltf_result_file_not_found;
        }
        const auto length = input.tellg();
        if(length < 0) {
            return cgltf_result_io_error;
        }
        const auto count = size && *size ? *size : static_cast<cgltf_size>(length);
        if(count > static_cast<cgltf_size>(length)) {
            return cgltf_result_data_too_short;
        }
        void* bytes = memory->alloc_func ? memory->alloc_func(memory->user_data, count) : std::malloc(count);
        if(!bytes) {
            return cgltf_result_out_of_memory;
        }
        input.seekg(0);
        input.read(static_cast<char*>(bytes), static_cast<std::streamsize>(count));
        if(input.gcount() != static_cast<std::streamsize>(count)) {
            if(memory->free_func) {
                memory->free_func(memory->user_data, bytes);
            } else {
                std::free(bytes);
            }
            return cgltf_result_io_error;
        }
        if(size) {
            *size = count;
        }
        *data = bytes;
        return cgltf_result_success;
    } catch(const std::exception&) {
        return cgltf_result_io_error;
    }
}
void ReleaseGltfFile(const cgltf_memory_options* memory, const cgltf_file_options*, void* bytes) {
    if(memory->free_func) {
        memory->free_func(memory->user_data, bytes);
    } else {
        std::free(bytes);
    }
}
}
cgltf_options GltfFileOptions() {
    cgltf_options options{};
    options.file.read = ReadGltfFile;
    options.file.release = ReleaseGltfFile;
    return options;
}
Result<float> GltfAnimationDuration(const std::filesystem::path& file, const std::string& clip) {
    auto options = GltfFileOptions();
    cgltf_data* parsed = nullptr;
    const auto native = file.generic_u8string();
    const std::string path(reinterpret_cast<const char*>(native.data()), native.size());
    if(cgltf_parse_file(&options, path.c_str(), &parsed) != cgltf_result_success) {
        return std::unexpected(Error(ErrorCategory::Resource, 390, "Cannot read animation source metadata"));
    }
    const std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(parsed, cgltf_free);
    if(cgltf_load_buffers(&options, data.get(), path.c_str()) != cgltf_result_success) {
        return std::unexpected(Error(ErrorCategory::Resource, 390, "Cannot read animation time buffers"));
    }
    const cgltf_animation* selected = nullptr;
    for(cgltf_size index = 0; index < data->animations_count; ++index) {
        const auto& animation = data->animations[index];
        // 元Loaderと同じ無名clip識別規則を使う。
        const std::string name = animation.name && animation.name[0] ? animation.name : "Animation_" + std::to_string(index);
        if(name == clip) {
            if(selected) {
                return std::unexpected(Error(ErrorCategory::Resource, 390, "Ambiguous animation source clip"));
            }
            selected = &animation;
        }
    }
    if(!selected) {
        return std::unexpected(Error(ErrorCategory::Resource, 390, "Animation source clip was not found"));
    }
    float duration = 0;
    for(cgltf_size index = 0; index < selected->samplers_count; ++index) {
        const auto* input = selected->samplers[index].input;
        if(!input || input->type != cgltf_type_scalar || input->component_type != cgltf_component_type_r_32f) {
            return std::unexpected(Error(ErrorCategory::Resource, 390, "Invalid animation time accessor"));
        }
        float previous = -1;
        for(cgltf_size key = 0; key < input->count; ++key) {
            float time = 0;
            if(!cgltf_accessor_read_float(input, key, &time, 1) || !std::isfinite(time) || time < 0 || time <= previous) {
                return std::unexpected(Error(ErrorCategory::Resource, 390, "Invalid animation source times"));
            }
            previous = time;
            duration = (std::max)(duration, time);
        }
    }
    return duration;
}
}
