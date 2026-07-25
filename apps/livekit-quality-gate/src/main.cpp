#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <livekit/livekit.h>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{

constexpr std::uint64_t opus_bitrate = 64'000;

struct Arguments
{
    std::string url;
    std::string token;
    std::string team_token;
    std::string specialization_token;
    std::string group_token;
    std::optional<std::string> recording_device_id;
    std::optional<std::string> playout_device_id;
    std::optional<std::chrono::seconds> ptt_duration;
    std::chrono::seconds wait_for_peer{30};
    bool hold_connection{false};
    bool list_audio_devices{false};
    bool publish_audio{false};
    bool expect_audio{false};
    bool expect_ptt{false};
};

void print_usage()
{
    std::cerr << "Usage:\n"
              << "  hvc-livekit-quality-gate --list-audio-devices\n"
              << "  hvc-livekit-quality-gate --url <ws-url> --token <jwt>\n"
              << "    [--publish-audio | --ptt <seconds>]\n"
              << "    [--expect-audio | --expect-ptt]\n"
              << "    [--recording-device <id>] [--playout-device <id>]\n"
              << "    (--wait-for-peer <seconds> | --hold <seconds>)\n"
              << "  hvc-livekit-quality-gate --url <ws-url>\n"
              << "    --team-token <jwt> --specialization-token <jwt> --group-token <jwt>\n"
              << "    [--expect-audio] [--playout-device <id>]\n"
              << "    (--wait-for-peer <seconds> | --hold <seconds>)\n";
}

[[nodiscard]] auto has_any_scope_token(const Arguments& arguments) -> bool
{
    return !arguments.team_token.empty() || !arguments.specialization_token.empty() ||
           !arguments.group_token.empty();
}

