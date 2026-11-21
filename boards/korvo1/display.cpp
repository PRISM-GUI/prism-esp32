#include "display.hpp"

#include "rgb565.hpp"

#include <prism/profile/profile.hpp>

#include "bsp/esp32_s31_korvo_1.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "sdkconfig.h"

#include <algorithm>
#include <thread>

extern "C" esp_lcd_rgb_timing_t korvo1_bsp_panel_timing(void); // bsp_timing.c

namespace korvo1 {

namespace {

constexpr std::size_t kPixels = std::size_t(Display::kWidth) * Display::kHeight;
constexpr std::size_t kPanelBufferBytes = kPixels * sizeof(std::uint16_t);
// A frame at 18 MHz takes ~28 ms: a flip not seen in 100 ms is lost, not worth freezing for.
constexpr TickType_t kFlipTimeout = pdMS_TO_TICKS(100);

#ifdef CONFIG_PRISM_DISPLAY_CPU_CONVERT
constexpr bool kCpuConvert = true;
#else
constexpr bool kCpuConvert = false;
#endif

#ifdef CONFIG_BSP_LCD_RGB_BOUNCE_BUFFER_MODE
constexpr std::size_t kBouncePixels = Display::kWidth * CONFIG_BSP_LCD_RGB_BOUNCE_BUFFER_HEIGHT;
#else
constexpr std::size_t kBouncePixels = 0;
#endif

std::string failure(const char *what, esp_err_t err, const char *hint)
{
    return std::string(what) + " (" + esp_err_to_name(err) + "): " + hint;
}

// The panel's interrupt: the driver now reads the buffer last passed to draw_bitmap.
IRAM_ATTR bool on_flipped(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *,
                          void *flipped)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(flipped), &woken);
    return woken == pdTRUE;
}

// The BSP's panel (pins, timing, PSRAM buffers, bounce-buffer setting), with two frame buffers
// and our pixel clock: bsp_display_new() fixes both at one buffer and 18 MHz.
esp_lcd_rgb_panel_config_t panel_config()
{
    esp_lcd_rgb_panel_config_t config{};
    config.clk_src = LCD_CLK_SRC_PLL160M;
    config.timings = korvo1_bsp_panel_timing();
    config.timings.pclk_hz = CONFIG_PRISM_LCD_PCLK_MHZ * 1000 * 1000;
    config.data_width = 16;
    config.in_color_format = LCD_COLOR_FMT_RGB565;
    config.num_fbs = 2;
    config.bounce_buffer_size_px = kBouncePixels;
    config.dma_burst_size = 64;
    config.hsync_gpio_num = BSP_LCD_RGB_HSYNC;
    config.vsync_gpio_num = BSP_LCD_RGB_VSYNC;
    config.de_gpio_num = BSP_LCD_RGB_DE;
    config.pclk_gpio_num = BSP_LCD_RGB_PCLK;
    config.disp_gpio_num = BSP_LCD_RGB_DISP;
    std::ranges::copy(std::array{BSP_LCD_RGB_DATA0, BSP_LCD_RGB_DATA1, BSP_LCD_RGB_DATA2,
                                 BSP_LCD_RGB_DATA3, BSP_LCD_RGB_DATA4, BSP_LCD_RGB_DATA5,
                                 BSP_LCD_RGB_DATA6, BSP_LCD_RGB_DATA7, BSP_LCD_RGB_DATA8,
                                 BSP_LCD_RGB_DATA9, BSP_LCD_RGB_DATA10, BSP_LCD_RGB_DATA11,
                                 BSP_LCD_RGB_DATA12, BSP_LCD_RGB_DATA13, BSP_LCD_RGB_DATA14,
                                 BSP_LCD_RGB_DATA15},
                      config.data_gpio_nums);
    config.flags.fb_in_psram = 1;
    return config;
}

// The callbacks, then reset and init: the panel starts scanning buffer 0, all black.
esp_err_t start_panel(esp_lcd_panel_handle_t panel, SemaphoreHandle_t flipped)
{
    esp_lcd_rgb_panel_event_callbacks_t callbacks{};
    callbacks.on_frame_buf_complete = on_flipped;
    esp_err_t err = esp_lcd_rgb_panel_register_event_callbacks(panel, &callbacks, flipped);
    if (err == ESP_OK)
        err = esp_lcd_panel_reset(panel);
    if (err == ESP_OK)
        err = esp_lcd_panel_init(panel);
    return err;
}

std::array<std::uint16_t *, 2> panel_buffers(esp_lcd_panel_handle_t panel)
{
    void *buffers[2] = {};
    esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &buffers[0], &buffers[1]);
    return {static_cast<std::uint16_t *>(buffers[0]), static_cast<std::uint16_t *>(buffers[1])};
}

