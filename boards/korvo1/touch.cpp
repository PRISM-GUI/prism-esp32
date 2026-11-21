#include "touch.hpp"

#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"

#include <algorithm>

namespace korvo1 {

namespace {

// The GT1151's status register: bit 7 says a new sample is ready, the low nibble counts fingers.
// The driver (esp_lcd_touch_gt1151) reads the points from here and clears it.
constexpr int kStatusRegister = 0x814E;
constexpr std::uint8_t kSampleReady = 0x80;

bool sample_ready(esp_lcd_touch_handle_t handle)
{
    std::uint8_t status = 0;
    return esp_lcd_panel_io_rx_param(handle->io, kStatusRegister, &status, 1) == ESP_OK &&
           (status & kSampleReady) != 0;
}

prism::core::Point on_panel(const esp_lcd_touch_point_data_t &point)
{
    return {prism::core::X{float(std::min<int>(point.x, BSP_LCD_H_RES - 1))},
            prism::core::Y{float(std::min<int>(point.y, BSP_LCD_V_RES - 1))}};
}

} // namespace

void touch_detail::TouchDelete::operator()(esp_lcd_touch_handle_t t) const
{
    esp_lcd_touch_del(t);
}

std::expected<Touch, std::string> Touch::open()
{
    esp_lcd_touch_handle_t handle = nullptr;
    if (const esp_err_t err = bsp_touch_new(nullptr, &handle); err != ESP_OK)
        return std::unexpected(std::string("touch panel not found (") + esp_err_to_name(err) +
                               "): the demo runs without input; check the panel's ribbon cable");
    Touch touch;
    touch.handle_.reset(handle);
    return touch;
}

// PRISM polls every ~16 ms and the controller samples every ~10 ms, so a poll can come before the
// next sample: the finger is then where it was, not lifted. Only a ready sample changes the state.
// The BSP configures the GT1151 with the panel's 800x480 and no swap or mirror, and the driver
// applies none: its coordinates are already the panel's.
std::optional<prism::core::Point> Touch::read()
{
    if (!handle_ || !sample_ready(handle_.get()))
        return last_;
    if (esp_lcd_touch_read_data(handle_.get()) != ESP_OK)   // a bad checksum: keep the last state
        return last_;
    esp_lcd_touch_point_data_t point{};
    std::uint8_t count = 0;
    const bool touched =
        esp_lcd_touch_get_data(handle_.get(), &point, &count, 1) == ESP_OK && count > 0;
    last_ = touched ? std::optional{on_panel(point)} : std::nullopt;
    return last_;
}

} // namespace korvo1
