#include "touch.hpp"

#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"

#include <algorithm>

namespace korvo1 {

namespace {

// The GT1151's status register: bit 7 says a new sample is ready, the low nibble counts fingers.
// The driver (esp_lcd_touch_gt1151) reads the points from here and clears it.
constexpr int kStatusRegister = 0x814E;
constexpr std::uint8_t kSampleReady = 0x80;

// The status byte: ESP_OK when the I2C read worked.
esp_err_t read_status(esp_lcd_touch_handle_t handle, std::uint8_t &status)
{
    return esp_lcd_panel_io_rx_param(handle->io, kStatusRegister, &status, 1);
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
// A failing bus must not leave a finger pressed: after kMaxErrors reads in a row that fail, the
// finger is released until a read works again.
std::optional<prism::core::Point> Touch::read()
{
    if (!handle_)
        return last_;
    std::uint8_t status = 0;
    esp_err_t err = read_status(handle_.get(), status);
    if (err == ESP_OK && !(status & kSampleReady)) {
        errors_ = 0;
        return last_;
    }
    if (err == ESP_OK)
        err = esp_lcd_touch_read_data(handle_.get());
    if (err != ESP_OK)
        return after_error(err);
    errors_ = 0;
    esp_lcd_touch_point_data_t point{};
    std::uint8_t count = 0;
    const bool touched =
        esp_lcd_touch_get_data(handle_.get(), &point, &count, 1) == ESP_OK && count > 0;
    last_ = touched ? std::optional{on_panel(point)} : std::nullopt;
    return last_;
}

std::optional<prism::core::Point> Touch::after_error(esp_err_t err)
{
    ++errors_;
    if (!reported_) {
        reported_ = true;
        ESP_LOGE("touch", "reading the controller failed (%s): check the panel's ribbon cable; "
                          "after %d failures in a row the finger is released",
                 esp_err_to_name(err), kMaxErrors);
    }
    if (errors_ >= kMaxErrors)
        last_ = std::nullopt;
    return last_;
}

} // namespace korvo1
