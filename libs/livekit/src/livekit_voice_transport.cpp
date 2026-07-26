#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <hvc/livekit/livekit_voice_transport.hpp>
#include <livekit/livekit.h>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
#include <xaudio2.h>

namespace hvc::livekit
{
namespace
{
constexpr std::uint64_t opus_bitrate = 64'000;
constexpr std::size_t voice_scope_count = 3;
constexpr std::uint32_t maximum_queued_audio_frames = 12;

struct LiveKitGlobalState final
{
    std::mutex mutex;
    std::size_t instance_count{0};
};

[[nodiscard]] auto globalLiveKitState() -> LiveKitGlobalState&
{
    static LiveKitGlobalState state;
    return state;
}

[[nodiscard]] auto scopeIndex(domain::VoiceScope scope) noexcept -> std::size_t
{
    return static_cast<std::size_t>(scope);
}

class LiveKitLifetime final
{
  public:
    LiveKitLifetime()
    {
        auto& global_state = globalLiveKitState();
        const std::scoped_lock lock{global_state.mutex};
        if (global_state.instance_count == 0 && !::livekit::initialize(::livekit::LogLevel::Info))
        {
            throw std::runtime_error{"LiveKit SDK initialization failed"};
        }
        ++global_state.instance_count;
    }

    ~LiveKitLifetime()
    {
        auto& global_state = globalLiveKitState();
        const std::scoped_lock lock{global_state.mutex};
        --global_state.instance_count;
        if (global_state.instance_count == 0)
        {
            ::livekit::shutdown();
        }
    }

    LiveKitLifetime(const LiveKitLifetime&) = delete;
    auto operator=(const LiveKitLifetime&) -> LiveKitLifetime& = delete;
    LiveKitLifetime(LiveKitLifetime&&) = delete;
    auto operator=(LiveKitLifetime&&) -> LiveKitLifetime& = delete;
};

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

class RemoteTrackPlayout final : private IXAudio2VoiceCallback
{
  public:
    RemoteTrackPlayout(XAudio2Engine& engine, const std::shared_ptr<::livekit::Track>& track,
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

    ~RemoteTrackPlayout()
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

    RemoteTrackPlayout(const RemoteTrackPlayout&) = delete;
    auto operator=(const RemoteTrackPlayout&) -> RemoteTrackPlayout& = delete;
    RemoteTrackPlayout(RemoteTrackPlayout&&) = delete;
    auto operator=(RemoteTrackPlayout&&) -> RemoteTrackPlayout& = delete;

    void setGain(float gain) noexcept
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
                auto* const samples_pointer = samples.get();
                {
                    const std::scoped_lock lock{buffers_mutex_};
                    pending_buffers_.insert(samples_pointer);
                }
                static_cast<void>(samples.release());
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
} // namespace

class LiveKitVoiceTransport::Impl final
{
  public:
    class RoomObserver final : public ::livekit::RoomDelegate
    {
      public:
        RoomObserver(Impl& owner, domain::VoiceScope scope) : owner_(owner), scope_(scope)
        {
        }

        void onParticipantConnected(::livekit::Room&,
                                    const ::livekit::ParticipantConnectedEvent& event) override
        {
            owner_.participantConnected(scope_, participantIdentity(event.participant));
        }

        void onParticipantDisconnected(
            ::livekit::Room&, const ::livekit::ParticipantDisconnectedEvent& event) override
        {
            owner_.participantDisconnected(scope_, participantIdentity(event.participant));
        }

        void onTrackSubscribed(::livekit::Room&,
                               const ::livekit::TrackSubscribedEvent& event) override
        {
            if (isOpusMicrophone(event.publication))
            {
                owner_.remoteAudioStarted(scope_, event.publication->sid(), event.track,
                                          participantIdentity(event.participant));
            }
        }

        void onTrackPublished(::livekit::Room&,
                              const ::livekit::TrackPublishedEvent& event) override
        {
            if (isOpusMicrophone(event.publication))
            {
                owner_.remoteAudioAvailable(scope_, event.publication,
                                            participantIdentity(event.participant));
            }
        }

