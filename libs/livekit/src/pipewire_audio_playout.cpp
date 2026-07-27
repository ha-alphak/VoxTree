#include "audio_playout.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <pipewire/pipewire.h>
#include <ranges>
#include <spa/param/audio/format-utils.h>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace hvc::livekit::detail
{
namespace
{
constexpr std::size_t sample_queue_capacity = 32'768;
constexpr auto registry_timeout = std::chrono::seconds{3};

class PipeWireLifetime final
{
  public:
    PipeWireLifetime()
    {
        const std::scoped_lock lock{mutex};
        if (instance_count == 0)
        {
            pw_init(nullptr, nullptr);
        }
        ++instance_count;
    }

    ~PipeWireLifetime()
    {
        const std::scoped_lock lock{mutex};
        --instance_count;
        if (instance_count == 0)
        {
            pw_deinit();
        }
    }

    PipeWireLifetime(const PipeWireLifetime&) = delete;
    auto operator=(const PipeWireLifetime&) -> PipeWireLifetime& = delete;
    PipeWireLifetime(PipeWireLifetime&&) = delete;
    auto operator=(PipeWireLifetime&&) -> PipeWireLifetime& = delete;

  private:
    static inline std::mutex mutex;
    static inline std::size_t instance_count{0};
};

[[nodiscard]] auto pipeWireLifetime() -> PipeWireLifetime&
{
    static PipeWireLifetime lifetime;
    return lifetime;
}

class SampleQueue final
{
  public:
    [[nodiscard]] auto push(std::span<const std::int16_t> samples, float gain) noexcept
        -> std::size_t
    {
        auto write_position = write_position_.load(std::memory_order_relaxed);
        const auto read_position = read_position_.load(std::memory_order_acquire);
        const auto available = sample_queue_capacity - (write_position - read_position);
        const auto count = std::min(samples.size(), available);
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto scaled =
                std::clamp(static_cast<float>(samples[index]) * gain,
                           static_cast<float>(std::numeric_limits<std::int16_t>::min()),
                           static_cast<float>(std::numeric_limits<std::int16_t>::max()));
            samples_[write_position % sample_queue_capacity] =
                static_cast<std::int16_t>(std::lrint(scaled));
            ++write_position;
        }
        write_position_.store(write_position, std::memory_order_release);
        return count;
    }

    [[nodiscard]] auto pop(std::span<std::int16_t> destination) noexcept -> std::size_t
    {
        auto read_position = read_position_.load(std::memory_order_relaxed);
        const auto write_position = write_position_.load(std::memory_order_acquire);
        const auto count = std::min(destination.size(), write_position - read_position);
        for (std::size_t index = 0; index < count; ++index)
        {
            destination[index] = samples_[read_position % sample_queue_capacity];
            ++read_position;
        }
        read_position_.store(read_position, std::memory_order_release);
        return count;
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t
    {
        const auto read_position = read_position_.load(std::memory_order_acquire);
        const auto write_position = write_position_.load(std::memory_order_acquire);
        return write_position - read_position;
    }

  private:
    std::array<std::int16_t, sample_queue_capacity> samples_{};
    std::atomic<std::size_t> read_position_{0};
    std::atomic<std::size_t> write_position_{0};
};

class PipeWireRegistrySnapshot final
{
  public:
    PipeWireRegistrySnapshot() : loop_(pw_thread_loop_new("hvc-pipewire-registry", nullptr))
    {
        if (loop_ == nullptr)
        {
            throw std::runtime_error{"PipeWire registry loop initialization failed"};
        }
        try
        {
            initialize();
        }
        catch (...)
        {
            release();
            throw;
        }
    }

    ~PipeWireRegistrySnapshot()
    {
        release();
    }

    PipeWireRegistrySnapshot(const PipeWireRegistrySnapshot&) = delete;
    auto operator=(const PipeWireRegistrySnapshot&) -> PipeWireRegistrySnapshot& = delete;
    PipeWireRegistrySnapshot(PipeWireRegistrySnapshot&&) = delete;
    auto operator=(PipeWireRegistrySnapshot&&) -> PipeWireRegistrySnapshot& = delete;

    [[nodiscard]] auto sinks() const -> std::vector<client::AudioDevice>
    {
        auto result = sinks_;
        std::ranges::sort(result, {}, &client::AudioDevice::display_name);
        return result;
    }

    [[nodiscard]] auto sources() const -> std::vector<client::AudioDevice>
    {
        auto result = sources_;
        std::ranges::sort(result, {}, &client::AudioDevice::display_name);
        return result;
    }

  private:
    void initialize()
    {
        context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
        if (context_ == nullptr)
        {
            throw std::runtime_error{"PipeWire registry context initialization failed"};
        }
        core_ = pw_context_connect(context_, nullptr, 0);
        if (core_ == nullptr)
        {
            throw std::runtime_error{"PipeWire connection failed"};
        }
        registry_ = pw_core_get_registry(core_, PW_VERSION_REGISTRY, 0);
        if (registry_ == nullptr)
        {
            throw std::runtime_error{"PipeWire registry is unavailable"};
        }

        static const auto registry_events = [] {
            pw_registry_events events{};
            events.version = PW_VERSION_REGISTRY_EVENTS;
            events.global = onGlobal;
            return events;
        }();
        static const auto core_events = [] {
            pw_core_events events{};
            events.version = PW_VERSION_CORE_EVENTS;
            events.done = onDone;
            events.error = onError;
            return events;
        }();
        pw_registry_add_listener(registry_, &registry_listener_, &registry_events, this);
        registry_listener_added_ = true;
        pw_core_add_listener(core_, &core_listener_, &core_events, this);
        core_listener_added_ = true;

        if (pw_thread_loop_start(loop_) < 0)
        {
            throw std::runtime_error{"PipeWire registry loop could not start"};
        }
        loop_started_ = true;

        pw_thread_loop_lock(loop_);
        sync_sequence_ = pw_core_sync(core_, PW_ID_CORE, 0);
        const auto deadline = std::chrono::steady_clock::now() + registry_timeout;
        while (!sync_complete_ && error_.empty() && std::chrono::steady_clock::now() < deadline)
        {
            pw_thread_loop_timed_wait(loop_, 250'000'000);
        }
        pw_thread_loop_unlock(loop_);
        if (!error_.empty())
        {
            throw std::runtime_error{"PipeWire registry failed: " + error_};
        }
        if (!sync_complete_)
        {
            throw std::runtime_error{"PipeWire registry enumeration timed out"};
        }
    }

    void release() noexcept
    {
        if (loop_started_)
        {
            pw_thread_loop_stop(loop_);
            loop_started_ = false;
        }
        if (core_listener_added_)
        {
            spa_hook_remove(&core_listener_);
            core_listener_added_ = false;
        }
        if (registry_listener_added_)
        {
            spa_hook_remove(&registry_listener_);
            registry_listener_added_ = false;
        }
        if (registry_ != nullptr)
        {
            pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry_));
            registry_ = nullptr;
        }
        if (core_ != nullptr)
        {
            pw_core_disconnect(core_);
            core_ = nullptr;
        }
        if (context_ != nullptr)
        {
            pw_context_destroy(context_);
            context_ = nullptr;
        }
        if (loop_ != nullptr)
        {
            pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
        }
    }

    static void onGlobal(void* data, std::uint32_t, std::uint32_t, const char* type, std::uint32_t,
                         const spa_dict* properties) noexcept
    {
        auto& self = *static_cast<PipeWireRegistrySnapshot*>(data);
        try
        {
            if (type == nullptr || std::string_view{type} != PW_TYPE_INTERFACE_Node ||
                properties == nullptr)
            {
                return;
            }
            const auto* const media_class = spa_dict_lookup(properties, PW_KEY_MEDIA_CLASS);
            if (media_class == nullptr)
            {
                return;
            }
            const auto* const node_name = spa_dict_lookup(properties, PW_KEY_NODE_NAME);
            if (node_name == nullptr || *node_name == '\0')
            {
                return;
            }
            const auto media_class_view = std::string_view{media_class};
            const auto node_name_view = std::string_view{node_name};
            const auto is_sink = media_class_view == "Audio/Sink";
            const auto is_source =
                media_class_view == "Audio/Source" && !node_name_view.ends_with(".monitor");
            if (!is_sink && !is_source)
            {
                return;
            }
            const auto* display_name = spa_dict_lookup(properties, PW_KEY_NODE_DESCRIPTION);
            if (display_name == nullptr || *display_name == '\0')
            {
                display_name = spa_dict_lookup(properties, PW_KEY_NODE_NICK);
            }
            if (display_name == nullptr || *display_name == '\0')
            {
                display_name = node_name;
            }
            auto& devices = is_sink ? self.sinks_ : self.sources_;
            devices.push_back(client::AudioDevice{node_name, display_name});
        }
        catch (...)
        {
            self.error_ = "audio device metadata allocation failed";
        }
    }

    static void onDone(void* data, std::uint32_t object_id, int sequence) noexcept
    {
        auto& self = *static_cast<PipeWireRegistrySnapshot*>(data);
        if (object_id == PW_ID_CORE && sequence == self.sync_sequence_)
        {
            self.sync_complete_ = true;
            pw_thread_loop_signal(self.loop_, false);
        }
    }

    static void onError(void* data, std::uint32_t, int, int result, const char* message) noexcept
    {
        auto& self = *static_cast<PipeWireRegistrySnapshot*>(data);
        try
        {
            self.error_ = message == nullptr ? "unknown PipeWire core error" : message;
            self.error_ += " (" + std::to_string(result) + ")";
        }
        catch (...)
        {
            self.error_ = "PipeWire core error";
        }
        pw_thread_loop_signal(self.loop_, false);
    }

    pw_thread_loop* loop_{nullptr};
    pw_context* context_{nullptr};
    pw_core* core_{nullptr};
    pw_registry* registry_{nullptr};
    spa_hook registry_listener_{};
    spa_hook core_listener_{};
    int sync_sequence_{0};
    bool sync_complete_{false};
    bool loop_started_{false};
    bool registry_listener_added_{false};
    bool core_listener_added_{false};
    std::string error_;
    std::vector<client::AudioDevice> sinks_;
    std::vector<client::AudioDevice> sources_;
};

