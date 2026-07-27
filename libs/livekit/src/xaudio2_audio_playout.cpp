#include "audio_playout.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
#include <xaudio2.h>

namespace hvc::livekit::detail
{
namespace
{
constexpr std::uint32_t maximum_queued_audio_frames = 12;

[[nodiscard]] auto toAudioDevices(const std::vector<::livekit::AudioDeviceInfo>& devices)
    -> std::vector<client::AudioDevice>
{
    std::vector<client::AudioDevice> result;
    result.reserve(devices.size());
    for (const auto& device : devices)
    {
        result.push_back(client::AudioDevice{device.id, device.name});
    }
    return result;
}

class XAudio2Engine final
{
  public:
    XAudio2Engine()
    {
        IXAudio2* engine = nullptr;
        if (FAILED(XAudio2Create(&engine)))
        {
            throw std::runtime_error{"XAudio2 initialization failed"};
        }
        engine_.Attach(engine);
        if (FAILED(engine_->CreateMasteringVoice(&mastering_voice_)))
        {
            throw std::runtime_error{"XAudio2 mastering voice initialization failed"};
        }
    }

    ~XAudio2Engine()
    {
        if (mastering_voice_ != nullptr)
        {
            mastering_voice_->DestroyVoice();
        }
    }

    XAudio2Engine(const XAudio2Engine&) = delete;
    auto operator=(const XAudio2Engine&) -> XAudio2Engine& = delete;
    XAudio2Engine(XAudio2Engine&&) = delete;
    auto operator=(XAudio2Engine&&) -> XAudio2Engine& = delete;

    [[nodiscard]] auto engine() const noexcept -> IXAudio2*
    {
        return engine_.Get();
    }

    void selectDevice(const std::string& device_id)
    {
        const auto required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, device_id.data(),
                                                  static_cast<int>(device_id.size()), nullptr, 0);
        if (required <= 0)
        {
            throw std::runtime_error{"XAudio2 playout device ID is not valid UTF-8"};
        }
        std::wstring wide_id(static_cast<std::size_t>(required), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, device_id.data(),
                                static_cast<int>(device_id.size()), wide_id.data(),
                                required) != required)
        {
            throw std::runtime_error{"XAudio2 playout device ID conversion failed"};
        }

        if (mastering_voice_ != nullptr)
        {
            mastering_voice_->DestroyVoice();
            mastering_voice_ = nullptr;
        }
        if (FAILED(engine_->CreateMasteringVoice(&mastering_voice_, XAUDIO2_DEFAULT_CHANNELS,
                                                 XAUDIO2_DEFAULT_SAMPLERATE, 0, wide_id.c_str())))
        {
            static_cast<void>(engine_->CreateMasteringVoice(&mastering_voice_));
            throw std::runtime_error{"XAudio2 playout device switch failed"};
        }
    }

  private:
    Microsoft::WRL::ComPtr<IXAudio2> engine_;
    IXAudio2MasteringVoice* mastering_voice_{nullptr};
};

class XAudio2RemotePlayout final : public RemoteAudioPlayout, private IXAudio2VoiceCallback
{
  public:
    XAudio2RemotePlayout(XAudio2Engine& engine, const std::shared_ptr<::livekit::Track>& track,
                         float gain)
        : engine_(engine), stream_(::livekit::AudioStream::fromTrack(
                               track, ::livekit::AudioStream::Options{8, {}, {}})),
          gain_(gain)
    {
        if (stream_ == nullptr)
        {
            throw std::runtime_error{"LiveKit remote audio stream initialization failed"};
        }
        thread_ = std::jthread{[this](std::stop_token stop_token) { run(stop_token); }};
    }

    ~XAudio2RemotePlayout() override
    {
        stream_->close();
        thread_.request_stop();
        if (thread_.joinable())
        {
            thread_.join();
        }
        const std::scoped_lock lock{voice_mutex_};
        if (voice_ != nullptr)
        {
            static_cast<void>(voice_->Stop());
            static_cast<void>(voice_->FlushSourceBuffers());
            voice_->DestroyVoice();
            voice_ = nullptr;
        }
        releasePendingBuffers();
    }

