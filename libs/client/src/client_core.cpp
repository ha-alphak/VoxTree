#include <array>
#include <atomic>
#include <hvc/client/voice_client.hpp>
#include <hvc/client_core.h>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
using hvc::client::AudioDevice;
using hvc::client::IVoiceClientObserver;
using hvc::client::IVoiceTransport;
using hvc::client::IVoiceTransportObserver;
using hvc::client::VoiceClient;
using hvc::client::VoiceRoomGrant;
using hvc::client::VoiceTransportError;
using hvc::client::VoiceTransportResult;
using hvc::client::VoiceTransportState;
using hvc::domain::VoiceScope;

[[nodiscard]] auto validString(hvc_client_core_string_view value) noexcept -> bool
{
    return value.size == 0 || value.data != nullptr;
}

[[nodiscard]] auto copyString(hvc_client_core_string_view value) -> std::string
{
    return value.size == 0 ? std::string{} : std::string{value.data, value.size};
}

[[nodiscard]] auto stringView(const std::string& value) noexcept -> hvc_client_core_string_view
{
    return {value.data(), value.size()};
}

[[nodiscard]] auto validScope(hvc_client_core_scope scope) noexcept -> bool
{
    return scope == HVC_CLIENT_CORE_SCOPE_TEAM || scope == HVC_CLIENT_CORE_SCOPE_SPECIALIZATION ||
           scope == HVC_CLIENT_CORE_SCOPE_GROUP;
}

[[nodiscard]] auto domainScope(hvc_client_core_scope scope) noexcept -> VoiceScope
{
    return static_cast<VoiceScope>(scope);
}

[[nodiscard]] auto abiMajor(std::uint32_t version) noexcept -> std::uint32_t
{
    return version >> 16U;
}

[[nodiscard]] auto transportError(hvc_client_core_transport_error error) noexcept
    -> VoiceTransportError
{
    switch (error)
    {
    case HVC_CLIENT_CORE_TRANSPORT_ERROR_NONE:
        return VoiceTransportError::none;
    case HVC_CLIENT_CORE_TRANSPORT_ERROR_INVALID_ARGUMENT:
        return VoiceTransportError::invalid_argument;
    case HVC_CLIENT_CORE_TRANSPORT_ERROR_INVALID_STATE:
        return VoiceTransportError::invalid_state;
    case HVC_CLIENT_CORE_TRANSPORT_ERROR_CONNECTION_FAILED:
        return VoiceTransportError::connection_failed;
    case HVC_CLIENT_CORE_TRANSPORT_ERROR_AUDIO_DEVICE_UNAVAILABLE:
        return VoiceTransportError::audio_device_unavailable;
    case HVC_CLIENT_CORE_TRANSPORT_ERROR_AUDIO_DEVICE_SWITCH_FAILED:
        return VoiceTransportError::audio_device_switch_failed;
    case HVC_CLIENT_CORE_TRANSPORT_ERROR_PUBLICATION_FAILED:
        return VoiceTransportError::publication_failed;
    case HVC_CLIENT_CORE_TRANSPORT_ERROR_INTERNAL:
        return VoiceTransportError::internal_error;
    }
    return VoiceTransportError::internal_error;
}

[[nodiscard]] auto transportState(hvc_client_core_connection_state state) noexcept
    -> VoiceTransportState
{
    switch (state)
    {
    case HVC_CLIENT_CORE_CONNECTION_DISCONNECTED:
        return VoiceTransportState::disconnected;
    case HVC_CLIENT_CORE_CONNECTION_CONNECTING:
        return VoiceTransportState::connecting;
    case HVC_CLIENT_CORE_CONNECTION_CONNECTED:
        return VoiceTransportState::connected;
    case HVC_CLIENT_CORE_CONNECTION_RECONNECTING:
        return VoiceTransportState::reconnecting;
    }
    return VoiceTransportState::disconnected;
}

[[nodiscard]] auto coreState(VoiceTransportState state) noexcept -> hvc_client_core_connection_state
{
    return static_cast<hvc_client_core_connection_state>(state);
}

