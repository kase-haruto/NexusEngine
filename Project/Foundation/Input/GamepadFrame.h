#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * GamepadSample
 * - Platformが採取した1台分の値。OS handleやdevice pointerをゲーム世界へ持ち込まない
 * - XInputの公開button配置と整数範囲だけを使用し、接続失敗は空sampleで表す
 *---------------------------------------------------------------------------------------*/
struct GamepadSample {
    bool connected=false; //< 接続の有無。focusと独立して検出する
    uint32_t buttons=0; //< 対応する14 buttonのbit mask
    int16_t leftX=0,leftY=0,rightX=0,rightY=0; //< 符号付き16 bitのstick値
    uint8_t leftTrigger=0,rightTrigger=0; //< 0〜255のtrigger値
};
/*-----------------------------------------------------------------------------------------
 * GamepadFrame
 * - 1台の接続・button edge・正規化analog値を所有するframe snapshot
 * - WindowとEditorの入力許可境界でedgeを再評価し、他のviewでの操作を漏らさない
 *---------------------------------------------------------------------------------------*/
struct GamepadFrame {
    static constexpr uint32_t ButtonMask=0xf3ff;
    bool connected=false,connectedThisFrame=false,disconnectedThisFrame=false; //< 接続はfocus喪失時にも維持する
    uint32_t down=0,pressed=0,released=0; //< 押下/解放は同じframeの全Systemで共有する
    std::array<float,2> leftStick{},rightStick{}; //< radial dead zone後の長さ0〜1。上は+Y
    float leftTrigger=0,rightTrigger=0; //< threshold除去後の0〜1
    /** \brief raw値を安全な範囲へ正規化し、前frameとの差からedgeを生成する */
    void Update(const GamepadSample& sample,bool focused) noexcept {
        GamepadFrame next; next.connected=sample.connected;
        if(sample.connected && focused) {
            next.down=sample.buttons&ButtonMask;
            next.leftStick=Stick(sample.leftX,sample.leftY,7849);
            next.rightStick=Stick(sample.rightX,sample.rightY,8689);
            next.leftTrigger=Trigger(sample.leftTrigger); next.rightTrigger=Trigger(sample.rightTrigger);
        }
        next.Gate(*this,focused);
        // pollより前のWM_KILLFOCUSで生成した解放を、空sampleで上書きしない。
        next.released|=released; *this=next;
    }
    /** \brief Game Viewの直前の許可状態と比較する。接続監視と入力許可を混同しない */
    void Gate(const GamepadFrame& previous,bool accepted) noexcept {
        connectedThisFrame=connected && !previous.connected;
        disconnectedThisFrame=!connected && previous.connected;
        if(!connected || !accepted) { down=0; leftStick={}; rightStick={}; leftTrigger=rightTrigger=0; }
        pressed=down&~previous.down; released=previous.down&~down;
    }
    /** \brief focus喪失で保持buttonを解放し、接続情報を残したままanalogを中立へ戻す */
    void LoseFocus() noexcept {
        released|=down; down=pressed=0; leftStick={}; rightStick={}; leftTrigger=rightTrigger=0;
    }
    /** \brief 非消費snapshotを配布した後で、次frame用のedgeだけを消す */
    void ClearTransitions() noexcept { pressed=released=0; connectedThisFrame=disconnectedThisFrame=false; }
private:
    /** \brief 円形dead zoneを除去し、対角入力も長さ1以内へ制限する */
    static std::array<float,2> Stick(int16_t rawX,int16_t rawY,int threshold) noexcept {
        const float x=float(rawX)/32767.0f,y=float(rawY)/32767.0f;
        const float length=std::sqrt(x*x+y*y),dead=float(threshold)/32767.0f;
        if(length<=dead) return {};
        const float magnitude=(std::min(length,1.0f)-dead)/(1.0f-dead);
        return {x/length*magnitude,y/length*magnitude};
    }
    /** \brief 30以下のノイズを除去し、残りのtrigger範囲を0〜1へ展開する */
    static float Trigger(uint8_t value) noexcept { return value<=30 ? 0.0f : float(value-30)/225.0f; }
};
}