class PipeWireMicrophoneSource final : public MicrophoneSource
{
  public:
    explicit PipeWireMicrophoneSource(std::string target_node)
        : target_node_(std::move(target_node)),
          audio_source_(std::make_shared<::livekit::AudioSource>(capture_sample_rate,
                                                                 capture_channel_count, 0)),
          processor_(processingOptions()),
          track_(::livekit::LocalAudioTrack::createLocalAudioTrack("hvc-microphone", audio_source_))
    {
        try
        {
            initializeStream();
            capture_thread_ =
                std::jthread{[this](std::stop_token stop_token) { capture(stop_token); }};
        }
        catch (...)
        {
            releaseStream();
            throw;
        }
    }

    ~PipeWireMicrophoneSource() override
    {
        capture_thread_.request_stop();
        if (capture_thread_.joinable())
        {
            capture_thread_.join();
        }
        releaseStream();
    }

    [[nodiscard]] auto track() const -> std::shared_ptr<::livekit::LocalAudioTrack> override
    {
        return track_;
    }

  private:
    static constexpr int capture_sample_rate = 48'000;
    static constexpr int capture_channel_count = 1;
    static constexpr std::size_t samples_per_frame = 480;

    [[nodiscard]] static auto processingOptions() -> ::livekit::AudioProcessingModule::Options
    {
        ::livekit::AudioProcessingModule::Options options;
        // AEC needs a time-aligned mixed reverse stream. KDE-00 evaluates it separately;
        // enabling AEC without that reference would degrade the microphone signal.
        options.echo_cancellation = false;
        options.noise_suppression = true;
        options.high_pass_filter = true;
        options.auto_gain_control = true;
        return options;
    }