[[nodiscard]] auto has_all_scope_tokens(const Arguments& arguments) -> bool
{
    return !arguments.team_token.empty() && !arguments.specialization_token.empty() &&
           !arguments.group_token.empty();
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
        else if (option == "--team-token" && index + 1 < argc)
        {
            arguments.team_token = argv[++index];
        }
        else if (option == "--specialization-token" && index + 1 < argc)
        {
            arguments.specialization_token = argv[++index];
        }
        else if (option == "--group-token" && index + 1 < argc)
        {
            arguments.group_token = argv[++index];
        }
        else if (option == "--recording-device" && index + 1 < argc)
        {
            arguments.recording_device_id = argv[++index];
        }
        else if (option == "--playout-device" && index + 1 < argc)
        {
            arguments.playout_device_id = argv[++index];
        }
        else if (option == "--ptt" && index + 1 < argc)
        {
            arguments.ptt_duration = std::chrono::seconds{std::stoll(argv[++index])};
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
        else if (option == "--list-audio-devices")
        {
            arguments.list_audio_devices = true;
        }
        else if (option == "--publish-audio")
        {
            arguments.publish_audio = true;
        }
        else if (option == "--expect-audio")
        {
            arguments.expect_audio = true;
        }
        else if (option == "--expect-ptt")
        {
            arguments.expect_ptt = true;
        }
        else
        {
            throw std::invalid_argument{"unknown or incomplete option: " + option};
        }
    }

    if (arguments.wait_for_peer.count() < 1)
    {
        throw std::invalid_argument{"probe duration must be at least one second"};
    }
    if (arguments.ptt_duration.has_value() && arguments.ptt_duration->count() < 1)
    {
        throw std::invalid_argument{"PTT duration must be at least one second"};
    }
    const auto has_scope_tokens = has_any_scope_token(arguments);
    if (!arguments.list_audio_devices && arguments.url.empty())
    {
        throw std::invalid_argument{"missing required URL"};
    }
    if (!arguments.list_audio_devices && arguments.token.empty() && !has_scope_tokens)
    {
        throw std::invalid_argument{"missing required token"};
    }
    if (has_scope_tokens && !has_all_scope_tokens(arguments))
    {
        throw std::invalid_argument{
            "the team, specialization, and group tokens must be provided together"};
    }
    if (!arguments.token.empty() && has_scope_tokens)
    {
        throw std::invalid_argument{
            "--token cannot be combined with the three scope-token options"};
    }
    if (has_scope_tokens && (arguments.publish_audio || arguments.ptt_duration.has_value()))
    {
        throw std::invalid_argument{
            "audio publication is not supported by the three-scope receiver probe"};
    }
    if (has_scope_tokens && arguments.expect_ptt)
    {
        throw std::invalid_argument{"--expect-ptt is only supported by the single-room probe"};
    }
    if (arguments.publish_audio && arguments.ptt_duration.has_value())
    {
        throw std::invalid_argument{"--publish-audio cannot be combined with --ptt"};
    }
    if (arguments.expect_audio && arguments.expect_ptt)
    {
        throw std::invalid_argument{"--expect-audio cannot be combined with --expect-ptt"};
    }
    if (arguments.ptt_duration.has_value() && arguments.hold_connection)
    {
        throw std::invalid_argument{"--ptt cannot be combined with --hold"};
    }
    if (arguments.recording_device_id.has_value() && !arguments.publish_audio &&
        !arguments.ptt_duration.has_value())
    {
        throw std::invalid_argument{"--recording-device requires --publish-audio or --ptt"};
    }
    if (arguments.playout_device_id.has_value() && !arguments.expect_audio && !arguments.expect_ptt)
    {
        throw std::invalid_argument{"--playout-device requires --expect-audio or --expect-ptt"};
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

class ProbeObserver final : public livekit::RoomDelegate
{
  public:
    void onParticipantConnected(livekit::Room&, const livekit::ParticipantConnectedEvent&) override
    {
        peer_connected_.store(true);
    }

    void onTrackSubscribed(livekit::Room&, const livekit::TrackSubscribedEvent& event) override
    {
        if (event.publication == nullptr ||
            event.publication->kind() != livekit::TrackKind::KIND_AUDIO)
        {
            return;
        }

        const std::scoped_lock lock{audio_mutex_};
        subscribed_audio_mime_ = event.publication->mimeType();
        subscribed_audio_source_ = event.publication->source();
        if (subscribed_audio_mime_ == "audio/opus" &&
            subscribed_audio_source_ == livekit::TrackSource::SOURCE_MICROPHONE)
        {
            received_opus_microphone_ = true;
            remote_audio_stopped_ = false;
        }
    }

    void onTrackUnsubscribed(livekit::Room&, const livekit::TrackUnsubscribedEvent& event) override
    {
        observe_audio_stop(event.publication);
    }

    void onTrackUnpublished(livekit::Room&, const livekit::TrackUnpublishedEvent& event) override
    {
        observe_audio_stop(event.publication);
    }

    [[nodiscard]] auto peerConnected() const -> bool
    {
        return peer_connected_.load();
    }

    [[nodiscard]] auto receivedOpusMicrophone() const -> bool
    {
        const std::scoped_lock lock{audio_mutex_};
        return received_opus_microphone_;
    }

    [[nodiscard]] auto receivedAndStoppedOpusMicrophone() const -> bool
    {
        const std::scoped_lock lock{audio_mutex_};
        return received_opus_microphone_ && remote_audio_stopped_;
    }

    [[nodiscard]] auto subscribedAudioDescription() const -> std::string
    {
        const std::scoped_lock lock{audio_mutex_};
        if (!subscribed_audio_mime_.has_value())
        {
            return "no remote audio track was subscribed";
        }
        return "remote audio MIME type was " + *subscribed_audio_mime_;
    }

  private:
    template <typename Publication>
    void observe_audio_stop(const std::shared_ptr<Publication>& publication)
    {
        if (publication == nullptr || publication->kind() != livekit::TrackKind::KIND_AUDIO)
        {
            return;
        }
        const std::scoped_lock lock{audio_mutex_};
        if (received_opus_microphone_ && publication->mimeType() == "audio/opus" &&
            publication->source() == livekit::TrackSource::SOURCE_MICROPHONE)
        {
            remote_audio_stopped_ = true;
        }
    }

    std::atomic<bool> peer_connected_{false};
    mutable std::mutex audio_mutex_;
    std::optional<std::string> subscribed_audio_mime_;
    std::optional<livekit::TrackSource> subscribed_audio_source_;
    bool received_opus_microphone_{false};
    bool remote_audio_stopped_{false};
};

void print_audio_devices(const livekit::PlatformAudio& platform_audio)
{
    const auto recording_devices = platform_audio.recordingDevices();
    const auto playout_devices = platform_audio.playoutDevices();

    std::cout << "Recording devices (" << recording_devices.size() << "):\n";
    for (const auto& device : recording_devices)
    {
        std::cout << "  [" << device.index << "] " << device.name << "\n"
                  << "      id: " << device.id << '\n';
    }

    std::cout << "Playout devices (" << playout_devices.size() << "):\n";
    for (const auto& device : playout_devices)
    {
        std::cout << "  [" << device.index << "] " << device.name << "\n"
                  << "      id: " << device.id << '\n';
    }
}

void configure_audio_devices(const Arguments& arguments,
                             const livekit::PlatformAudio& platform_audio)
{
    if (arguments.recording_device_id.has_value())
    {
        platform_audio.setRecordingDevice(*arguments.recording_device_id);
        std::cout << "Selected requested recording device.\n";
    }
    if (arguments.playout_device_id.has_value())
    {
        platform_audio.setPlayoutDevice(*arguments.playout_device_id);
        std::cout << "Selected requested playout device.\n";
    }
}

struct PublishedAudio
{
    std::shared_ptr<livekit::PlatformAudioSource> source;
    std::shared_ptr<livekit::LocalAudioTrack> track;
};

auto publish_microphone(livekit::Room& room, const livekit::PlatformAudio& platform_audio)
    -> PublishedAudio
{
    if (platform_audio.recordingDeviceCount() < 1)
    {
        throw std::runtime_error{"no microphone is available"};
    }

    livekit::PlatformAudioOptions audio_options;
    audio_options.echo_cancellation = true;
    audio_options.noise_suppression = true;
    audio_options.auto_gain_control = true;

    PublishedAudio published_audio;
    published_audio.source = platform_audio.createAudioSource(audio_options);
    published_audio.track = livekit::LocalAudioTrack::createLocalAudioTrack(
        "hvc-quality-gate-microphone", published_audio.source);

    const auto local_participant = room.localParticipant().lock();
    if (local_participant == nullptr)
    {
        throw std::runtime_error{"LiveKit local participant is unavailable"};
    }

    livekit::TrackPublishOptions publish_options;
    publish_options.source = livekit::TrackSource::SOURCE_MICROPHONE;
    publish_options.audio_encoding = livekit::AudioEncodingOptions{opus_bitrate};
    publish_options.dtx = true;
    publish_options.red = false;
    local_participant->publishTrack(published_audio.track, publish_options);

    std::cout << "Publishing the selected microphone as an Opus audio track at up to "
              << opus_bitrate << " bit/s.\n";
    return published_audio;
}

void stop_microphone(livekit::Room& room, const PublishedAudio& published_audio)
{
    const auto local_participant = room.localParticipant().lock();
    if (local_participant == nullptr)
    {
        throw std::runtime_error{"LiveKit local participant is unavailable during PTT release"};
    }
    const auto publication = published_audio.track->publication();
    if (publication == nullptr || publication->sid().empty())
    {
        throw std::runtime_error{"published microphone has no LiveKit publication SID"};
    }

    local_participant->unpublishTrack(publication->sid());
    if (!local_participant->trackPublications().empty())
    {
        throw std::runtime_error{"microphone publication remained after PTT release"};
    }
    if (room.connectionState() != livekit::ConnectionState::Connected)
    {
        throw std::runtime_error{"room disconnected while releasing PTT"};
    }
}

auto probe_succeeded(const Arguments& arguments, const ProbeObserver& observer) -> bool
{
    if (arguments.expect_ptt)
    {
        return observer.receivedAndStoppedOpusMicrophone();
    }
    if (arguments.expect_audio)
    {
        return observer.receivedOpusMicrophone();
    }
    return observer.peerConnected();
}

struct ScopeRoomProbe
{
    ScopeRoomProbe(std::string scope_name, std::string scope_token)
        : name(std::move(scope_name)), token(std::move(scope_token))
    {
        room.setDelegate(&observer);
    }

    std::string name;
    std::string token;
    ProbeObserver observer;
    livekit::Room room;
};

[[nodiscard]] auto scope_probe_succeeded(const Arguments& arguments, const ScopeRoomProbe& probe)
    -> bool
{
    const auto peer_present = !probe.room.remoteParticipants().empty();
    if (arguments.expect_audio)
    {
        return peer_present && probe.observer.receivedOpusMicrophone();
    }
    return peer_present;
}

[[nodiscard]] auto all_scope_probes_succeeded(
    const Arguments& arguments, const std::vector<std::unique_ptr<ScopeRoomProbe>>& probes) -> bool
{
    for (const auto& probe : probes)
    {
        if (!scope_probe_succeeded(arguments, *probe))
        {
            return false;
        }
    }
    return true;
}

void print_scope_probe_failures(const Arguments& arguments,
                                const std::vector<std::unique_ptr<ScopeRoomProbe>>& probes)
{
    for (const auto& probe : probes)
    {
        if (scope_probe_succeeded(arguments, *probe))
        {
            continue;
        }
        if (arguments.expect_audio)
        {
            std::cerr << "FAIL [" << probe->name << "]: ";
            if (probe->room.remoteParticipants().empty())
            {
                std::cerr << "no remote participant is currently connected.\n";
            }
            else
            {
                std::cerr << probe->observer.subscribedAudioDescription() << ".\n";
            }
        }
        else
        {
            std::cerr << "FAIL [" << probe->name
                      << "]: no second participant appeared before the timeout.\n";
        }
    }
}

auto run_three_scope_probe(const Arguments& arguments) -> int
{
    const std::array scope_tokens{
        std::pair<std::string_view, const std::string*>{"team", &arguments.team_token},
        std::pair<std::string_view, const std::string*>{"specialization",
                                                        &arguments.specialization_token},
        std::pair<std::string_view, const std::string*>{"group", &arguments.group_token}};
    std::vector<std::unique_ptr<ScopeRoomProbe>> probes;
    probes.reserve(scope_tokens.size());

    for (const auto& [name, token] : scope_tokens)
    {
        auto probe = std::make_unique<ScopeRoomProbe>(std::string{name}, *token);
        std::cout << "Connecting " << name << " scope to " << arguments.url << "...\n";
        if (!probe->room.connect(arguments.url, probe->token, livekit::RoomOptions{}))
        {
            std::cerr << "LiveKit " << name << " room connection failed.\n";
            return EXIT_FAILURE;
        }
        probes.push_back(std::move(probe));
    }

    std::cout << "All three scope rooms are connected in parallel. "
              << (arguments.hold_connection ? "Holding for " : "Waiting up to ")
              << arguments.wait_for_peer.count() << " seconds for "
              << (arguments.expect_audio ? "a remote Opus microphone track in every scope"
                                         : "a second participant in every scope")
              << "...\n";

    if (arguments.hold_connection)
    {
        std::this_thread::sleep_for(arguments.wait_for_peer);
    }
    else
    {
        const auto deadline = std::chrono::steady_clock::now() + arguments.wait_for_peer;
        while (std::chrono::steady_clock::now() < deadline &&
               !all_scope_probes_succeeded(arguments, probes))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
    }

    if (!all_scope_probes_succeeded(arguments, probes))
    {
        print_scope_probe_failures(arguments, probes);
        return EXIT_FAILURE;
    }

    if (arguments.expect_audio)
    {
        std::cout << "PASS: Team, specialization, and group rooms simultaneously received "
                     "remote Opus microphone tracks; platform playout active.\n";
    }
    else
    {
        std::cout << "PASS: Team, specialization, and group rooms simultaneously have remote "
                     "participants.\n";
    }
    std::this_thread::sleep_for(std::chrono::seconds{1});
    return EXIT_SUCCESS;
}

auto run_room_probe(const Arguments& arguments, livekit::PlatformAudio* platform_audio) -> int
{
    ProbeObserver observer;
    livekit::Room room;
    room.setDelegate(&observer);

    std::cout << "Connecting to " << arguments.url << "...\n";
    if (!room.connect(arguments.url, arguments.token, livekit::RoomOptions{}))
    {
        std::cerr << "LiveKit room connection failed.\n";
        return EXIT_FAILURE;
    }

    std::optional<PublishedAudio> published_audio;
    if (arguments.publish_audio || arguments.ptt_duration.has_value())
    {
        if (platform_audio == nullptr)
        {
            throw std::logic_error{"platform audio was not initialized"};
        }
        published_audio = publish_microphone(room, *platform_audio);
    }

    if (arguments.ptt_duration.has_value())
    {
        std::cout << "PTT pressed. Publishing for " << arguments.ptt_duration->count()
                  << " seconds...\n";
        std::this_thread::sleep_for(*arguments.ptt_duration);
        stop_microphone(room, *published_audio);
        published_audio.reset();
        std::cout << "PTT released. Microphone track unpublished; room remains connected.\n";
        std::this_thread::sleep_for(std::chrono::seconds{2});
        if (room.connectionState() != livekit::ConnectionState::Connected)
        {
            std::cerr << "FAIL: room disconnected after PTT release.\n";
            return EXIT_FAILURE;
        }
        std::cout << "PASS: PTT publication started and stopped cleanly without disconnecting "
                     "the room.\n";
        return EXIT_SUCCESS;
    }

    if (arguments.hold_connection)
    {
        std::cout << "Connected. Holding the room connection for "
                  << arguments.wait_for_peer.count() << " seconds...\n";
        std::this_thread::sleep_for(arguments.wait_for_peer);

        if (arguments.expect_ptt && !observer.receivedAndStoppedOpusMicrophone())
        {
            std::cerr << "FAIL: remote Opus microphone did not start and stop cleanly.\n";
            return EXIT_FAILURE;
        }
        if (arguments.expect_audio && !observer.receivedOpusMicrophone())
        {
            std::cerr << "FAIL: " << observer.subscribedAudioDescription() << ".\n";
            return EXIT_FAILURE;
        }
        if (arguments.expect_ptt)
        {
            std::cout << "PASS: observed remote Opus PTT start and clean track removal.\n";
        }
        else if (arguments.expect_audio)
        {
            std::cout
                << "PASS: subscribed to a remote Opus microphone track; platform playout active.\n";
        }
        else if (arguments.publish_audio)
        {
            std::cout << "PASS: microphone capture and Opus publication remained active.\n";
        }
        else
        {
            std::cout << "PASS: native Windows client remained connected.\n";
        }
        return EXIT_SUCCESS;
    }

    std::cout << "Connected. Waiting up to " << arguments.wait_for_peer.count() << " seconds for "
              << (arguments.expect_audio ? "a remote Opus microphone track"
                  : arguments.expect_ptt ? "a remote Opus PTT start and stop"
                                         : "a second participant")
              << "...\n";
    const auto deadline = std::chrono::steady_clock::now() + arguments.wait_for_peer;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (probe_succeeded(arguments, observer) ||
            (!arguments.expect_audio && !room.remoteParticipants().empty()))
        {
            if (arguments.expect_ptt)
            {
                std::cout << "PASS: observed remote Opus PTT start and clean track removal.\n";
            }
            else if (arguments.expect_audio)
            {
                std::cout << "PASS: subscribed to a remote Opus microphone track; platform "
                             "playout active.\n";
            }
            else
            {
                std::cout << "PASS: two native Windows clients are connected.\n";
            }
            std::this_thread::sleep_for(std::chrono::seconds{1});
            return EXIT_SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    if (arguments.expect_ptt)
    {
        std::cerr << "FAIL: remote Opus microphone did not start and stop before the timeout.\n";
    }
    else if (arguments.expect_audio)
    {
        std::cerr << "FAIL: " << observer.subscribedAudioDescription() << " before the timeout.\n";
    }
    else
    {
        std::cerr << "FAIL: no second participant appeared before the timeout.\n";
    }
    return EXIT_FAILURE;
}

} // namespace

auto main(const int argc, char** argv) -> int
{
    try
    {
        const auto arguments = parse_arguments(argc, argv);
        const LiveKitLifetime livekit_lifetime;

        std::optional<livekit::PlatformAudio> platform_audio;
        if (arguments.list_audio_devices || arguments.publish_audio || arguments.expect_audio ||
            arguments.ptt_duration.has_value() || arguments.expect_ptt)
        {
            platform_audio.emplace();
            configure_audio_devices(arguments, *platform_audio);
        }

        if (arguments.list_audio_devices)
        {
            print_audio_devices(*platform_audio);
            return EXIT_SUCCESS;
        }
        if (arguments.expect_audio && platform_audio->playoutDeviceCount() < 1)
        {
            throw std::runtime_error{"no audio playout device is available"};
        }

        if (has_all_scope_tokens(arguments))
        {
            return run_three_scope_probe(arguments);
        }
        return run_room_probe(arguments, platform_audio ? &*platform_audio : nullptr);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        print_usage();
        return EXIT_FAILURE;
    }
}