[[nodiscard]] auto callResult(hvc_client_core_transport_error error,
                              hvc_client_core_string_view message) -> VoiceTransportResult
{
    if (error == HVC_CLIENT_CORE_TRANSPORT_ERROR_NONE)
    {
        return VoiceTransportResult::success();
    }
    auto diagnostic = validString(message) ? copyString(message) : std::string{};
    if (diagnostic.empty())
    {
        diagnostic = "host voice transport operation failed";
    }
    return VoiceTransportResult::failure(transportError(error), std::move(diagnostic));
}

[[nodiscard]] auto voiceErrorCode(VoiceTransportError error) -> std::string
{
    switch (error)
    {
    case VoiceTransportError::none:
        return "voice_transport_none";
    case VoiceTransportError::invalid_argument:
        return "voice_transport_invalid_argument";
    case VoiceTransportError::invalid_state:
        return "voice_transport_invalid_state";
    case VoiceTransportError::connection_failed:
        return "voice_transport_connection_failed";
    case VoiceTransportError::audio_device_unavailable:
        return "voice_transport_audio_device_unavailable";
    case VoiceTransportError::audio_device_switch_failed:
        return "voice_transport_audio_device_switch_failed";
    case VoiceTransportError::publication_failed:
        return "voice_transport_publication_failed";
    case VoiceTransportError::internal_error:
        return "voice_transport_internal_error";
    }
    return "voice_transport_internal_error";
}

class CVoiceTransport final : public IVoiceTransport
{
  public:
    explicit CVoiceTransport(hvc_client_core_transport_v1 transport) : transport_(transport)
    {
        observer_table_.struct_size = sizeof(observer_table_);
        observer_table_.user_data = this;
        observer_table_.connection_state_changed = &CVoiceTransport::stateChanged;
        observer_table_.remote_audio_available = &CVoiceTransport::audioAvailable;
        observer_table_.remote_audio_unavailable = &CVoiceTransport::audioUnavailable;
        observer_table_.speaker_started = &CVoiceTransport::speakerStarted;
        observer_table_.speaker_stopped = &CVoiceTransport::speakerStopped;
        observer_table_.error = &CVoiceTransport::transportFailure;
    }

    void setObserver(IVoiceTransportObserver* observer) noexcept override
    {
        observer_.store(observer);
        hvc_client_core_string_view message{};
        const auto error = transport_.set_observer(
            transport_.user_data, observer == nullptr ? nullptr : &observer_table_, &message);
        observer_attached_.store(error == HVC_CLIENT_CORE_TRANSPORT_ERROR_NONE);
        if (error != HVC_CLIENT_CORE_TRANSPORT_ERROR_NONE)
        {
            observer_.store(nullptr);
        }
    }

    [[nodiscard]] auto observerAttached() const noexcept -> bool
    {
        return observer_attached_.load();
    }

    [[nodiscard]] auto state() const noexcept -> VoiceTransportState override
    {
        return transportState(transport_.state(transport_.user_data));
    }

    [[nodiscard]] auto connect(std::span<const VoiceRoomGrant> grants)
        -> VoiceTransportResult override
    {
        std::vector<hvc_client_core_room_grant_v1> values;
        values.reserve(grants.size());
        for (const auto& grant : grants)
        {
            values.push_back({sizeof(hvc_client_core_room_grant_v1),
                              static_cast<hvc_client_core_scope>(grant.scope),
                              stringView(grant.url), stringView(grant.token)});
        }
        hvc_client_core_string_view message{};
        const auto error =
            transport_.connect(transport_.user_data, values.data(), values.size(), &message);
        return callResult(error, message);
    }

    [[nodiscard]] auto disconnect() -> VoiceTransportResult override
    {
        hvc_client_core_string_view message{};
        const auto error = transport_.disconnect(transport_.user_data, &message);
        return callResult(error, message);
    }

    [[nodiscard]] auto startMicrophone(VoiceScope scope) -> VoiceTransportResult override
    {
        hvc_client_core_string_view message{};
        const auto error = transport_.start_microphone(
            transport_.user_data, static_cast<hvc_client_core_scope>(scope), &message);
        return callResult(error, message);
    }

