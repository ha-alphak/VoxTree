#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <hvc/application/control_plane.hpp>
#include <hvc/domain/model.hpp>
#include <hvc/persistence/sqlite_control_plane_repository.hpp>
#include <memory>
#include <mutex>
#include <netdb.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{
namespace application = hvc::application;
namespace domain = hvc::domain;
namespace persistence = hvc::persistence;

using Clock = std::chrono::steady_clock;

constexpr std::size_t default_group_players = 200;
constexpr std::size_t probe_players = 2;
constexpr std::size_t players_per_team = 5;
constexpr std::size_t teams_per_specialization = 10;
constexpr std::size_t default_concurrency = 32;
constexpr std::size_t default_membership_iterations = 5;
constexpr std::chrono::milliseconds setup_latency_target{300};
constexpr std::chrono::milliseconds membership_latency_target{1'000};

struct Options final
{
    std::string command;
    std::filesystem::path database_path;
    std::filesystem::path identities_path;
    std::filesystem::path report_path;
    std::string host{"127.0.0.1"};
    std::string credential_prefix{"hvc-load-token-"};
    std::uint16_t port{8080};
    std::size_t group_players{default_group_players};
    std::size_t concurrency{default_concurrency};
    std::size_t membership_iterations{default_membership_iterations};
    std::chrono::seconds duration{30};
    bool require_recovery{false};
    bool skip_timeout{false};
    bool show_help{false};
};

struct HttpResponse final
{
    int status{};
    std::string body;
    std::chrono::microseconds elapsed{};
};

struct PlayerSession final
{
    std::string player_id;
    std::string device_id;
    std::string credential;
    std::string session_id;
};

struct LatencySummary final
{
    std::size_t samples{};
    double minimum_ms{};
    double p50_ms{};
    double p95_ms{};
    double p99_ms{};
    double maximum_ms{};
};

struct RunReport final
{
    std::size_t group_players{};
    std::size_t total_virtual_players{};
    std::size_t requests{};
    std::size_t unexpected_errors{};
    std::size_t incorrect_recipients{};
    LatencySummary membership_latency;
    LatencySummary authorization_latency;
    double membership_propagation_ms{};
    double timeout_revocation_ms{};
    bool security_checks_passed{false};
    bool speaker_limit_passed{false};
    bool independent_scopes_passed{false};
    bool moderation_passed{false};
    bool membership_change_passed{false};
    bool timeout_passed{false};
};

struct SoakReport final
{
    std::size_t total_virtual_players{};
    std::size_t requests{};
    std::size_t successful_requests{};
    std::size_t failed_requests{};
    bool interruption_observed{false};
    bool recovered_after_interruption{false};
    LatencySummary successful_latency;
};

class Socket final
{
  public:
    explicit Socket(int descriptor) : descriptor_(descriptor)
    {
    }

    ~Socket()
    {
        if (descriptor_ >= 0)
        {
            static_cast<void>(::close(descriptor_));
        }
    }

    Socket(const Socket&) = delete;
    auto operator=(const Socket&) -> Socket& = delete;

    Socket(Socket&& other) noexcept : descriptor_(std::exchange(other.descriptor_, -1))
    {
    }

    auto operator=(Socket&& other) noexcept -> Socket&
    {
        if (this != &other)
        {
            if (descriptor_ >= 0)
            {
                static_cast<void>(::close(descriptor_));
            }
            descriptor_ = std::exchange(other.descriptor_, -1);
        }
        return *this;
    }

    [[nodiscard]] auto get() const noexcept -> int
    {
        return descriptor_;
    }

  private:
    int descriptor_;
};

[[nodiscard]] auto parsePositiveSize(std::string_view value, std::string_view name) -> std::size_t
{
    std::size_t parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed == 0)
    {
        throw std::invalid_argument{std::string{name} + " must be a positive integer."};
    }
    return parsed;
}

