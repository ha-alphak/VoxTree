#include <cstdio>
#include <exception>
#include <filesystem>
#include <hvc/persistence/sqlite_session_repository.hpp>
#include <string_view>

auto main(int argument_count, char** arguments) -> int
{
    try
    {
        std::filesystem::path database_path{"hvc-control-plane.db"};
        if (argument_count == 2 && std::string_view{arguments[1]} == "--help")
        {
            std::puts("Usage: hvc-control-plane [--database <path>]");
            return 0;
        }
        if (argument_count == 3 && std::string_view{arguments[1]} == "--database")
        {
            database_path = arguments[2];
        }
        else if (argument_count != 1)
        {
            std::fputs("Invalid arguments. Use --help for usage.\n", stderr);
            return 2;
        }

        const hvc::persistence::SqliteSessionRepository sessions{database_path};
        std::printf("hvc-control-plane: database schema %u ready\n", sessions.schemaVersion());
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "hvc-control-plane: startup failed: %s\n", error.what());
        return 1;
    }
}