    [[nodiscard]] auto stopMicrophone() -> VoiceTransportResult override
    {
        hvc_client_core_string_view message{};
        const auto error = transport_.stop_microphone(transport_.user_data, &message);
        return callResult(error, message);
    }

    [[nodiscard]] auto activeTransmissionScope() const noexcept
        -> std::optional<VoiceScope> override
    {
        hvc_client_core_scope scope{};
        if (transport_.active_microphone_scope(transport_.user_data, &scope) == 0 ||
            !validScope(scope))
        {
            return std::nullopt;
        }
        return domainScope(scope);
    }

    [[nodiscard]] auto recordingDevices() const -> std::vector<AudioDevice> override
    {
        return {};
    }

    [[nodiscard]] auto playoutDevices() const -> std::vector<AudioDevice> override
    {
        return {};
    }

    [[nodiscard]] auto selectRecordingDevice(const std::string&) -> VoiceTransportResult override
    {
        return VoiceTransportResult::failure(
            VoiceTransportError::invalid_state,
            "audio-device selection is outside client-core ABI version 1");
    }

    [[nodiscard]] auto selectPlayoutDevice(const std::string&) -> VoiceTransportResult override
    {
        return VoiceTransportResult::failure(
            VoiceTransportError::invalid_state,
            "audio-device selection is outside client-core ABI version 1");
    }

    [[nodiscard]] auto configureRemoteAudio(VoiceScope scope, const std::string& participant_id,
                                            bool admitted, float gain)
        -> VoiceTransportResult override
    {
        hvc_client_core_string_view message{};
        const auto error = transport_.configure_remote_audio(
            transport_.user_data, static_cast<hvc_client_core_scope>(scope),
            stringView(participant_id), static_cast<std::uint8_t>(admitted), gain, &message);
        return callResult(error, message);
    }

    [[nodiscard]] auto remoteParticipantCount(VoiceScope) const -> std::size_t override
    {
        return 0;
    }

    [[nodiscard]] auto hasRemoteAudio(VoiceScope) const -> bool override
    {
        return false;
    }

  private:
    [[nodiscard]] auto currentObserver() const noexcept -> IVoiceTransportObserver*
    {
        return observer_.load();
    }

    static void HVC_CLIENT_CORE_CALL stateChanged(void* user_data,
                                                  hvc_client_core_connection_state state) noexcept
    {
        auto& self = *static_cast<CVoiceTransport*>(user_data);
        if (auto* observer = self.currentObserver(); observer != nullptr)
        {
            observer->onTransportStateChanged(transportState(state));
        }
    }

    static void HVC_CLIENT_CORE_CALL
    audioAvailable(void* user_data, hvc_client_core_scope scope,
                   hvc_client_core_string_view participant_id) noexcept
    {
        auto& self = *static_cast<CVoiceTransport*>(user_data);
        auto* observer = self.currentObserver();
        if (observer == nullptr || !validScope(scope) || !validString(participant_id))
        {
            return;
        }
        try
        {
            observer->onRemoteAudioAvailable(domainScope(scope), copyString(participant_id));
        }
        catch (...)
        {
            observer->onTransportError(VoiceTransportError::internal_error,
                                       "remote-audio callback could not be decoded");
        }
    }

    static void HVC_CLIENT_CORE_CALL
    audioUnavailable(void* user_data, hvc_client_core_scope scope,
                     hvc_client_core_string_view participant_id) noexcept
    {
        auto& self = *static_cast<CVoiceTransport*>(user_data);
        auto* observer = self.currentObserver();
        if (observer == nullptr || !validScope(scope) || !validString(participant_id))
        {
            return;
        }
        try
        {
            observer->onRemoteAudioUnavailable(domainScope(scope), copyString(participant_id));
        }
        catch (...)
        {
            observer->onTransportError(VoiceTransportError::internal_error,
                                       "remote-audio callback could not be decoded");
        }
    }

