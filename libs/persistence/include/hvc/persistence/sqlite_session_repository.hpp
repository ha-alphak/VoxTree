#pragma once

#include <hvc/persistence/sqlite_control_plane_repository.hpp>

namespace hvc::persistence
{
/// Backward-compatible name for the unified SQLite control-plane repository.
using SqliteSessionRepository = SqliteControlPlaneRepository;
} // namespace hvc::persistence
