#pragma once
#include "Foundation/Error/Result.h"
#include <cstdint>
#include <filesystem>
#include <memory>

namespace NexusEngine {
/*-----------------------------------------------------------------------------------------
 * AudioWorld
 * - Level内のCPU音声asset cacheとXAudio2 voiceを所有する。言語別のAudio Managerは作らない
 * - VoiceはProcessで再利用しない整数Handleで識別し、Native pointerを公開しない
 * - 全操作と破棄を生成threadに限定する。Audio callbackからゲームコードを呼ばない
 *---------------------------------------------------------------------------------------*/
class AudioWorld final {
public:
    /** \brief Projectの絶対Rootを固定する。デバイス初期化は最初のPlayまで遅延する */
    explicit AudioWorld(std::filesystem::path root);
    /** \brief Source voice、音声data、Mastering voice、XAudio2、COMの順で解放する */
    ~AudioWorld();
    AudioWorld(const AudioWorld&)=delete;
    AudioWorld& operator=(const AudioWorld&)=delete;
    /** \brief Root内のPCM/IEEE float WAVを再生する。ownerはservices client寿命token */
    Result<uint64_t> Play(uint64_t owner,const std::filesystem::path& asset,float volume,bool loop);
    /** \brief 同じownerのvoiceを停止・破棄する。解放済みHandleは失敗する */
    Result<void> Stop(uint64_t owner,uint64_t voice);
    /** \brief 完了済み音声はfalse。別owner・未知Handleは失敗する */
    Result<bool> IsPlaying(uint64_t owner,uint64_t voice);
    /** \brief 同じownerの音量を更新する。0〜4の有限値に限定する */
    Result<void> SetVolume(uint64_t owner,uint64_t voice,float volume);
    /** \brief client失効時にそのclientが所有する全voiceを同期解放する */
    void StopOwner(uint64_t owner) noexcept;
    /** \brief Level一時停止。再開失敗時は停止状態を保ちエラーを返す */
    Result<void> Pause(bool paused);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_; //< XAudio2/COM ABIは公開SDKのheaderへ漏らさない
};
}
