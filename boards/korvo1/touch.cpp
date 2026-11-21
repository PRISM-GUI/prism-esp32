#include "touch.hpp"

#include "bsp/touch.h"
#include "esp_err.h"

namespace korvo1 {

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

// The BSP configures the GT1151 with the panel's 800x480 and no swap or mirror, and the driver
// applies none: its coordinates are already the panel's.
std::optional<prism::core::Point> Touch::read()
{
    if (!handle_ || esp_lcd_touch_read_data(handle_.get()) != ESP_OK)
        return std::nullopt;
    esp_lcd_touch_point_data_t point{};
    std::uint8_t count = 0;
    if (esp_lcd_touch_get_data(handle_.get(), &point, &count, 1) != ESP_OK || count == 0)
        return std::nullopt;
    return prism::core::Point{prism::core::X{float(point.x)}, prism::core::Y{float(point.y)}};
}

} // namespace korvo1
