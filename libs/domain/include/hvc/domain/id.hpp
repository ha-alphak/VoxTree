#pragma once

#include <compare>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

/**
 * Define the hierarchy, identities, routing rules, and state machines of HVC.
 */
namespace hvc::domain
{
/**
 * Store a non-empty, strongly typed identifier.
 *
 * Different tag types make identifiers from unrelated domains incompatible at
 * compile time even though they share the same string representation.
 *
 * @tparam Tag Type that distinguishes one identifier category from another.
 */
template <typename Tag> class StrongId final
{
  public:
    /**
     * Construct an identifier from its textual representation.
     *
     * @param value Non-empty identifier value.
     * @throws std::invalid_argument Thrown when `value` is empty.
     * @exceptsafe Strong exception guarantee.
     */
    explicit StrongId(std::string value) : value_(std::move(value))
    {
        if (value_.empty())
        {
            throw std::invalid_argument{"An identifier must not be empty."};
        }
    }

    /**
     * Return the textual representation.
     *
     * @returns A view that remains valid while this identifier is unchanged and
     *     alive.
     */
    [[nodiscard]] auto value() const noexcept -> std::string_view
    {
        return value_;
    }

    /**
     * Compare identifiers by their textual representation.
     *
     * @returns The strong ordering of the two identifier values.
     */
    auto operator<=>(const StrongId&) const noexcept = default;

  private:
    std::string value_;
};

/// Tag that distinguishes player identifiers.
struct PlayerIdTag;
/// Tag that distinguishes group identifiers.
struct GroupIdTag;
/// Tag that distinguishes specialization identifiers.
struct SpecializationIdTag;
/// Tag that distinguishes team identifiers.
struct TeamIdTag;
/// Tag that distinguishes hierarchy identifiers.
struct HierarchyIdTag;
/// Tag that distinguishes server transmission identifiers.
struct TransmissionIdTag;
/// Tag that distinguishes client-generated transmission identifiers.
struct ClientTransmissionIdTag;
/// Tag that distinguishes role identifiers.
struct RoleIdTag;
/// Tag that distinguishes authenticated session identifiers.
struct SessionIdTag;
/// Tag that distinguishes client device identifiers.
struct DeviceIdTag;
/// Tag that distinguishes request and audit correlation identifiers.
struct CorrelationIdTag;

/// Strong identifier for a participant.
using PlayerId = StrongId<PlayerIdTag>;
/// Strong identifier for a top-level communication group.
using GroupId = StrongId<GroupIdTag>;
/// Strong identifier for a specialization within a group.
using SpecializationId = StrongId<SpecializationIdTag>;
/// Strong identifier for a team within a specialization.
using TeamId = StrongId<TeamIdTag>;
/// Strong identifier for a versioned hierarchy definition.
using HierarchyId = StrongId<HierarchyIdTag>;
/// Strong identifier assigned by the server to an authorized transmission.
using TransmissionId = StrongId<TransmissionIdTag>;
/// Strong identifier assigned by a client to correlate a start request.
using ClientTransmissionId = StrongId<ClientTransmissionIdTag>;
/// Strong identifier for an authorization role.
using RoleId = StrongId<RoleIdTag>;
/// Strong identifier for an authenticated control-plane session.
using SessionId = StrongId<SessionIdTag>;
/// Strong identifier for a client device.
using DeviceId = StrongId<DeviceIdTag>;
/// Strong identifier used to correlate operations and audit events.
using CorrelationId = StrongId<CorrelationIdTag>;
} // namespace hvc::domain
