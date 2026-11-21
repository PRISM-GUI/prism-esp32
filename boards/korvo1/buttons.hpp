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

class Button {
public:
    // The BSP's buttons, and a callback on `which`. The error says what failed.
    static std::expected<Button, std::string> open(bsp_button_t which);

    // True once for each press since the last call.
    bool pressed() { return down_->exchange(false); }

private:
    explicit Button(std::unique_ptr<std::atomic<bool>> down) : down_(std::move(down)) {}

    std::unique_ptr<std::atomic<bool>> down_; // heap: the callback keeps its address
};

} // namespace korvo1
