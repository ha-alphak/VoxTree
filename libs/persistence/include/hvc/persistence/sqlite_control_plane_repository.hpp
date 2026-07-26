#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <hvc/application/control_plane.hpp>
#include <memory>
#include <stdexcept>
#include <vector>

/**
 * Persist control-plane sessions, memberships, and audit events.
 */
namespace hvc::persistence
{
/// Report a failed SQLite operation or violated persistence invariant.
class PersistenceError : public std::runtime_error
{
  public:
    /// Inherit constructors that accept a diagnostic message.
    using std::runtime_error::runtime_error;
};

/// Pair a durable audit sequence with its decoded event.
struct StoredTransmissionAuditEvent final
{
    /// Monotonically increasing database sequence.
    std::uint64_t sequence;
    /// Decoded audit-event payload.
    application::TransmissionAuditEvent event;
};

/**
 * Persist control-plane state transactionally in one SQLite database.
 *
 * Construction opens the database, enables required safety settings, and
 * migrates the schema atomically to `latest_schema_version`. Public methods
 * serialize access to the shared connection.
 */
class SqliteControlPlaneRepository final
    : public application::IMutableSessionRepository,
      public application::IMutableAuthoritativeMembershipRepository,
      public application::ITransmissionAuditEventSink
{
  public:
    /// Latest schema version understood by this binary.
    static constexpr std::uint32_t latest_schema_version{4};

    /**
     * Open or create a control-plane database.
     *
     * @param database_path Filesystem path or SQLite special path such as
     *     `:memory:`.
     * @throws PersistenceError Thrown when opening, configuring, or migrating
     *     the database fails.
     * @exceptsafe Strong exception guarantee.
     */
    explicit SqliteControlPlaneRepository(std::filesystem::path database_path);
    /// Close the database and release prepared resources.
    ~SqliteControlPlaneRepository() override;

    /// Copy construction is disabled.
    SqliteControlPlaneRepository(const SqliteControlPlaneRepository&) = delete;
    /// Copy assignment is disabled.
    auto operator=(const SqliteControlPlaneRepository&) -> SqliteControlPlaneRepository& = delete;
    /// Move construction is disabled.
    SqliteControlPlaneRepository(SqliteControlPlaneRepository&&) = delete;
    /// Move assignment is disabled.
    auto operator=(SqliteControlPlaneRepository&&) -> SqliteControlPlaneRepository& = delete;

    /// @copydoc application::ISessionRepository::find
    [[nodiscard]] auto find(const domain::SessionId& session_id) const
        -> std::optional<application::AuthenticatedSession> override;
    /// @copydoc application::IMutableSessionRepository::upsert
    void upsert(application::AuthenticatedSession session) override;
    /// @copydoc application::IMutableSessionRepository::erase
    [[nodiscard]] auto erase(const domain::SessionId& session_id) -> bool override;
    /**
     * Read a bounded batch of sessions that are no longer active.
     *
     * @param now Authoritative expiration boundary.
     * @param limit Maximum number of identifiers to return.
     * @returns Expired session identifiers ordered by expiration time.
     */
    [[nodiscard]] auto expiredSessionIds(application::TimePoint now, std::size_t limit) const
        -> std::vector<domain::SessionId>;

    /// @copydoc application::IAuthoritativeMembershipProvider::currentFor
    [[nodiscard]] auto currentFor(const domain::PlayerId& player_id) const
        -> std::optional<application::AuthoritativeMembershipContext> override;
    /// @copydoc application::IMutableAuthoritativeMembershipRepository::upsertIfNewer
    [[nodiscard]] auto upsertIfNewer(const domain::PlayerId& player_id,
                                     application::AuthoritativeMembershipContext context)
        -> std::optional<application::AuthoritativeMembershipWriteError> override;
    /// @copydoc application::IMutableAuthoritativeMembershipRepository::erase
    [[nodiscard]] auto erase(const domain::PlayerId& player_id) -> bool override;

    /// @copydoc application::ITransmissionAuditEventSink::record
    void record(const application::TransmissionAuditEvent& event) noexcept override;
    /**
     * Read a bounded page of durable audit events.
     *
     * @param sequence Exclusive lower sequence bound.
     * @param limit Maximum number of events to return.
     * @returns Events ordered by ascending sequence.
     * @throws std::invalid_argument Thrown when `limit` is zero.
     * @throws PersistenceError Thrown when the query or decoding fails.
     */
    [[nodiscard]] auto auditEventsAfter(std::uint64_t sequence, std::size_t limit) const
        -> std::vector<StoredTransmissionAuditEvent>;
    /**
     * Delete a bounded batch of expired audit events.
     *
     * @param exclusive_cutoff Delete events older than this time.
     * @param limit Maximum number of events to delete.
     * @returns Number of rows deleted.
     * @throws std::invalid_argument Thrown when `limit` is zero.
     * @throws PersistenceError Thrown when the transaction fails.
     */
    [[nodiscard]] auto eraseAuditEventsBefore(application::TimePoint exclusive_cutoff,
                                              std::size_t limit) -> std::size_t;
    /**
     * Return the number of audit events dropped after non-throwing writes failed.
     *
     * @returns Process-local cumulative dropped-event count.
     */
    [[nodiscard]] auto droppedAuditEventCount() const noexcept -> std::uint64_t;

    /**
     * Read the current database schema version.
     *
     * @returns Version stored by the migration subsystem.
     * @throws PersistenceError Thrown when the version cannot be read.
     */
    [[nodiscard]] auto schemaVersion() const -> std::uint32_t;

  private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};
} // namespace hvc::persistence
