#pragma once

// The Korvo-1's 800x480 RGB panel as PRISM's FrameSink (prism/platform/frame_target.hpp).
// PRISM draws ARGB8888 into one of two frames in PSRAM, cleared by the PPA's fill. present()
// hands the frame to a present task and returns: the task converts it into the panel's RGB565
// back buffer (the PPA, or the CPU with CONFIG_PRISM_DISPLAY_CPU_CONVERT) while the render thread
// draws the next frame into the other one, then hands that buffer to the panel, which switches
// to it at its next refresh. The task waits for that switch before converting into the other
// panel buffer: only then has the panel stopped reading it.
// The esp_lcd RGB driver owns the two RGB565 buffers (PSRAM, 750 KB each).

#include <prism/platform/frame_target.hpp>

#include "driver/ppa.h"
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <type_traits>

namespace korvo1 {

namespace display_detail {
struct HeapFree {
    void operator()(std::uint32_t *p) const;
};
struct SemaphoreDelete {
    void operator()(SemaphoreHandle_t s) const;
};
struct PpaUnregister {
    void operator()(ppa_client_handle_t c) const;
};
struct PanelDelete {
    void operator()(esp_lcd_panel_handle_t p) const;
};
} // namespace display_detail

class Display final : public prism::platform::FrameSink {
public:
    static constexpr int kWidth = 800;
    static constexpr int kHeight = 480;

    // PSRAM, the ARGB8888 frame, the PPA, then the panel (on, showing a black buffer). The error
    // says what failed and what to try.
    static std::expected<Display, std::string> open();

    std::span<std::uint32_t> back() override;
    bool clear(std::uint32_t argb) override;   // the PPA's fill: 4.8 ms, the CPU's 19 ms
    void present() override;
    prism::core::Size size() const override
    {
        return {prism::core::Width{float(kWidth)}, prism::core::Height{float(kHeight)}};
    }

private:
    using Semaphore = std::unique_ptr<std::remove_pointer_t<SemaphoreHandle_t>,
                                      display_detail::SemaphoreDelete>;
    using Ppa = std::unique_ptr<std::remove_pointer_t<ppa_client_handle_t>,
                                display_detail::PpaUnregister>;
    using Panel = std::unique_ptr<std::remove_pointer_t<esp_lcd_panel_handle_t>,
                                  display_detail::PanelDelete>;
    using Frame = std::unique_ptr<std::uint32_t[], display_detail::HeapFree>;

    Display() = default;
    esp_err_t convert_into(const std::uint32_t *frame, std::uint16_t *panel_buffer);
    void present_loop();                       // the present task
    void show(const std::uint32_t *frame);     // convert, then hand the buffer to the panel
    void report_first_failure(esp_err_t err);   // logged once, not every frame

    // Declared in the order they are made: the panel goes first on destruction, so its
    // interrupt stops giving `flipped_` before the semaphore is deleted.
    std::array<Frame, 2> frames_;
    Semaphore flipped_;    // given by the panel's interrupt once it reads the new buffer
    Semaphore converted_;  // counting: given by the present task once a frame is converted
    Semaphore to_show_;    // a queue (one frame index): the frame handed to the present task
    Ppa ppa_;              // null on the CPU path
    Ppa filler_;           // the PPA's fill; null: the CPU clears
    Panel panel_;
    std::array<std::uint16_t *, 2> panel_buffers_{};
    int draw_ = 0;                // the frame the render thread draws into
    int frames_in_flight_ = 0;    // render thread: 0 until the first frame is handed over
    bool presenter_started_ = false;
    // The present task's own: the panel buffer to convert into, and whether the panel is still
    // to switch to the one it was handed last.
    int back_index_ = 1;   // the panel starts on buffer 0
    bool flip_pending_ = false;
    bool conversion_failed_ = false;
};

} // namespace korvo1