    static void HVC_CLIENT_CORE_CALL
    speakerStarted(void* user_data, hvc_client_core_scope scope,
                   hvc_client_core_string_view participant_id) noexcept
    {
        auto& self = *static_cast<CVoiceTransport*>(user_data);
        auto* observer = self.currentObserver();
        if (observer == nullptr || !validScope(scope) || !validString(participant_id))
        {
            return;
        }
        try
        {
            observer->onRemoteAudioStarted(domainScope(scope), copyString(participant_id));
        }
        catch (...)
        {
            observer->onTransportError(VoiceTransportError::internal_error,
                                       "speaker callback could not be decoded");
        }
    }

    static void HVC_CLIENT_CORE_CALL
    speakerStopped(void* user_data, hvc_client_core_scope scope,
                   hvc_client_core_string_view participant_id) noexcept
    {
        auto& self = *static_cast<CVoiceTransport*>(user_data);
        auto* observer = self.currentObserver();
        if (observer == nullptr || !validScope(scope) || !validString(participant_id))
        {
            return;
        }
        try
        {
            observer->onRemoteAudioStopped(domainScope(scope), copyString(participant_id));
        }
        catch (...)
        {
            observer->onTransportError(VoiceTransportError::internal_error,
                                       "speaker callback could not be decoded");
        }
    }

    static void HVC_CLIENT_CORE_CALL transportFailure(void* user_data,
                                                      hvc_client_core_transport_error error,
                                                      hvc_client_core_string_view message) noexcept
    {
        auto& self = *static_cast<CVoiceTransport*>(user_data);
        auto* observer = self.currentObserver();
        if (observer == nullptr || !validString(message))
        {
            return;
        }
        try
        {
            observer->onTransportError(transportError(error), copyString(message));
        }
        catch (...)
        {
            observer->onTransportError(VoiceTransportError::internal_error,
                                       "transport-error callback could not be decoded");
        }
    }

    hvc_client_core_transport_v1 transport_;
    hvc_client_core_transport_observer_v1 observer_table_{};
    std::atomic<IVoiceTransportObserver*> observer_{nullptr};
    std::atomic<bool> observer_attached_{false};
};

struct MembershipStorage final
{
    std::uint64_t version{0};
    std::string hierarchy_id;
    std::string player_id;
    std::string group_id;
    std::string specialization_id;
    std::string team_id;
    std::vector<std::string> role_ids;
    bool connected{false};
    bool can_receive_voice{false};
    bool transmit_muted{false};
};

[[nodiscard]] auto validTransport(const hvc_client_core_transport_v1& transport) noexcept -> bool
{
    return transport.struct_size >= sizeof(hvc_client_core_transport_v1) &&
           transport.set_observer != nullptr && transport.state != nullptr &&
           transport.connect != nullptr && transport.disconnect != nullptr &&
           transport.start_microphone != nullptr && transport.stop_microphone != nullptr &&
           transport.active_microphone_scope != nullptr &&
           transport.configure_remote_audio != nullptr;
}

[[nodiscard]] auto copyMembership(const hvc_client_core_membership_v1& value)
    -> std::optional<MembershipStorage>
{
    const std::array strings{value.hierarchy_id, value.player_id, value.group_id,
                             value.specialization_id, value.team_id};
    if (value.struct_size < sizeof(hvc_client_core_membership_v1) || value.version == 0 ||
        (value.role_count != 0 && value.role_ids == nullptr))
    {
        return std::nullopt;
    }
    for (const auto current : strings)
    {
        if (!validString(current) || current.size == 0)
        {
            return std::nullopt;
        }
    }

    MembershipStorage result{};
    result.version = value.version;
    result.hierarchy_id = copyString(value.hierarchy_id);
    result.player_id = copyString(value.player_id);
    result.group_id = copyString(value.group_id);
    result.specialization_id = copyString(value.specialization_id);
    result.team_id = copyString(value.team_id);
    result.role_ids.reserve(value.role_count);
    for (std::size_t index = 0; index < value.role_count; ++index)
    {
        if (!validString(value.role_ids[index]) || value.role_ids[index].size == 0)
        {
            return std::nullopt;
        }
        result.role_ids.push_back(copyString(value.role_ids[index]));
    }
    result.connected = value.connected != 0;
    result.can_receive_voice = value.can_receive_voice != 0;
    result.transmit_muted = value.transmit_muted != 0;
    return result;
}
} // namespace