    void initializeStream()
    {
        loop_ = pw_thread_loop_new("hvc-pipewire-capture", nullptr);
        if (loop_ == nullptr)
        {
            throw std::runtime_error{"PipeWire capture loop initialization failed"};
        }

        auto* properties = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY,
                                             "Capture", PW_KEY_MEDIA_ROLE, "Communication",
                                             PW_KEY_NODE_NAME, "hvc.microphone", nullptr);
        if (!target_node_.empty())
        {
            pw_properties_set(properties, PW_KEY_TARGET_OBJECT, target_node_.c_str());
        }
        static const auto events = [] {
            pw_stream_events stream_events{};
            stream_events.version = PW_VERSION_STREAM_EVENTS;
            stream_events.process = onProcess;
            return stream_events;
        }();
        stream_ = pw_stream_new_simple(pw_thread_loop_get_loop(loop_), "HVC microphone", properties,
                                       &events, this);
        if (stream_ == nullptr)
        {
            throw std::runtime_error{"PipeWire capture stream initialization failed"};
        }

        std::array<std::byte, 1'024> parameter_buffer{};
        spa_pod_builder builder{};
        spa_pod_builder_init(&builder, parameter_buffer.data(),
                             static_cast<std::uint32_t>(parameter_buffer.size()));
        spa_audio_info_raw audio_info{};
        audio_info.format = SPA_AUDIO_FORMAT_S16_LE;
        audio_info.rate = capture_sample_rate;
        audio_info.channels = capture_channel_count;
        audio_info.position[0] = SPA_AUDIO_CHANNEL_MONO;
        std::array<const spa_pod*, 1> parameters{
            spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &audio_info)};
        // PipeWire defines flags as enum bits and requires their bitwise combination.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        const auto stream_flags = static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS);
        const auto result =
            pw_stream_connect(stream_, PW_DIRECTION_INPUT, PW_ID_ANY, stream_flags,
                              parameters.data(), static_cast<std::uint32_t>(parameters.size()));
        if (result < 0)
        {
            throw std::runtime_error{"PipeWire capture stream connection failed"};
        }
        if (pw_thread_loop_start(loop_) < 0)
        {
            throw std::runtime_error{"PipeWire capture loop could not start"};
        }
        loop_started_ = true;
    }

    void releaseStream() noexcept
    {
        if (loop_started_)
        {
            pw_thread_loop_stop(loop_);
            loop_started_ = false;
        }
        if (stream_ != nullptr)
        {
            pw_stream_destroy(stream_);
            stream_ = nullptr;
        }
        if (loop_ != nullptr)
        {
            pw_thread_loop_destroy(loop_);
            loop_ = nullptr;
        }
    }

    void capture(std::stop_token stop_token) noexcept
    {
        try
        {
            std::array<std::int16_t, samples_per_frame> frame_samples{};
            while (!stop_token.stop_requested())
            {
                if (sample_queue_.size() < frame_samples.size())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
                    continue;
                }
                const auto copied = sample_queue_.pop(frame_samples);
                if (copied != frame_samples.size())
                {
                    continue;
                }
                auto frame = ::livekit::AudioFrame{
                    std::vector<std::int16_t>{frame_samples.begin(), frame_samples.end()},
                    capture_sample_rate, capture_channel_count,
                    static_cast<int>(samples_per_frame)};
                processor_.processStream(frame);
                audio_source_->captureFrame(frame, 20);
            }
        }
        catch (...)
        {
            capture_failed_.store(true);
            // Capture failures stop this publication source without terminating the client.
        }
    }

    static void onProcess(void* data) noexcept
    {
        auto& self = *static_cast<PipeWireMicrophoneSource*>(data);
        auto* const pipewire_buffer = pw_stream_dequeue_buffer(self.stream_);
        if (pipewire_buffer == nullptr || pipewire_buffer->buffer == nullptr ||
            pipewire_buffer->buffer->n_datas < 1)
        {
            return;
        }
        auto& buffer_data = pipewire_buffer->buffer->datas[0];
        if (buffer_data.data != nullptr && buffer_data.chunk != nullptr)
        {
            const auto offset = std::min(buffer_data.chunk->offset, buffer_data.maxsize);
            const auto available_bytes = buffer_data.maxsize - offset;
            const auto byte_count = std::min(buffer_data.chunk->size, available_bytes);
            const auto* const samples = reinterpret_cast<const std::int16_t*>(
                static_cast<const std::byte*>(buffer_data.data) + offset);
            static_cast<void>(self.sample_queue_.push(
                std::span{samples, static_cast<std::size_t>(byte_count) / sizeof(std::int16_t)},
                1.0F));
        }
        pw_stream_queue_buffer(self.stream_, pipewire_buffer);
    }

    std::string target_node_;
    std::shared_ptr<::livekit::AudioSource> audio_source_;
    ::livekit::AudioProcessingModule processor_;
    std::shared_ptr<::livekit::LocalAudioTrack> track_;
    SampleQueue sample_queue_;
    std::jthread capture_thread_;
    pw_thread_loop* loop_{nullptr};
    pw_stream* stream_{nullptr};
    bool loop_started_{false};
    std::atomic_bool capture_failed_{false};
};

