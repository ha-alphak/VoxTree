#pragma once

#include <hvc/client/control_plane_client.hpp>
#include <hvc/client/voice_client.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace hvc::client
{
enum class VoiceSessionErrorSource : std::uint8_t
{
    client_state,
    control_plane,
    voice_transport
};

struct VoiceSessionError final
{
    VoiceSessionErrorSource source{VoiceSessionErrorSource::client_state};
    std::string code;
    std::string message;
    int status_code{0};
};

struct VoiceSessionResult final
{
    [[nodiscard]] static auto success(std::optional<StartedTransmission> started_transmission =
                                          std::nullopt) -> VoiceSessionResult;
    [[nodiscard]] static auto failure(VoiceSessionError action_error) -> VoiceSessionResult;
    [[nodiscard]] explicit operator bool() const noexcept;

    bool successful{false};
    std::optional<StartedTransmission> transmission;
    std::optional<VoiceSessionError> error;
};

class IPushToTalkTarget
{
  public:
    IPushToTalkTarget() = default;
    IPushToTalkTarget(const IPushToTalkTarget&) = delete;
    auto operator=(const IPushToTalkTarget&) -> IPushToTalkTarget& = delete;
    IPushToTalkTarget(IPushToTalkTarget&&) = delete;
    auto operator=(IPushToTalkTarget&&) -> IPushToTalkTarget& = delete;
    virtual ~IPushToTalkTarget() = default;

    [[nodiscard]] virtual auto pressPushToTalk(domain::VoiceScope scope) -> VoiceSessionResult = 0;
    [[nodiscard]] virtual auto releasePushToTalk() -> VoiceSessionResult = 0;
};

class AuthorizedVoiceClient final : public IPushToTalkTarget
{
  public:
    AuthorizedVoiceClient(ControlPlaneClient& control_plane, VoiceClient& voice_client);

    [[nodiscard]] auto connect(std::string_view external_credential) -> VoiceSessionResult;
    [[nodiscard]] auto disconnect() -> VoiceSessionResult;
    [[nodiscard]] auto pressPushToTalk(domain::VoiceScope scope) -> VoiceSessionResult override;
    [[nodiscard]] auto releasePushToTalk() -> VoiceSessionResult override;
    [[nodiscard]] auto endInterruptedTransmission() -> VoiceSessionResult;

    [[nodiscard]] auto membership() const -> std::optional<MembershipView>;
    [[nodiscard]] auto activeTransmission() const -> std::optional<StartedTransmission>;

  private:
    [[nodiscard]] static auto controlPlaneFailure(const ControlPlaneError& error)
        -> VoiceSessionResult;
    [[nodiscard]] static auto transportFailure(const VoiceTransportResult& result)
        -> VoiceSessionResult;

    ControlPlaneClient& control_plane_;
    VoiceClient& voice_client_;
    std::optional<MembershipView> membership_;
    std::optional<StartedTransmission> active_transmission_;
};
} // namespace hvc::client