        void onTrackUnsubscribed(::livekit::Room&,
                                 const ::livekit::TrackUnsubscribedEvent& event) override
        {
            if (isOpusMicrophone(event.publication))
            {
                owner_.remoteAudioStopped(scope_, event.publication->sid(),
                                          participantIdentity(event.participant));
            }
        }

        void onTrackUnpublished(::livekit::Room&,
                                const ::livekit::TrackUnpublishedEvent& event) override
        {
            if (isOpusMicrophone(event.publication))
            {
                owner_.remoteAudioStopped(scope_, event.publication->sid(),
                                          participantIdentity(event.participant));
                owner_.remoteAudioUnavailable(scope_, event.publication->sid(),
                                              participantIdentity(event.participant));
            }
        }

        void onReconnecting(::livekit::Room&, const ::livekit::ReconnectingEvent&) override
        {
            owner_.reconnecting();
        }

        void onReconnected(::livekit::Room&, const ::livekit::ReconnectedEvent&) override
        {
            owner_.reconnected();
        }

        void onDisconnected(::livekit::Room&, const ::livekit::DisconnectedEvent&) override
        {
            owner_.roomDisconnected();
        }

      private:
        template <typename Publication>
        [[nodiscard]] static auto isOpusMicrophone(const std::shared_ptr<Publication>& publication)
            -> bool
        {
            return publication != nullptr &&
                   publication->kind() == ::livekit::TrackKind::KIND_AUDIO &&
                   publication->mimeType() == "audio/opus" &&
                   publication->source() == ::livekit::TrackSource::SOURCE_MICROPHONE;
        }

        template <typename Participant>
        [[nodiscard]] static auto participantIdentity(Participant* participant) -> std::string
        {
            return participant == nullptr ? std::string{} : participant->identity();
        }

        Impl& owner_;
        domain::VoiceScope scope_;
    };

    struct ScopeRoom final
    {
        ScopeRoom(Impl& owner, const client::VoiceRoomGrant& room_grant)
            : grant(room_grant), observer(owner, room_grant.scope)
        {
            room.setDelegate(&observer);
        }

        client::VoiceRoomGrant grant;
        RoomObserver observer;
        ::livekit::Room room;
    };

    struct PublishedAudio final
    {
        domain::VoiceScope scope{domain::VoiceScope::team};
        std::shared_ptr<::livekit::PlatformAudioSource> source;
        std::shared_ptr<::livekit::LocalAudioTrack> track;
    };

    struct RemoteAudioPublication final
    {
        std::string publication_id;
        std::shared_ptr<::livekit::RemoteTrackPublication> publication;
        bool admitted{false};
        float gain{0.0F};
        std::shared_ptr<RemoteTrackPlayout> playout;
    };

    Impl() = default;

    void setObserver(client::IVoiceTransportObserver* observer) noexcept
    {
        observer_.store(observer);
    }

    [[nodiscard]] auto state() const noexcept -> client::VoiceTransportState
    {
        return state_.load();
    }

    [[nodiscard]] auto connect(std::span<const client::VoiceRoomGrant> grants)
        -> client::VoiceTransportResult
    {
        if (state() != client::VoiceTransportState::disconnected)
        {
            return failure(client::VoiceTransportError::invalid_state,
                           "LiveKit transport is already active");
        }
        auto validation = validateGrants(grants);
        if (!validation)
        {
            return validation;
        }

        state_.store(client::VoiceTransportState::connecting);
        notifyState(client::VoiceTransportState::connecting);
        std::array<std::shared_ptr<ScopeRoom>, voice_scope_count> new_rooms;
        try
        {
            for (const auto& grant : grants)
            {
                auto scope_room = std::make_shared<ScopeRoom>(*this, grant);
                ::livekit::RoomOptions room_options;
                room_options.auto_subscribe = false;
                if (!scope_room->room.connect(grant.url, grant.token, room_options))
                {
                    static_cast<void>(disconnectRooms(new_rooms));
                    state_.store(client::VoiceTransportState::disconnected);
                    notifyState(client::VoiceTransportState::disconnected);
                    return failure(client::VoiceTransportError::connection_failed,
                                   "LiveKit room connection failed");
                }
                new_rooms[scopeIndex(grant.scope)] = std::move(scope_room);
            }
        }
        catch (const std::exception& error)
        {
            static_cast<void>(disconnectRooms(new_rooms));
            state_.store(client::VoiceTransportState::disconnected);
            notifyState(client::VoiceTransportState::disconnected);
            return failure(client::VoiceTransportError::connection_failed, error.what());
        }

        {
            const std::scoped_lock lock{rooms_mutex_};
            rooms_ = std::move(new_rooms);
        }
        state_.store(client::VoiceTransportState::connected);
        notifyState(client::VoiceTransportState::connected);
        return client::VoiceTransportResult::success();
    }

