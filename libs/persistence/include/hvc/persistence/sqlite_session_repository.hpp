#pragma once

#include <cstdint>
#include <filesystem>
#include <hvc/application/control_plane.hpp>
#include <memory>
#include <stdexcept>

namespace hvc::persistence
{
class PersistenceError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

class SqliteSessionRepository final : public application::IMutableSessionRepository
{
  public:
    static constexpr std::uint32_t latest_schema_version{1};

    explicit SqliteSessionRepository(std::filesystem::path database_path);
    ~SqliteSessionRepository() override;

    SqliteSessionRepository(const SqliteSessionRepository&) = delete;
    auto operator=(const SqliteSessionRepository&) -> SqliteSessionRepository& = delete;
    SqliteSessionRepository(SqliteSessionRepository&&) = delete;
    auto operator=(SqliteSessionRepository&&) -> SqliteSessionRepository& = delete;

    [[nodiscard]] auto find(const domain::SessionId& session_id) const
        -> std::optional<application::AuthenticatedSession> override;
    void upsert(application::AuthenticatedSession session) override;
    [[nodiscard]] auto erase(const domain::SessionId& session_id) -> bool override;

    [[nodiscard]] auto schemaVersion() const -> std::uint32_t;

  private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};
} // namespace hvc::persistence
