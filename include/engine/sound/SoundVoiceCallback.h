#pragma once
#include <xaudio2.h>

class SoundVoiceCallback : public IXAudio2VoiceCallback {
  public:
    // バッファ再生終了時に呼ばれる
    void STDMETHODCALLTYPE OnBufferEnd(void *context) override {
        auto *voice = reinterpret_cast<IXAudio2SourceVoice *>(context);
        if (voice) {
            voice->DestroyVoice();
        }
    }

    // 以下は未使用なので空実装
    void STDMETHODCALLTYPE OnStreamEnd() override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnBufferStart(void *) override {}
    void STDMETHODCALLTYPE OnLoopEnd(void *) override {}
    void STDMETHODCALLTYPE OnVoiceError(void *, HRESULT) override {}
};