    [[nodiscard]] auto disconnect() -> client::VoiceTransportResult
    {
        if (state() == client::VoiceTransportState::disconnected)
        {
            return failure(client::VoiceTransportError::invalid_state,
                           "LiveKit transport is already disconnected");
        }

        static_cast<void>(stopMicrophoneIfActive());
        state_.store(client::VoiceTransportState::disconnected);
        controlled_reconnect_.store(true);
        auto rooms = takeRooms();
        static_cast<void>(disconnectRooms(rooms));
        controlled_reconnect_.store(false);
        resetRemoteAudio();
        resetRemotePublications();
        notifyState(client::VoiceTransportState::disconnected);
        return client::VoiceTransportResult::success();
    }

    [[nodiscard]] auto startMicrophone(domain::VoiceScope scope) -> client::VoiceTransportResult
    {
        if (state() != client::VoiceTransportState::connected)
        {
            return failure(client::VoiceTransportError::invalid_state,
                           "LiveKit transport is not connected");
        }

        const auto scope_room = room(scope);
        if (scope_room == nullptr ||
            scope_room->room.connectionState() != ::livekit::ConnectionState::Connected)
        {
            return failure(client::VoiceTransportError::invalid_state,
                           "the requested LiveKit scope is not connected");
        }

        const std::scoped_lock lock{publication_mutex_};
        if (published_audio_.has_value())
        {
            return failure(client::VoiceTransportError::invalid_state,
                           "a microphone publication is already active");
        }

        try
        {
            if (platform_audio_.recordingDeviceCount() < 1)
            {
                return failure(client::VoiceTransportError::audio_device_unavailable,
                               "no microphone is available");
            }

            ::livekit::PlatformAudioOptions audio_options;
            audio_options.echo_cancellation = true;
            audio_options.noise_suppression = true;
            audio_options.auto_gain_control = true;

            PublishedAudio published_audio;
            published_audio.scope = scope;
            published_audio.source = platform_audio_.createAudioSource(audio_options);
            published_audio.track = ::livekit::LocalAudioTrack::createLocalAudioTrack(
                "hvc-microphone", published_audio.source);

            const auto local_participant = scope_room->room.localParticipant().lock();
            if (local_participant == nullptr)
            {
                return failure(client::VoiceTransportError::publication_failed,
                               "LiveKit local participant is unavailable");
            }

            ::livekit::TrackPublishOptions publish_options;
            publish_options.source = ::livekit::TrackSource::SOURCE_MICROPHONE;
            publish_options.audio_encoding = ::livekit::AudioEncodingOptions{opus_bitrate};
            publish_options.dtx = true;
            publish_options.red = false;
            local_participant->publishTrack(published_audio.track, publish_options);
            published_audio_ = std::move(published_audio);
            return client::VoiceTransportResult::success();
        }
        catch (const std::exception& error)
        {
            return failure(client::VoiceTransportError::publication_failed, error.what());
        }
    }

    [[nodiscard]] auto stopMicrophone() -> client::VoiceTransportResult
    {
        auto result = stopMicrophoneIfActive();
        if (!result)
        {
            notifyError(result.error, result.message);
        }
        return result;
    }

