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
#include <hvc/livekit/livekit_token.hpp>
#include <hvc/network/control_plane_http.hpp>
#include <hvc/persistence/sqlite_control_plane_repository.hpp>
#include <iterator>
#include <mutex>
#include <optional>
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
namespace livekit = hvc::livekit;
namespace network = hvc::network;
namespace persistence = hvc::persistence;

struct Options final
{
    std::filesystem::path database_path{"hvc-control-plane.db"};
    std::string listen_address;
    std::uint16_t port{8080};
    std::filesystem::path bootstrap_token_file;
    std::string bootstrap_player;
    std::string livekit_url;
    std::string livekit_api_key;
    std::filesystem::path livekit_api_secret_file;
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
        else if (argument == "--livekit-url")
        {
            options.livekit_url = value;
        }
        else if (argument == "--livekit-api-key")
        {
            options.livekit_api_key = value;
        }
        else if (argument == "--livekit-api-secret-file")
        {
            options.livekit_api_secret_file = value;
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
    std::puts("           [--livekit-url <ws-url> --livekit-api-key <key>");
    std::puts("            --livekit-api-secret-file <path>]");
#endif
}

[[nodiscard]] auto readCredentialFile(const std::filesystem::path& path,
                                      std::string_view description) -> std::string
{
    std::ifstream stream{path, std::ios::binary};
    if (!stream)
    {
        throw std::runtime_error{"Cannot open the " + std::string{description} + " file."};
    }
    std::string credential{std::istreambuf_iterator<char>{stream},
                           std::istreambuf_iterator<char>{}};
    while (!credential.empty() && (credential.back() == '\r' || credential.back() == '\n'))
    {
        credential.pop_back();
    }
    if (credential.empty())
    {
        throw std::runtime_error{"The " + std::string{description} + " file is empty."};
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

class DenyMembershipAdministration final : public application::IAdministrativeMembershipAuthorizer
{
  public:
    [[nodiscard]] auto canRead(const domain::PlayerId&, const domain::PlayerId&) const
        -> bool override
    {
        return false;
    }

    [[nodiscard]] auto canRemove(const domain::PlayerId&, const domain::PlayerId&) const
        -> bool override
    {
        return false;
    }

    [[nodiscard]] auto canReplace(const domain::PlayerId&, const domain::PlayerId&) const
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
            if (!options.bootstrap_token_file.empty() || !options.bootstrap_player.empty() ||
                !options.livekit_url.empty() || !options.livekit_api_key.empty() ||
                !options.livekit_api_secret_file.empty())
            {
                throw std::invalid_argument{"Authentication and LiveKit options require --listen."};
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
        BootstrapIdentityProvider identities{
            readCredentialFile(options.bootstrap_token_file, "bootstrap token"),
            domain::PlayerId{options.bootstrap_player}};
        application::IdentitySessionAuthenticator authenticator{
            identities, identifiers, application::IdentitySessionPolicy{std::chrono::minutes{15}}};
        application::InMemoryControlPlaneStore runtime_store{repository, repository, &repository};
        application::InMemoryTransmissionRateLimiter rate_limiter{
            application::TransmissionRateLimit{10, std::chrono::seconds{1}},
            application::TransmissionRateLimit{20, std::chrono::seconds{1}}};
        DenyModeration moderation;
        DenyMembershipAdministration membership_administration;
        application::TransmissionApplicationService transmissions{
            runtime_store,
            runtime_store,
            identifiers,
            runtime_store,
            rate_limiter,
            moderation,
            application::TransmissionLifecyclePolicy{std::chrono::seconds{30}},
            &repository};
        const auto has_any_livekit_option = !options.livekit_url.empty() ||
                                            !options.livekit_api_key.empty() ||
                                            !options.livekit_api_secret_file.empty();
        const auto has_all_livekit_options = !options.livekit_url.empty() &&
                                             !options.livekit_api_key.empty() &&
                                             !options.livekit_api_secret_file.empty();
        if (has_any_livekit_option && !has_all_livekit_options)
        {
            throw std::invalid_argument{
                "LiveKit grant issuance requires URL, API key and API secret file."};
        }

        std::optional<application::VoiceGrantAuthorizationService> voice_grants;
        std::optional<livekit::LiveKitTokenAdapter> voice_grant_issuer;
        if (has_all_livekit_options)
        {
            voice_grants.emplace(repository, runtime_store,
                                 application::VoiceGrantPolicy{std::chrono::seconds{30}});
            voice_grant_issuer.emplace(livekit::LiveKitCredentials{
                options.livekit_api_key,
                readCredentialFile(options.livekit_api_secret_file, "LiveKit API secret")});
        }

        network::ControlPlaneHttpAdapter api{authenticator,
                                             repository,
                                             runtime_store,
                                             transmissions,
                                             &runtime_store,
                                             &membership_administration,
                                             voice_grants ? &*voice_grants : nullptr,
                                             voice_grant_issuer ? &*voice_grant_issuer : nullptr,
                                             options.livekit_url};
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
