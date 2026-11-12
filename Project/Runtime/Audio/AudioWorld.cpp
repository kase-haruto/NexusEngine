#include "AudioWorld.h"
#include "Runtime/Application/ApplicationPaths.h"
#include <Windows.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <thread>
#include <vector>
#pragma comment(lib,"xaudio2.lib")

namespace NexusEngine {
namespace {
Error Failure(const char* message,HRESULT result=S_OK) { return Error(ErrorCategory::Resource,270,message,result); }
uint16_t U16(const uint8_t* data) { uint16_t value; std::memcpy(&value,data,sizeof(value)); return value; }
uint32_t U32(const uint8_t* data) { uint32_t value; std::memcpy(&value,data,sizeof(value)); return value; }
/** XAudio2 voiceはCOM objectではないため、ReleaseでなくDestroyVoiceを使用する。 */
struct DestroyVoice {
    void operator()(IXAudio2Voice* voice) const noexcept { if(voice) voice->DestroyVoice(); }
};
/*-----------------------------------------------------------------------------------------
 * AudioClip
 * - 検証済みWAVの固定formatとsample bufferを所有する
 * - bufferは最後のSource voiceのDestroyVoice終了まで保持し、Audio threadの参照を守る
 *---------------------------------------------------------------------------------------*/
struct AudioClip {
    WAVEFORMATEX format{}; //< XAudio2へ渡す値。圧縮formatや可変長extensionは受け付けない
    std::vector<uint8_t> samples; //< 非streaming再生用の所有buffer
};
/** \brief RIFFのchunk境界と演算を検証し、PCM/float WAVだけを受け入れる */
Result<std::shared_ptr<AudioClip>> ReadClip(const std::filesystem::path& file) {
    std::ifstream stream(file,std::ios::binary|std::ios::ate);
    if(!stream) return std::unexpected(Failure("Cannot open WAV asset."));
    const auto size=stream.tellg();
    if(size<12 || size>32*1024*1024) return std::unexpected(Failure("WAV asset must be between 12 bytes and 32 MiB."));
    std::vector<uint8_t> data(static_cast<size_t>(size));
    stream.seekg(0); stream.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size()));
    if(!stream || std::memcmp(data.data(),"RIFF",4) || std::memcmp(data.data()+8,"WAVE",4) ||
        static_cast<uint64_t>(U32(data.data()+4))+8!=data.size()) return std::unexpected(Failure("Invalid WAV RIFF header."));
    auto clip=std::make_shared<AudioClip>(); bool hasFormat=false,hasData=false;
    for(size_t cursor=12;cursor<data.size();) {
        if(data.size()-cursor<8) return std::unexpected(Failure("Truncated WAV chunk."));
        const auto count=static_cast<size_t>(U32(data.data()+cursor+4));
        if(count>data.size()-cursor-8) return std::unexpected(Failure("WAV chunk exceeds its file."));
        const auto* bytes=data.data()+cursor+8;
        if(!std::memcmp(data.data()+cursor,"fmt ",4)) {
            if(hasFormat || count<16) return std::unexpected(Failure("Invalid WAV format chunk."));
            hasFormat=true;
            auto& format=clip->format;
            format.wFormatTag=U16(bytes); format.nChannels=U16(bytes+2); format.nSamplesPerSec=U32(bytes+4);
            format.nAvgBytesPerSec=U32(bytes+8); format.nBlockAlign=U16(bytes+12); format.wBitsPerSample=U16(bytes+14);
            const bool pcm=format.wFormatTag==WAVE_FORMAT_PCM && (format.wBitsPerSample==8 || format.wBitsPerSample==16 || format.wBitsPerSample==24 || format.wBitsPerSample==32);
            const bool floating=format.wFormatTag==WAVE_FORMAT_IEEE_FLOAT && format.wBitsPerSample==32;
            if((!pcm && !floating) || format.nChannels<1 || format.nChannels>2 || format.nSamplesPerSec<8000 || format.nSamplesPerSec>192000 ||
                format.nBlockAlign!=format.nChannels*(format.wBitsPerSample/8) || format.nAvgBytesPerSec!=format.nSamplesPerSec*format.nBlockAlign)
                return std::unexpected(Failure("Unsupported or inconsistent WAV format."));
        } else if(!std::memcmp(data.data()+cursor,"data",4)) {
            if(hasData || !count) return std::unexpected(Failure("Invalid WAV data chunk."));
            hasData=true; clip->samples.assign(bytes,bytes+count);
        }
        cursor+=8+count;
        if(count&1) { if(cursor==data.size()) return std::unexpected(Failure("Missing WAV chunk padding.")); ++cursor; }
    }
    if(!hasFormat || !hasData || clip->samples.size()%clip->format.nBlockAlign) return std::unexpected(Failure("Incomplete WAV sample frames."));
    if(clip->format.wFormatTag==WAVE_FORMAT_IEEE_FLOAT) {
        for(size_t index=0;index<clip->samples.size();index+=4) {
            float value; std::memcpy(&value,clip->samples.data()+index,4);
            if(!std::isfinite(value)) return std::unexpected(Failure("WAV contains nonfinite float samples."));
        }
    }
    return clip;
}
/** \brief Worldを作り直しても古いVoiceが復活しないProcess共通の非再利用tokenを発行する */
uint64_t NextVoice() {
    static std::atomic<uint64_t> next{1};
    auto value=next.load();
    while(value && value!=UINT64_MAX) if(next.compare_exchange_weak(value,value+1)) return value;
    return 0;
}
}
/*-----------------------------------------------------------------------------------------
 * AudioWorld::Impl
 * - デバイスとVoice/sampleの寿命を管理し、ゲーム側へ所有権を移さない
 * - 既存Projectのpath解決を使い、WAVのlanguage別保存形式を作らない
 *---------------------------------------------------------------------------------------*/