    [[nodiscard]] auto activeTransmissionScope() const noexcept -> std::optional<domain::VoiceScope>
    {
        const std::scoped_lock lock{publication_mutex_};
        if (!published_audio_.has_value())
        {
            return std::nullopt;
        }
        return published_audio_->scope;
    }

    [[nodiscard]] auto recordingDevices() const -> std::vector<client::AudioDevice>
    {
        try
        {
            return toAudioDevices(platform_audio_.recordingDevices());
        }
        catch (const std::exception& error)
        {
            notifyError(client::VoiceTransportError::audio_device_unavailable, error.what());
            return {};
        }
    }

    [[nodiscard]] auto playoutDevices() const -> std::vector<client::AudioDevice>
    {
        try
        {
            return toAudioDevices(platform_audio_.playoutDevices());
        }
        catch (const std::exception& error)
        {
            notifyError(client::VoiceTransportError::audio_device_unavailable, error.what());
            return {};
        }
    }

    [[nodiscard]] auto selectRecordingDevice(const std::string& device_id)
        -> client::VoiceTransportResult
    {
        if (device_id.empty())
        {
            return failure(client::VoiceTransportError::invalid_argument,
                           "recording device ID must not be empty");
        }
        try
        {
            platform_audio_.setRecordingDevice(device_id);
            return client::VoiceTransportResult::success();
        }
        catch (const std::exception& error)
        {
            return failure(client::VoiceTransportError::audio_device_switch_failed, error.what());
        }
    }

    [[nodiscard]] auto selectPlayoutDevice(const std::string& device_id)
        -> client::VoiceTransportResult
    {
        if (device_id.empty())
        {
            return failure(client::VoiceTransportError::invalid_argument,
                           "playout device ID must not be empty");
        }
        if (state() == client::VoiceTransportState::connected)
        {
            return reconnectForPlayoutDevice(device_id);
        }
        try
        {
            platform_audio_.setPlayoutDevice(device_id);
            playout_engine_.selectDevice(device_id);
            return client::VoiceTransportResult::success();
        }
        catch (const std::exception& error)
        {
            return failure(client::VoiceTransportError::audio_device_switch_failed, error.what());
        }
    }

    [[nodiscard]] auto configureRemoteAudio(domain::VoiceScope scope,
                                            const std::string& participant_id, bool admitted,
                                            float gain) -> client::VoiceTransportResult
    {
        if (participant_id.empty() || !std::isfinite(gain) || gain < 0.0F || gain > 1.0F)
        {
            return failure(
                client::VoiceTransportError::invalid_argument,
                "remote audio policy requires a participant ID and gain from zero to one");
        }

        std::shared_ptr<::livekit::RemoteTrackPublication> publication;
        {
            const std::scoped_lock lock{remote_publications_mutex_};
            auto& publications = remote_publications_[scopeIndex(scope)];
            const auto iterator = publications.find(participant_id);
            if (iterator == publications.end())
            {
                return failure(client::VoiceTransportError::invalid_state,
                               "remote microphone publication is unavailable");
            }
            iterator->second.admitted = admitted;
            iterator->second.gain = admitted ? gain : 0.0F;
            if (iterator->second.playout != nullptr)
            {
                iterator->second.playout->setGain(iterator->second.gain);
            }
            publication = iterator->second.publication;
        }

        try
        {
            if (publication->subscribed() != admitted)
            {
                publication->setSubscribed(admitted);
            }
            return client::VoiceTransportResult::success();
        }
        catch (const std::exception& error)
        {
            return failure(client::VoiceTransportError::internal_error, error.what());
        }
    }

    [[nodiscard]] auto remoteParticipantCount(domain::VoiceScope scope) const -> std::size_t
    {
        const auto scope_room = room(scope);
        return scope_room == nullptr ? 0 : scope_room->room.remoteParticipants().size();
    }

    [[nodiscard]] auto hasRemoteAudio(domain::VoiceScope scope) const -> bool
    {
        const auto index = scopeIndex(scope);
        if (index >= remote_audio_tracks_.size())
        {
            return false;
        }
        const std::scoped_lock lock{remote_audio_mutex_};
        return !remote_audio_tracks_[index].empty();
    }

