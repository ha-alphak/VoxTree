#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <hvc/application/control_plane.hpp>
#include <memory>
#include <stdexcept>
#include <vector>

namespace hvc::persistence
{
class PersistenceError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

struct StoredTransmissionAuditEvent final
{
    std::uint64_t sequence;
    application::TransmissionAuditEvent event;
};

class SqliteControlPlaneRepository final
    : public application::IMutableSessionRepository,
      public application::IMutableAuthoritativeMembershipRepository,
      public application::ITransmissionAuditEventSink
{
  public:
    static constexpr std::uint32_t latest_schema_version{3};

    explicit SqliteControlPlaneRepository(std::filesystem::path database_path);
    ~SqliteControlPlaneRepository() override;

    SqliteControlPlaneRepository(const SqliteControlPlaneRepository&) = delete;
    auto operator=(const SqliteControlPlaneRepository&) -> SqliteControlPlaneRepository& = delete;
    SqliteControlPlaneRepository(SqliteControlPlaneRepository&&) = delete;
    auto operator=(SqliteControlPlaneRepository&&) -> SqliteControlPlaneRepository& = delete;

    [[nodiscard]] auto find(const domain::SessionId& session_id) const
        -> std::optional<application::AuthenticatedSession> override;
    void upsert(application::AuthenticatedSession session) override;
    [[nodiscard]] auto erase(const domain::SessionId& session_id) -> bool override;

    [[nodiscard]] auto currentFor(const domain::PlayerId& player_id) const
        -> std::optional<application::AuthoritativeMembershipContext> override;
    [[nodiscard]] auto upsertIfNewer(const domain::PlayerId& player_id,
                                     application::AuthoritativeMembershipContext context)
        -> std::optional<application::AuthoritativeMembershipWriteError> override;
    [[nodiscard]] auto erase(const domain::PlayerId& player_id) -> bool override;

    void record(const application::TransmissionAuditEvent& event) noexcept override;
    [[nodiscard]] auto auditEventsAfter(std::uint64_t sequence, std::size_t limit) const
        -> std::vector<StoredTransmissionAuditEvent>;
    [[nodiscard]] auto eraseAuditEventsBefore(application::TimePoint exclusive_cutoff,
                                              std::size_t limit) -> std::size_t;
    [[nodiscard]] auto droppedAuditEventCount() const noexcept -> std::uint64_t;

    [[nodiscard]] auto schemaVersion() const -> std::uint32_t;

  private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};
} // namespace hvc::persistence
