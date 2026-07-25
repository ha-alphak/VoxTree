#pragma once

#include <compare>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace hvc::domain
{
template <typename Tag> class StrongId final
{
  public:
    explicit StrongId(std::string value) : value_(std::move(value))
    {
        if (value_.empty())
        {
            throw std::invalid_argument{"An identifier must not be empty."};
        }
    }

    [[nodiscard]] auto value() const noexcept -> std::string_view
    {
        return value_;
    }

    auto operator<=>(const StrongId&) const noexcept = default;

  private:
    std::string value_;
};

struct PlayerIdTag;
struct GroupIdTag;
struct SpecializationIdTag;
struct TeamIdTag;
struct HierarchyIdTag;
struct TransmissionIdTag;
struct ClientTransmissionIdTag;
struct RoleIdTag;

using PlayerId = StrongId<PlayerIdTag>;
using GroupId = StrongId<GroupIdTag>;
using SpecializationId = StrongId<SpecializationIdTag>;
using TeamId = StrongId<TeamIdTag>;
using HierarchyId = StrongId<HierarchyIdTag>;
using TransmissionId = StrongId<TransmissionIdTag>;
using ClientTransmissionId = StrongId<ClientTransmissionIdTag>;
using RoleId = StrongId<RoleIdTag>;
} // namespace hvc::domain