  private:
    [[nodiscard]] static auto validateGrants(std::span<const client::VoiceRoomGrant> grants)
        -> client::VoiceTransportResult
    {
        if (grants.empty() || grants.size() > voice_scope_count)
        {
            return client::VoiceTransportResult::failure(
                client::VoiceTransportError::invalid_argument,
                "between one and three LiveKit room grants are required");
        }

        std::array<bool, voice_scope_count> seen_scopes{};
        for (const auto& grant : grants)
        {
            const auto index = scopeIndex(grant.scope);
            if (index >= seen_scopes.size() || seen_scopes[index] || grant.url.empty() ||
                grant.token.empty())
            {
                return client::VoiceTransportResult::failure(
                    client::VoiceTransportError::invalid_argument,
                    "LiveKit grants require unique scopes, URLs, and tokens");
            }
            seen_scopes[index] = true;
        }
        return client::VoiceTransportResult::success();
    }

    [[nodiscard]] auto failure(client::VoiceTransportError error, std::string message) const
        -> client::VoiceTransportResult
    {
        notifyError(error, message);
        return client::VoiceTransportResult::failure(error, std::move(message));
    }

    void notifyState(client::VoiceTransportState state) const
    {
        auto* const observer = observer_.load();
        if (observer != nullptr)
        {
            observer->onTransportStateChanged(state);
        }
    }

    void notifyError(client::VoiceTransportError error, const std::string& message) const
    {
        auto* const observer = observer_.load();
        if (observer != nullptr)
        {
            observer->onTransportError(error, message);
        }
    }

    void participantConnected(domain::VoiceScope scope, const std::string& participant_id) const
    {
        auto* const observer = observer_.load();
        if (observer != nullptr)
        {
            observer->onRemoteParticipantConnected(scope, participant_id);
        }
    }

    void participantDisconnected(domain::VoiceScope scope, const std::string& participant_id)
    {
        clearParticipantAudio(scope, participant_id);
        std::shared_ptr<RemoteTrackPlayout> playout;
        auto publication_removed = false;
        {
            const std::scoped_lock lock{remote_publications_mutex_};
            auto& publications = remote_publications_[scopeIndex(scope)];
            const auto publication = publications.find(participant_id);
            if (publication != publications.end())
            {
                playout = std::move(publication->second.playout);
                publications.erase(publication);
                publication_removed = true;
            }
        }
        playout.reset();
        auto* const observer = observer_.load();
        if (observer != nullptr)
        {
            if (publication_removed)
            {
                observer->onRemoteAudioUnavailable(scope, participant_id);
            }
            observer->onRemoteParticipantDisconnected(scope, participant_id);
        }
    }

    void remoteAudioStarted(domain::VoiceScope scope, const std::string& publication_id,
                            const std::shared_ptr<::livekit::Track>& track,
                            const std::string& participant_id)
    {
        {
            const std::scoped_lock lock{remote_publications_mutex_};
            auto& publications = remote_publications_[scopeIndex(scope)];
            const auto publication = publications.find(participant_id);
            if (publication == publications.end() ||
                publication->second.publication_id != publication_id ||
                !publication->second.admitted)
            {
                return;
            }
            try
            {
                publication->second.playout = std::make_shared<RemoteTrackPlayout>(
                    playout_engine_, track, publication->second.gain);
            }
            catch (const std::exception& error)
            {
                notifyError(client::VoiceTransportError::audio_device_unavailable, error.what());
                return;
            }
        }
        {
            const std::scoped_lock lock{remote_audio_mutex_};
            const auto [iterator, inserted] =
                remote_audio_tracks_[scopeIndex(scope)].insert_or_assign(publication_id,
                                                                         participant_id);
            static_cast<void>(iterator);
            if (!inserted)
            {
                return;
            }
        }
        auto* const observer = observer_.load();
        if (observer != nullptr)
        {
            observer->onRemoteAudioStarted(scope, participant_id);
        }
    }

