#include <cstdio>
#include <exception>
#include <hvc/client/authorized_voice_client.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
namespace client = hvc::client;
namespace domain = hvc::domain;

class Identifiers final : public client::IClientIdentifierGenerator
{
  public:
    [[nodiscard]] auto nextCorrelationId() -> domain::CorrelationId override
    {
        return domain::CorrelationId{"correlation-" + std::to_string(++correlation_count)};
    }

    [[nodiscard]] auto nextTransmissionId() -> domain::ClientTransmissionId override
    {
        return domain::ClientTransmissionId{"client-transmission-" +
                                            std::to_string(++transmission_count)};
    }

    int correlation_count{0};
    int transmission_count{0};
};

class ScriptedHttpTransport final : public client::IClientHttpTransport
{
  public:
    [[nodiscard]] auto send(const client::ClientHttpRequest& request)
        -> client::ClientHttpResponse override
    {
        targets.push_back(request.target);
        if (request.target == "/api/v1/sessions")
        {
            return json(
                201,
                R"({"api_version":"v1","session_id":"session-1","player_id":"player-1","device_id":"device-1","expires_at_unix_ms":2000000})");
        }
        if (request.target == "/api/v1/membership")
        {
            return json(
                200,
                R"({"api_version":"v1","membership_version":42,"hierarchy_id":"hierarchy-1","player_id":"player-1","group_id":"group-1","specialization_id":"specialization-1","team_id":"team-1","role_ids":["speaker"],"connected":true,"can_receive_voice":true,"transmit_muted":false})");
        }
        if (request.target == "/api/v1/voice-grants")
        {
            return json(
                201,
                R"({"api_version":"v1","server_url":"ws://voice","membership_version":42,"expires_at_unix_ms":1030000,"grants":[{"scope":"team","room_name":"team:team-1","access_token":"team-token"},{"scope":"group","room_name":"group:group-1","access_token":"group-token"}]})");
        }
        if (request.target == "/api/v1/transmissions")
        {
            if (reject_start)
            {
                return json(
                    403,
                    R"({"api_version":"v1","error":{"code":"voice_scope_not_authorized","message":"scope denied"}})");
            }
            start_authorized = true;
            return json(
                201,
                R"({"api_version":"v1","transmission_id":"transmission-1","client_transmission_id":"client-transmission-1","scope":"group","membership_version":42,"recipient_count":7})");
        }
        if (request.target == "/api/v1/transmissions/transmission-1")
        {
            ++end_calls;
            if (end_failures_remaining > 0)
            {
                --end_failures_remaining;
                return json(
                    503,
                    R"({"api_version":"v1","error":{"code":"temporarily_unavailable","message":"retry"}})");
            }
            return json(
                200,
                R"({"api_version":"v1","transmission_id":"transmission-1","status":"ended","stop_reason":"push_to_talk_released"})");
        }
        return json(
            404, R"({"api_version":"v1","error":{"code":"route_not_found","message":"missing"}})");
    }

    [[nodiscard]] static auto json(int status, std::string body) -> client::ClientHttpResponse
    {
        return {status, {{"x-hvc-api-version", "v1"}}, std::move(body), {}};
    }

    std::vector<std::string> targets;
    bool reject_start{false};
    bool start_authorized{false};
    int end_calls{0};
    int end_failures_remaining{0};
};

class FakeVoiceTransport final : public client::IVoiceTransport
{
  public:
    explicit FakeVoiceTransport(const ScriptedHttpTransport& http) : http_(http)
    {
    }

    void setObserver(client::IVoiceTransportObserver* observer) noexcept override
    {
        observer_ = observer;
    }

    [[nodiscard]] auto state() const noexcept -> client::VoiceTransportState override
    {
        return state_;
    }

    [[nodiscard]] auto connect(std::span<const client::VoiceRoomGrant> grants)
        -> client::VoiceTransportResult override
    {
        grant_count = grants.size();
        state_ = client::VoiceTransportState::connected;
        if (observer_ != nullptr)
        {
            observer_->onTransportStateChanged(state_);
        }
        return client::VoiceTransportResult::success();
    }

    [[nodiscard]] auto disconnect() -> client::VoiceTransportResult override
    {
        state_ = client::VoiceTransportState::disconnected;
        if (observer_ != nullptr)
        {
            observer_->onTransportStateChanged(state_);
        }
        return client::VoiceTransportResult::success();
    }

    [[nodiscard]] auto startMicrophone(domain::VoiceScope scope)
        -> client::VoiceTransportResult override
    {
        ++start_calls;
        if (!http_.start_authorized)
        {
            return client::VoiceTransportResult::failure(
                client::VoiceTransportError::invalid_state,
                "microphone started before control-plane authorization");
        }
        if (fail_microphone_start)
        {
            return client::VoiceTransportResult::failure(
                client::VoiceTransportError::publication_failed, "microphone failed");
        }
        active_scope_ = scope;
        return client::VoiceTransportResult::success();
    }

    [[nodiscard]] auto stopMicrophone() -> client::VoiceTransportResult override
    {
        active_scope_.reset();
        return client::VoiceTransportResult::success();
    }

    [[nodiscard]] auto activeTransmissionScope() const noexcept
        -> std::optional<domain::VoiceScope> override
    {
        return active_scope_;
    }

    [[nodiscard]] auto recordingDevices() const -> std::vector<client::AudioDevice> override
    {
        return {};
    }

    [[nodiscard]] auto playoutDevices() const -> std::vector<client::AudioDevice> override
    {
        return {};
    }