class PipeWireRemotePlayout final : public RemoteAudioPlayout
{
  public:
    PipeWireRemotePlayout(std::string target_node, const std::shared_ptr<::livekit::Track>& track,
                          float gain)
        : target_node_(std::move(target_node)),
          stream_reader_(
              ::livekit::AudioStream::fromTrack(track, ::livekit::AudioStream::Options{8, {}, {}})),
          gain_(gain)
    {
        if (stream_reader_ == nullptr)
        {
            throw std::runtime_error{"LiveKit remote audio stream initialization failed"};
        }
        reader_thread_ = std::jthread{[this](std::stop_token stop_token) { read(stop_token); }};
    }

    ~PipeWireRemotePlayout() override
    {
        stream_reader_->close();
        reader_thread_.request_stop();
        if (reader_thread_.joinable())
        {
            reader_thread_.join();
        }
        if (loop_started_)
        {
            pw_thread_loop_stop(loop_);
        }
        if (stream_ != nullptr)
        {
            pw_stream_destroy(stream_);
        }
        if (loop_ != nullptr)
        {
            pw_thread_loop_destroy(loop_);
        }
    }

    void setGain(float gain) noexcept override
    {
        gain_.store(gain);
    }

  private:
    void read(std::stop_token stop_token) noexcept
    {
        try
        {
            ::livekit::AudioFrameEvent event;
            while (!stop_token.stop_requested() && stream_reader_->read(event))
            {
                const auto& frame = event.frame;
                if (frame.totalSamples() == 0 || frame.sampleRate() <= 0 ||
                    frame.numChannels() <= 0)
                {
                    continue;
                }
                if (stream_ == nullptr)
                {
                    initializeStream(frame.sampleRate(), frame.numChannels());
                }
                if (frame.sampleRate() != sample_rate_ || frame.numChannels() != channel_count_)
                {
                    throw std::runtime_error{"LiveKit remote audio format changed during playout"};
                }
                static_cast<void>(sample_queue_.push(frame.data(), gain_.load()));
            }
        }
        catch (...)
        {
            // Media worker failures stop only this remote track; network callbacks remain alive.
            stream_reader_->close();
        }
    }

