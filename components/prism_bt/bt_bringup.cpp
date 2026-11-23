// Bluetooth bring-up: the controller and Bluedroid, then the profiles the headset needs, then the
// link task. Once per boot, from start().

#include "bt_link.hpp"

#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_heap_caps.h>
#include <esp_err.h>
#include <nvs_flash.h>

#include <cstddef>
#include <cstdio>

namespace prism::esp32::bluetooth::link {

namespace {

// IDF's default-config macros are C initializers that leave fields out: C++ warns, -Werror stops.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
void start_bluedroid()
{
    check("esp_bt_controller_mem_release", esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    check("esp_bt_controller_init", esp_bt_controller_init(&controller));
    check("esp_bt_controller_enable", esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    esp_bluedroid_config_t bluedroid = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    check("esp_bluedroid_init_with_cfg", esp_bluedroid_init_with_cfg(&bluedroid));
    check("esp_bluedroid_enable", esp_bluedroid_enable());
}
#pragma GCC diagnostic pop

// Bluedroid keeps the bonded headsets in NVS: without it a headset has to be paired again after
// every reset. Idempotent, so an app that uses NVS itself may have done it already.
void start_nvs()
{
    auto err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        check("nvs_flash_erase", nvs_flash_erase());
        err = nvs_flash_init();
    }
    check("nvs_flash_init", err);
}

void log_heap(const char* where)
{
    std::printf("bt: heap %s: internal free %zu, largest %zu\n", where,
                static_cast<std::size_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                static_cast<std::size_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
}

// Bluedroid's target accepts no passthrough command until told which: its default set is empty,
// so every headset button is refused as "not implemented" before our callback sees it (and a Sony
// then asks for the play status, PDU 0x30, which goes unanswered too).
void accept_remote_buttons()
{
    esp_avrc_psth_bit_mask_t buttons{};
    for (const auto key : {ESP_AVRC_PT_CMD_PLAY, ESP_AVRC_PT_CMD_PAUSE, ESP_AVRC_PT_CMD_STOP,
                           ESP_AVRC_PT_CMD_FORWARD, ESP_AVRC_PT_CMD_BACKWARD})
        esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &buttons, key);
    check("esp_avrc_tg_set_psth_cmd_filter",
          esp_avrc_tg_set_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_SUPPORTED_CMD, &buttons));
}

void start_profiles()
{
    check("esp_bt_gap_set_device_name", esp_bt_gap_set_device_name("PRISM"));
    check("esp_bt_gap_register_callback", esp_bt_gap_register_callback(gap_callback));
    check("esp_avrc_ct_init", esp_avrc_ct_init());
    check("esp_avrc_ct_register_callback", esp_avrc_ct_register_callback(avrc_ct_callback));
    check("esp_avrc_tg_init", esp_avrc_tg_init());
    check("esp_avrc_tg_register_callback", esp_avrc_tg_register_callback(avrc_tg_callback));
    accept_remote_buttons();
    check("esp_a2d_source_init", esp_a2d_source_init());
    check("esp_a2d_register_callback", esp_a2d_register_callback(a2d_callback));
    check("esp_a2d_source_register_data_callback",
          esp_a2d_source_register_data_callback(data_callback));
    // Connectable but not discoverable: a headset that knows us may come back by itself, while
    // nothing new can pair without the headset's pairing mode.
    check("esp_bt_gap_set_scan_mode",
          esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE));
}

} // namespace

// The queue first: the callbacks may post as soon as they are registered.
void start_bluetooth()
{
    events = xQueueCreate(kQueueDepth, sizeof(Event));
    start_nvs();
    start_bluedroid();
    log_heap("after bluedroid");
    start_profiles();
    start_link_task();   // the first attempt, now, and every retry from there
}

} // namespace prism::esp32::bluetooth::link
