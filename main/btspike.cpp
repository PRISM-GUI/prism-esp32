// Throwaway spike: does Bluetooth Classic audio (A2DP source + AVRCP) fit in internal RAM next to
// PRISM's panel? Connects to the first headset named WH-1000XM3, streams a 440 Hz tone, runs the
// widget demo, and prints the heap every 5 s. Based on ESP-IDF's classic_bt/a2dp_source example.

#include "app_support.hpp"
#include "band_display.hpp"
#include "profile_dump.hpp"
#include "touch.hpp"

#include <embedded/widgets_app.hpp>
#include <prism/platform/frame_app.hpp>

#include <esp_a2dp_api.h>
#include <esp_avrc_api.h>
#include <esp_bt.h>
#include <esp_bt_device.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <nvs_flash.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <string_view>

namespace {

enum class State { scanning, connecting, streaming };

constexpr std::string_view target_name = "WH-1000XM3";
constexpr float tone_hz = 440.0f;
constexpr float sample_rate_hz = 44100.0f;

std::atomic<State> state{State::scanning};
std::atomic<unsigned> underruns{0};
esp_bd_addr_t peer_address{};
esp_a2d_conn_hdl_t connection_handle = ESP_A2D_INVALID_CONN_HDL;
bool peer_found = false;

const char *state_name(State s)
{
    constexpr const char *names[] = {"scanning", "connecting", "streaming"};
    return names[static_cast<int>(s)];
}

void start_discovery()
{
    peer_found = false;
    state = State::scanning;
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
}

std::string_view name_of(const esp_bt_gap_cb_param_t::disc_res_param &found)
{
    for (int i = 0; i < found.num_prop; ++i) {
        const auto &p = found.prop[i];
        if (p.type != ESP_BT_GAP_DEV_PROP_EIR)
            continue;
        uint8_t length = 0;
        auto *eir = static_cast<uint8_t *>(p.val);
        auto *name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &length);
        if (!name)
            name = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &length);
        if (name)
            return {reinterpret_cast<const char *>(name), length};
    }
    return {};
}

void on_device_found(const esp_bt_gap_cb_param_t::disc_res_param &found)
{
    if (name_of(found).find(target_name) == std::string_view::npos)
        return;
    std::memcpy(peer_address, found.bda, ESP_BD_ADDR_LEN);
    peer_found = true;
    esp_bt_gap_cancel_discovery();
}

void on_discovery_stopped()
{
    if (!peer_found)
        return start_discovery();
    state = State::connecting;
    esp_a2d_source_connect(peer_address);
}

void gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT:
        if (state == State::scanning)
            on_device_found(param->disc_res);
        break;
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED)
            on_discovery_stopped();
        break;
    case ESP_BT_GAP_CFM_REQ_EVT:
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    default:
        break;
    }
}

void on_connection_state(const esp_a2d_cb_param_t::a2d_conn_stat_param &connection)
{
    if (connection.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        connection_handle = connection.conn_hdl;
        esp_a2d_media_ctrl_by_handle(connection_handle, ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
    }
    else if (connection.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
        start_discovery();
}

void on_media_ack(const esp_a2d_cb_param_t::media_ctrl_stat_param &ack)
{
    if (ack.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY && ack.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS)
        esp_a2d_media_ctrl_by_handle(connection_handle, ESP_A2D_MEDIA_CTRL_START);
}

void a2d_callback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        on_connection_state(param->conn_stat);
        break;
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
        on_media_ack(param->media_ctrl_stat);
        break;
    case ESP_A2D_AUDIO_STATE_EVT:
        if (param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED)
            state = State::streaming;
        break;
    default:
        break;
    }
}

// Interleaved 16-bit stereo, the same sample on both sides.
int32_t tone_callback(uint8_t *data, int32_t length)
{
    static float phase = 0.0f;
    if (!data || length <= 0) {
        ++underruns;
        return 0;
    }
    auto *frames = reinterpret_cast<int16_t *>(data);
    for (int32_t i = 0; i < length / 4; ++i) {
        const auto sample = static_cast<int16_t>(8000.0f * std::sin(phase));
        frames[2 * i] = frames[2 * i + 1] = sample;
        phase = std::fmod(phase + 2.0f * std::numbers::pi_v<float> * tone_hz / sample_rate_hz,
                          2.0f * std::numbers::pi_v<float>);
    }
    return length & ~3;
}

void ignore_avrc_ct(esp_avrc_ct_cb_event_t, esp_avrc_ct_cb_param_t *) {}
void ignore_avrc_tg(esp_avrc_tg_cb_event_t, esp_avrc_tg_cb_param_t *) {}

void init_flash()
{
    auto err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

// IDF's default-config macro is a C initializer that leaves fields out: C++ warns, -Werror stops.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
void start_bluedroid()
{
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&controller);
    esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    esp_bluedroid_config_t bluedroid = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    esp_bluedroid_init_with_cfg(&bluedroid);
    esp_bluedroid_enable();
}
#pragma GCC diagnostic pop

void start_profiles()
{
    esp_bt_gap_set_device_name("PRISM_BTSPIKE");
    esp_bt_gap_register_callback(gap_callback);
    esp_avrc_ct_init();
    esp_avrc_ct_register_callback(ignore_avrc_ct);
    esp_avrc_tg_init();
    esp_avrc_tg_register_callback(ignore_avrc_tg);
    esp_a2d_source_init();
    esp_a2d_register_callback(a2d_callback);
    esp_a2d_source_register_data_callback(tone_callback);
    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
}

void print_status(void *)
{
    std::printf("btspike: %s internal free %zu, largest block %zu, psram free %zu, underruns %u\n",
                state_name(state),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM), underruns.load());
}

void start_status_line()
{
    static const esp_timer_create_args_t args{.callback = print_status, .arg = nullptr,
                                              .dispatch_method = ESP_TIMER_TASK, .name = "btspike",
                                              .skip_unhandled_events = false};
    esp_timer_handle_t timer;
    esp_timer_create(&args, &timer);
    esp_timer_start_periodic(timer, 5'000'000);
}

void start_bluetooth()
{
    init_flash();
    start_bluedroid();
    korvo1::print_internal_heap("after bluedroid");
    start_profiles();
    start_discovery();
    start_status_line();
}

} // namespace

extern "C" void app_main()
{
    auto display = korvo1::BandDisplay::open();
    if (!display)
        korvo1::halt(display.error());
    korvo1::Touch input = korvo1::open_touch_or_none();

    start_bluetooth();

    static prism_examples::WidgetsModel model;
    model.title.set({.value = "PRISM + Bluetooth", .heading = true});
    prism::platform::frame_app(*display, input, model, [](prism::platform::FrameContext &ctx) {
        prism_examples::attach_widgets(ctx, model);
        ctx.on_stats(korvo1::print_stats);
        ctx.on_loop(korvo1::dump_profile_on_play());
    }, korvo1::pinned_threads());
    korvo1::halt_runtime_stopped();
}
