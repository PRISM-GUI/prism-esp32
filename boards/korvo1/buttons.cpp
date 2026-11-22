#include "buttons.hpp"

#include "esp_err.h"
#include "iot_button.h"

#include <array>

namespace korvo1 {

namespace {

void on_press_down(void *, void *down)
{
    static_cast<std::atomic<bool> *>(down)->store(true);
}

std::unexpected<std::string> failure(const char *what, esp_err_t err)
{
    return std::unexpected(std::string(what) + ": " + esp_err_to_name(err));
}

struct BspButtons {
    std::array<button_handle_t, BSP_BUTTON_NUM> handles{};
    esp_err_t err = ESP_OK;
};

// The BSP creates all four buttons together, and a second creation fails ("The button_index has
// been used"): they are created once, by the first open().
const BspButtons &bsp_buttons()
{
    static const BspButtons buttons = [] {
        BspButtons b;
        b.err = bsp_iot_button_create(b.handles.data(), nullptr, b.handles.size());
        return b;
    }();
    return buttons;
}

} // namespace

std::expected<Button, std::string> Button::open(bsp_button_t which)
{
    const auto &buttons = bsp_buttons();
    if (buttons.err)
        return failure("buttons", buttons.err);
    auto down = std::make_unique<std::atomic<bool>>(false);
    if (const esp_err_t err = iot_button_register_cb(buttons.handles[which], BUTTON_PRESS_DOWN,
                                                     nullptr, on_press_down, down.get()))
        return failure("button callback", err);
    return Button{std::move(down)};
}

} // namespace korvo1
