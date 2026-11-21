#pragma once

// The Korvo-1's GT1151 touch controller (the BSP's handle, on I2C) as PRISM's TouchSource
// (prism/platform/frame_target.hpp), polled: the first finger, in panel pixels.

#include <prism/platform/frame_target.hpp>

#include "esp_lcd_touch.h"

#include <expected>
#include <memory>
#include <optional>
#include <string>

namespace korvo1 {

namespace touch_detail {
struct TouchDelete {
    void operator()(esp_lcd_touch_handle_t t) const;
};
} // namespace touch_detail

class Touch final : public prism::platform::TouchSource {
public:
    // The BSP's I2C bus and GT1151. The error says what failed and what to try.
    static std::expected<Touch, std::string> open();

    Touch() = default;   // no controller: read() always says no finger

    std::optional<prism::core::Point> read() override;

private:
    std::unique_ptr<esp_lcd_touch_t, touch_detail::TouchDelete> handle_;
    std::optional<prism::core::Point> last_;   // the last sample's finger, or none
};

} // namespace korvo1
