#pragma once

#include <cstdint>
#include <functional>
#include <hvc/client_core.h>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

/** Provide an idiomatic C++ facade over the versioned client-core C ABI. */
namespace hvc::client_core
{
/// Identify one hierarchy scope.
enum class Scope : std::uint8_t
{
    /// Address the current team.
    team = HVC_CLIENT_CORE_SCOPE_TEAM,
    /// Address the current specialization.
    specialization = HVC_CLIENT_CORE_SCOPE_SPECIALIZATION,
    /// Address the current group.
    group = HVC_CLIENT_CORE_SCOPE_GROUP
};

/// Describe the observable aggregate connection state.
enum class ConnectionState : std::uint8_t
{
    /// No voice-room connection is active.
    disconnected = HVC_CLIENT_CORE_CONNECTION_DISCONNECTED,
    /// Granted voice-room connections are being established.
    connecting = HVC_CLIENT_CORE_CONNECTION_CONNECTING,
    /// Every granted voice-room connection is established.
    connected = HVC_CLIENT_CORE_CONNECTION_CONNECTED,
    /// At least one established connection is recovering.
    reconnecting = HVC_CLIENT_CORE_CONNECTION_RECONNECTING
};

/// Classify a synchronous client-core operation outcome.
enum class Result : std::uint8_t
{
    /// The operation completed successfully.
    success = HVC_CLIENT_CORE_RESULT_OK,
    /// A required argument was invalid.
    invalid_argument = HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT,
    /// The requested ABI contract is unsupported.
    incompatible_abi = HVC_CLIENT_CORE_RESULT_INCOMPATIBLE_ABI,
    /// Current state rejects the operation.
    invalid_state = HVC_CLIENT_CORE_RESULT_INVALID_STATE,
    /// The host transport rejected the operation.
    transport_error = HVC_CLIENT_CORE_RESULT_TRANSPORT_ERROR,
    /// An unexpected implementation failure was contained.
    internal_error = HVC_CLIENT_CORE_RESULT_INTERNAL_ERROR
};

/// Classify a host voice-transport failure.
enum class TransportError : std::uint8_t
{
    /// No error occurred.
    none = HVC_CLIENT_CORE_TRANSPORT_ERROR_NONE,
    /// A supplied argument was invalid.
    invalid_argument = HVC_CLIENT_CORE_TRANSPORT_ERROR_INVALID_ARGUMENT,
    /// Current transport state rejects the operation.
    invalid_state = HVC_CLIENT_CORE_TRANSPORT_ERROR_INVALID_STATE,
    /// A voice-room connection could not be established.
    connection_failed = HVC_CLIENT_CORE_TRANSPORT_ERROR_CONNECTION_FAILED,
    /// No usable audio device is available.
    audio_device_unavailable = HVC_CLIENT_CORE_TRANSPORT_ERROR_AUDIO_DEVICE_UNAVAILABLE,
    /// The selected audio device could not be activated.
    audio_device_switch_failed = HVC_CLIENT_CORE_TRANSPORT_ERROR_AUDIO_DEVICE_SWITCH_FAILED,
    /// Microphone publication could not be changed.
    publication_failed = HVC_CLIENT_CORE_TRANSPORT_ERROR_PUBLICATION_FAILED,
    /// The transport encountered an unclassified failure.
    internal_error = HVC_CLIENT_CORE_TRANSPORT_ERROR_INTERNAL
};

/// Classify events delivered to an embedding application.
enum class EventKind : std::uint8_t
{
    /// A complete authoritative membership snapshot was applied.
    membership_updated = HVC_CLIENT_CORE_EVENT_MEMBERSHIP_UPDATED,
    /// The authoritative membership was cleared.
    membership_cleared = HVC_CLIENT_CORE_EVENT_MEMBERSHIP_CLEARED,
    /// The aggregate connection state changed.
    connection_state_changed = HVC_CLIENT_CORE_EVENT_CONNECTION_STATE_CHANGED,
    /// A remote participant became audible.
    speaker_started = HVC_CLIENT_CORE_EVENT_SPEAKER_STARTED,
    /// A remote participant ceased to be audible.
    speaker_stopped = HVC_CLIENT_CORE_EVENT_SPEAKER_STOPPED,
    /// A synchronous operation or asynchronous transport callback failed.
    error = HVC_CLIENT_CORE_EVENT_ERROR
};

/// Hold the outcome of one host-transport operation.
struct TransportResult final
{
    /// Error classification, or `none` after success.
    TransportError error{TransportError::none};
    /// Diagnostic detail, empty after success.
    std::string message;