    void setGain(float gain) noexcept override
    {
        gain_.store(gain);
        const std::scoped_lock lock{voice_mutex_};
        if (voice_ != nullptr)
        {
            static_cast<void>(voice_->SetVolume(gain));
        }
    }

  private:
    void run(std::stop_token stop_token) noexcept
    {
        try
        {
            ::livekit::AudioFrameEvent event;
            while (!stop_token.stop_requested() && stream_->read(event))
            {
                if (event.frame.totalSamples() == 0 || event.frame.sampleRate() <= 0 ||
                    event.frame.numChannels() <= 0 ||
                    !ensureVoice(event.frame.sampleRate(), event.frame.numChannels()))
                {
                    continue;
                }
                waitForQueue(stop_token);
                if (stop_token.stop_requested())
                {
                    return;
                }

                auto samples = std::make_unique<std::vector<std::int16_t>>(event.frame.data());
                auto* samples_pointer = samples.get();
                {
                    const std::scoped_lock lock{buffers_mutex_};
                    pending_buffers_.insert(samples_pointer);
                }
                samples_pointer = samples.release();
                XAUDIO2_BUFFER buffer{};
                buffer.AudioBytes =
                    static_cast<UINT32>(samples_pointer->size() * sizeof(std::int16_t));
                buffer.pAudioData = reinterpret_cast<const BYTE*>(samples_pointer->data());
                buffer.pContext = samples_pointer;

                const std::scoped_lock lock{voice_mutex_};
                if (voice_ == nullptr || FAILED(voice_->SubmitSourceBuffer(&buffer)))
                {
                    releaseBuffer(samples_pointer);
                    return;
                }
            }
        }
        catch (...)
        {
            // Audio callback threads must never terminate the process on allocation/SDK failures.
            stream_->close();
        }
    }

    [[nodiscard]] auto ensureVoice(int sample_rate, int channels) noexcept -> bool
    {
        const std::scoped_lock lock{voice_mutex_};
        if (voice_ != nullptr)
        {
            return sample_rate_ == sample_rate && channels_ == channels;
        }

        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = static_cast<WORD>(channels);
        format.nSamplesPerSec = static_cast<DWORD>(sample_rate);
        format.wBitsPerSample = 16;
        format.nBlockAlign = static_cast<WORD>(format.nChannels * (format.wBitsPerSample / 8U));
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

        if (FAILED(engine_.engine()->CreateSourceVoice(&voice_, &format, 0,
                                                       XAUDIO2_DEFAULT_FREQ_RATIO, this)) ||
            voice_ == nullptr)
        {
            return false;
        }
        sample_rate_ = sample_rate;
        channels_ = channels;
        if (FAILED(voice_->SetVolume(gain_.load())) || FAILED(voice_->Start()))
        {
            voice_->DestroyVoice();
            voice_ = nullptr;
            return false;
        }
        return true;
    }

    void waitForQueue(std::stop_token stop_token) const noexcept
    {
        while (!stop_token.stop_requested())
        {
            XAUDIO2_VOICE_STATE state{};
            {
                const std::scoped_lock lock{voice_mutex_};
                if (voice_ == nullptr)
                {
                    return;
                }
                voice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            }
            if (state.BuffersQueued < maximum_queued_audio_frames)
            {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
    }

    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) noexcept override
    {
    }
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() noexcept override
    {
    }
    void STDMETHODCALLTYPE OnStreamEnd() noexcept override
    {
    }
    void STDMETHODCALLTYPE OnBufferStart(void*) noexcept override
    {
    }
    void STDMETHODCALLTYPE OnBufferEnd(void* context) noexcept override
    {
        releaseBuffer(static_cast<std::vector<std::int16_t>*>(context));
    }
    void STDMETHODCALLTYPE OnLoopEnd(void*) noexcept override
    {
    }
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) noexcept override
    {
    }

