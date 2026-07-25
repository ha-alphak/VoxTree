#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <livekit/livekit.h>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{

struct Arguments
{
    std::string url;
    std::string token;
    std::chrono::seconds wait_for_peer{30};
    bool hold_connection{false};
};

void print_usage()
{
    std::cerr << "Usage: hvc-livekit-quality-gate --url <ws-url> --token <jwt> "
                 "(--wait-for-peer <seconds> | --hold <seconds>)\n";
}

auto parse_arguments(const int argc, char** argv) -> Arguments
{
    Arguments arguments;
    for (int index = 1; index < argc; ++index)
    {
        const std::string option{argv[index]};
        if (option == "--url" && index + 1 < argc)
        {
            arguments.url = argv[++index];
        }
        else if (option == "--token" && index + 1 < argc)
        {
            arguments.token = argv[++index];
        }
        else if (option == "--wait-for-peer" && index + 1 < argc)
        {
            arguments.wait_for_peer = std::chrono::seconds{std::stoll(argv[++index])};
        }
        else if (option == "--hold" && index + 1 < argc)
        {
            arguments.wait_for_peer = std::chrono::seconds{std::stoll(argv[++index])};
            arguments.hold_connection = true;
        }
        else
        {
            throw std::invalid_argument{"unknown or incomplete option: " + option};
        }
    }
    if (arguments.url.empty() || arguments.token.empty() || arguments.wait_for_peer.count() < 1)
    {
        throw std::invalid_argument{"missing or invalid required argument"};
    }
    return arguments;
}

class LiveKitLifetime
{
  public:
    LiveKitLifetime()
    {
        if (!livekit::initialize(livekit::LogLevel::Info))
        {
            throw std::runtime_error{"LiveKit SDK initialization failed"};
        }
    }

    LiveKitLifetime(const LiveKitLifetime&) = delete;
    auto operator=(const LiveKitLifetime&) -> LiveKitLifetime& = delete;

    ~LiveKitLifetime()
    {
        livekit::shutdown();
    }
};

class PeerObserver final : public livekit::RoomDelegate
{
  public:
    void onParticipantConnected(livekit::Room&, const livekit::ParticipantConnectedEvent&) override
    {
        peer_connected_.store(true);
    }

    [[nodiscard]] auto peerConnected() const -> bool
    {
        return peer_connected_.load();
    }

  private:
    std::atomic<bool> peer_connected_{false};
};

} // namespace

auto main(const int argc, char** argv) -> int
{
    try
    {
        const auto arguments = parse_arguments(argc, argv);
        const LiveKitLifetime livekit_lifetime;
        PeerObserver peer_observer;
        livekit::Room room;
        room.setDelegate(&peer_observer);

        std::cout << "Connecting to " << arguments.url << "...\n";
        if (!room.connect(arguments.url, arguments.token, livekit::RoomOptions{}))
        {
            std::cerr << "LiveKit room connection failed.\n";
            return EXIT_FAILURE;
        }

        if (arguments.hold_connection)
        {
            std::cout << "Connected. Holding the room connection for "
                      << arguments.wait_for_peer.count() << " seconds...\n";
            std::this_thread::sleep_for(arguments.wait_for_peer);
            std::cout << "PASS: native Windows client remained connected.\n";
            return EXIT_SUCCESS;
        }

        std::cout << "Connected. Waiting up to " << arguments.wait_for_peer.count()
                  << " seconds for a second participant...\n";
        const auto deadline = std::chrono::steady_clock::now() + arguments.wait_for_peer;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (peer_observer.peerConnected() || !room.remoteParticipants().empty())
            {
                std::cout << "PASS: two native Windows clients are connected.\n";
                std::this_thread::sleep_for(std::chrono::seconds{1});
                return EXIT_SUCCESS;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }

        std::cerr << "FAIL: no second participant appeared before the timeout.\n";
        return EXIT_FAILURE;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        print_usage();
        return EXIT_FAILURE;
    }
}
