#include "ProjectThumbnailService.h"
#include "Foundation/Logging/Logger.h"
#include "Graphics/Renderer/GraphicsSystem.h"
#include "Platform/FileSystem/AtomicFile.h"
#include "Runtime/Project/ProjectContext.h"
#include "Runtime/Project/ProjectJson.h"
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>

namespace NexusEngine {
namespace {
using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;
constexpr UINT kWidth = 640, kHeight = 360;

// COMの初期化と後始末は画像を処理するスレッド内で対にする。
struct ComScope {
    HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED); //< COM利用可否
    ~ComScope() {
        if(SUCCEEDED(status)) {
            CoUninitialize();
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
// 完全にdecodeできる共通画像だけを再利用する
////////////////////////////////////////////////////////////////////////////////
bool ValidPng(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if(error || !size || size > 8 * 1024 * 1024) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if(!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        return false;
    }
    // 小さいPNGのIHDRが巨大に壊れている場合にも、decode前の確保量を制限する。
    constexpr uint8_t signature[]{137, 80, 78, 71, 13, 10, 26, 10};
    if(bytes.size() < 24 || !std::equal(std::begin(signature), std::end(signature), bytes.begin())) {
        return false;
    }
    const auto dimension = [&](size_t offset) {
        return (static_cast<uint32_t>(bytes[offset]) << 24) | (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
            (static_cast<uint32_t>(bytes[offset + 2]) << 8) | static_cast<uint32_t>(bytes[offset + 3]);
    };
    if(dimension(16) != kWidth || dimension(20) != kHeight) {
        return false;
    }
    const auto image = WicImageDecoder{}.Decode(bytes);
    return image && image->width == kWidth && image->height == kHeight;
}

////////////////////////////////////////////////////////////////////////////////
// SDRを縦横比を維持してWICで縮小し、PNGをメモリへエンコードする
////////////////////////////////////////////////////////////////////////////////
bool Encode(const DecodedImage& image, std::string& png) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> bgra;
    const double scale = std::min(static_cast<double>(kWidth) / image.width, static_cast<double>(kHeight) / image.height);
    const UINT width = std::clamp(static_cast<UINT>(image.width * scale + .5), 1u, kWidth);
    const UINT height = std::clamp(static_cast<UINT>(image.height * scale + .5), 1u, kHeight);
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateBitmapFromMemory(image.width, image.height, GUID_WICPixelFormat32bppRGBA,
            image.width * 4, static_cast<UINT>(image.rgbaPixels.size()), const_cast<BYTE*>(image.rgbaPixels.data()), &bitmap)) ||
        FAILED(factory->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(bitmap.Get(), width, height, WICBitmapInterpolationModeFant)) ||
        FAILED(factory->CreateFormatConverter(&bgra)) ||
        FAILED(bgra->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
        return false;
    }

    // Letterboxを不透明な黒で初期化し、元のViewport投影を保つ。
    std::vector<uint8_t> canvas(kWidth * kHeight * 4, 0);
    for(size_t alpha = 3; alpha < canvas.size(); alpha += 4) {
        canvas[alpha] = 255;
    }
    std::vector<uint8_t> scaled(width * height * 4);
    if(FAILED(bgra->CopyPixels(nullptr, width * 4, static_cast<UINT>(scaled.size()), scaled.data()))) {
        return false;
    }
    for(UINT y = 0; y < height; ++y) {
        auto* row = canvas.data() + ((y + (kHeight - height) / 2) * kWidth + (kWidth - width) / 2) * 4;
        // Scalerの内部変換に依存せず、PNG encoderに渡すBGRAを明示する。
        std::copy_n(scaled.data() + y * width * 4, width * 4, row);
        for(UINT x = 0; x < width; ++x) {
            row[x * 4 + 3] = 255;
        }
    }

    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    auto format = GUID_WICPixelFormat32bppBGRA;
    if(FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(kWidth, kHeight)) || FAILED(frame->SetPixelFormat(&format)) ||
        format != GUID_WICPixelFormat32bppBGRA ||
        FAILED(frame->WritePixels(kHeight, kWidth * 4, static_cast<UINT>(canvas.size()), canvas.data())) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        return false;
    }
    STATSTG stat{};
    LARGE_INTEGER start{};
    if(FAILED(stream->Stat(&stat, STATFLAG_NONAME)) || stat.cbSize.QuadPart > 8 * 1024 * 1024 ||
        FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr))) {
        return false;
    }
    png.resize(static_cast<size_t>(stat.cbSize.QuadPart));
    ULONG read = 0;
    return SUCCEEDED(stream->Read(png.data(), static_cast<ULONG>(png.size()), &read)) && read == png.size();
}