    void releaseBuffer(std::vector<std::int16_t>* buffer) noexcept
    {
        const std::scoped_lock lock{buffers_mutex_};
        if (pending_buffers_.erase(buffer) > 0)
        {
            delete buffer;
        }
    }

    void releasePendingBuffers() noexcept
    {
        const std::scoped_lock lock{buffers_mutex_};
        for (auto* buffer : pending_buffers_)
        {
            delete buffer;
        }
        pending_buffers_.clear();
    }

    XAudio2Engine& engine_;
    std::shared_ptr<::livekit::AudioStream> stream_;
    std::atomic<float> gain_{1.0F};
    std::jthread thread_;
    mutable std::mutex voice_mutex_;
    IXAudio2SourceVoice* voice_{nullptr};
    int sample_rate_{0};
    int channels_{0};
    std::mutex buffers_mutex_;
    std::unordered_set<std::vector<std::int16_t>*> pending_buffers_;
};

class PlatformMicrophoneSource final : public MicrophoneSource
{
  public:
    explicit PlatformMicrophoneSource(::livekit::PlatformAudio& platform_audio)
    {
        ::livekit::PlatformAudioOptions options;
        options.echo_cancellation = true;
        options.noise_suppression = true;
        options.auto_gain_control = true;
        source_ = platform_audio.createAudioSource(options);
        track_ = ::livekit::LocalAudioTrack::createLocalAudioTrack("hvc-microphone", source_);
    }

    [[nodiscard]] auto track() const -> std::shared_ptr<::livekit::LocalAudioTrack> override
    {
        return track_;
    }

  private:
    std::shared_ptr<::livekit::PlatformAudioSource> source_;
    std::shared_ptr<::livekit::LocalAudioTrack> track_;
};

class PlatformMicrophoneBackend final : public MicrophoneBackend
{
  public:
    explicit PlatformMicrophoneBackend(::livekit::PlatformAudio& platform_audio)
        : platform_audio_(platform_audio)
    {
    }

    [[nodiscard]] auto devices() const -> std::vector<client::AudioDevice> override
    {
        return toAudioDevices(platform_audio_.recordingDevices());
    }

    void selectDevice(const std::string& device_id) override
    {
        platform_audio_.setRecordingDevice(device_id);
    }

    [[nodiscard]] auto createSource() -> std::shared_ptr<MicrophoneSource> override
    {
        return std::make_shared<PlatformMicrophoneSource>(platform_audio_);
    }

  private:
    ::livekit::PlatformAudio& platform_audio_;
};

class XAudio2Backend final : public AudioPlayoutBackend
{
  public:
    explicit XAudio2Backend(::livekit::PlatformAudio& platform_audio)
        : platform_audio_(platform_audio)
    {
    }

    [[nodiscard]] auto devices() const -> std::vector<client::AudioDevice> override
    {
        return toAudioDevices(platform_audio_.playoutDevices());
    }

    void selectDevice(const std::string& device_id) override
    {
        platform_audio_.setPlayoutDevice(device_id);
        engine_.selectDevice(device_id);
    }

    [[nodiscard]] auto createPlayout(const std::shared_ptr<::livekit::Track>& track, float gain)
        -> std::shared_ptr<RemoteAudioPlayout> override
    {
        return std::make_shared<XAudio2RemotePlayout>(engine_, track, gain);
    }

  private:
    ::livekit::PlatformAudio& platform_audio_;
    XAudio2Engine engine_;
};
} // namespace

auto createAudioPlayoutBackend(::livekit::PlatformAudio& platform_audio)
    -> std::unique_ptr<AudioPlayoutBackend>
{
    return std::make_unique<XAudio2Backend>(platform_audio);
}

auto createMicrophoneBackend(::livekit::PlatformAudio& platform_audio)
    -> std::unique_ptr<MicrophoneBackend>
{
    return std::make_unique<PlatformMicrophoneBackend>(platform_audio);
}
} // namespace hvc::livekit::detail