struct hvc_client_core final : IVoiceClientObserver
{
    explicit hvc_client_core(const hvc_client_core_config_v1& config)
        : callback(config.event_callback), callback_data(config.event_user_data),
          transport(config.transport), voice_client(transport)
    {
        voice_client.setObserver(this);
    }

    ~hvc_client_core() override
    {
        voice_client.setObserver(nullptr);
    }

    hvc_client_core(const hvc_client_core&) = delete;
    auto operator=(const hvc_client_core&) -> hvc_client_core& = delete;
    hvc_client_core(hvc_client_core&&) = delete;
    auto operator=(hvc_client_core&&) -> hvc_client_core& = delete;

    void onVoiceStateChanged(VoiceTransportState state) override
    {
        hvc_client_core_event_v1 event{};
        initializeEvent(event, HVC_CLIENT_CORE_EVENT_CONNECTION_STATE_CHANGED);
        event.connection_state = coreState(state);
        emit(event);
    }

    void onSpeakerStarted(VoiceScope scope, const std::string& participant_id) override
    {
        speakerEvent(HVC_CLIENT_CORE_EVENT_SPEAKER_STARTED, scope, participant_id);
    }

    void onSpeakerStopped(VoiceScope scope, const std::string& participant_id) override
    {
        speakerEvent(HVC_CLIENT_CORE_EVENT_SPEAKER_STOPPED, scope, participant_id);
    }

    void onVoiceError(VoiceTransportError error, const std::string& message) override
    {
        reportError(voiceErrorCode(error), message);
    }

    void reportError(const std::string& code, const std::string& message)
    {
        hvc_client_core_event_v1 event{};
        initializeEvent(event, HVC_CLIENT_CORE_EVENT_ERROR);
        event.error_code = stringView(code);
        event.message = stringView(message);
        emit(event);
    }

    void emitMembership(const MembershipStorage& value)
    {
        std::vector<hvc_client_core_string_view> roles;
        roles.reserve(value.role_ids.size());
        for (const auto& role : value.role_ids)
        {
            roles.push_back(stringView(role));
        }
        const hvc_client_core_membership_v1 event_membership{
            sizeof(hvc_client_core_membership_v1),
            value.version,
            stringView(value.hierarchy_id),
            stringView(value.player_id),
            stringView(value.group_id),
            stringView(value.specialization_id),
            stringView(value.team_id),
            roles.data(),
            roles.size(),
            static_cast<std::uint8_t>(value.connected),
            static_cast<std::uint8_t>(value.can_receive_voice),
            static_cast<std::uint8_t>(value.transmit_muted)};
        hvc_client_core_event_v1 event{};
        initializeEvent(event, HVC_CLIENT_CORE_EVENT_MEMBERSHIP_UPDATED);
        event.membership = &event_membership;
        emit(event);
    }

    void emitMembershipCleared()
    {
        hvc_client_core_event_v1 event{};
        initializeEvent(event, HVC_CLIENT_CORE_EVENT_MEMBERSHIP_CLEARED);
        emit(event);
    }

    void initializeEvent(hvc_client_core_event_v1& event, hvc_client_core_event_kind kind)
    {
        event.struct_size = sizeof(event);
        event.abi_version = HVC_CLIENT_CORE_ABI_VERSION;
        event.sequence = next_sequence.fetch_add(1);
        event.kind = kind;
        event.connection_state = HVC_CLIENT_CORE_CONNECTION_DISCONNECTED;
        event.scope = HVC_CLIENT_CORE_SCOPE_TEAM;
    }

    void emit(const hvc_client_core_event_v1& event) const noexcept
    {
        if (callback != nullptr)
        {
            callback(callback_data, &event);
        }
    }