[[nodiscard]] auto parsePort(std::string_view value) -> std::uint16_t
{
    const auto parsed = parsePositiveSize(value, "port");
    if (parsed > 65'535)
    {
        throw std::invalid_argument{"port must not exceed 65535."};
    }
    return static_cast<std::uint16_t>(parsed);
}

[[nodiscard]] auto nextValue(int& index, int argument_count, char** arguments,
                             std::string_view option) -> std::string_view
{
    if (index + 1 >= argument_count)
    {
        throw std::invalid_argument{"A value is missing after " + std::string{option} + '.'};
    }
    return arguments[++index];
}

[[nodiscard]] auto parseOptions(int argument_count, char** arguments) -> Options
{
    Options options;
    if (argument_count < 2)
    {
        options.show_help = true;
        return options;
    }
    options.command = arguments[1];
    if (options.command == "--help" || options.command == "-h")
    {
        options.show_help = true;
        return options;
    }

    for (int index = 2; index < argument_count; ++index)
    {
        const std::string_view option{arguments[index]};
        if (option == "--database")
        {
            options.database_path = nextValue(index, argument_count, arguments, option);
        }
        else if (option == "--identities")
        {
            options.identities_path = nextValue(index, argument_count, arguments, option);
        }
        else if (option == "--report")
        {
            options.report_path = nextValue(index, argument_count, arguments, option);
        }
        else if (option == "--host")
        {
            options.host = nextValue(index, argument_count, arguments, option);
        }
        else if (option == "--port")
        {
            options.port = parsePort(nextValue(index, argument_count, arguments, option));
        }
        else if (option == "--group-players")
        {
            options.group_players = parsePositiveSize(
                nextValue(index, argument_count, arguments, option), "group player count");
        }
        else if (option == "--concurrency")
        {
            options.concurrency = parsePositiveSize(
                nextValue(index, argument_count, arguments, option), "concurrency");
        }
        else if (option == "--membership-iterations")
        {
            options.membership_iterations = parsePositiveSize(
                nextValue(index, argument_count, arguments, option), "membership iteration count");
        }
        else if (option == "--duration")
        {
            options.duration = std::chrono::seconds{
                parsePositiveSize(nextValue(index, argument_count, arguments, option), "duration")};
        }
        else if (option == "--credential-prefix")
        {
            options.credential_prefix = nextValue(index, argument_count, arguments, option);
        }
        else if (option == "--require-recovery")
        {
            options.require_recovery = true;
        }
        else if (option == "--skip-timeout")
        {
            options.skip_timeout = true;
        }
        else
        {
            throw std::invalid_argument{"Unknown option: " + std::string{option}};
        }
    }
    if (options.group_players < default_group_players)
    {
        throw std::invalid_argument{"--group-players must be at least 200."};
    }
    if (options.command == "prepare" &&
        (options.database_path.empty() || options.identities_path.empty()))
    {
        throw std::invalid_argument{"prepare requires --database and --identities."};
    }
    if (options.command != "prepare" && options.command != "run" && options.command != "soak")
    {
        throw std::invalid_argument{"Command must be prepare, run, or soak."};
    }
    return options;
}

void printUsage()
{
    std::puts("Usage:\n"
              "  hvc-load-driver prepare --database <path> --identities <path>\n"
              "      [--group-players <count>] [--credential-prefix <prefix>]\n"
              "  hvc-load-driver run [--host <IPv4-or-DNS>] [--port <port>]\n"
              "      [--group-players <count>] [--concurrency <count>]\n"
              "      [--membership-iterations <count>] [--skip-timeout] [--report <path>]\n"
              "  hvc-load-driver soak [--host <IPv4-or-DNS>] [--port <port>]\n"
              "      [--group-players <count>] [--concurrency <count>] [--duration <seconds>]\n"
              "      [--require-recovery] [--report <path>]\n");
}

[[nodiscard]] auto paddedNumber(std::size_t value) -> std::string
{
    auto text = std::to_string(value);
    if (text.size() < 4)
    {
        text.insert(text.begin(), 4 - text.size(), '0');
    }
    return text;
}

[[nodiscard]] auto playerId(std::size_t index) -> std::string
{
    return "load-player-" + paddedNumber(index);
}

[[nodiscard]] auto deviceId(std::size_t index) -> std::string
{
    return "load-device-" + paddedNumber(index);
}

[[nodiscard]] auto teamId(std::size_t index) -> std::string
{
    return "load-team-" + paddedNumber(index);
}

[[nodiscard]] auto specializationId(std::size_t index) -> std::string
{
    return "load-specialization-" + paddedNumber(index);
}

[[nodiscard]] auto makeFixture(std::size_t group_players)
    -> application::AuthoritativeMembershipContext
{
    std::vector<domain::ScopeDefinition> scopes;
    scopes.emplace_back(domain::VoiceScope::team, "Team", 1, 1);
    scopes.emplace_back(domain::VoiceScope::specialization, "Specialization", 2, 2);
    scopes.emplace_back(domain::VoiceScope::group, "Group", 3, 2);

    std::vector<domain::Group> groups;
    groups.emplace_back(domain::GroupId{"load-group"}, "Load Group");
    groups.emplace_back(domain::GroupId{"isolated-group"}, "Isolated Group");

    const auto team_count = static_cast<std::size_t>(
        std::ceil(static_cast<double>(group_players) / static_cast<double>(players_per_team)));
    const auto specialization_count = static_cast<std::size_t>(
        std::ceil(static_cast<double>(team_count) / static_cast<double>(teams_per_specialization)));
    std::vector<domain::Specialization> specializations;
    specializations.reserve(specialization_count + 1);
    for (std::size_t index = 0; index < specialization_count; ++index)
    {
        specializations.emplace_back(domain::SpecializationId{specializationId(index)},
                                     domain::GroupId{"load-group"},
                                     "Load Specialization " + std::to_string(index));
    }
    specializations.emplace_back(domain::SpecializationId{"isolated-specialization"},
                                 domain::GroupId{"isolated-group"}, "Isolated Specialization");

    std::vector<domain::Team> teams;
    teams.reserve(team_count + 1);
    for (std::size_t index = 0; index < team_count; ++index)
    {
        teams.emplace_back(
            domain::TeamId{teamId(index)},
            domain::SpecializationId{specializationId(index / teams_per_specialization)},
            "Load Team " + std::to_string(index));
    }
    teams.emplace_back(domain::TeamId{"isolated-team"},
                       domain::SpecializationId{"isolated-specialization"}, "Isolated Team");

    std::vector<domain::VoiceMembership> memberships;
    memberships.reserve(group_players + probe_players);
    for (std::size_t index = 0; index < group_players; ++index)
    {
        const auto current_team = index / players_per_team;
        std::vector<domain::RoleId> roles{domain::RoleId{"speaker"}};
        if (index == 0)
        {
            roles.emplace_back("administrator");
            roles.emplace_back("moderator");
        }
        if (index + 1 == group_players)
        {
            roles = {domain::RoleId{"listener"}};
        }
        memberships.emplace_back(
            domain::PlayerId{playerId(index)}, domain::GroupId{"load-group"},
            domain::SpecializationId{specializationId(current_team / teams_per_specialization)},
            domain::TeamId{teamId(current_team)}, std::move(roles));
    }
    memberships.emplace_back(
        domain::PlayerId{playerId(group_players)}, domain::GroupId{"isolated-group"},
        domain::SpecializationId{"isolated-specialization"}, domain::TeamId{"isolated-team"},
        std::vector<domain::RoleId>{domain::RoleId{"sender-only"}});
    memberships.emplace_back(
        domain::PlayerId{playerId(group_players + 1)}, domain::GroupId{"isolated-group"},
        domain::SpecializationId{"isolated-specialization"}, domain::TeamId{"isolated-team"},
        std::vector<domain::RoleId>{domain::RoleId{"speaker"}});

    const std::vector all_scopes{domain::VoiceScope::team, domain::VoiceScope::specialization,
                                 domain::VoiceScope::group};
    std::vector<domain::RolePermissions> permissions;
    permissions.emplace_back(domain::RoleId{"speaker"}, all_scopes, all_scopes);
    permissions.emplace_back(domain::RoleId{"listener"}, std::vector<domain::VoiceScope>{},
                             all_scopes);
    permissions.emplace_back(domain::RoleId{"sender-only"}, all_scopes,
                             std::vector<domain::VoiceScope>{});
    permissions.emplace_back(domain::RoleId{"administrator"}, std::vector<domain::VoiceScope>{},
                             std::vector<domain::VoiceScope>{});
    permissions.emplace_back(domain::RoleId{"moderator"}, std::vector<domain::VoiceScope>{},
                             std::vector<domain::VoiceScope>{});

    domain::Hierarchy hierarchy{domain::HierarchyId{"load-hierarchy"}, std::move(scopes),
                                std::move(groups), std::move(specializations), std::move(teams)};
    return {std::make_shared<const domain::MembershipSnapshot>(1, std::move(hierarchy),
                                                               std::move(memberships)),
            std::make_shared<const domain::RolePolicy>(std::move(permissions))};
}

void prepareFixture(const Options& options)
{
    if (options.database_path.has_parent_path())
    {
        std::filesystem::create_directories(options.database_path.parent_path());
    }
    if (options.identities_path.has_parent_path())
    {
        std::filesystem::create_directories(options.identities_path.parent_path());
    }
    if (std::filesystem::exists(options.database_path))
    {
        throw std::runtime_error{"The fixture database already exists: " +
                                 options.database_path.string()};
    }

    const auto context = makeFixture(options.group_players);
    persistence::SqliteControlPlaneRepository repository{options.database_path};
    const auto total_players = options.group_players + probe_players;
    for (std::size_t index = 0; index < total_players; ++index)
    {
        const auto error = repository.upsertIfNewer(domain::PlayerId{playerId(index)}, context);
        if (error.has_value())
        {
            throw std::runtime_error{"Could not persist the load membership for " +
                                     playerId(index) + '.'};
        }
    }

    std::ofstream identities{options.identities_path, std::ios::binary | std::ios::trunc};
    if (!identities)
    {
        throw std::runtime_error{"Could not create the load identity file."};
    }
    identities << "# generated section 10.2 load identities\n";
    for (std::size_t index = 0; index < total_players; ++index)
    {
        identities << options.credential_prefix << paddedNumber(index) << '\t' << playerId(index)
                   << '\t' << deviceId(index) << '\n';
    }
    identities.close();
    if (!identities)
    {
        throw std::runtime_error{"Could not finish the load identity file."};
    }
    std::printf("Prepared %zu group players and %zu isolated security probes in %s.\n",
                options.group_players, probe_players, options.database_path.string().c_str());
}

[[nodiscard]] auto connectSocket(std::string_view host, std::uint16_t port) -> Socket
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses{};
    const auto port_text = std::to_string(port);
    const auto lookup =
        ::getaddrinfo(std::string{host}.c_str(), port_text.c_str(), &hints, &addresses);
    if (lookup != 0)
    {
        throw std::runtime_error{"DNS lookup failed: " + std::string{::gai_strerror(lookup)}};
    }
    const std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> address_owner{addresses,
                                                                             &::freeaddrinfo};
    for (auto* address = addresses; address != nullptr; address = address->ai_next)
    {
        const auto descriptor =
            ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (descriptor < 0)
        {
            continue;
        }
        Socket candidate{descriptor};
        timeval timeout{};
        timeout.tv_sec = 5;
        static_cast<void>(
            ::setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
        static_cast<void>(
            ::setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)));
        if (::connect(descriptor, address->ai_addr, address->ai_addrlen) == 0)
        {
            return candidate;
        }
    }
    throw std::runtime_error{"Could not connect to " + std::string{host} + ':' + port_text + '.'};
}