    /**
     * Return whether the operation succeeded.
     *
     * @returns `true` when `error` is `none`.
     */
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == TransportError::none;
    }
};

/// Grant access to one scope-specific voice room.
struct RoomGrant final
{
    /// Hierarchy scope carried by the room.
    Scope scope{Scope::team};
    /// Voice-service URL.
    std::string url;
    /// Short-lived access token.
    std::string token;
};

/// Describe one complete authoritative membership snapshot.
struct Membership final
{
    /// Monotonically increasing authoritative version.
    std::uint64_t version{0};
    /// Hierarchy definition referenced by the membership.
    std::string hierarchy_id;
    /// Authenticated public participant identifier.
    std::string player_id;
    /// Current group identifier.
    std::string group_id;
    /// Current specialization identifier.
    std::string specialization_id;
    /// Current team identifier.
    std::string team_id;
    /// Roles used to derive permissions.
    std::vector<std::string> role_ids;
    /// Whether the server considers the participant connected.
    bool connected{false};
    /// Whether receiving voice is authorized.
    bool can_receive_voice{false};
    /// Whether transmitting is prohibited.
    bool transmit_muted{false};
};

/// Own all data carried by one client-core callback.
struct Event final
{
    /// Per-instance monotonically increasing sequence.
    std::uint64_t sequence{0};
    /// Event classification.
    EventKind kind{EventKind::connection_state_changed};
    /// New state for a connection event.
    ConnectionState connection_state{ConnectionState::disconnected};
    /// Scope associated with a speaker event.
    Scope scope{Scope::team};
    /// Membership associated with an update event.
    std::optional<Membership> membership;
    /// Participant associated with a speaker event.
    std::string participant_id;
    /// Stable machine-readable code associated with an error event.
    std::string error_code;
    /// Diagnostic detail associated with an error event.
    std::string message;
};

/**
 * Receive events produced by a concrete host voice transport.
 *
 * A transport owns no observer and must detach it synchronously.
 */
class TransportObserver
{
  public:
    /// Destroy the observer interface.
    virtual ~TransportObserver() = default;

    /**
     * Report a new aggregate connection state.
     *
     * @param state New host-transport state.
     */
    virtual void onConnectionStateChanged(ConnectionState state) = 0;
    /**
     * Report a remote stream becoming available for admission.
     *
     * @param scope Scope containing the publication.
     * @param participant_id Non-empty public participant identifier.
     */
    virtual void onRemoteAudioAvailable(Scope scope, const std::string& participant_id) = 0;
    /**
     * Report a remote stream becoming unavailable.
     *
     * @param scope Scope containing the publication.
     * @param participant_id Non-empty public participant identifier.
     */
    virtual void onRemoteAudioUnavailable(Scope scope, const std::string& participant_id) = 0;
    /**
     * Report the start of audible remote audio.
     *
     * @param scope Scope containing the publication.
     * @param participant_id Non-empty public participant identifier.
     */
    virtual void onSpeakerStarted(Scope scope, const std::string& participant_id) = 0;
    /**
     * Report the end of audible remote audio.
     *
     * @param scope Scope containing the publication.
     * @param participant_id Non-empty public participant identifier.
     */
    virtual void onSpeakerStopped(Scope scope, const std::string& participant_id) = 0;
    /**
     * Report an asynchronous transport failure.
     *
     * @param error Stable failure classification.
     * @param message Diagnostic detail.
     */
    virtual void onError(TransportError error, const std::string& message) = 0;
};

/**
 * Supply voice connectivity and media operations to the embedded client core.
 *
 * Implementations may use LiveKit, a test double, or another transport. They
 * must serialize lifecycle mutations and stop callbacks before returning from
 * `setObserver(nullptr)`.
 */
class Transport
{
  public:
    /// Destroy the transport interface.
    virtual ~Transport() = default;

