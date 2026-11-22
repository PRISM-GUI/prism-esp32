#pragma once

// One of the Korvo-1's four buttons, which share one ADC pin (the BSP's ADC buttons, debounced by
// the button component). A press is recorded by the component's task; pressed() reports it to
// the thread that polls.

#include "bsp/esp32_s31_korvo_1.h"

#include <atomic>
#include <expected>
#include <memory>
#include <string>

namespace korvo1 {

// The button printed MODE on the Korvo-1 V1.1. The board-support package swaps MODE and VOL-:
// its BSP_BUTTON_VOLM voltage window is the one this button pulls the shared pin into (checked
// on the board, 2026-11-21).
inline constexpr bsp_button_t mode_button = BSP_BUTTON_VOLM;
// The buttons printed VOL- and VOL+. VOL- is MODE's half of the swap (inferred, not yet pressed on
// the board); VOL+ is taken as the BSP names it (unchecked).
inline constexpr bsp_button_t volume_down_button = BSP_BUTTON_MODE;
inline constexpr bsp_button_t volume_up_button = BSP_BUTTON_VOLP;

class Button {
public:
    // The BSP's buttons (created by the first call), and a callback on `which`. Open each button
    // once. The error says what failed.
    static std::expected<Button, std::string> open(bsp_button_t which);

    // True once for each press since the last call.
    bool pressed() { return down_->exchange(false); }

private:
    explicit Button(std::unique_ptr<std::atomic<bool>> down) : down_(std::move(down)) {}

    std::unique_ptr<std::atomic<bool>> down_; // heap: the callback keeps its address
};

} // namespace korvo1