    void initializeStream(int sample_rate, int channel_count)
    {
        loop_ = pw_thread_loop_new("hvc-pipewire-playout", nullptr);
        if (loop_ == nullptr)
        {
            throw std::runtime_error{"PipeWire playout loop initialization failed"};
        }

        auto* properties = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY,
                                             "Playback", PW_KEY_MEDIA_ROLE, "Communication",
                                             PW_KEY_NODE_NAME, "hvc.remote-voice", nullptr);
        if (!target_node_.empty())
        {
            pw_properties_set(properties, PW_KEY_TARGET_OBJECT, target_node_.c_str());
        }
        static const auto events = [] {
            pw_stream_events stream_events{};
            stream_events.version = PW_VERSION_STREAM_EVENTS;
            stream_events.process = onProcess;
            return stream_events;
        }();
        stream_ = pw_stream_new_simple(pw_thread_loop_get_loop(loop_), "HVC remote voice",
                                       properties, &events, this);
        if (stream_ == nullptr)
        {
            throw std::runtime_error{"PipeWire playout stream initialization failed"};
        }

        std::array<std::byte, 1'024> parameter_buffer{};
        spa_pod_builder builder{};
        spa_pod_builder_init(&builder, parameter_buffer.data(),
                             static_cast<std::uint32_t>(parameter_buffer.size()));
        spa_audio_info_raw audio_info{};
        audio_info.format = SPA_AUDIO_FORMAT_S16_LE;
        audio_info.rate = static_cast<std::uint32_t>(sample_rate);
        audio_info.channels = static_cast<std::uint32_t>(channel_count);
        if (channel_count == 1)
        {
            audio_info.position[0] = SPA_AUDIO_CHANNEL_MONO;
        }
        else if (channel_count == 2)
        {
            audio_info.position[0] = SPA_AUDIO_CHANNEL_FL;
            audio_info.position[1] = SPA_AUDIO_CHANNEL_FR;
        }
        std::array<const spa_pod*, 1> parameters{
            spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &audio_info)};
        // PipeWire defines flags as enum bits and requires their bitwise combination.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        const auto stream_flags = static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS);
        const auto result =
            pw_stream_connect(stream_, PW_DIRECTION_OUTPUT, PW_ID_ANY, stream_flags,
                              parameters.data(), static_cast<std::uint32_t>(parameters.size()));
        if (result < 0)
        {
            throw std::runtime_error{"PipeWire playout stream connection failed"};
        }
        if (pw_thread_loop_start(loop_) < 0)
        {
            throw std::runtime_error{"PipeWire playout loop could not start"};
        }
        loop_started_ = true;
        sample_rate_ = sample_rate;
        channel_count_ = channel_count;
    }

    static void onProcess(void* data) noexcept
    {
        auto& self = *static_cast<PipeWireRemotePlayout*>(data);
        auto* const pipewire_buffer = pw_stream_dequeue_buffer(self.stream_);
        if (pipewire_buffer == nullptr || pipewire_buffer->buffer == nullptr ||
            pipewire_buffer->buffer->n_datas < 1)
        {
            return;
        }
        auto& buffer_data = pipewire_buffer->buffer->datas[0];
        if (buffer_data.data == nullptr)
        {
            pw_stream_queue_buffer(self.stream_, pipewire_buffer);
            return;
        }

        const auto sample_capacity =
            static_cast<std::size_t>(buffer_data.maxsize) / sizeof(std::int16_t);
        auto samples = std::span{static_cast<std::int16_t*>(buffer_data.data), sample_capacity};
        const auto copied = self.sample_queue_.pop(samples);
        std::fill(samples.begin() + static_cast<std::ptrdiff_t>(copied), samples.end(), 0);
        buffer_data.chunk->offset = 0;
        buffer_data.chunk->stride =
            static_cast<std::int32_t>(self.channel_count_ * static_cast<int>(sizeof(std::int16_t)));
        buffer_data.chunk->size = static_cast<std::uint32_t>(samples.size_bytes());
        pw_stream_queue_buffer(self.stream_, pipewire_buffer);
    }

    std::string target_node_;
    std::shared_ptr<::livekit::AudioStream> stream_reader_;
    std::atomic<float> gain_{1.0F};
    SampleQueue sample_queue_;
    std::jthread reader_thread_;
    pw_thread_loop* loop_{nullptr};
    pw_stream* stream_{nullptr};
    bool loop_started_{false};
    int sample_rate_{0};
    int channel_count_{0};
};