    void remoteAudioAvailable(domain::VoiceScope scope,
                              const std::shared_ptr<::livekit::RemoteTrackPublication>& publication,
                              const std::string& participant_id)
    {
        {
            const std::scoped_lock lock{remote_publications_mutex_};
            remote_publications_[scopeIndex(scope)].insert_or_assign(
                participant_id,
                RemoteAudioPublication{publication->sid(), publication, false, 0.0F, nullptr});
        }
        auto* const observer = observer_.load();
        if (observer != nullptr)
        {
            observer->onRemoteAudioAvailable(scope, participant_id);
        }
    }

    void remoteAudioUnavailable(domain::VoiceScope scope, const std::string& publication_id,
                                const std::string& participant_id)
    {
        std::shared_ptr<RemoteTrackPlayout> playout;
        {
            const std::scoped_lock lock{remote_publications_mutex_};
            auto& publications = remote_publications_[scopeIndex(scope)];
            const auto iterator = publications.find(participant_id);
            if (iterator == publications.end() || iterator->second.publication_id != publication_id)
            {
                return;
            }
            playout = std::move(iterator->second.playout);
            publications.erase(iterator);
        }
        playout.reset();
        auto* const observer = observer_.load();
        if (observer != nullptr)
        {
            observer->onRemoteAudioUnavailable(scope, participant_id);
        }
    }

    void remoteAudioStopped(domain::VoiceScope scope, const std::string& publication_id,
                            const std::string& participant_id)
    {
        std::shared_ptr<RemoteTrackPlayout> playout;
        {
            const std::scoped_lock lock{remote_audio_mutex_};
            if (remote_audio_tracks_[scopeIndex(scope)].erase(publication_id) == 0)
            {
                return;
            }
        }
        {
            const std::scoped_lock lock{remote_publications_mutex_};
            auto& publications = remote_publications_[scopeIndex(scope)];
            const auto publication = publications.find(participant_id);
            if (publication != publications.end() &&
                publication->second.publication_id == publication_id)
            {
                playout = std::move(publication->second.playout);
            }
        }
        playout.reset();
        auto* const observer = observer_.load();
        if (observer != nullptr)
        {
            observer->onRemoteAudioStopped(scope, participant_id);
        }
    }

    void reconnecting()
    {
        if (controlled_reconnect_.load())
        {
            return;
        }
        if (activeTransmissionScope().has_value())
        {
            static_cast<void>(stopMicrophoneIfActive());
        }
        state_.store(client::VoiceTransportState::reconnecting);
        notifyState(client::VoiceTransportState::reconnecting);
    }

    void reconnected()
    {
        if (controlled_reconnect_.load())
        {
            return;
        }
        if (allRoomsConnected())
        {
            state_.store(client::VoiceTransportState::connected);
            notifyState(client::VoiceTransportState::connected);
        }
    }

    void roomDisconnected()
    {
        if (controlled_reconnect_.load())
        {
            return;
        }
        if (activeTransmissionScope().has_value())
        {
            static_cast<void>(stopMicrophoneIfActive());
        }
        state_.store(client::VoiceTransportState::disconnected);
        notifyState(client::VoiceTransportState::disconnected);
    }

    [[nodiscard]] auto stopMicrophoneIfActive() -> client::VoiceTransportResult
    {
        const std::scoped_lock lock{publication_mutex_};
        if (!published_audio_.has_value())
        {
            return client::VoiceTransportResult::failure(client::VoiceTransportError::invalid_state,
                                                         "no microphone publication is active");
        }

        auto published_audio = std::move(*published_audio_);
        published_audio_.reset();
        const auto scope_room = room(published_audio.scope);
        if (scope_room == nullptr)
        {
            return client::VoiceTransportResult::failure(
                client::VoiceTransportError::publication_failed,
                "the publishing LiveKit room is unavailable");
        }

        try
        {
            const auto local_participant = scope_room->room.localParticipant().lock();
            if (local_participant == nullptr)
            {
                return client::VoiceTransportResult::failure(
                    client::VoiceTransportError::publication_failed,
                    "LiveKit local participant is unavailable during PTT release");
            }
            const auto publication = published_audio.track->publication();
            if (publication == nullptr || publication->sid().empty())
            {
                return client::VoiceTransportResult::failure(
                    client::VoiceTransportError::publication_failed,
                    "published microphone has no LiveKit publication SID");
            }
            local_participant->unpublishTrack(publication->sid());
            if (!local_participant->trackPublications().empty())
            {
                return client::VoiceTransportResult::failure(
                    client::VoiceTransportError::publication_failed,
                    "microphone publication remained after PTT release");
            }
            return client::VoiceTransportResult::success();
        }
        catch (const std::exception& error)
        {
            return client::VoiceTransportResult::failure(
                client::VoiceTransportError::publication_failed, error.what());
        }
    }

