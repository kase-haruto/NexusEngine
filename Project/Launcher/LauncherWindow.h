#pragma once
#include "Platform/Window/ProjectWindow.h"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <functional>
#include <filesystem>
#include <vector>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * LauncherWindow
 * - Launcher専用の軽量UI描画とウィンドウを所有する
 * - Project操作やEditorの描画基盤には依存しない
 *---------------------------------------------------------------------------------------*/
class LauncherWindow final {
public:
    ~LauncherWindow();
    /** \brief WindowとImGuiを初期化する。dataRootは内蔵フォントだけに使う */
    [[nodiscard]] Result<void> Initialize(const std::filesystem::path& dataRoot, bool visible = true);
    /** \brief UIを描画する。終了要求時はfalseを返す */
    [[nodiscard]] Result<bool> Frame(const std::function<void()>& draw, const std::filesystem::path& capture = {});
    /** \brief 最後の描画フレームをPNGへ保存する */
    [[nodiscard]] Result<void> Capture(const std::filesystem::path& destination);
    [[nodiscard]] HWND Handle() const { return window_.GetNativeHandle(); }
    /** \brief 最初のUI描画が完成した後でProject Browserを表示する */
    void Show() { window_.SetVisible(true); }
private:
    [[nodiscard]] Result<void> CreateTarget();
    static bool Message(void*, HWND, UINT, WPARAM, LPARAM);
    ProjectWindow window_; //< Deviceより長く生存するウィンドウ
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    std::vector<std::vector<char>> fonts_; //< Atlasが参照するフォントのメモリ
    bool imgui_ = false, win32_ = false, renderer_ = false;
    float dpi_ = 1.0f; //< スタイルの基準DPI
};
}