    [[nodiscard]] auto selectRecordingDevice(const std::string&)
        -> client::VoiceTransportResult override
    {
        return client::VoiceTransportResult::success();
    }

    [[nodiscard]] auto selectPlayoutDevice(const std::string&)
        -> client::VoiceTransportResult override
    {
        return client::VoiceTransportResult::success();
    }

    [[nodiscard]] auto configureRemoteAudio(domain::VoiceScope, const std::string&, bool, float)
        -> client::VoiceTransportResult override
    {
        return client::VoiceTransportResult::success();
    }

    [[nodiscard]] auto remoteParticipantCount(domain::VoiceScope) const -> std::size_t override
    {
        return 0;
    }

    [[nodiscard]] auto hasRemoteAudio(domain::VoiceScope) const -> bool override
    {
        return false;
    }

    const ScriptedHttpTransport& http_;
    client::IVoiceTransportObserver* observer_{nullptr};
    client::VoiceTransportState state_{client::VoiceTransportState::disconnected};
    std::optional<domain::VoiceScope> active_scope_;
    std::size_t grant_count{0};
    int start_calls{0};
    bool fail_microphone_start{false};
};

auto connectsAndAuthorizesBeforePublishing() -> bool
{
    ScriptedHttpTransport http;
    Identifiers identifiers;
    client::ControlPlaneClient control{http, identifiers, domain::DeviceId{"device-1"}};
    FakeVoiceTransport voice_transport{http};
    client::VoiceClient voice{voice_transport};
    client::AuthorizedVoiceClient authorized{control, voice};

    const auto connected = authorized.connect("external-credential");
    const auto started = authorized.pressPushToTalk(domain::VoiceScope::group);
    const auto ended = authorized.releasePushToTalk();
    const auto disconnected = authorized.disconnect();
    return connected && started && ended && disconnected && voice_transport.grant_count == 2 &&
           voice_transport.start_calls == 1 && started.transmission.has_value() &&
           started.transmission->recipient_count == 7 && http.end_calls == 1 &&
           !authorized.activeTransmission().has_value();
}

auto neverPublishesWhenAuthorizationIsRejected() -> bool
{
    ScriptedHttpTransport http;
    http.reject_start = true;
    Identifiers identifiers;
    client::ControlPlaneClient control{http, identifiers, domain::DeviceId{"device-1"}};
    FakeVoiceTransport voice_transport{http};
    client::VoiceClient voice{voice_transport};
    client::AuthorizedVoiceClient authorized{control, voice};

    return authorized.connect("external-credential") &&
           !authorized.pressPushToTalk(domain::VoiceScope::group) &&
           voice_transport.start_calls == 0 && http.end_calls == 0;
}

auto rollsBackAuthorizationWhenMicrophoneFails() -> bool
{
    ScriptedHttpTransport http;
    Identifiers identifiers;
    client::ControlPlaneClient control{http, identifiers, domain::DeviceId{"device-1"}};
    FakeVoiceTransport voice_transport{http};
    voice_transport.fail_microphone_start = true;
    client::VoiceClient voice{voice_transport};
    client::AuthorizedVoiceClient authorized{control, voice};

    return authorized.connect("external-credential") &&
           !authorized.pressPushToTalk(domain::VoiceScope::group) && http.end_calls == 1 &&
           !authorized.activeTransmission().has_value();
}

auto retainsFailedRollbackForExplicitCleanup() -> bool
{
    ScriptedHttpTransport http;
    http.end_failures_remaining = 1;
    Identifiers identifiers;
    client::ControlPlaneClient control{http, identifiers, domain::DeviceId{"device-1"}};
    FakeVoiceTransport voice_transport{http};
    voice_transport.fail_microphone_start = true;
    client::VoiceClient voice{voice_transport};
    client::AuthorizedVoiceClient authorized{control, voice};

    if (!authorized.connect("external-credential") ||
        authorized.pressPushToTalk(domain::VoiceScope::group) ||
        !authorized.activeTransmission().has_value())
    {
        return false;
    }
    return authorized.endInterruptedTransmission() && http.end_calls == 2 &&
           !authorized.activeTransmission().has_value();
}

class InvalidVersionTransport final : public client::IClientHttpTransport
{
  public:
    [[nodiscard]] auto send(const client::ClientHttpRequest&) -> client::ClientHttpResponse override
    {
        return {
            201,
            {{"X-HVC-API-Version", "v2"}},
            R"({"api_version":"v1","session_id":"session-1","player_id":"player-1","device_id":"device-1","expires_at_unix_ms":2000000})",
            {}};
    }
};

auto rejectsMismatchedProtocolHeader() -> bool
{
    InvalidVersionTransport http;
    Identifiers identifiers;
    client::ControlPlaneClient control{http, identifiers, domain::DeviceId{"device-1"}};

    const auto result = control.createSession("external-credential");
    return !result && result.error.has_value() &&
           result.error->kind == client::ControlPlaneErrorKind::invalid_response;
}
} // namespace

auto main() noexcept -> int
{
    try
    {
        if (!connectsAndAuthorizesBeforePublishing() ||
            !neverPublishesWhenAuthorizationIsRejected() ||
            !rollsBackAuthorizationWhenMicrophoneFails() ||
            !retainsFailedRollbackForExplicitCleanup() || !rejectsMismatchedProtocolHeader())
        {
            std::fputs("A control-plane client assertion failed.\n", stderr);
            return 1;
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Unexpected exception: %s\n", error.what());
        return 1;
    }
    return 0;
}