    [[nodiscard]] auto reconnectForPlayoutDevice(const std::string& device_id)
        -> client::VoiceTransportResult
    {
        controlled_reconnect_.store(true);
        if (activeTransmissionScope().has_value())
        {
            static_cast<void>(stopMicrophoneIfActive());
        }
        state_.store(client::VoiceTransportState::reconnecting);
        notifyState(client::VoiceTransportState::reconnecting);

        auto old_rooms = takeRooms();
        std::vector<client::VoiceRoomGrant> grants;
        for (const auto& scope_room : old_rooms)
        {
            if (scope_room != nullptr)
            {
                grants.push_back(scope_room->grant);
            }
        }
        static_cast<void>(disconnectRooms(old_rooms));
        resetRemoteAudio();
        resetRemotePublications();

        try
        {
            platform_audio_.setPlayoutDevice(device_id);
            playout_engine_.selectDevice(device_id);
        }
        catch (const std::exception& error)
        {
            controlled_reconnect_.store(false);
            state_.store(client::VoiceTransportState::disconnected);
            notifyState(client::VoiceTransportState::disconnected);
            return failure(client::VoiceTransportError::audio_device_switch_failed, error.what());
        }

        controlled_reconnect_.store(false);
        state_.store(client::VoiceTransportState::disconnected);
        auto result = connect(grants);
        if (!result)
        {
            return failure(client::VoiceTransportError::audio_device_switch_failed,
                           "playout device changed but authorized rooms could not reconnect");
        }
        return client::VoiceTransportResult::success();
    }

    [[nodiscard]] auto room(domain::VoiceScope scope) const -> std::shared_ptr<ScopeRoom>
    {
        const auto index = scopeIndex(scope);
        if (index >= rooms_.size())
        {
            return nullptr;
        }
        const std::scoped_lock lock{rooms_mutex_};
        return rooms_[index];
    }

    [[nodiscard]] auto takeRooms() -> std::array<std::shared_ptr<ScopeRoom>, voice_scope_count>
    {
        const std::scoped_lock lock{rooms_mutex_};
        auto rooms = std::move(rooms_);
        rooms_ = {};
        return rooms;
    }

    [[nodiscard]] static auto disconnectRooms(
        std::array<std::shared_ptr<ScopeRoom>, voice_scope_count>& rooms) noexcept -> bool
    {
        auto all_disconnected = true;
        for (auto& scope_room : rooms)
        {
            if (scope_room != nullptr)
            {
                try
                {
                    static_cast<void>(scope_room->room.disconnect());
                }
                catch (const std::exception&)
                {
                    all_disconnected = false;
                }
                scope_room.reset();
            }
        }
        return all_disconnected;
    }

    [[nodiscard]] auto allRoomsConnected() const -> bool
    {
        const std::scoped_lock lock{rooms_mutex_};
        auto found_room = false;
        for (const auto& scope_room : rooms_)
        {
            if (scope_room == nullptr)
            {
                continue;
            }
            found_room = true;
            if (scope_room->room.connectionState() != ::livekit::ConnectionState::Connected)
            {
                return false;
            }
        }
        return found_room;
    }

    void resetRemoteAudio() noexcept
    {
        const std::scoped_lock lock{remote_audio_mutex_};
        for (auto& tracks : remote_audio_tracks_)
        {
            tracks.clear();
        }
    }

