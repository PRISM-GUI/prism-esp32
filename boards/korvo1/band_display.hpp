#pragma once

// The Korvo-1's 800x480 RGB panel as PRISM's BandSink (prism/platform/frame_target.hpp).
// PRISM draws each frame as 48-row RGB565 bands into one of two band buffers in internal SRAM.
// flush() starts the PPA copying the band into the panel's back buffer and returns, so the
// render thread draws the next band into the other buffer meanwhile; band() waits for a buffer's
// copy before handing it out again. present() hands the back buffer to the panel, which switches
// to it at its next refresh. The first band of the next frame waits for that switch before its
// copy: only then has the panel stopped reading the buffer it writes into.
// The esp_lcd RGB driver owns the two RGB565 frame buffers (PSRAM, 750 KB each).

#include <prism/platform/frame_target.hpp>
#include <prism/raster/pixel.hpp>

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
    void operator()(std::uint16_t *p) const;
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

class BandDisplay final : public prism::platform::BandSink<prism::raster::Rgb565> {
public:
    static constexpr int kWidth = 800;
    static constexpr int kHeight = 480;
    static constexpr int kBandHeight = 48;

    // PSRAM, the two bands in SRAM, the PPA, then the panel (on, showing a black buffer). The
    // error says what failed and what to try.
    static std::expected<BandDisplay, std::string> open();

    int band_height() const override { return kBandHeight; }
    std::span<std::uint16_t> band(int index) override;
    void flush(int index, int first_row, int rows) override;
    void present() override;
    // Two panel buffers that flip: the one drawn into shows the frame before last.
    int frame_age() const override { return 2; }
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
    using Band = std::unique_ptr<std::uint16_t[], display_detail::HeapFree>;

    BandDisplay() = default;
    void take_band(int index);    // waits for the band's copy, once per copy
    esp_err_t copy(int index, int first_row, int rows);
    void wait_for_panel();
    void report_first_failure(esp_err_t err);   // logged once, not every frame

    // Declared in the order they are made: the panel goes first on destruction, so its
    // interrupt stops giving `flipped_` before the semaphore is deleted.
    std::array<Band, 2> bands_;
    std::array<Semaphore, 2> copied_;   // given once a band's copy is done (the PPA's interrupt)
    Semaphore flipped_;    // given by the panel's interrupt once it reads the new buffer
    Ppa ppa_;              // null on the CPU path
    Panel panel_;
    std::array<std::uint16_t *, 2> panel_buffers_{};
    // The runtime asks for band 0 once more than it flushes it (to make its canvas): a band
    // already taken and not yet flushed is handed out without waiting again.
    std::array<bool, 2> held_{};
    int back_index_ = 1;   // the panel starts on buffer 0
    bool flip_pending_ = false;
    bool copy_failed_ = false;
};

} // namespace korvo1