struct AudioWorld::Impl {
    struct Voice {
        uint64_t owner; //< services clientの寿命に追従する所有者
        std::shared_ptr<AudioClip> clip; //< sourceより先に宣言し、DestroyVoice後までsampleを保持する
        std::unique_ptr<IXAudio2SourceVoice,DestroyVoice> source; //< 停止/完了/owner終了時に同期解放する
    };
    std::filesystem::path root; //< Projectの絶対Root。外部pathとlinkによる逸脱を拒否する
    std::thread::id thread=std::this_thread::get_id(); //< API・COM破棄を同じthreadに限定する
    bool com=false,paused=false; //< COMの参照数を取得した場合だけCoUninitializeする
    Microsoft::WRL::ComPtr<IXAudio2> engine; //< masterとsource voiceより長く保持する
    std::unique_ptr<IXAudio2MasteringVoice,DestroyVoice> master; //< 出力先default device
    std::map<uint64_t,Voice> voices; //< 最大128 voice。ゲームにpointerを公開しない
    std::map<std::filesystem::path,std::shared_ptr<AudioClip>> clips; //< 最大64 MiBのNative共通asset cache
    explicit Impl(std::filesystem::path value):root(std::move(value)) {}
    ~Impl() {
        voices.clear(); clips.clear(); master.reset(); engine.Reset();
        if(com) CoUninitialize();
    }
    /** \brief デバイスは最初の再生でだけ作る。headless/音声未使用のRuntimeを失敗させない */
    Result<void> Initialize() {
        if(engine) return {};
        if(!com) {
            const auto result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            if(FAILED(result) && result!=RPC_E_CHANGED_MODE) return std::unexpected(Failure("Cannot initialize Audio COM.",result));
            com=SUCCEEDED(result);
        }
        auto result=XAudio2Create(engine.GetAddressOf(),0,XAUDIO2_DEFAULT_PROCESSOR);
        if(FAILED(result)) return std::unexpected(Failure("Cannot initialize XAudio2.",result));
        IXAudio2MasteringVoice* voice=nullptr;
        result=engine->CreateMasteringVoice(&voice);
        if(FAILED(result)) { engine.Reset(); return std::unexpected(Failure("Cannot open the default Audio output device.",result)); }
        master.reset(voice);
        if(paused) engine->StopEngine();
        return {};
    }
    /** \brief 完了voiceをquery/再生境界で回収し、sampleはDestroyVoice後に解放する */
    void Collect() {
        for(auto it=voices.begin();it!=voices.end();) {
            XAUDIO2_VOICE_STATE state{}; it->second.source->GetState(&state,XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if(state.BuffersQueued==0) it=voices.erase(it); else ++it;
        }
    }
};
AudioWorld::AudioWorld(std::filesystem::path root):impl_(std::make_unique<Impl>(std::move(root))) {}
AudioWorld::~AudioWorld()=default;
Result<uint64_t> AudioWorld::Play(uint64_t owner,const std::filesystem::path& asset,float volume,bool loop) {
    if(impl_->thread!=std::this_thread::get_id()) return std::unexpected(Failure("Audio requires its owner thread."));
    if(!owner || !std::isfinite(volume) || volume<0 || volume>4) return std::unexpected(Failure("Invalid Audio owner or volume."));
    auto path=ResolveProjectPath(impl_->root,asset); if(!path) return std::unexpected(path.error());
    if(path->extension()!=L".wav" && path->extension()!=L".WAV") return std::unexpected(Failure("Audio currently accepts PCM/float WAV assets only."));
    impl_->Collect();
    if(impl_->voices.size()>=128) return std::unexpected(Failure("Audio voice capacity exceeded."));
    auto found=impl_->clips.find(*path);
    if(found==impl_->clips.end()) {
        auto loaded=ReadClip(*path); if(!loaded) return std::unexpected(loaded.error());
        size_t used=0;
        for(auto it=impl_->clips.begin();it!=impl_->clips.end();) {
            if(it->second.use_count()==1) it=impl_->clips.erase(it);
            else { used+=it->second->samples.size(); ++it; }
        }
        if(used+(*loaded)->samples.size()>64*1024*1024) return std::unexpected(Failure("Audio asset cache capacity exceeded."));
        found=impl_->clips.emplace(*path,std::move(*loaded)).first;
    }
    auto initialized=impl_->Initialize(); if(!initialized) return std::unexpected(initialized.error());
    Impl::Voice voice{owner,found->second,{}};
    IXAudio2SourceVoice* source=nullptr;
    auto result=impl_->engine->CreateSourceVoice(&source,&voice.clip->format);
    if(FAILED(result)) return std::unexpected(Failure("Cannot create Audio source voice.",result));
    voice.source.reset(source);
    XAUDIO2_BUFFER buffer{}; buffer.Flags=XAUDIO2_END_OF_STREAM; buffer.AudioBytes=static_cast<UINT32>(voice.clip->samples.size());
    buffer.pAudioData=voice.clip->samples.data(); buffer.LoopCount=loop ? XAUDIO2_LOOP_INFINITE : 0;
    result=source->SetVolume(volume);
    if(SUCCEEDED(result)) result=source->SubmitSourceBuffer(&buffer);
    if(SUCCEEDED(result)) result=source->Start();
    if(FAILED(result)) return std::unexpected(Failure("Cannot start Audio playback.",result));
    const auto token=NextVoice(); if(!token) return std::unexpected(Failure("Audio voice token space exhausted."));
    impl_->voices.emplace(token,std::move(voice)); return token;
}
Result<void> AudioWorld::Stop(uint64_t owner,uint64_t voice) {
    if(impl_->thread!=std::this_thread::get_id()) return std::unexpected(Failure("Audio requires its owner thread."));
    auto found=impl_->voices.find(voice);
    if(found==impl_->voices.end() || found->second.owner!=owner) return std::unexpected(Failure("Audio voice is no longer owned by this client."));
    impl_->voices.erase(found); return {};
}
Result<bool> AudioWorld::IsPlaying(uint64_t owner,uint64_t voice) {
    if(impl_->thread!=std::this_thread::get_id()) return std::unexpected(Failure("Audio requires its owner thread."));
    auto found=impl_->voices.find(voice);
    if(found==impl_->voices.end()) return false;
    if(found->second.owner!=owner) return std::unexpected(Failure("Audio voice belongs to another client."));
    XAUDIO2_VOICE_STATE state{}; found->second.source->GetState(&state,XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return state.BuffersQueued!=0;
}
Result<void> AudioWorld::SetVolume(uint64_t owner,uint64_t voice,float volume) {
    if(impl_->thread!=std::this_thread::get_id() || !std::isfinite(volume) || volume<0 || volume>4) return std::unexpected(Failure("Invalid Audio thread or volume."));
    auto found=impl_->voices.find(voice);
    if(found==impl_->voices.end() || found->second.owner!=owner) return std::unexpected(Failure("Invalid Audio voice owner."));
    const auto result=found->second.source->SetVolume(volume);
    if(FAILED(result)) return std::unexpected(Failure("Cannot set Audio volume.",result));
    return {};
}
void AudioWorld::StopOwner(uint64_t owner) noexcept {
    for(auto it=impl_->voices.begin();it!=impl_->voices.end();) {
        if(it->second.owner==owner) it=impl_->voices.erase(it); else ++it;
    }
}
Result<void> AudioWorld::Pause(bool paused) {
    if(impl_->thread!=std::this_thread::get_id()) return std::unexpected(Failure("Audio requires its owner thread."));
    if(impl_->engine) {
        if(paused) impl_->engine->StopEngine();
        else { const auto result=impl_->engine->StartEngine(); if(FAILED(result)) return std::unexpected(Failure("Cannot resume Audio engine.",result)); }
    }
    impl_->paused=paused; return {};
}
}
