// The headset as a PRISM audio device: the "headset" backend, the data callback the A2DP stack
// calls for the stream's bytes, and the public surface of bluetooth.hpp. Grown from
// prism-mp3-player's BtOutput and, before that, from prism-esp32's main/btspike.cpp.
//
// The state machine behind it (which headset, when to retry, the stream's handshake) is
// bt_link.hpp; the callbacks that feed it are bt_callbacks.cpp; the bring-up is bt_bringup.cpp.
// The device methods run on the thread that makes the Outputs, the callback on the Bluetooth task.

#include "bluetooth.hpp"

#include "bt_shared.hpp"

#include <prism/audio/backend.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace prism::esp32::bluetooth {

namespace {

constexpr const char* kId = "headset";
constexpr int kChannels = 2;
constexpr int kRate = 44100;   // what every A2DP source streams, and all this device offers

// "Connected to <name>": the whole name once discovery or a bond gave it, the configured one before.
std::string device_name()
{
    const std::lock_guard lock(link::shared.text_mutex);
    return link::shared.name.empty() ? std::string{link::kTargetName} : link::shared.name;
}

// One Output's stream: it owns the pull function and hands it to the data callback's gate for as
// long as it lives; the destructor takes it out again, which is what lets Output destroy its
// render.
class HeadsetStream final : public audio::DeviceStream {
public:
    explicit HeadsetStream(audio::PullFn pull) : pull_{std::move(pull)}
    {
        link::attach(pull_);
        link::post(link::event(link::Event::Kind::play));   // start the stream if it is not running
    }

    ~HeadsetStream() override { link::detach(pull_); }

    // The connection this stream was opened on is gone: the Output reports lost, and its owner
    // makes a new one when the headset comes back.
    [[nodiscard]] bool lost() const override { return link::shared.link != link::Link::connected; }

    // simplify: the volume scales the samples in the Output, because absolute volume over AVRCP
    // needs the peer to register for it, an exchange that cannot be exercised here. Scaling works
    // with every headset and costs one multiply per sample.
    // Upgrade: esp_avrc_ct_send_set_absolute_volume_cmd() once the peer's features are read.
    bool set_volume(float) override { return false; }

private:
    audio::PullFn pull_;
};

// The backend the registry holds, registered once by start().
class HeadsetBackend final : public audio::DeviceBackend {
public:
    [[nodiscard]] std::vector<audio::DeviceInfo> devices() override
    {
        return {audio::DeviceInfo{.id = kId,
                                  .name = device_name(),
                                  .rates = {kRate},
                                  .max_channels = kChannels,
                                  .native = audio::SampleType::s16,
                                  .is_default = false}};
    }

    [[nodiscard]] std::unique_ptr<audio::DeviceStream> open(std::string_view id,
                                                            const audio::Format& format,
                                                            audio::PullFn pull,
                                                            std::string& error) override
    {
        if (id != kId) {
            error = "no such device";
            return nullptr;
        }
        if (format.channels != kChannels) {
            error = "the headset plays stereo only";
            return nullptr;
        }
        if (format.sample_rate != kRate) {
            error = "the headset plays 44.1 kHz only";
            return nullptr;
        }
        // Nothing to play yet: an Output opened now is lost, and its owner tries again when the
        // headset is back.
        if (link::shared.link != link::Link::connected) {
            error = "headset not connected";
            return nullptr;
        }
        return std::make_unique<HeadsetStream>(std::move(pull));
    }
};

} // namespace

// One buffer, the Bluetooth task: hand the bytes to whatever Output is attached, and silence when
// there is none, so the headset never has to cope with a half-written stream. Nothing here
// allocates, blocks or takes a lock (prism/audio/backend.hpp).
std::int32_t link::data_callback(std::uint8_t* data, std::int32_t length)
{
    if (data == nullptr || length <= 0) return 0;   // a null buffer means the stream is stopping
    const auto frames = static_cast<std::size_t>(length) / link::kBytesPerFrame;
    const auto block =
        std::span<std::byte>{reinterpret_cast<std::byte*>(data), frames * link::kBytesPerFrame};
    if (!link::shared.gate.call(block, static_cast<int>(frames)))
        std::ranges::fill(block, std::byte{0});
    return static_cast<std::int32_t>(block.size());
}

void start()
{
    static std::once_flag once;
    std::call_once(once, [] {
        static HeadsetBackend backend;   // the registry holds it for the firmware's life
        link::start_bluetooth();
        audio::register_backend(backend);
    });
}

void on_remote(std::function<void(Remote)> handler)
{
    const std::lock_guard lock(link::shared.text_mutex);
    link::shared.remote = std::move(handler);
}

std::string state_text()
{
    switch (link::shared.link.load()) {
    case link::Link::waiting: return "Off";
    case link::Link::scanning: return "Scanning…";
    case link::Link::connecting: return "Connecting…";
    case link::Link::connected: break;
    }
    return "Connected to " + device_name();
}

std::uint32_t state_revision() { return link::shared.revision.load(); }

} // namespace prism::esp32::bluetooth