    void speakerEvent(hvc_client_core_event_kind kind, VoiceScope scope,
                      const std::string& participant_id)
    {
        hvc_client_core_event_v1 event{};
        initializeEvent(event, kind);
        event.scope = static_cast<hvc_client_core_scope>(scope);
        event.participant_id = stringView(participant_id);
        emit(event);
    }

    hvc_client_core_event_callback_v1 callback{nullptr};
    void* callback_data{nullptr};
    CVoiceTransport transport;
    VoiceClient voice_client;
    std::atomic<std::uint64_t> next_sequence{1};
    std::mutex membership_mutex;
    std::optional<MembershipStorage> membership;
};

namespace
{
[[nodiscard]] auto operationResult(hvc_client_core& core, const VoiceTransportResult& result)
    -> hvc_client_core_result
{
    if (result)
    {
        return HVC_CLIENT_CORE_RESULT_OK;
    }

    core.reportError(voiceErrorCode(result.error), result.message);
    if (result.error == VoiceTransportError::invalid_argument)
    {
        return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
    }
    if (result.error == VoiceTransportError::invalid_state)
    {
        return HVC_CLIENT_CORE_RESULT_INVALID_STATE;
    }
    return HVC_CLIENT_CORE_RESULT_TRANSPORT_ERROR;
}

void reportArgumentError(hvc_client_core& core, const std::string& message)
{
    core.reportError("client_invalid_argument", message);
}
} // namespace