    /**
     * Replace the transport observer.
     *
     * @param observer Observer to notify, or `nullptr` to detach.
     * @returns Outcome of the synchronous attach or detach operation.
     */
    [[nodiscard]] virtual auto setObserver(TransportObserver* observer) -> TransportResult = 0;
    /**
     * Return the current aggregate state.
     *
     * @returns Latest host-transport state.
     */
    [[nodiscard]] virtual auto state() const noexcept -> ConnectionState = 0;
    /**
     * Connect all granted rooms.
     *
     * @param grants One non-empty, unique grant per authorized scope.
     * @returns Outcome of starting the connection operation.
     */
    [[nodiscard]] virtual auto connect(std::span<const RoomGrant> grants) -> TransportResult = 0;
    /**
     * Disconnect every room and stop publication.
     *
     * @returns Outcome of releasing transport resources.
     */
    [[nodiscard]] virtual auto disconnect() -> TransportResult = 0;
    /**
     * Start microphone publication in one scope.
     *
     * @param scope Scope receiving microphone audio.
     * @returns Outcome of starting publication.
     */
    [[nodiscard]] virtual auto startMicrophone(Scope scope) -> TransportResult = 0;
    /**
     * Stop active microphone publication.
     *
     * @returns Outcome of stopping publication.
     */
    [[nodiscard]] virtual auto stopMicrophone() -> TransportResult = 0;
    /**
     * Return the active microphone scope.
     *
     * @returns Active scope, or no value while publication is stopped.
     */
    [[nodiscard]] virtual auto activeMicrophoneScope() const noexcept -> std::optional<Scope> = 0;
    /**
     * Apply admission and gain to one remote stream.
     *
     * @param scope Scope containing the publication.
     * @param participant_id Non-empty public participant identifier.
     * @param admitted Whether the stream may be decoded and played.
     * @param gain Linear gain in the inclusive range `[0.0F, 1.0F]`.
     * @returns Outcome of applying the playback policy.
     */
    [[nodiscard]] virtual auto configureRemoteAudio(Scope scope, const std::string& participant_id,
                                                    bool admitted, float gain)
        -> TransportResult = 0;
};

/**
 * Own one C-ABI client-core handle with C++ values and RAII lifetime.
 *
 * The referenced transport must outlive this object. Event values are copied
 * before the callback is invoked. Callback exceptions are contained at the C
 * boundary and do not enter the DLL.
 */
class ClientCore final : private TransportObserver
{
  public:
    /// Callback receiving an owning C++ event value.
    using EventCallback = std::function<void(Event)>;

    /**
     * Create one embedded client-core instance.
     *
     * @param transport Host transport that must outlive this object.
     * @param callback Optional callback for normalized events.
     * @throws std::invalid_argument If the host transport table is rejected.
     * @throws std::runtime_error If the DLL cannot create the instance.
     */
    explicit ClientCore(Transport& transport, EventCallback callback = {})
        : transport_(transport), callback_(std::move(callback))
    {
        hvc_client_core_config_v1 config{};
        config.struct_size = sizeof(config);
        config.abi_version = HVC_CLIENT_CORE_ABI_VERSION;
        config.transport = transportTable();
        config.event_callback = &ClientCore::eventThunk;
        config.event_user_data = this;
        const auto result = hvc_client_core_create(&config, &core_);
        if (result == HVC_CLIENT_CORE_RESULT_INVALID_ARGUMENT ||
            result == HVC_CLIENT_CORE_RESULT_INCOMPATIBLE_ABI)
        {
            throw std::invalid_argument{"client-core configuration was rejected"};
        }
        if (result != HVC_CLIENT_CORE_RESULT_OK)
        {
            throw std::runtime_error{"client-core instance could not be created"};
        }
    }

    /// Detach the host transport and destroy the client-core handle.
    ~ClientCore() override
    {
        hvc_client_core_destroy(core_);
    }

    /// Copy construction is disabled.
    ClientCore(const ClientCore&) = delete;
    /// Copy assignment is disabled.
    auto operator=(const ClientCore&) -> ClientCore& = delete;
    /// Move construction is disabled because callback contexts contain `this`.
    ClientCore(ClientCore&&) = delete;
    /// Move assignment is disabled because callback contexts contain `this`.
    auto operator=(ClientCore&&) -> ClientCore& = delete;

    /**
     * Start connecting one to three authorized rooms.
     *
     * @param grants One non-empty, unique grant per authorized scope.
     * @returns Stable synchronous outcome.
     */
    [[nodiscard]] auto connect(std::span<const RoomGrant> grants) -> Result
    {
        std::vector<hvc_client_core_room_grant_v1> values;
        values.reserve(grants.size());
        for (const auto& grant : grants)
        {
            values.push_back({sizeof(hvc_client_core_room_grant_v1),
                              static_cast<hvc_client_core_scope>(grant.scope), view(grant.url),
                              view(grant.token)});
        }
        return static_cast<Result>(hvc_client_core_connect(core_, values.data(), values.size()));
    }