////////////////////////////////////////////////////////////////////////////////
// Projectの保存と共通画像の初回公開を分離する
////////////////////////////////////////////////////////////////////////////////
bool Save(const std::filesystem::path& file, const DecodedImage& image, bool shared) {
    ComScope com;
    if(FAILED(com.status)) {
        return false;
    }
    std::string png;
    if(!Encode(image, png)) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    if(error) {
        return false;
    }
    if(!shared) {
        return WriteFileAtomically(file, png).has_value();
    }
    // 別Editorが先に正常な共通画像を公開した場合はその画像を保持する。
    return WithFileLock(file, [&]() -> Result<void> {
        if(ValidPng(file)) {
            return {};
        }
        return WriteFileAtomically(file, png);
    }).has_value();
}

////////////////////////////////////////////////////////////////////////////////
// 共有キャッシュのパス構成要素を単一の安全なファイル名に限定する
////////////////////////////////////////////////////////////////////////////////
bool SafeSegment(const std::string& value, bool version = false) {
    const auto allowed = version ? "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.+" :
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.";
    return !value.empty() && value.back() != '.' && value.find_first_not_of(allowed) == std::string::npos;
}
}

////////////////////////////////////////////////////////////////////////////////
// 初期Levelの生成時ハッシュを照合し、編集済みのLevelを共通キャッシュから除外する
////////////////////////////////////////////////////////////////////////////////
void ProjectThumbnailService::Initialize(const ApplicationPaths& paths, const std::filesystem::path& initialLevel) {
    if(!paths.project) {
        return;
    }
    try {
        auto directory = ResolveProjectPath(paths.projectRoot, ".nexus-preview");
        if(!directory) {
            return;
        }
        projectFile_ = *directory / "ProjectThumbnail.png";
        auto marker = ProjectJson::Read(*directory / "Template.json");
        if(!marker) {
            return;
        }
        const auto id = ProjectJson::Text(*marker, "id");
        const auto& engine = paths.project->Descriptor().engine;
        const auto version = engine.version.empty() ? "development" : engine.version;
        wchar_t* local = nullptr;
        size_t count = 0;
        _wdupenv_s(&local, &count, L"LOCALAPPDATA");
        const std::unique_ptr<wchar_t, decltype(&std::free)> env(local, &std::free);
        if(!local || !std::filesystem::path(local).is_absolute() || !SafeSegment(id) || !SafeSegment(engine.id) || !SafeSegment(version, true)) {
            return;
        }
        defaultFile_ = std::filesystem::path(local) / "NexusEngine/Projects/Thumbnails" /
            engine.id / version / ("ProjectThumbnail_" + id + ".png");
        if(!SameFilePath(initialLevel, paths.project->StartupLevel()) || ValidPng(defaultFile_)) {
            return;
        }
        const auto hash = Sha256File(paths.project->StartupLevel());
        defaultPending_ = hash && *hash == ProjectJson::Text(*marker, "initialLevelSha256");
        initialDeadline_ = Clock::now() + 8s;
    } catch(...) {
        NEXUS_LOG_WARNING("ProjectThumbnail", "Could not initialize project thumbnails.");
    }
}

////////////////////////////////////////////////////////////////////////////////
// 保存通知はGPU側の処理と独立し、最新世代へ集約する
////////////////////////////////////////////////////////////////////////////////
void ProjectThumbnailService::ObserveSave(uint64_t revision) noexcept {
    if(saveRevision_ != revision) {
        saveRevision_ = revision;
        saveDeadline_ = Clock::now() + 2s;
        projectPending_ = Enabled();
    }
}