class PipeWireBackend final : public AudioPlayoutBackend
{
  public:
    [[nodiscard]] auto devices() const -> std::vector<client::AudioDevice> override
    {
        return PipeWireRegistrySnapshot{}.sinks();
    }

    void selectDevice(const std::string& device_id) override
    {
        const auto available_devices = devices();
        const auto selected =
            std::ranges::find(available_devices, device_id, &client::AudioDevice::id);
        if (selected == available_devices.end())
        {
            throw std::runtime_error{"selected PipeWire playout device is unavailable"};
        }
        const std::scoped_lock lock{device_mutex_};
        selected_device_ = device_id;
    }

    [[nodiscard]] auto createPlayout(const std::shared_ptr<::livekit::Track>& track, float gain)
        -> std::shared_ptr<RemoteAudioPlayout> override
    {
        std::string selected_device;
        {
            const std::scoped_lock lock{device_mutex_};
            selected_device = selected_device_;
        }
        return std::make_shared<PipeWireRemotePlayout>(std::move(selected_device), track, gain);
    }

  private:
    std::mutex device_mutex_;
    std::string selected_device_;
};

class PipeWireMicrophoneBackend final : public MicrophoneBackend
{
  public:
    [[nodiscard]] auto devices() const -> std::vector<client::AudioDevice> override
    {
        return PipeWireRegistrySnapshot{}.sources();
    }

    void selectDevice(const std::string& device_id) override
    {
        const auto available_devices = devices();
        const auto selected =
            std::ranges::find(available_devices, device_id, &client::AudioDevice::id);
        if (selected == available_devices.end())
        {
            throw std::runtime_error{"selected PipeWire recording device is unavailable"};
        }
        const std::scoped_lock lock{device_mutex_};
        selected_device_ = device_id;
    }

    [[nodiscard]] auto createSource() -> std::shared_ptr<MicrophoneSource> override
    {
        std::string selected_device;
        {
            const std::scoped_lock lock{device_mutex_};
            selected_device = selected_device_;
        }
        return std::make_shared<PipeWireMicrophoneSource>(std::move(selected_device));
    }

  private:
    std::mutex device_mutex_;
    std::string selected_device_;
};
} // namespace

auto createAudioPlayoutBackend(::livekit::PlatformAudio&) -> std::unique_ptr<AudioPlayoutBackend>
{
    static_cast<void>(pipeWireLifetime());
    return std::make_unique<PipeWireBackend>();
}

auto createMicrophoneBackend(::livekit::PlatformAudio&) -> std::unique_ptr<MicrophoneBackend>
{
    static_cast<void>(pipeWireLifetime());
    return std::make_unique<PipeWireMicrophoneBackend>();
}
} // namespace hvc::livekit::detail
