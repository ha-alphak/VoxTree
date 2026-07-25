#include <cstdio>
#include <exception>
#include <hvc/domain/state_machine.hpp>

namespace
{
using namespace hvc::domain;

auto connect(VoiceSessionStateMachine& session, std::uint64_t version) -> bool
{
    return session.beginConnect() && session.transportConnected() &&
           session.membershipRefreshed(version) && session.subscriptionsRestored(version);
}

auto testConnectionRecoverySequence() -> bool
{
    ConnectionStateMachine connection;
    if (connection.state() != ConnectionState::disconnected || connection.ready() ||
        connection.transportConnected() || !connection.beginConnect() ||
        connection.state() != ConnectionState::connecting_transport)
    {
        return false;
    }
    if (!connection.transportConnected() ||
        connection.state() != ConnectionState::refreshing_membership ||
        connection.subscriptionsRestored(10) || connection.membershipRefreshed(0) ||
        !connection.membershipRefreshed(10) ||
        connection.state() != ConnectionState::restoring_subscriptions ||
        connection.subscriptionsRestored(11) || !connection.subscriptionsRestored(10) ||
        !connection.ready() || connection.membershipVersion() != 10)
    {
        return false;
    }
    if (connection.membershipChanged(10) || !connection.membershipChanged(11) ||
        connection.ready() || !connection.subscriptionsRestored(11) ||
        !connection.connectionLost() ||
        connection.state() != ConnectionState::reconnecting_transport ||
        connection.membershipVersion().has_value())
    {
        return false;
    }

    return connection.transportConnected() && connection.membershipRefreshed(12) &&
           connection.subscriptionsRestored(12) && connection.ready() &&
           connection.membershipVersion() == 12;
}

auto testTransmissionLifecycleAndCorrelation() -> bool
{
    TransmissionStateMachine transmission;
    const ClientTransmissionId first_request{"client-1"};
    const ClientTransmissionId second_request{"client-2"};
    const TransmissionId first_transmission{"server-1"};
    const TransmissionId second_transmission{"server-2"};

    if (transmission.state() != TransmissionState::idle ||
        transmission.requestStart(ClientTransmissionId{"invalid"}, VoiceScope::team, 0) ||
        !transmission.requestStart(ClientTransmissionId{"client-1"}, VoiceScope::group, 21) ||
        transmission.state() != TransmissionState::requesting ||
        transmission.scope() != VoiceScope::group || transmission.membershipVersion() != 21 ||
        transmission.accepted(second_request, TransmissionId{"wrong"}) ||
        !transmission.accepted(first_request, TransmissionId{"server-1"}) ||
        transmission.state() != TransmissionState::transmitting ||
        transmission.transmissionId() == nullptr ||
        *transmission.transmissionId() != first_transmission || !transmission.requestEnd() ||
        transmission.state() != TransmissionState::ending ||
        transmission.ended(second_transmission) || !transmission.ended(first_transmission) ||
        transmission.state() != TransmissionState::idle ||
        transmission.lastStopReason() != TransmissionStopReason::push_to_talk_released)
    {
        return false;
    }

    if (!transmission.requestStart(ClientTransmissionId{"client-1"}, VoiceScope::team, 22) ||
        !transmission.requestEnd() || transmission.state() != TransmissionState::idle ||
        !transmission.requestStart(ClientTransmissionId{"client-2"}, VoiceScope::team, 22) ||
        transmission.accepted(first_request, TransmissionId{"stale"}) ||
        !transmission.accepted(second_request, TransmissionId{"server-2"}) ||
        !transmission.interrupt(TransmissionStopReason::timed_out))
    {
        return false;
    }

    return transmission.state() == TransmissionState::idle &&
           transmission.clientRequestId() == nullptr && transmission.transmissionId() == nullptr &&
           transmission.lastStopReason() == TransmissionStopReason::timed_out;
}

auto testReconnectNeverResumesTransmission() -> bool
{
    VoiceSessionStateMachine session;
    session.selectScope(VoiceScope::group);
    const ClientTransmissionId original_request{"before-disconnect"};
    const TransmissionId original_transmission{"original-server-id"};

    if (!connect(session, 42) ||
        !session.requestTransmission(ClientTransmissionId{"before-disconnect"}) ||
        !session.transmissionAccepted(original_request, TransmissionId{"original-server-id"}) ||
        session.transmissionState() != TransmissionState::transmitting ||
        !session.connectionLost() ||
        session.connectionState() != ConnectionState::reconnecting_transport ||
        session.transmissionState() != TransmissionState::idle ||
        session.lastTransmissionStopReason() != TransmissionStopReason::connection_lost ||
        session.selectedScope() != VoiceScope::group)
    {
        return false;
    }

    if (!session.transportConnected() || !session.membershipRefreshed(43) ||
        !session.subscriptionsRestored(43) || session.connectionState() != ConnectionState::ready ||
        session.transmissionState() != TransmissionState::idle ||
        session.transmissionAccepted(original_request, TransmissionId{"late-server-id"}) ||
        session.endTransmission())
    {
        return false;
    }

    const ClientTransmissionId new_request{"after-reconnect"};
    return session.requestTransmission(ClientTransmissionId{"after-reconnect"}) &&
           session.transmissionAccepted(new_request, TransmissionId{"new-server-id"}) &&
           session.transmissionState() == TransmissionState::transmitting &&
           session.transmissionId() != nullptr &&
           *session.transmissionId() != original_transmission;
}

auto testMembershipAndPermissionInterruptions() -> bool
{
    VoiceSessionStateMachine session;
    const ClientTransmissionId first_request{"membership-change"};
    if (!connect(session, 100) ||
        !session.requestTransmission(ClientTransmissionId{"membership-change"}) ||
        !session.transmissionAccepted(first_request, TransmissionId{"active-1"}) ||
        !session.membershipChanged(101) ||
        session.connectionState() != ConnectionState::restoring_subscriptions ||
        session.transmissionState() != TransmissionState::idle ||
        session.lastTransmissionStopReason() != TransmissionStopReason::membership_changed ||
        session.requestTransmission(ClientTransmissionId{"too-early"}) ||
        !session.subscriptionsRestored(101))
    {
        return false;
    }

    const ClientTransmissionId second_request{"permission-change"};
    if (!session.requestTransmission(ClientTransmissionId{"permission-change"}) ||
        !session.transmissionAccepted(second_request, TransmissionId{"active-2"}) ||
        !session.permissionRevoked() || session.transmissionState() != TransmissionState::idle ||
        session.lastTransmissionStopReason() != TransmissionStopReason::permission_revoked)
    {
        return false;
    }

    const ClientTransmissionId rejected_request{"rejected"};
    if (!session.requestTransmission(ClientTransmissionId{"rejected"}) ||
        !session.transmissionRejected(rejected_request) ||
        session.lastTransmissionStopReason() != TransmissionStopReason::rejected ||
        !session.requestTransmission(ClientTransmissionId{"transport-failure"}) ||
        !session.transmissionTransportFailed() ||
        session.lastTransmissionStopReason() != TransmissionStopReason::transport_error)
    {
        return false;
    }

    return session.requestTransmission(ClientTransmissionId{"pending-disconnect"}) &&
           session.disconnect() && session.connectionState() == ConnectionState::disconnected &&
           session.transmissionState() == TransmissionState::idle &&
           session.lastTransmissionStopReason() == TransmissionStopReason::disconnected;
}
} // namespace

auto main() noexcept -> int
{
    try
    {
        const bool passed =
            testConnectionRecoverySequence() && testTransmissionLifecycleAndCorrelation() &&
            testReconnectNeverResumesTransmission() && testMembershipAndPermissionInterruptions();
        if (!passed)
        {
            std::fputs("A state-machine assertion failed.\n", stderr);
            return 1;
        }
    }
    catch (const std::exception& error)
    {
        std::fputs("Unexpected exception: ", stderr);
        std::fputs(error.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }

    return 0;
}
