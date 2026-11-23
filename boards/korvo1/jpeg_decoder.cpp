#include "jpeg_decoder.hpp"

#include <driver/jpeg_decode.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <prism/render/image_decoder.hpp>
#include <prism/render/image_ops.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <span>
#include <string>

namespace prism::esp32 {
namespace {

// simplify: the whole picture is decoded before it is scaled, so its RGB888 copy must fit in
// PSRAM: 1600x1600 is 7.7 MB. A larger picture is refused (the caller falls back to whatever it
// shows instead). Upgrade: decode in strips, or ask the engine for a scaled output where it can.
constexpr std::uint32_t kMaxPixels = 1600u * 1600u;

// The engine writes whole MCUs: a picture's rows and columns round up to 16.
std::uint32_t mcu_aligned(std::uint32_t n) {
    return (n + 15u) & ~15u;
}

jpeg_decoder_handle_t engine() {
    static jpeg_decoder_handle_t handle = nullptr;
    static bool created = false;
    if (created) return handle;
    created = true;
    jpeg_decode_engine_cfg_t cfg{};
    cfg.intr_priority = 0;
    cfg.timeout_ms = 200;
    if (const auto err = jpeg_new_decoder_engine(&cfg, &handle); err != ESP_OK) {
        handle = nullptr;
        std::printf("jpeg: no JPEG engine (%s)\n", esp_err_to_name(err));
    }
    return handle;
}

// What the engine allocated, freed the way it allocated it.
struct Picture {
    std::uint8_t* data = nullptr;
    std::size_t size = 0;

    ~Picture() {
        if (data) heap_caps_free(data);
    }
    Picture(const Picture&) = delete;
    Picture& operator=(const Picture&) = delete;
    Picture() = default;
};

class JpegDecoder final : public render::ImageDecoder {
public:
    std::shared_ptr<const render::Image> decode(std::span<const std::byte> bytes,
                                               const render::DecodeOptions& options,
                                               std::string& error) override {
        if (bytes.size() < 2 || bytes[0] != std::byte{0xFF} || bytes[1] != std::byte{0xD8}) {
            error = "not a JPEG";
            return nullptr;
        }
        // The engine is one unit of hardware: decodes are serialised here, whatever thread PRISM
        // asks from.
        const std::lock_guard lock{mutex_};
        const auto handle = engine();
        if (!handle) {
            error = "no JPEG engine";
            return nullptr;
        }

        const auto* bits = reinterpret_cast<const std::uint8_t*>(bytes.data());
        const auto size = static_cast<std::uint32_t>(bytes.size());
        jpeg_decode_picture_info_t info{};
        if (jpeg_decoder_get_info(bits, size, &info) != ESP_OK) {
            error = "cannot read the JPEG header";
            return nullptr;
        }
        if (info.width == 0 || info.height == 0) {
            error = "empty JPEG";
            return nullptr;
        }
        if (static_cast<std::uint64_t>(info.width) * info.height > kMaxPixels) {
            error = "JPEG too large";
            return nullptr;
        }

        const auto stride = mcu_aligned(info.width) * 3;
        jpeg_decode_memory_alloc_cfg_t alloc{};
        alloc.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
        Picture picture;
        picture.data = static_cast<std::uint8_t*>(jpeg_alloc_decoder_mem(
            stride * mcu_aligned(info.height), &alloc, &picture.size));
        if (!picture.data) {
            error = "out of memory for the picture";
            return nullptr;
        }

        jpeg_decode_cfg_t cfg{};
        cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB888;
        cfg.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB;
        cfg.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
        std::uint32_t written = 0;
        if (jpeg_decoder_process(handle, &cfg, bits, size, picture.data, picture.size, &written) != ESP_OK) {
            error = "the JPEG engine could not decode it";
            return nullptr;
        }

        // The engine writes RGB888 rows padded out to whole MCUs: the view's stride skips the pad,
        // and fit_pixels averages it into the size asked for (the whole picture when none was).
        const render::PixelView view{
            std::span<const std::byte>{reinterpret_cast<const std::byte*>(picture.data), picture.size},
            static_cast<int>(info.width), static_cast<int>(info.height), static_cast<int>(stride),
            render::SourceFormat::rgb888};
        const auto box = options.fit.value_or(render::Size{render::Width{static_cast<float>(info.width)},
                                                           render::Height{static_cast<float>(info.height)}});
        auto image = render::fit_pixels(view, box, options.format);
        if (!image) {
            error = "cannot scale the image";
            return nullptr;
        }
        return image;
    }

private:
    std::mutex mutex_;
};

} // namespace

void register_jpeg_decoder() {
    static JpegDecoder decoder;
    static const bool registered = (render::register_decoder(decoder), true);
    (void)registered;
}

} // namespace prism::esp32
