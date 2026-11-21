#include "display.hpp"

#include "rgb565.hpp"

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
    // The DMA capability makes the heap align to the cache line, as the PPA reads it by DMA.
    display.frame_.reset(static_cast<std::uint32_t *>(heap_caps_aligned_alloc(
        CONFIG_CACHE_L1_DCACHE_LINE_SIZE, kPixels * sizeof(std::uint32_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA)));
    if (!display.frame_)
        return std::unexpected(std::string("no room in PSRAM for the 1.5 MB frame: is the "
                                           "module's PSRAM 16 MB?"));
    std::fill_n(display.frame_.get(), kPixels, 0xFF000000u);

    display.flipped_.reset(xSemaphoreCreateBinary());
    if (!display.flipped_)
        return std::unexpected(std::string("no memory for the display's semaphore"));

    if (!kCpuConvert) {
        ppa_client_config_t ppa_config{};
        ppa_config.oper_type = PPA_OPERATION_SRM;
        ppa_config.max_pending_trans_num = 1;
        ppa_client_handle_t ppa = nullptr;
        if (const esp_err_t err = ppa_register_client(&ppa_config, &ppa); err != ESP_OK)
            return std::unexpected(failure("PPA client registration failed", err,
                                           "build with CONFIG_PRISM_DISPLAY_CPU_CONVERT=y to "
                                           "convert on the CPU instead"));
        display.ppa_.reset(ppa);
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
    if (const esp_err_t err = display.convert_into(display.panel_buffers_[1]); err != ESP_OK)
        return std::unexpected(failure("frame conversion failed", err,
                                       "build with CONFIG_PRISM_DISPLAY_CPU_CONVERT=y to convert "
                                       "on the CPU instead"));
    if (const esp_err_t err = esp_lcd_panel_disp_on_off(panel, true); err != ESP_OK)
        return std::unexpected(failure("panel power-on failed", err,
                                       "reset the board; if it persists, report it"));
    return display;
}

std::span<std::uint32_t> Display::back() { return {frame_.get(), kPixels}; }

// simplify: if the panel's interrupt fires between the stale-signal drain and the driver arming
// the switch (its cache write-back of the buffer included), `flipped_` is given for the old
// buffer, and that one present() returns while the panel still reads what is now back(): one
// torn frame. In both modes: the link-switch event without bounce buffers, the frame wrap with
// them. Upgrade: have the interrupt report which buffer it switched to.
void Display::present()
{
    std::uint16_t *target = panel_buffers_[back_index_];
    const esp_err_t converted = convert_into(target);
    report_first_failure(converted);
    if (converted != ESP_OK)
        return;   // the buffer was not written: keep showing the last good one
    xSemaphoreTake(flipped_.get(), 0);
    // A buffer of the panel's own: the driver only writes the cache back and switches to it.
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_lcd_panel_draw_bitmap(panel_.get(), 0, 0, kWidth, kHeight, target));
    xSemaphoreTake(flipped_.get(), kFlipTimeout);
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

esp_err_t Display::convert_into(std::uint16_t *panel_buffer)
{
    if constexpr (kCpuConvert) {
        argb8888_to_rgb565({frame_.get(), kPixels}, {panel_buffer, kPixels});
        return ESP_OK;
    } else {
        const ppa_srm_oper_config_t config = srm_config(frame_.get(), panel_buffer);
        return ppa_do_scale_rotate_mirror(ppa_.get(), &config);
    }
}

} // namespace korvo1