ppa_srm_oper_config_t srm_config(const std::uint32_t *frame, std::uint16_t *panel_buffer)
{
    ppa_srm_oper_config_t config{};
    config.in.buffer = frame;
    config.in.pic_w = config.in.block_w = Display::kWidth;
    config.in.pic_h = config.in.block_h = Display::kHeight;
    config.in.srm_cm = PPA_SRM_COLOR_MODE_ARGB8888;
    config.out.buffer = panel_buffer;
    config.out.buffer_size = kPanelBufferBytes;
    config.out.pic_w = Display::kWidth;
    config.out.pic_h = Display::kHeight;
    config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    config.scale_x = config.scale_y = 1.f;
    config.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
    config.mode = PPA_TRANS_MODE_BLOCKING;
    return config;
}

ppa_fill_oper_config_t fill_config(std::uint32_t *frame, std::uint32_t argb)
{
    ppa_fill_oper_config_t config{};
    config.out.buffer = frame;
    config.out.buffer_size = kPixels * sizeof(std::uint32_t);
    config.out.pic_w = Display::kWidth;
    config.out.pic_h = Display::kHeight;
    config.out.fill_cm = PPA_FILL_COLOR_MODE_ARGB8888;
    config.fill_block_w = Display::kWidth;
    config.fill_block_h = Display::kHeight;
    config.fill_color_val = argb;
    config.mode = PPA_TRANS_MODE_BLOCKING;
    return config;
}

std::uint32_t *alloc_frame()
{
    // The DMA capability makes the heap align to the cache line, as the PPA reads it by DMA.
    auto *frame = static_cast<std::uint32_t *>(heap_caps_aligned_alloc(
        CONFIG_CACHE_L1_DCACHE_LINE_SIZE, kPixels * sizeof(std::uint32_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA));
    if (frame)
        std::fill_n(frame, kPixels, 0xFF000000u);
    return frame;
}

ppa_client_handle_t ppa_client(ppa_operation_t operation)
{
    ppa_client_config_t config{};
    config.oper_type = operation;
    config.max_pending_trans_num = 1;
    ppa_client_handle_t client = nullptr;
    return ppa_register_client(&config, &client) == ESP_OK ? client : nullptr;
}

} // namespace

void display_detail::HeapFree::operator()(std::uint32_t *p) const { heap_caps_free(p); }
void display_detail::SemaphoreDelete::operator()(SemaphoreHandle_t s) const { vSemaphoreDelete(s); }
void display_detail::PpaUnregister::operator()(ppa_client_handle_t c) const { ppa_unregister_client(c); }
void display_detail::PanelDelete::operator()(esp_lcd_panel_handle_t p) const { esp_lcd_panel_del(p); }

std::expected<Display, std::string> Display::open()
{
    if (!esp_psram_is_initialized())
        return std::unexpected(std::string("no PSRAM found: is this a WROOM-3 module? The display "
                                           "needs 3 MB of PSRAM"));
    Display display;
    for (Frame &frame : display.frames_) {
        frame.reset(alloc_frame());
        if (!frame)
            return std::unexpected(std::string("no room in PSRAM for the two 1.5 MB frames: is "
                                               "the module's PSRAM 16 MB?"));
    }

    display.flipped_.reset(xSemaphoreCreateBinary());
    display.converted_.reset(xSemaphoreCreateCounting(2, 0));
    display.to_show_.reset(xQueueCreate(1, sizeof(int)));
    if (!display.flipped_ || !display.converted_ || !display.to_show_)
        return std::unexpected(std::string("no memory for the display's semaphores"));

    if (!kCpuConvert) {
        display.ppa_.reset(ppa_client(PPA_OPERATION_SRM));
        if (!display.ppa_)
            return std::unexpected(std::string("PPA client registration failed: build with "
                                               "CONFIG_PRISM_DISPLAY_CPU_CONVERT=y to convert "
                                               "on the CPU instead"));
        display.filler_.reset(ppa_client(PPA_OPERATION_FILL));   // none: the CPU clears
    }

    const esp_lcd_rgb_panel_config_t config = panel_config();
    esp_lcd_panel_handle_t panel = nullptr;
    if (const esp_err_t err = esp_lcd_new_rgb_panel(&config, &panel); err != ESP_OK)
        return std::unexpected(failure("RGB panel creation failed", err,
                                       "its two 750 KB buffers need 1.5 MB of free PSRAM; "
                                       "is the module's PSRAM 16 MB?"));
    display.panel_.reset(panel);
    if (const esp_err_t err = start_panel(panel, display.flipped_.get()); err != ESP_OK)
        return std::unexpected(failure("RGB panel start failed", err,
                                       "reset the board; if it persists, report it"));
    display.panel_buffers_ = panel_buffers(panel);

    // One conversion now, so a PPA that refuses this frame says so here rather than every frame.
    if (const esp_err_t err = display.convert_into(display.frames_[0].get(), display.panel_buffers_[1]);
        err != ESP_OK)
        return std::unexpected(failure("frame conversion failed", err,
                                       "build with CONFIG_PRISM_DISPLAY_CPU_CONVERT=y to convert "
                                       "on the CPU instead"));
    if (const esp_err_t err = esp_lcd_panel_disp_on_off(panel, true); err != ESP_OK)
        return std::unexpected(failure("panel power-on failed", err,
                                       "reset the board; if it persists, report it"));
    return display;
}