    /**
     * Stop publication and disconnect all rooms.
     *
     * @returns Stable synchronous outcome.
     */
    [[nodiscard]] auto disconnect() -> Result
    {
        return static_cast<Result>(hvc_client_core_disconnect(core_));
    }

    /**
     * Start exclusive push-to-talk publication.
     *
     * @param scope Authorized scope selected by the embedding application.
     * @returns Stable synchronous outcome.
     */
    [[nodiscard]] auto pressPushToTalk(Scope scope) -> Result
    {
        return static_cast<Result>(
            hvc_client_core_press_push_to_talk(core_, static_cast<hvc_client_core_scope>(scope)));
    }

    /**
     * Stop active push-to-talk publication.
     *
     * @returns Stable synchronous outcome.
     */
    [[nodiscard]] auto releasePushToTalk() -> Result
    {
        return static_cast<Result>(hvc_client_core_release_push_to_talk(core_));
    }

    /**
     * Publish a complete authoritative membership snapshot.
     *
     * @param membership Snapshot with a strictly increasing version.
     * @returns Stable synchronous outcome.
     */
    [[nodiscard]] auto updateMembership(const Membership& membership) -> Result
    {
        std::vector<hvc_client_core_string_view> roles;
        roles.reserve(membership.role_ids.size());
        for (const auto& role : membership.role_ids)
        {
            roles.push_back(view(role));
        }
        const hvc_client_core_membership_v1 value{
            sizeof(hvc_client_core_membership_v1),
            membership.version,
            view(membership.hierarchy_id),
            view(membership.player_id),
            view(membership.group_id),
            view(membership.specialization_id),
            view(membership.team_id),
            roles.data(),
            roles.size(),
            static_cast<std::uint8_t>(membership.connected),
            static_cast<std::uint8_t>(membership.can_receive_voice),
            static_cast<std::uint8_t>(membership.transmit_muted)};
        return static_cast<Result>(hvc_client_core_update_membership(core_, &value));
    }

    /**
     * Clear the current membership.
     *
     * @returns Stable synchronous outcome.
     */
    [[nodiscard]] auto clearMembership() -> Result
    {
        return static_cast<Result>(hvc_client_core_clear_membership(core_));
    }

    /**
     * Return the latest normalized connection state.
     *
     * @returns Current state.
     * @throws std::runtime_error If the opaque handle unexpectedly rejects the
     *     query.
     */
    [[nodiscard]] auto connectionState() const -> ConnectionState
    {
        hvc_client_core_connection_state state{};
        if (hvc_client_core_get_connection_state(core_, &state) != HVC_CLIENT_CORE_RESULT_OK)
        {
            throw std::runtime_error{"client-core connection state is unavailable"};
        }
        return static_cast<ConnectionState>(state);
    }

  private:
    [[nodiscard]] static auto view(const std::string& value) noexcept -> hvc_client_core_string_view
    {
        return {value.data(), value.size()};
    }

    [[nodiscard]] static auto string(hvc_client_core_string_view value) -> std::string
    {
        return value.size == 0 ? std::string{} : std::string{value.data, value.size};
    }

    [[nodiscard]] auto transportTable() noexcept -> hvc_client_core_transport_v1
    {
        return {sizeof(hvc_client_core_transport_v1),
                this,
                &ClientCore::setObserverThunk,
                &ClientCore::stateThunk,
                &ClientCore::connectThunk,
                &ClientCore::disconnectThunk,
                &ClientCore::startMicrophoneThunk,
                &ClientCore::stopMicrophoneThunk,
                &ClientCore::activeMicrophoneScopeThunk,
                &ClientCore::configureRemoteAudioThunk};
    }

    [[nodiscard]] static auto finishTransportCall(TransportResult result,
                                                  hvc_client_core_string_view* message) noexcept
        -> hvc_client_core_transport_error
    {
        thread_local std::string diagnostic;
        diagnostic = std::move(result.message);
        if (message != nullptr)
        {
            *message = view(diagnostic);
        }
        return static_cast<hvc_client_core_transport_error>(result.error);
    }

    [[nodiscard]] static auto internalTransportFailure(
        hvc_client_core_string_view* message) noexcept -> hvc_client_core_transport_error
    {
        return finishTransportCall({TransportError::internal_error, "C++ transport threw"},
                                   message);
    }