extern "C"
{
    auto HVC_CLIENT_CORE_CALL hvc_client_core_api_version(void) -> uint32_t
    {
        return HVC_CLIENT_CORE_ABI_VERSION;
    }

    auto HVC_CLIENT_CORE_CALL hvc_client_core_create(const hvc_client_core_config_v1* config,
                                                     hvc_client_core** core)
        -> hvc_client_core_result
    {
        if (core == nullptr)
        {
            return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
        }
        *core = nullptr;
        if (config == nullptr || config->struct_size < sizeof(hvc_client_core_config_v1))
        {
            return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
        }
        if (abiMajor(config->abi_version) != abiMajor(HVC_CLIENT_CORE_ABI_VERSION) ||
            config->abi_version > HVC_CLIENT_CORE_ABI_VERSION)
        {
            return HVC_CLIENT_CORE_RESULT_INCOMPATIBLE_ABI;
        }
        if (!validTransport(config->transport))
        {
            return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
        }

        try
        {
            auto instance = std::make_unique<hvc_client_core>(*config);
            if (!instance->transport.observerAttached())
            {
                return HVC_CLIENT_CORE_RESULT_TRANSPORT_ERROR;
            }
            *core = instance.release();
            return HVC_CLIENT_CORE_RESULT_OK;
        }
        catch (const std::bad_alloc&)
        {
            return HVC_CLIENT_CORE_RESULT_INTERNAL_ERROR;
        }
        catch (...)
        {
            return HVC_CLIENT_CORE_RESULT_INTERNAL_ERROR;
        }
    }

    void HVC_CLIENT_CORE_CALL hvc_client_core_destroy(hvc_client_core* core)
    {
        delete core;
    }

    auto HVC_CLIENT_CORE_CALL hvc_client_core_connect(hvc_client_core* core,
                                                      const hvc_client_core_room_grant_v1* grants,
                                                      size_t grant_count) -> hvc_client_core_result
    {
        if (core == nullptr)
        {
            return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
        }
        if (grants == nullptr || grant_count == 0 || grant_count > 3)
        {
            reportArgumentError(*core, "between one and three room grants are required");
            return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
        }

        try
        {
            std::vector<VoiceRoomGrant> values;
            values.reserve(grant_count);
            for (std::size_t index = 0; index < grant_count; ++index)
            {
                const auto& grant = grants[index];
                if (grant.struct_size < sizeof(hvc_client_core_room_grant_v1) ||
                    !validScope(grant.scope) || !validString(grant.url) ||
                    !validString(grant.token) || grant.url.size == 0 || grant.token.size == 0)
                {
                    reportArgumentError(*core, "room grants contain an invalid entry");
                    return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
                }
                values.push_back(
                    {domainScope(grant.scope), copyString(grant.url), copyString(grant.token)});
            }
            return operationResult(*core, core->voice_client.connect(values));
        }
        catch (...)
        {
            core->reportError("client_internal_error", "room grants could not be copied");
            return HVC_CLIENT_CORE_RESULT_INTERNAL_ERROR;
        }
    }

    auto HVC_CLIENT_CORE_CALL hvc_client_core_disconnect(hvc_client_core* core)
        -> hvc_client_core_result
    {
        if (core == nullptr)
        {
            return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
        }
        try
        {
            return operationResult(*core, core->voice_client.disconnect());
        }
        catch (...)
        {
            core->reportError("client_internal_error", "disconnect failed unexpectedly");
            return HVC_CLIENT_CORE_RESULT_INTERNAL_ERROR;
        }
    }

    auto HVC_CLIENT_CORE_CALL hvc_client_core_press_push_to_talk(hvc_client_core* core,
                                                                 hvc_client_core_scope scope)
        -> hvc_client_core_result
    {
        if (core == nullptr)
        {
            return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
        }
        if (!validScope(scope))
        {
            reportArgumentError(*core, "push-to-talk scope is invalid");
            return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
        }
        try
        {
            return operationResult(*core, core->voice_client.pressPushToTalk(domainScope(scope)));
        }
        catch (...)
        {
            core->reportError("client_internal_error", "push-to-talk start failed unexpectedly");
            return HVC_CLIENT_CORE_RESULT_INTERNAL_ERROR;
        }
    }

    auto HVC_CLIENT_CORE_CALL hvc_client_core_release_push_to_talk(hvc_client_core* core)
        -> hvc_client_core_result
    {
        if (core == nullptr)
        {
            return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
        }
        try
        {
            return operationResult(*core, core->voice_client.releasePushToTalk());
        }
        catch (...)
        {
            core->reportError("client_internal_error", "push-to-talk stop failed unexpectedly");
            return HVC_CLIENT_CORE_RESULT_INTERNAL_ERROR;
        }
    }

    auto HVC_CLIENT_CORE_CALL hvc_client_core_update_membership(
        hvc_client_core* core, const hvc_client_core_membership_v1* membership)
        -> hvc_client_core_result
    {
        if (core == nullptr)
        {
            return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
        }
        if (membership == nullptr)
        {
            reportArgumentError(*core, "membership is required");
            return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
        }
        try
        {
            auto value = copyMembership(*membership);
            if (!value.has_value())
            {
                reportArgumentError(*core, "membership contains an invalid field");
                return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
            }
            auto event_value = *value;
            auto stale_version = false;
            {
                const std::scoped_lock lock{core->membership_mutex};
                if (core->membership.has_value() && value->version <= core->membership->version)
                {
                    stale_version = true;
                }
                else
                {
                    core->membership = std::move(value);
                }
            }
            if (stale_version)
            {
                reportArgumentError(*core, "membership version must increase strictly");
                return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
            }
            core->emitMembership(event_value);
            return HVC_CLIENT_CORE_RESULT_OK;
        }
        catch (...)
        {
            core->reportError("client_internal_error", "membership could not be copied");
            return HVC_CLIENT_CORE_RESULT_INTERNAL_ERROR;
        }
    }

    auto HVC_CLIENT_CORE_CALL hvc_client_core_clear_membership(hvc_client_core* core)
        -> hvc_client_core_result
    {
        if (core == nullptr)
        {
            return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
        }
        {
            const std::scoped_lock lock{core->membership_mutex};
            core->membership.reset();
        }
        core->emitMembershipCleared();
        return HVC_CLIENT_CORE_RESULT_OK;
    }

    auto HVC_CLIENT_CORE_CALL hvc_client_core_get_connection_state(
        const hvc_client_core* core, hvc_client_core_connection_state* state)
        -> hvc_client_core_result
    {
        if (core == nullptr || state == nullptr)
        {
            return HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT;
        }
        *state = coreState(core->voice_client.state());
        return HVC_CLIENT_CORE_RESULT_OK;
    }
}
