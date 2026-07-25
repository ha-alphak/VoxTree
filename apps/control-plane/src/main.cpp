#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <hvc/application/identity.hpp>
#include <hvc/application/in_memory_control_plane.hpp>
#include <hvc/network/control_plane_http.hpp>
#include <hvc/persistence/sqlite_control_plane_repository.hpp>
#include <iterator>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(HVC_HAS_LINUX_HTTP_SERVER)
#include "linux_http_server.hpp"
#endif

namespace
{
namespace application = hvc::application;
namespace domain = hvc::domain;
namespace network = hvc::network;
namespace persistence = hvc::persistence;

struct Options final
{
    std::filesystem::path database_path{"hvc-control-plane.db"};
    std::string listen_address;
    std::uint16_t port{8080};
    std::filesystem::path bootstrap_token_file;
    std::string bootstrap_player;
    bool show_help{false};
};

[[nodiscard]] auto parsePort(std::string_view value) -> std::uint16_t
{
    unsigned int port{};
    const auto conversion = std::from_chars(value.data(), value.data() + value.size(), port);
    if (conversion.ec != std::errc{} || conversion.ptr != value.data() + value.size() ||
        port == 0 || port > 65'535U)
    {
        throw std::invalid_argument{"The port must be an integer from 1 through 65535."};
    }
    return static_cast<std::uint16_t>(port);
}

[[nodiscard]] auto parseOptions(int argument_count, char** arguments) -> Options
{
    Options options;
    for (int index = 1; index < argument_count; ++index)
    {
        const std::string_view argument{arguments[index]};
        if (argument == "--help")
        {
            options.show_help = true;
            continue;
        }
        if (index + 1 >= argument_count)
        {
            throw std::invalid_argument{"A value is missing after " + std::string{argument} + '.'};
        }
        const std::string_view value{arguments[++index]};
        if (argument == "--database")
        {
            options.database_path = value;
        }
        else if (argument == "--listen")
        {
            options.listen_address = value;
        }
        else if (argument == "--port")
        {
            options.port = parsePort(value);
        }
        else if (argument == "--bootstrap-token-file")
        {
            options.bootstrap_token_file = value;
        }
        else if (argument == "--bootstrap-player")
        {
            options.bootstrap_player = value;
        }
        else
        {
            throw std::invalid_argument{"Unknown argument: " + std::string{argument}};
        }
    }
    return options;
}

void printUsage()
{
    std::puts("Usage: hvc-control-plane [--database <path>]");
#if defined(HVC_HAS_LINUX_HTTP_SERVER)
    std::puts("       hvc-control-plane --listen <address> [--port <port>]");
    std::puts("           --bootstrap-token-file <path> --bootstrap-player <player-id>");
#endif
}

[[nodiscard]] auto readCredentialFile(const std::filesystem::path& path) -> std::string
{
    std::ifstream stream{path, std::ios::binary};
    if (!stream)
    {
        throw std::runtime_error{"Cannot open the bootstrap token file."};
    }
    std::string credential{std::istreambuf_iterator<char>{stream},
                           std::istreambuf_iterator<char>{}};
    while (!credential.empty() && (credential.back() == '\r' || credential.back() == '\n'))
    {
        credential.pop_back();
    }
    if (credential.empty())
    {
        throw std::runtime_error{"The bootstrap token file is empty."};
    }
    return credential;
}

[[nodiscard]] auto constantTimeEqual(std::string_view left, std::string_view right) noexcept -> bool
{
    const auto maximum_size = std::max(left.size(), right.size());
    std::size_t difference = left.size() ^ right.size();
    for (std::size_t index = 0; index < maximum_size; ++index)
    {
        const unsigned char left_character =
            index < left.size() ? static_cast<unsigned char>(left[index]) : 0U;
        const unsigned char right_character =
            index < right.size() ? static_cast<unsigned char>(right[index]) : 0U;
        difference |= static_cast<std::size_t>(left_character ^ right_character);
    }
    return difference == 0;
}

class RandomIdentifiers final : public application::ITransmissionIdGenerator,
                                public application::ISessionIdGenerator
{
  public:
    [[nodiscard]] auto next() -> domain::TransmissionId override
    {
        return domain::TransmissionId{generate("tx_")};
    }

    [[nodiscard]] auto nextSession() -> domain::SessionId override
    {
        return domain::SessionId{generate("ses_")};
    }

  private:
    [[nodiscard]] auto generate(std::string_view prefix) -> std::string
    {
        constexpr char hexadecimal[] = "0123456789abcdef";
        std::scoped_lock lock{mutex_};
        std::string identifier{prefix};
        identifier.reserve(prefix.size() + 32);
        for (int word_index = 0; word_index < 4; ++word_index)
        {
            const std::uint32_t word = static_cast<std::uint32_t>(random_device_());
            for (int shift = 28; shift >= 0; shift -= 4)
            {
                const auto digit = static_cast<std::size_t>((word >> shift) & 0x0FU);
                identifier.push_back(hexadecimal[digit]);
            }
        }
        return identifier;
    }

    std::mutex mutex_;
    std::random_device random_device_;
};

class BootstrapIdentityProvider final : public application::IIdentityProvider
{
  public:
    BootstrapIdentityProvider(std::string credential, domain::PlayerId player)
        : credential_(std::move(credential)), player_(std::move(player))
    {
    }

    [[nodiscard]] auto verify(std::string_view credential, const domain::DeviceId&,
                              application::TimePoint)
        -> application::IdentityVerificationResult override
    {
        if (!constantTimeEqual(credential, credential_))
        {
            return application::IdentityVerificationResult::rejected(
                application::SessionAuthenticationError::invalid_credentials);
        }
        return application::IdentityVerificationResult::verified(
            {player_, std::chrono::minutes{15}});
    }

  private:
    std::string credential_;
    domain::PlayerId player_;
};

class DenyModeration final : public application::ITransmissionModerationAuthorizer
{
  public:
    [[nodiscard]] auto canInterrupt(const domain::PlayerId&, const domain::TransmissionId&) const
        -> bool override
    {
        return false;
    }
};
} // namespace

auto main(int argument_count, char** arguments) -> int
{
    try
    {
        const auto options = parseOptions(argument_count, arguments);
        if (options.show_help)
        {
            printUsage();
            return 0;
        }

        persistence::SqliteControlPlaneRepository repository{options.database_path};
        if (options.listen_address.empty())
        {
            if (!options.bootstrap_token_file.empty() || !options.bootstrap_player.empty())
            {
                throw std::invalid_argument{"Bootstrap authentication options require --listen."};
            }
            std::printf("hvc-control-plane: database schema %u ready\n",
                        repository.schemaVersion());
            return 0;
        }

#if defined(HVC_HAS_LINUX_HTTP_SERVER)
        if (options.bootstrap_token_file.empty() || options.bootstrap_player.empty())
        {
            throw std::invalid_argument{
                "--listen requires --bootstrap-token-file and --bootstrap-player."};
        }

        RandomIdentifiers identifiers;
        BootstrapIdentityProvider identities{readCredentialFile(options.bootstrap_token_file),
                                             domain::PlayerId{options.bootstrap_player}};
        application::IdentitySessionAuthenticator authenticator{
            identities, identifiers, application::IdentitySessionPolicy{std::chrono::minutes{15}}};
        application::InMemoryControlPlaneStore runtime_store{repository, repository, &repository};
        application::InMemoryTransmissionRateLimiter rate_limiter{
            application::TransmissionRateLimit{10, std::chrono::seconds{1}},
            application::TransmissionRateLimit{20, std::chrono::seconds{1}}};
        DenyModeration moderation;
        application::TransmissionApplicationService transmissions{
            runtime_store,
            runtime_store,
            identifiers,
            runtime_store,
            rate_limiter,
            moderation,
            application::TransmissionLifecyclePolicy{std::chrono::seconds{30}},
            &repository};
        network::ControlPlaneHttpAdapter api{authenticator, repository, runtime_store,
                                             transmissions};
        return hvc::control_plane::runLinuxHttpServer(options.listen_address, options.port, api);
#else
        throw std::invalid_argument{"--listen is supported only by the Linux build."};
#endif
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "hvc-control-plane: startup failed: %s\n", error.what());
        return 1;
    }
}
