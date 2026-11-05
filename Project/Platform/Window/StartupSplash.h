#pragma once
#include "Foundation/Error/Result.h"
#include <memory>
#include <string>
#include <string_view>

namespace NexusEngine {
struct StartupSplashOptions {
    std::wstring title = L"NexusEngine"; //< 表示する製品名
    std::wstring subtitle; //< Project Browser / Editorなどの起動対象
    std::wstring version; //< 実行中ホストの版
    bool visible = true; //< 自動検証時は非表示Windowで描画する
};
/*-----------------------------------------------------------------------------------------
 * StartupSplash
 * - GPU初期化前に表示できる起動案内Windowを専用UIスレッドで所有する
 * - 読み込み処理・Project・Graphicsは所有せず、実際の起動段階を文字列で受け取る
 *---------------------------------------------------------------------------------------*/
class StartupSplash final {
public:
    StartupSplash();
    ~StartupSplash();
    StartupSplash(const StartupSplash&) = delete;
    StartupSplash& operator=(const StartupSplash&) = delete;
    /** \brief 起動案内を開始する。専用スレッドで生成が完了した結果を返す */
    [[nodiscard]] Result<void> Show(StartupSplashOptions options);
    /** \brief 読み込み状況を更新する。描画は所有UIスレッドへ通知する */
    void SetStatus(std::wstring_view text);
    /** \brief 自分のWindowとUIスレッドだけを終了する。繰り返し呼べる */
    void Close() noexcept;
private:
    class Impl;
    std::unique_ptr<Impl> impl_; //< Window・GDI・UIスレッドの寿命をまとめて管理する
};
}