    static void HVC_CLIENT_CORE_CALL eventThunk(void* user_data,
                                                const hvc_client_core_event_v1* event) noexcept
    {
        if (user_data == nullptr || event == nullptr)
        {
            return;
        }
        auto& self = *static_cast<ClientCore*>(user_data);
        try
        {
            if (!self.callback_)
            {
                return;
            }
            Event value{};
            value.sequence = event->sequence;
            value.kind = static_cast<EventKind>(event->kind);
            value.connection_state = static_cast<ConnectionState>(event->connection_state);
            value.scope = static_cast<Scope>(event->scope);
            value.participant_id = string(event->participant_id);
            value.error_code = string(event->error_code);
            value.message = string(event->message);
            if (event->membership != nullptr)
            {
                const auto& source = *event->membership;
                Membership membership{};
                membership.version = source.version;
                membership.hierarchy_id = string(source.hierarchy_id);
                membership.player_id = string(source.player_id);
                membership.group_id = string(source.group_id);
                membership.specialization_id = string(source.specialization_id);
                membership.team_id = string(source.team_id);
                membership.connected = source.connected != 0;
                membership.can_receive_voice = source.can_receive_voice != 0;
                membership.transmit_muted = source.transmit_muted != 0;
                membership.role_ids.reserve(source.role_count);
                for (std::size_t index = 0; index < source.role_count; ++index)
                {
                    membership.role_ids.push_back(string(source.role_ids[index]));
                }
                value.membership = std::move(membership);
            }
            self.callback_(std::move(value));
        }
        catch (...)
        {
            // Never unwind a host callback through the versioned C boundary.
            return;
        }
    }

    [[nodiscard]] static auto HVC_CLIENT_CORE_CALL setObserverThunk(
        void* user_data, const hvc_client_core_transport_observer_v1* observer,
        hvc_client_core_string_view* message) noexcept -> hvc_client_core_transport_error
    {
        auto& self = *static_cast<ClientCore*>(user_data);
        try
        {
            {
                const std::scoped_lock lock{self.observer_mutex_};
                self.transport_observer_ =
                    observer == nullptr ? std::optional<hvc_client_core_transport_observer_v1>{}
                                        : *observer;
            }
            auto result = self.transport_.setObserver(observer == nullptr ? nullptr : &self);
            if (!result && observer != nullptr)
            {
                const std::scoped_lock lock{self.observer_mutex_};
                self.transport_observer_.reset();
            }
            return finishTransportCall(std::move(result), message);
        }
        catch (...)
        {
            return internalTransportFailure(message);
        }
    }

    [[nodiscard]] static auto HVC_CLIENT_CORE_CALL stateThunk(void* user_data) noexcept
        -> hvc_client_core_connection_state
    {
        try
        {
            const auto& self = *static_cast<ClientCore*>(user_data);
            return static_cast<hvc_client_core_connection_state>(self.transport_.state());
        }
        catch (...)
        {
            return HVC_CLIENT_CORE_CONNECTION_DISCONNECTED;
        }
    }

    [[nodiscard]] static auto HVC_CLIENT_CORE_CALL connectThunk(
        void* user_data, const hvc_client_core_room_grant_v1* grants, std::size_t grant_count,
        hvc_client_core_string_view* message) noexcept -> hvc_client_core_transport_error
    {
        auto& self = *static_cast<ClientCore*>(user_data);
        try
        {
            std::vector<RoomGrant> values;
            values.reserve(grant_count);
            for (std::size_t index = 0; index < grant_count; ++index)
            {
                values.push_back({static_cast<Scope>(grants[index].scope),
                                  string(grants[index].url), string(grants[index].token)});
            }
            return finishTransportCall(self.transport_.connect(values), message);
        }
        catch (...)
        {
            return internalTransportFailure(message);
        }
    }

    [[nodiscard]] static auto HVC_CLIENT_CORE_CALL
    disconnectThunk(void* user_data, hvc_client_core_string_view* message) noexcept
        -> hvc_client_core_transport_error
    {
        try
        {
            auto& self = *static_cast<ClientCore*>(user_data);
            return finishTransportCall(self.transport_.disconnect(), message);
        }
        catch (...)
        {
            return internalTransportFailure(message);
        }
    }