void sendAll(int socket, std::string_view data)
{
    std::size_t sent{};
    while (sent < data.size())
    {
        const auto result = ::send(socket, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (result <= 0)
        {
            throw std::runtime_error{"HTTP request send failed."};
        }
        sent += static_cast<std::size_t>(result);
    }
}

[[nodiscard]] auto receiveAll(int socket) -> std::string
{
    std::string response;
    std::array<char, 8'192> buffer{};
    while (true)
    {
        const auto received = ::recv(socket, buffer.data(), buffer.size(), 0);
        if (received == 0)
        {
            break;
        }
        if (received < 0)
        {
            throw std::runtime_error{"HTTP response receive failed."};
        }
        response.append(buffer.data(), static_cast<std::size_t>(received));
        if (response.size() > std::size_t{128} * std::size_t{1'024})
        {
            throw std::runtime_error{"HTTP response exceeded the load-driver limit."};
        }
    }
    return response;
}

[[nodiscard]] auto request(const Options& options, std::string_view method, std::string_view target,
                           std::string_view authorization = {}, std::string_view device = {},
                           std::string_view body = {}) -> HttpResponse
{
    const auto started = Clock::now();
    auto socket = connectSocket(options.host, options.port);
    std::string wire;
    wire.reserve(512 + body.size());
    wire += method;
    wire += ' ';
    wire += target;
    wire += " HTTP/1.1\r\nHost: ";
    wire += options.host;
    wire += "\r\nConnection: close\r\nX-Correlation-ID: load-";
    wire += std::to_string(started.time_since_epoch().count());
    wire += "\r\n";
    if (!authorization.empty())
    {
        wire += "Authorization: ";
        wire += authorization;
        wire += "\r\n";
    }
    if (!device.empty())
    {
        wire += "X-HVC-Device-ID: ";
        wire += device;
        wire += "\r\n";
    }
    if (!body.empty())
    {
        wire += "Content-Type: application/json; charset=utf-8\r\nContent-Length: ";
        wire += std::to_string(body.size());
        wire += "\r\n";
    }
    wire += "\r\n";
    wire += body;
    sendAll(socket.get(), wire);
    const auto raw = receiveAll(socket.get());
    const auto line_end = raw.find("\r\n");
    const auto body_start = raw.find("\r\n\r\n");
    if (line_end == std::string::npos || body_start == std::string::npos)
    {
        throw std::runtime_error{"Malformed HTTP response."};
    }
    const auto first_space = raw.find(' ');
    if (first_space == std::string::npos || first_space >= line_end)
    {
        throw std::runtime_error{"Malformed HTTP status line."};
    }
    int status{};
    const auto status_begin = raw.data() + first_space + 1;
    const auto status_end = status_begin + 3;
    const auto parsed = std::from_chars(status_begin, status_end, status);
    if (parsed.ec != std::errc{})
    {
        throw std::runtime_error{"Malformed HTTP status code."};
    }
    return {status, raw.substr(body_start + 4),
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started)};
}

[[nodiscard]] auto stringField(std::string_view body, std::string_view name) -> std::string
{
    const auto marker = '"' + std::string{name} + "\":\"";
    const auto begin = body.find(marker);
    if (begin == std::string_view::npos)
    {
        throw std::runtime_error{"Response has no string field " + std::string{name} + '.'};
    }
    const auto value_begin = begin + marker.size();
    const auto end = body.find('"', value_begin);
    if (end == std::string_view::npos)
    {
        throw std::runtime_error{"Response has a malformed string field " + std::string{name} +
                                 '.'};
    }
    return std::string{body.substr(value_begin, end - value_begin)};
}

[[nodiscard]] auto unsignedField(std::string_view body, std::string_view name) -> std::uint64_t
{
    const auto marker = '"' + std::string{name} + "\":";
    const auto begin = body.find(marker);
    if (begin == std::string_view::npos)
    {
        throw std::runtime_error{"Response has no numeric field " + std::string{name} + '.'};
    }
    const auto value_begin = body.data() + begin + marker.size();
    const auto value_end = body.data() + body.size();
    std::uint64_t value{};
    const auto parsed = std::from_chars(value_begin, value_end, value);
    if (parsed.ec != std::errc{})
    {
        throw std::runtime_error{"Response has a malformed numeric field " + std::string{name} +
                                 '.'};
    }
    return value;
}

[[nodiscard]] auto sessionAuthorization(const PlayerSession& session) -> std::string
{
    return "Session " + session.session_id;
}

template <typename Function>
void parallelFor(std::size_t count, std::size_t concurrency, Function function)
{
    std::atomic<std::size_t> next{};
    std::exception_ptr first_error;
    std::mutex error_mutex;
    const auto worker_count = std::min(count, concurrency);
    std::vector<std::jthread> workers;
    workers.reserve(worker_count);
    for (std::size_t worker = 0; worker < worker_count; ++worker)
    {
        workers.emplace_back([&] {
            while (true)
            {
                const auto index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= count)
                {
                    return;
                }
                try
                {
                    function(index);
                }
                catch (...)
                {
                    std::scoped_lock lock{error_mutex};
                    if (first_error == nullptr)
                    {
                        first_error = std::current_exception();
                    }
                    return;
                }
            }
        });
    }
    workers.clear();
    if (first_error != nullptr)
    {
        std::rethrow_exception(first_error);
    }
}

[[nodiscard]] auto makeSessions(const Options& options, std::atomic<std::size_t>& requests)
    -> std::vector<PlayerSession>
{
    const auto total_players = options.group_players + probe_players;
    std::vector<PlayerSession> sessions(total_players);
    parallelFor(total_players, options.concurrency, [&](std::size_t index) {
        PlayerSession session{
            playerId(index), deviceId(index), options.credential_prefix + paddedNumber(index), {}};
        const auto response = request(options, "POST", "/api/v1/sessions",
                                      "Bearer " + session.credential, session.device_id);
        requests.fetch_add(1, std::memory_order_relaxed);
        if (response.status != 201)
        {
            throw std::runtime_error{"Session creation failed for " + session.player_id +
                                     " with HTTP " + std::to_string(response.status) + ": " +
                                     response.body};
        }
        session.session_id = stringField(response.body, "session_id");
        if (stringField(response.body, "player_id") != session.player_id)
        {
            throw std::runtime_error{"Session response crossed player identities."};
        }
        sessions[index] = std::move(session);
    });
    return sessions;
}

[[nodiscard]] auto summarize(std::vector<std::chrono::microseconds> samples) -> LatencySummary
{
    if (samples.empty())
    {
        return {};
    }
    std::ranges::sort(samples);
    const auto percentile = [&](double quantile) {
        const auto index = static_cast<std::size_t>(
            std::ceil(quantile * static_cast<double>(samples.size())) - 1.0);
        return static_cast<double>(samples[std::min(index, samples.size() - 1)].count()) / 1'000.0;
    };
    return {samples.size(),   static_cast<double>(samples.front().count()) / 1'000.0,
            percentile(0.50), percentile(0.95),
            percentile(0.99), static_cast<double>(samples.back().count()) / 1'000.0};
}

void requireStatus(const HttpResponse& response, int expected, std::string_view scenario)
{
    if (response.status != expected)
    {
        throw std::runtime_error{std::string{scenario} + " returned HTTP " +
                                 std::to_string(response.status) + ": " + response.body};
    }
}

[[nodiscard]] auto startTransmission(const Options& options, const PlayerSession& session,
                                     std::string_view scope, std::uint64_t membership_version,
                                     std::string_view client_id) -> HttpResponse
{
    const auto body = "{\"client_transmission_id\":\"" + std::string{client_id} +
                      "\",\"scope\":\"" + std::string{scope} +
                      "\",\"membership_version\":" + std::to_string(membership_version) + '}';
    return request(options, "POST", "/api/v1/transmissions", sessionAuthorization(session),
                   session.device_id, body);
}

[[nodiscard]] auto endTransmission(const Options& options, const PlayerSession& session,
                                   std::string_view transmission_id) -> HttpResponse
{
    return request(options, "DELETE", "/api/v1/transmissions/" + std::string{transmission_id},
                   sessionAuthorization(session), session.device_id);
}

[[nodiscard]] auto reportJson(const RunReport& report) -> std::string
{
    const auto latency = [](const LatencySummary& value) {
        return "{\"samples\":" + std::to_string(value.samples) +
               ",\"min_ms\":" + std::to_string(value.minimum_ms) +
               ",\"p50_ms\":" + std::to_string(value.p50_ms) +
               ",\"p95_ms\":" + std::to_string(value.p95_ms) +
               ",\"p99_ms\":" + std::to_string(value.p99_ms) +
               ",\"max_ms\":" + std::to_string(value.maximum_ms) + '}';
    };
    return "{\"schema\":\"hvc-load-report-v1\",\"group_players\":" +
           std::to_string(report.group_players) +
           ",\"total_virtual_players\":" + std::to_string(report.total_virtual_players) +
           ",\"requests\":" + std::to_string(report.requests) +
           ",\"unexpected_errors\":" + std::to_string(report.unexpected_errors) +
           ",\"incorrect_recipients\":" + std::to_string(report.incorrect_recipients) +
           ",\"membership_latency\":" + latency(report.membership_latency) +
           ",\"authorization_latency\":" + latency(report.authorization_latency) +
           ",\"membership_propagation_ms\":" + std::to_string(report.membership_propagation_ms) +
           ",\"timeout_revocation_ms\":" + std::to_string(report.timeout_revocation_ms) +
           ",\"security_checks_passed\":" + (report.security_checks_passed ? "true" : "false") +
           ",\"speaker_limit_passed\":" + (report.speaker_limit_passed ? "true" : "false") +
           ",\"independent_scopes_passed\":" +
           (report.independent_scopes_passed ? "true" : "false") +
           ",\"moderation_passed\":" + (report.moderation_passed ? "true" : "false") +
           ",\"membership_change_passed\":" + (report.membership_change_passed ? "true" : "false") +
           ",\"timeout_passed\":" + (report.timeout_passed ? "true" : "false") + "}";
}

[[nodiscard]] auto reportJson(const SoakReport& report) -> std::string
{
    const auto& value = report.successful_latency;
    return "{\"schema\":\"hvc-soak-report-v1\",\"total_virtual_players\":" +
           std::to_string(report.total_virtual_players) +
           ",\"requests\":" + std::to_string(report.requests) +
           ",\"successful_requests\":" + std::to_string(report.successful_requests) +
           ",\"failed_requests\":" + std::to_string(report.failed_requests) +
           ",\"interruption_observed\":" + (report.interruption_observed ? "true" : "false") +
           ",\"recovered_after_interruption\":" +
           (report.recovered_after_interruption ? "true" : "false") +
           ",\"successful_latency\":{\"samples\":" + std::to_string(value.samples) +
           ",\"p50_ms\":" + std::to_string(value.p50_ms) +
           ",\"p95_ms\":" + std::to_string(value.p95_ms) +
           ",\"p99_ms\":" + std::to_string(value.p99_ms) +
           ",\"max_ms\":" + std::to_string(value.maximum_ms) + "}}";
}

void emitReport(std::string_view json, const std::filesystem::path& path)
{
    std::printf("%.*s\n", static_cast<int>(json.size()), json.data());
    if (path.empty())
    {
        return;
    }
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output << json << '\n';
    if (!output)
    {
        throw std::runtime_error{"Could not write report " + path.string() + '.'};
    }
}

[[nodiscard]] auto runLoad(const Options& options) -> RunReport
{
    RunReport report;
    report.group_players = options.group_players;
    report.total_virtual_players = options.group_players + probe_players;
    std::atomic<std::size_t> requests{};

    requireStatus(request(options, "GET", "/api/v1/health"), 200, "readiness");
    ++requests;
    auto sessions = makeSessions(options, requests);

    std::vector<std::chrono::microseconds> membership_samples(report.total_virtual_players *
                                                              options.membership_iterations);
    parallelFor(membership_samples.size(), options.concurrency, [&](std::size_t index) {
        const auto& session = sessions[index % sessions.size()];
        const auto response = request(options, "GET", "/api/v1/membership",
                                      sessionAuthorization(session), session.device_id);
        requests.fetch_add(1, std::memory_order_relaxed);
        requireStatus(response, 200, "membership load");
        if (stringField(response.body, "player_id") != session.player_id ||
            response.body.find("\"membership_version\":1") == std::string::npos)
        {
            throw std::runtime_error{"Membership response crossed an authorization boundary."};
        }
        membership_samples[index] = response.elapsed;
    });
    report.membership_latency = summarize(std::move(membership_samples));

    const auto invalid_identity = request(options, "POST", "/api/v1/sessions",
                                          "Bearer invalid-load-token", sessions.front().device_id);
    const auto device_mismatch = request(options, "GET", "/api/v1/membership",
                                         sessionAuthorization(sessions.front()), "foreign-device");
    const auto bearer_reuse =
        request(options, "GET", "/api/v1/membership", "Bearer " + sessions.front().credential,
                sessions.front().device_id);
    const auto manipulated =
        request(options, "POST", "/api/v1/transmissions", sessionAuthorization(sessions[1]),
                sessions[1].device_id,
                "{\"client_transmission_id\":\"malicious\",\"scope\":\"group\","
                "\"membership_version\":1,\"recipient_ids\":\"load-player-9999\"}");
    const auto unauthorized = startTransmission(options, sessions[options.group_players - 1],
                                                "group", 1, "listener-attack");
    requests += 5;
    report.security_checks_passed = invalid_identity.status == 401 &&
                                    device_mismatch.status == 403 && bearer_reuse.status == 401 &&
                                    manipulated.status == 400 && unauthorized.status == 403;
    if (!report.security_checks_passed)
    {
        throw std::runtime_error{"One or more manipulated-client checks failed."};
    }

    std::vector<std::chrono::microseconds> authorization_samples;
    authorization_samples.reserve(10);
    for (std::size_t index = 1; index <= 10; ++index)
    {
        const auto started = startTransmission(options, sessions[index], "group", 1,
                                               "group-load-" + std::to_string(index));
        ++requests;
        requireStatus(started, 201, "200-recipient group transmission");
        if (unsignedField(started.body, "recipient_count") != options.group_players)
        {
            ++report.incorrect_recipients;
            throw std::runtime_error{"Group transmission returned an incorrect recipient count."};
        }
        authorization_samples.push_back(started.elapsed);
        const auto ended =
            endTransmission(options, sessions[index], stringField(started.body, "transmission_id"));
        ++requests;
        requireStatus(ended, 200, "group transmission end");
    }
    report.authorization_latency = summarize(std::move(authorization_samples));
    if (report.authorization_latency.p95_ms >= static_cast<double>(setup_latency_target.count()))
    {
        throw std::runtime_error{"Transmission authorization p95 exceeded 300 ms."};
    }

    constexpr std::size_t independent_transmissions = 8;
    std::array<std::optional<std::string>, independent_transmissions> active_team_transmissions;
    parallelFor(independent_transmissions, independent_transmissions, [&](std::size_t index) {
        const auto player_index = 1 + (index * players_per_team);
        const auto started = startTransmission(options, sessions[player_index], "team", 1,
                                               "independent-team-" + std::to_string(index));
        requests.fetch_add(1, std::memory_order_relaxed);
        requireStatus(started, 201, "independent team transmission");
        if (unsignedField(started.body, "recipient_count") != players_per_team)
        {
            throw std::runtime_error{"Team transmission returned an incorrect recipient count."};
        }
        active_team_transmissions[index] = stringField(started.body, "transmission_id");
    });
    parallelFor(independent_transmissions, independent_transmissions, [&](std::size_t index) {
        const auto player_index = 1 + (index * players_per_team);
        const auto ended =
            endTransmission(options, sessions[player_index], *active_team_transmissions[index]);
        requests.fetch_add(1, std::memory_order_relaxed);
        requireStatus(ended, 200, "independent team transmission end");
    });
    report.independent_scopes_passed = true;

    const auto limited_first =
        startTransmission(options, sessions[1], "team", 1, "speaker-limit-first");
    ++requests;
    requireStatus(limited_first, 201, "speaker limit setup");
    const auto limited_second =
        startTransmission(options, sessions[2], "team", 1, "speaker-limit-second");
    ++requests;
    report.speaker_limit_passed =
        limited_second.status == 409 &&
        limited_second.body.find("speaker_limit_reached") != std::string::npos;
    requireStatus(
        endTransmission(options, sessions[1], stringField(limited_first.body, "transmission_id")),
        200, "speaker limit cleanup");
    ++requests;
    if (!report.speaker_limit_passed)
    {
        throw std::runtime_error{"The same-scope speaker limit was not enforced atomically."};
    }

    const auto moderated = startTransmission(options, sessions[6], "team", 1, "moderation-load");
    ++requests;
    requireStatus(moderated, 201, "moderation setup");
    const auto moderation_response = request(
        options, "POST",
        "/api/v1/transmissions/" + stringField(moderated.body, "transmission_id") + "/interrupt",
        sessionAuthorization(sessions[0]), sessions[0].device_id);
    ++requests;
    report.moderation_passed =
        moderation_response.status == 200 &&
        moderation_response.body.find("moderation_interrupted") != std::string::npos;
    if (!report.moderation_passed)
    {
        throw std::runtime_error{"Moderation did not interrupt the loaded transmission."};
    }

    const auto changing =
        startTransmission(options, sessions[11], "team", 1, "membership-change-load");
    ++requests;
    requireStatus(changing, 201, "membership-change setup");
    const auto propagation_started = Clock::now();
    const auto replacement_body =
        "{\"membership_version\":2,\"group_id\":\"load-group\","
        "\"specialization_id\":\"load-specialization-0000\","
        "\"team_id\":\"load-team-0002\",\"role_ids\":\"speaker\","
        "\"connected\":true,\"can_receive_voice\":true,\"transmit_muted\":false}";
    const auto replacement =
        request(options, "PUT", "/api/v1/admin/memberships/" + sessions[11].player_id,
                sessionAuthorization(sessions[0]), sessions[0].device_id, replacement_body);
    ++requests;
    requireStatus(replacement, 200, "membership replacement");
    bool propagated{};
    while (Clock::now() - propagation_started < membership_latency_target)
    {
        const auto membership = request(options, "GET", "/api/v1/membership",
                                        sessionAuthorization(sessions[11]), sessions[11].device_id);
        ++requests;
        if (membership.status == 200 &&
            membership.body.find("\"membership_version\":2") != std::string::npos)
        {
            propagated = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
    report.membership_propagation_ms =
        static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                                Clock::now() - propagation_started)
                                .count()) /
        1'000.0;
    const auto stale_end =
        endTransmission(options, sessions[11], stringField(changing.body, "transmission_id"));
    ++requests;
    report.membership_change_passed =
        propagated && stale_end.status == 404 &&
        report.membership_propagation_ms < static_cast<double>(membership_latency_target.count());
    if (!report.membership_change_passed)
    {
        throw std::runtime_error{
            "Membership propagation or atomic transmission revocation failed."};
    }

    report.timeout_passed = options.skip_timeout;
    if (!options.skip_timeout)
    {
        const auto timed = startTransmission(options, sessions[16], "team", 1, "timeout-load");
        ++requests;
        requireStatus(timed, 201, "timeout setup");
        const auto timeout_started = Clock::now();
        const auto timeout_deadline = timeout_started + std::chrono::seconds{32};
        while (Clock::now() < timeout_deadline)
        {
            const auto metrics = request(options, "GET", "/api/v1/metrics");
            ++requests;
            if (metrics.status == 200 &&
                metrics.body.find("hvc_active_transmissions 0") != std::string::npos)
            {
                report.timeout_passed = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
        report.timeout_revocation_ms =
            static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                                    Clock::now() - timeout_started)
                                    .count()) /
            1'000.0;
        if (!report.timeout_passed)
        {
            throw std::runtime_error{"Transmission timeout did not revoke the active state."};
        }
    }

    report.requests = requests.load(std::memory_order_relaxed);
    return report;
}

[[nodiscard]] auto runSoak(const Options& options) -> SoakReport
{
    std::atomic<std::size_t> setup_requests{};
    const auto sessions = makeSessions(options, setup_requests);
    std::atomic<std::size_t> requests{setup_requests.load(std::memory_order_relaxed)};
    std::atomic<std::size_t> successful{};
    std::atomic<std::size_t> failed{};
    std::atomic<bool> interruption{};
    std::atomic<bool> recovery{};
    std::mutex samples_mutex;
    std::vector<std::chrono::microseconds> samples;
    const auto deadline = Clock::now() + options.duration;

    std::vector<std::jthread> workers;
    workers.reserve(options.concurrency);
    for (std::size_t worker = 0; worker < options.concurrency; ++worker)
    {
        workers.emplace_back([&, worker] {
            std::size_t index = worker % sessions.size();
            while (Clock::now() < deadline)
            {
                try
                {
                    const auto& session = sessions[index];
                    const auto response = request(options, "GET", "/api/v1/membership",
                                                  sessionAuthorization(session), session.device_id);
                    requests.fetch_add(1, std::memory_order_relaxed);
                    if (response.status == 200)
                    {
                        successful.fetch_add(1, std::memory_order_relaxed);
                        if (interruption.load(std::memory_order_relaxed))
                        {
                            recovery.store(true, std::memory_order_relaxed);
                        }
                        std::scoped_lock lock{samples_mutex};
                        samples.push_back(response.elapsed);
                    }
                    else
                    {
                        failed.fetch_add(1, std::memory_order_relaxed);
                        interruption.store(true, std::memory_order_relaxed);
                    }
                }
                catch (const std::exception&)
                {
                    requests.fetch_add(1, std::memory_order_relaxed);
                    failed.fetch_add(1, std::memory_order_relaxed);
                    interruption.store(true, std::memory_order_relaxed);
                    std::this_thread::sleep_for(std::chrono::milliseconds{25});
                }
                index = (index + options.concurrency) % sessions.size();
            }
        });
    }
    workers.clear();

    SoakReport report;
    report.total_virtual_players = sessions.size();
    report.requests = requests.load(std::memory_order_relaxed);
    report.successful_requests = successful.load(std::memory_order_relaxed);
    report.failed_requests = failed.load(std::memory_order_relaxed);
    report.interruption_observed = interruption.load(std::memory_order_relaxed);
    report.recovered_after_interruption = recovery.load(std::memory_order_relaxed);
    report.successful_latency = summarize(std::move(samples));
    if (report.successful_requests == 0)
    {
        throw std::runtime_error{"Soak test completed without a successful request."};
    }
    if (options.require_recovery &&
        (!report.interruption_observed || !report.recovered_after_interruption))
    {
        throw std::runtime_error{
            "Soak test did not observe both the expected outage and subsequent recovery."};
    }
    return report;
}
} // namespace

auto main(int argument_count, char** arguments) noexcept -> int
{
    try
    {
        const auto options = parseOptions(argument_count, arguments);
        if (options.show_help)
        {
            printUsage();
            return 0;
        }
        if (options.command == "prepare")
        {
            prepareFixture(options);
            return 0;
        }
        if (options.command == "run")
        {
            const auto report = runLoad(options);
            emitReport(reportJson(report), options.report_path);
            return 0;
        }
        const auto report = runSoak(options);
        emitReport(reportJson(report), options.report_path);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "hvc-load-driver: %s\n", error.what());
        return 1;
    }
}