    void resetRemotePublications() noexcept
    {
        const std::scoped_lock lock{remote_publications_mutex_};
        for (auto& publications : remote_publications_)
        {
            publications.clear();
        }
    }

    void clearParticipantAudio(domain::VoiceScope scope, const std::string& participant_id) const
    {
        const std::scoped_lock lock{remote_audio_mutex_};
        auto& tracks = remote_audio_tracks_[scopeIndex(scope)];
        for (auto iterator = tracks.begin(); iterator != tracks.end();)
        {
            if (iterator->second == participant_id)
            {
                iterator = tracks.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    LiveKitLifetime lifetime_;
    XAudio2Engine playout_engine_;
    ::livekit::PlatformAudio platform_audio_;
    std::atomic<client::IVoiceTransportObserver*> observer_{nullptr};
    std::atomic<client::VoiceTransportState> state_{client::VoiceTransportState::disconnected};
    std::atomic<bool> controlled_reconnect_{false};
    mutable std::mutex rooms_mutex_;
    std::array<std::shared_ptr<ScopeRoom>, voice_scope_count> rooms_;
    mutable std::mutex publication_mutex_;
    std::optional<PublishedAudio> published_audio_;
    mutable std::mutex remote_audio_mutex_;
    mutable std::array<std::unordered_map<std::string, std::string>, voice_scope_count>
        remote_audio_tracks_;
    mutable std::mutex remote_publications_mutex_;
    std::array<std::unordered_map<std::string, RemoteAudioPublication>, voice_scope_count>
        remote_publications_;
};

LiveKitVoiceTransport::LiveKitVoiceTransport() : impl_(std::make_unique<Impl>())
{
}

LiveKitVoiceTransport::~LiveKitVoiceTransport() = default;

void LiveKitVoiceTransport::setObserver(client::IVoiceTransportObserver* observer) noexcept
{
    impl_->setObserver(observer);
}

auto LiveKitVoiceTransport::state() const noexcept -> client::VoiceTransportState
{
    return impl_->state();
}

auto LiveKitVoiceTransport::connect(std::span<const client::VoiceRoomGrant> grants)
    -> client::VoiceTransportResult
{
    return impl_->connect(grants);
}

auto LiveKitVoiceTransport::disconnect() -> client::VoiceTransportResult
{
    return impl_->disconnect();
}

auto LiveKitVoiceTransport::startMicrophone(domain::VoiceScope scope)
    -> client::VoiceTransportResult
{
    return impl_->startMicrophone(scope);
}

auto LiveKitVoiceTransport::stopMicrophone() -> client::VoiceTransportResult
{
    return impl_->stopMicrophone();
}

auto LiveKitVoiceTransport::activeTransmissionScope() const noexcept
    -> std::optional<domain::VoiceScope>
{
    return impl_->activeTransmissionScope();
}

auto LiveKitVoiceTransport::recordingDevices() const -> std::vector<client::AudioDevice>
{
    return impl_->recordingDevices();
}

auto LiveKitVoiceTransport::playoutDevices() const -> std::vector<client::AudioDevice>
{
    return impl_->playoutDevices();
}

auto LiveKitVoiceTransport::selectRecordingDevice(const std::string& device_id)
    -> client::VoiceTransportResult
{
    return impl_->selectRecordingDevice(device_id);
}

auto LiveKitVoiceTransport::selectPlayoutDevice(const std::string& device_id)
    -> client::VoiceTransportResult
{
    return impl_->selectPlayoutDevice(device_id);
}

auto LiveKitVoiceTransport::configureRemoteAudio(domain::VoiceScope scope,
                                                 const std::string& participant_id, bool admitted,
                                                 float gain) -> client::VoiceTransportResult
{
    return impl_->configureRemoteAudio(scope, participant_id, admitted, gain);
}

auto LiveKitVoiceTransport::remoteParticipantCount(domain::VoiceScope scope) const -> std::size_t
{
    return impl_->remoteParticipantCount(scope);
}

auto LiveKitVoiceTransport::hasRemoteAudio(domain::VoiceScope scope) const -> bool
{
    return impl_->hasRemoteAudio(scope);
}
} // namespace hvc::livekit