    [[nodiscard]] static auto HVC_CLIENT_CORE_CALL startMicrophoneThunk(
        void* user_data, hvc_client_core_scope scope, hvc_client_core_string_view* message) noexcept
        -> hvc_client_core_transport_error
    {
        try
        {
            auto& self = *static_cast<ClientCore*>(user_data);
            return finishTransportCall(self.transport_.startMicrophone(static_cast<Scope>(scope)),
                                       message);
        }
        catch (...)
        {
            return internalTransportFailure(message);
        }
    }

    [[nodiscard]] static auto HVC_CLIENT_CORE_CALL
    stopMicrophoneThunk(void* user_data, hvc_client_core_string_view* message) noexcept
        -> hvc_client_core_transport_error
    {
        try
        {
            auto& self = *static_cast<ClientCore*>(user_data);
            return finishTransportCall(self.transport_.stopMicrophone(), message);
        }
        catch (...)
        {
            return internalTransportFailure(message);
        }
    }

    [[nodiscard]] static auto HVC_CLIENT_CORE_CALL activeMicrophoneScopeThunk(
        void* user_data, hvc_client_core_scope* scope) noexcept -> std::uint8_t
    {
        try
        {
            const auto& self = *static_cast<ClientCore*>(user_data);
            const auto current = self.transport_.activeMicrophoneScope();
            if (!current.has_value())
            {
                return 0;
            }
            *scope = static_cast<hvc_client_core_scope>(*current);
            return 1;
        }
        catch (...)
        {
            return 0;
        }
    }

    [[nodiscard]] static auto HVC_CLIENT_CORE_CALL configureRemoteAudioThunk(
        void* user_data, hvc_client_core_scope scope, hvc_client_core_string_view participant_id,
        std::uint8_t admitted, float gain, hvc_client_core_string_view* message) noexcept
        -> hvc_client_core_transport_error
    {
        try
        {
            auto& self = *static_cast<ClientCore*>(user_data);
            return finishTransportCall(
                self.transport_.configureRemoteAudio(static_cast<Scope>(scope),
                                                     string(participant_id), admitted != 0, gain),
                message);
        }
        catch (...)
        {
            return internalTransportFailure(message);
        }
    }

    [[nodiscard]] auto observer() const noexcept
        -> std::optional<hvc_client_core_transport_observer_v1>
    {
        const std::scoped_lock lock{observer_mutex_};
        return transport_observer_;
    }

    void onConnectionStateChanged(ConnectionState state) override
    {
        const auto current = observer();
        if (current.has_value() && current->connection_state_changed != nullptr)
        {
            current->connection_state_changed(current->user_data,
                                              static_cast<hvc_client_core_connection_state>(state));
        }
    }

    void onRemoteAudioAvailable(Scope scope, const std::string& participant_id) override
    {
        const auto current = observer();
        if (current.has_value() && current->remote_audio_available != nullptr)
        {
            current->remote_audio_available(current->user_data,
                                            static_cast<hvc_client_core_scope>(scope),
                                            view(participant_id));
        }
    }

    void onRemoteAudioUnavailable(Scope scope, const std::string& participant_id) override
    {
        const auto current = observer();
        if (current.has_value() && current->remote_audio_unavailable != nullptr)
        {
            current->remote_audio_unavailable(current->user_data,
                                              static_cast<hvc_client_core_scope>(scope),
                                              view(participant_id));
        }
    }

    void onSpeakerStarted(Scope scope, const std::string& participant_id) override
    {
        const auto current = observer();
        if (current.has_value() && current->speaker_started != nullptr)
        {
            current->speaker_started(current->user_data, static_cast<hvc_client_core_scope>(scope),
                                     view(participant_id));
        }
    }

    void onSpeakerStopped(Scope scope, const std::string& participant_id) override
    {
        const auto current = observer();
        if (current.has_value() && current->speaker_stopped != nullptr)
        {
            current->speaker_stopped(current->user_data, static_cast<hvc_client_core_scope>(scope),
                                     view(participant_id));
        }
    }

    void onError(TransportError error, const std::string& message) override
    {
        const auto current = observer();
        if (current.has_value() && current->error != nullptr)
        {
            current->error(current->user_data, static_cast<hvc_client_core_transport_error>(error),
                           view(message));
        }
    }

    Transport& transport_;
    EventCallback callback_;
    hvc_client_core* core_{nullptr};
    mutable std::mutex observer_mutex_;
    std::optional<hvc_client_core_transport_observer_v1> transport_observer_;
};
} // namespace hvc::client_core