std::span<std::uint32_t> Display::back() { return {frames_[draw_].get(), kPixels}; }

bool Display::clear(std::uint32_t argb)
{
    if (!filler_)
        return false;
    const ppa_fill_oper_config_t config = fill_config(frames_[draw_].get(), argb);
    return ppa_do_fill(filler_.get(), &config) == ESP_OK;   // failed: the CPU clears
}

// Hands the frame to the present task and returns: the task converts it while the render thread
// draws the next one, into the other frame, once that one's conversion is over.
//
// simplify: the present task runs for as long as the board does; a Display is never destroyed
// once it has shown a frame (frame_app does not return on this board). Upgrade, if one is ever
// torn down: a stop message on `to_show_`, and a join.
void Display::present()
{
    if (!presenter_started_) {
        std::thread([this] { present_loop(); }).detach();
        presenter_started_ = true;
    }
    xQueueSend(to_show_.get(), &draw_, portMAX_DELAY);
    draw_ ^= 1;
    if (frames_in_flight_ == 0) {
        frames_in_flight_ = 1;   // the first frame: the other one was never handed over
        return;
    }
    PRISM_PROFILE_SCOPE("wait for convert");
    xSemaphoreTake(converted_.get(), portMAX_DELAY);
}

void Display::present_loop()
{
    prism::profile::name_thread("present");
    for (;;) {
        int frame = 0;
        xQueueReceive(to_show_.get(), &frame, portMAX_DELAY);
        show(frames_[frame].get());
    }
}

// The wait for the panel comes before writing, not after showing: the panel leaves `target` only
// when it switches to the buffer the last show() handed it.
//
// simplify: if the panel's interrupt fires between the stale-signal drain and the driver arming
// the switch (its cache write-back of the buffer included), `flipped_` is given for the old
// buffer, and the next show() writes into the buffer the panel still reads: one torn frame.
// In both modes: the link-switch event without bounce buffers, the frame wrap with them.
// Upgrade: have the interrupt report which buffer it switched to.
void Display::show(const std::uint32_t *frame)
{
    std::uint16_t *target = panel_buffers_[back_index_];
    if (flip_pending_) {
        PRISM_PROFILE_SCOPE("wait for panel");
        xSemaphoreTake(flipped_.get(), kFlipTimeout);
        flip_pending_ = false;
    }
    esp_err_t converted;
    {
        PRISM_PROFILE_SCOPE("convert");
        converted = convert_into(frame, target);
    }
    xSemaphoreGive(converted_.get());   // the render thread may draw into `frame` again
    report_first_failure(converted);
    if (converted != ESP_OK)
        return;   // the buffer was not written: keep showing the last good one
    xSemaphoreTake(flipped_.get(), 0);
    // A buffer of the panel's own: the driver only writes the cache back and switches to it.
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_lcd_panel_draw_bitmap(panel_.get(), 0, 0, kWidth, kHeight, target));
    flip_pending_ = true;
    back_index_ ^= 1;
}

void Display::report_first_failure(esp_err_t err)
{
    if (err == ESP_OK || conversion_failed_)
        return;
    conversion_failed_ = true;
    ESP_LOGE("display", "%s",
             failure("frame conversion failed", err,
                     "build with CONFIG_PRISM_DISPLAY_CPU_CONVERT=y to convert on the CPU "
                     "instead; later failures are not reported")
                 .c_str());
}

esp_err_t Display::convert_into(const std::uint32_t *frame, std::uint16_t *panel_buffer)
{
    if constexpr (kCpuConvert) {
        argb8888_to_rgb565({frame, kPixels}, {panel_buffer, kPixels});
        return ESP_OK;
    } else {
        const ppa_srm_oper_config_t config = srm_config(frame, panel_buffer);
        return ppa_do_scale_rotate_mirror(ppa_.get(), &config);
    }
}

} // namespace korvo1