////////////////////////////////////////////////////////////////////////////////
// Projectを開いていないホストでは撮影要求を生成しない
////////////////////////////////////////////////////////////////////////////////
bool ProjectThumbnailService::Enabled() const noexcept {
    return !projectFile_.empty();
}

////////////////////////////////////////////////////////////////////////////////
// 保存したSceneの切替を、コピー予約までFrameをまたいで待つ
////////////////////////////////////////////////////////////////////////////////
bool ProjectThumbnailService::ReadyToSwitchScene() noexcept {
    if(Clock::now() >= saveDeadline_) {
        // 期限切れの要求を切替後の別Sceneへ持ち越さない。
        projectPending_ = false;
    }
    return !projectPending_;
}

////////////////////////////////////////////////////////////////////////////////
// CPU専用WorkerによるPNG公開を待つ。I/Oが止まった場合は期限で終了する
////////////////////////////////////////////////////////////////////////////////
bool ProjectThumbnailService::ReadyToExit(bool requested) noexcept {
    if(!requested) {
        return false;
    }
    if(exitDeadline_ == Clock::time_point{}) {
        defaultPending_ = false;
        projectPending_ = Enabled();
        exitDeadline_ = Clock::now() + 4s;
    }
    return Clock::now() >= exitDeadline_ ||
        (!projectPending_ && inFlight_ == Capture::None && (!writeDone_ || writeDone_->load() != WriteState::Running));
}

////////////////////////////////////////////////////////////////////////////////
// Frame Fenceはpollだけを行い、画像の変換とファイルI/OをWorkerへ引き渡す
////////////////////////////////////////////////////////////////////////////////
bool ProjectThumbnailService::PrepareCapture(GraphicsSystem& graphics, bool sceneReady) {
    if(writeDone_ && writeDone_->load() != WriteState::Running) {
        if(writeDone_->load() == WriteState::Failed) {
            NEXUS_LOG_WARNING("ProjectThumbnail", "Could not encode or publish the project thumbnail.");
        }
        writeDone_.reset();
    }
    if(inFlight_ != Capture::None) {
        if(auto image = graphics.CollectViewportCapture()) {
            StartWrite(std::move(*image));
            inFlight_ = Capture::None;
        }
    }
    if(defaultPending_ && Clock::now() >= initialDeadline_) {
        defaultPending_ = false;
    }
    if(!sceneReady || inFlight_ != Capture::None || writeDone_ ||
        (!defaultPending_ && !projectPending_) || !graphics.RequestViewportCapture()) {
        return false;
    }
    if(defaultPending_) {
        inFlight_ = Capture::Default;
        defaultPending_ = false;
    } else {
        inFlight_ = Capture::Project;
        projectPending_ = false;
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////
// 初期状態のコピーを記録したFrameからEditorの操作を開始する
////////////////////////////////////////////////////////////////////////////////
bool ProjectThumbnailService::WaitingForInitialFrame() const noexcept {
    return defaultPending_ && Clock::now() < initialDeadline_;
}

////////////////////////////////////////////////////////////////////////////////
// 停止時の期限を超えてもWorkerがGPUやEditorの所有物へ触れない
////////////////////////////////////////////////////////////////////////////////
void ProjectThumbnailService::StartWrite(DecodedImage image) {
    if(!image.width || !image.height) {
        NEXUS_LOG_WARNING("ProjectThumbnail", "Could not read the viewport image.");
        return;
    }
    const bool shared = inFlight_ == Capture::Default;
    auto file = shared ? defaultFile_ : projectFile_;
    try {
        auto done = std::make_shared<std::atomic<WriteState>>(WriteState::Running);
        std::thread([image = std::move(image), file = std::move(file), shared, done]() {
            auto state = WriteState::Failed;
            try {
                if(Save(file, image, shared)) {
                    state = WriteState::Succeeded;
                }
            } catch(...) {
                // 任意のプレビュー書込みの失敗はEditorの動作から分離する。
            }
            done->store(state);
        }).detach();
        writeDone_ = std::move(done);
    } catch(...) {
        NEXUS_LOG_WARNING("ProjectThumbnail", "Could not start the thumbnail writer.");
    }
}
}
