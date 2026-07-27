#include <algorithm>
#include <array>
#include <atomic>
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
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(HVC_HAS_LINUX_HTTP_SERVER)
#include "linux_http_server.hpp"
#include "livekit_room_service_client.hpp"
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
    std::size_t http_workers{8};
    std::size_t http_queue_capacity{64};
    std::filesystem::path bootstrap_token_file;
    std::string bootstrap_player;
    std::filesystem::path identity_file;
    std::string livekit_url;
    std::string livekit_control_url;
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

[[nodiscard]] auto parsePositiveSize(std::string_view value, std::string_view description)
    -> std::size_t
{
    std::size_t parsed{};
    const auto conversion = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (conversion.ec != std::errc{} || conversion.ptr != value.data() + value.size() ||
        parsed == 0)
    {
        throw std::invalid_argument{std::string{description} + " must be a positive integer."};
    }
    return parsed;
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
        else if (argument == "--http-workers")
        {
            options.http_workers = parsePositiveSize(value, "HTTP worker count");
        }
        else if (argument == "--http-queue-capacity")
        {
            options.http_queue_capacity = parsePositiveSize(value, "HTTP queue capacity");
        }
        else if (argument == "--bootstrap-token-file")
        {
            options.bootstrap_token_file = value;
        }
        else if (argument == "--bootstrap-player")
        {
            options.bootstrap_player = value;
        }
        else if (argument == "--identity-file")
        {
            options.identity_file = value;
        }
        else if (argument == "--livekit-url")
        {
            options.livekit_url = value;
        }
        else if (argument == "--livekit-control-url")
        {
            options.livekit_control_url = value;
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
    std::puts("           [--http-workers <count> --http-queue-capacity <count>]");
    std::puts("           --bootstrap-token-file <path> --bootstrap-player <player-id>");
    std::puts("       or: --identity-file <tab-separated account file>");
    std::puts("           [--livekit-url <ws-url> --livekit-api-key <key>");
    std::puts("            --livekit-api-secret-file <path>]");
    std::puts("           [--livekit-control-url <internal-ws-url>]");
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

struct DeploymentIdentity final
{
    std::string credential;
    domain::PlayerId player_id;
    std::vector<std::string> allowed_devices;
};

class MultiUserIdentityProvider final : public application::IIdentityProvider
{
  public:
    explicit MultiUserIdentityProvider(const std::filesystem::path& path)
    {
        std::ifstream stream{path};
        if (!stream)
        {
            throw std::runtime_error{"Cannot open the identity file."};
        }
        std::string line;
        std::size_t line_number{};
        while (std::getline(stream, line))
        {
            ++line_number;
            if (line.empty() || line.front() == '#')
            {
                continue;
            }
            std::istringstream fields{line};
            std::string credential;
            std::string player;
            std::string devices;
            if (!std::getline(fields, credential, '\t') || !std::getline(fields, player, '\t') ||
                !std::getline(fields, devices) || credential.empty() || player.empty() ||
                devices.empty())
            {
                throw std::runtime_error{"Invalid identity file row " +
                                         std::to_string(line_number) + '.'};
            }
            if (!devices.empty() && devices.back() == '\r')
            {
                devices.pop_back();
            }
            if (devices.empty())
            {
                throw std::runtime_error{"Identity row has no allowed device."};
            }
            if (std::ranges::any_of(identities_, [&](const DeploymentIdentity& identity) {
                    return constantTimeEqual(credential, identity.credential) ||
                           identity.player_id.value() == player;
                }))
            {
                throw std::runtime_error{"Duplicate credential or player in identity row " +
                                         std::to_string(line_number) + '.'};
            }
            std::vector<std::string> allowed_devices;
            std::istringstream device_fields{devices};
            for (std::string device; std::getline(device_fields, device, ',');)
            {
                if (!device.empty())
                {
                    allowed_devices.push_back(std::move(device));
                }
            }
            if (allowed_devices.empty())
            {
                throw std::runtime_error{"Identity row has no allowed device."};
            }
            identities_.push_back(
                {std::move(credential), domain::PlayerId{player}, std::move(allowed_devices)});
        }
        if (identities_.size() < 2)
        {
            throw std::runtime_error{
                "Production identity mode requires at least two configured accounts."};
        }
    }

    [[nodiscard]] auto verify(std::string_view credential, const domain::DeviceId& device_id,
                              application::TimePoint)
        -> application::IdentityVerificationResult override
    {
        const DeploymentIdentity* selected{};
        for (const auto& identity : identities_)
        {
            if (constantTimeEqual(credential, identity.credential))
            {
                selected = &identity;
            }
        }
        if (selected == nullptr)
        {
            return application::IdentityVerificationResult::rejected(
                application::SessionAuthenticationError::invalid_credentials);
        }
        const auto device_allowed =
            std::ranges::find(selected->allowed_devices, "*") != selected->allowed_devices.end() ||
            std::ranges::find(selected->allowed_devices, device_id.value()) !=
                selected->allowed_devices.end();
        if (!device_allowed)
        {
            return application::IdentityVerificationResult::rejected(
                application::SessionAuthenticationError::device_not_allowed);
        }
        return application::IdentityVerificationResult::verified(
            {selected->player_id, std::chrono::minutes{15}});
    }

  private:
    std::vector<DeploymentIdentity> identities_;
};

class MembershipRoleAuthorizer final : public application::ITransmissionModerationAuthorizer,
                                       public application::IAdministrativeMembershipAuthorizer
{
  public:
    explicit MembershipRoleAuthorizer(
        const application::IAuthoritativeMembershipProvider& memberships)
        : memberships_(memberships)
    {
    }

    [[nodiscard]] auto canInterrupt(const domain::PlayerId& actor,
                                    const domain::TransmissionId&) const -> bool override
    {
        return hasRole(actor, "moderator") || hasRole(actor, "administrator");
    }

    [[nodiscard]] auto canRead(const domain::PlayerId& actor, const domain::PlayerId&) const
        -> bool override
    {
        return hasRole(actor, "administrator");
    }

    [[nodiscard]] auto canRemove(const domain::PlayerId& actor, const domain::PlayerId&) const
        -> bool override
    {
        return hasRole(actor, "administrator");
    }

    [[nodiscard]] auto canReplace(const domain::PlayerId& actor, const domain::PlayerId&) const
        -> bool override
    {
        return hasRole(actor, "administrator");
    }

  private:
    [[nodiscard]] auto hasRole(const domain::PlayerId& actor, std::string_view role) const -> bool
    {
        const auto context = memberships_.currentFor(actor);
        if (!context)
        {
            return false;
        }
        const auto* membership = context->snapshot->find(actor);
        return membership != nullptr &&
               std::ranges::any_of(membership->role_ids, [role](const domain::RoleId& role_id) {
                   return role_id.value() == role;
               });
    }

    const application::IAuthoritativeMembershipProvider& memberships_;
};

class MembershipTransportPresenceProvider final : public application::ITransportPresenceProvider
{
  public:
    explicit MembershipTransportPresenceProvider(
        const application::IAuthoritativeMembershipProvider& memberships)
        : memberships_(memberships)
    {
    }

    [[nodiscard]] auto connectedScopeCount(const domain::PlayerId& player_id) const
        -> std::size_t override
    {
        const auto context = memberships_.currentFor(player_id);
        if (!context)
        {
            return 0;
        }
        const auto* membership = context->snapshot->find(player_id);
        return membership != nullptr && membership->connected ? 1 : 0;
    }

  private:
    const application::IAuthoritativeMembershipProvider& memberships_;
};

class RuntimeHttpHandler final : public network::IHttpRequestHandler
{
  public:
    RuntimeHttpHandler(network::IHttpRequestHandler& api,
                       const application::InMemoryControlPlaneStore& runtime_store,
                       const persistence::SqliteControlPlaneRepository& repository,
                       const livekit::LiveKitPublicationController* publication_controller)
        : api_(api), runtime_store_(runtime_store), repository_(repository),
          publication_controller_(publication_controller)
    {
    }

    [[nodiscard]] auto handle(const network::HttpRequest& request, application::TimePoint now)
        -> network::HttpResponse override
    {
        request_count_.fetch_add(1, std::memory_order_relaxed);
        if (request.method == "GET" && request.target == "/api/v1/metrics")
        {
            std::string body;
            body += "hvc_http_requests_total " +
                    std::to_string(request_count_.load(std::memory_order_relaxed)) + '\n';
            body += "hvc_http_error_responses_total " +
                    std::to_string(error_count_.load(std::memory_order_relaxed)) + '\n';
            body +=
                "hvc_active_transmissions " + std::to_string(runtime_store_.activeCount()) + '\n';
            body += "hvc_livekit_operation_failures_total " +
                    std::to_string(publication_controller_ == nullptr
                                       ? 0
                                       : publication_controller_->failureCount()) +
                    '\n';
            body += "hvc_sqlite_dropped_audit_events_total " +
                    std::to_string(repository_.droppedAuditEventCount()) + '\n';
            return {200,
                    {{"cache-control", "no-store"},
                     {"content-type", "text/plain; version=0.0.4; charset=utf-8"},
                     {"x-hvc-api-version", "v1"}},
                    std::move(body)};
        }
        auto response = api_.handle(request, now);
        if (response.status_code >= 400)
        {
            error_count_.fetch_add(1, std::memory_order_relaxed);
        }
        return response;
    }

  private:
    network::IHttpRequestHandler& api_;
    const application::InMemoryControlPlaneStore& runtime_store_;
    const persistence::SqliteControlPlaneRepository& repository_;
    const livekit::LiveKitPublicationController* publication_controller_;
    std::atomic<std::uint64_t> request_count_{0};
    std::atomic<std::uint64_t> error_count_{0};
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
                !options.identity_file.empty() || !options.livekit_url.empty() ||
                !options.livekit_control_url.empty() || !options.livekit_api_key.empty() ||
                !options.livekit_api_secret_file.empty())
            {
                throw std::invalid_argument{"Authentication and LiveKit options require --listen."};
            }
            std::printf("hvc-control-plane: database schema %u ready\n",
                        repository.schemaVersion());
            return 0;
        }

#if defined(HVC_HAS_LINUX_HTTP_SERVER)
        const auto has_bootstrap =
            !options.bootstrap_token_file.empty() || !options.bootstrap_player.empty();
        if (has_bootstrap &&
            (options.bootstrap_token_file.empty() || options.bootstrap_player.empty()))
        {
            throw std::invalid_argument{
                "Bootstrap mode requires both --bootstrap-token-file and --bootstrap-player."};
        }
        if (has_bootstrap == !options.identity_file.empty())
        {
            throw std::invalid_argument{
                "--listen requires exactly one of --identity-file or bootstrap mode."};
        }

        RandomIdentifiers identifiers;
        std::unique_ptr<application::IIdentityProvider> identities;
        if (!options.identity_file.empty())
        {
            identities = std::make_unique<MultiUserIdentityProvider>(options.identity_file);
        }
        else
        {
            identities = std::make_unique<BootstrapIdentityProvider>(
                readCredentialFile(options.bootstrap_token_file, "bootstrap token"),
                domain::PlayerId{options.bootstrap_player});
            std::fputs("{\"level\":\"warning\",\"event\":\"bootstrap_identity_mode\","
                       "\"message\":\"single-user development authentication is enabled\"}\n",
                       stderr);
        }
        application::IdentitySessionAuthenticator authenticator{
            *identities, identifiers, application::IdentitySessionPolicy{std::chrono::minutes{15}}};

        const auto has_any_livekit_option =
            !options.livekit_url.empty() || !options.livekit_control_url.empty() ||
            !options.livekit_api_key.empty() || !options.livekit_api_secret_file.empty();
        const auto has_all_livekit_options = !options.livekit_url.empty() &&
                                             !options.livekit_api_key.empty() &&
                                             !options.livekit_api_secret_file.empty();
        if (has_any_livekit_option && !has_all_livekit_options)
        {
            throw std::invalid_argument{
                "LiveKit integration requires URL, API key and API secret file."};
        }
        std::optional<livekit::LiveKitCredentials> livekit_credentials;
        std::optional<hvc::control_plane::LiveKitRoomServiceClient> room_service;
        std::optional<livekit::LiveKitPublicationController> publication_controller;
        if (has_all_livekit_options)
        {
            livekit_credentials.emplace(
                options.livekit_api_key,
                readCredentialFile(options.livekit_api_secret_file, "LiveKit API secret"));
            room_service.emplace(options.livekit_control_url.empty() ? options.livekit_url
                                                                     : options.livekit_control_url);
            publication_controller.emplace(*livekit_credentials, *room_service);
        }

        application::InMemoryControlPlaneStore runtime_store{
            repository, repository, &repository,
            publication_controller ? &*publication_controller : nullptr};
        application::InMemoryTransmissionRateLimiter rate_limiter{
            application::TransmissionRateLimit{10, std::chrono::seconds{1}},
            application::TransmissionRateLimit{20, std::chrono::seconds{1}}};
        MembershipRoleAuthorizer deployment_authorization{runtime_store};
        MembershipTransportPresenceProvider transport_presence{runtime_store};
        application::DirectoryApplicationService directory{runtime_store, transport_presence};
        application::TransmissionApplicationService transmissions{
            runtime_store,
            runtime_store,
            identifiers,
            runtime_store,
            rate_limiter,
            deployment_authorization,
            application::TransmissionLifecyclePolicy{std::chrono::seconds{30}},
            &repository};
        std::optional<application::VoiceGrantAuthorizationService> voice_grants;
        std::optional<livekit::LiveKitTokenAdapter> voice_grant_issuer;
        if (has_all_livekit_options)
        {
            voice_grants.emplace(repository, runtime_store,
                                 application::VoiceGrantPolicy{std::chrono::seconds{30}});
            voice_grant_issuer.emplace(*livekit_credentials);
        }

        network::ControlPlaneHttpAdapter api{authenticator,
                                             repository,
                                             runtime_store,
                                             transmissions,
                                             &runtime_store,
                                             &deployment_authorization,
                                             voice_grants ? &*voice_grants : nullptr,
                                             voice_grant_issuer ? &*voice_grant_issuer : nullptr,
                                             options.livekit_url,
                                             &directory};
        RuntimeHttpHandler runtime_api{api, runtime_store, repository,
                                       publication_controller ? &*publication_controller : nullptr};
        std::jthread maintenance{[&](std::stop_token stop_token) {
            auto next_session_sweep = application::Clock::now();
            auto next_retention_sweep = application::Clock::now();
            while (!stop_token.stop_requested())
            {
                try
                {
                    const auto now = application::Clock::now();
                    static_cast<void>(transmissions.expireTimedOut(
                        now, domain::CorrelationId{"scheduler-timeout"}));
                    if (now >= next_session_sweep)
                    {
                        for (const auto& session_id : repository.expiredSessionIds(now, 256))
                        {
                            static_cast<void>(runtime_store.removeSession(
                                session_id, now,
                                domain::CorrelationId{"scheduler-session-cleanup"}));
                            static_cast<void>(repository.erase(session_id));
                        }
                        next_session_sweep = now + std::chrono::seconds{5};
                    }
                    if (now >= next_retention_sweep)
                    {
                        static_cast<void>(repository.eraseAuditEventsBefore(
                            now - std::chrono::hours{24 * 30}, 1'000));
                        next_retention_sweep = now + std::chrono::minutes{1};
                    }
                }
                catch (const std::exception& error)
                {
                    std::fprintf(stderr,
                                 "{\"level\":\"error\",\"event\":\"maintenance_failed\","
                                 "\"message\":\"%s\"}\n",
                                 error.what());
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{250});
            }
        }};
        const auto result = hvc::control_plane::runLinuxHttpServer(
            options.listen_address, options.port, runtime_api,
            {options.http_workers, options.http_queue_capacity});
        maintenance.request_stop();
        return result;
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
