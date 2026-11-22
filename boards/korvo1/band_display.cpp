#include "band_display.hpp"

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
#include <cstring>

extern "C" esp_lcd_rgb_timing_t korvo1_bsp_panel_timing(void); // bsp_timing.c

namespace korvo1 {

namespace {

constexpr std::size_t kPanelPixels = std::size_t(kPanelWidth) * kPanelHeight;
constexpr std::size_t kPanelBufferBytes = kPanelPixels * sizeof(std::uint16_t);
constexpr std::size_t kBandPixels = std::size_t(BandDisplay::kWidth) * BandDisplay::kBandHeight;
constexpr std::size_t kBandKiB = kBandPixels * sizeof(std::uint16_t) / 1024;
// A frame at 18 MHz takes ~28 ms: a flip not seen in 100 ms is lost, not worth freezing for.
constexpr TickType_t kFlipTimeout = pdMS_TO_TICKS(100);

#ifdef CONFIG_PRISM_DISPLAY_CPU_COPY
constexpr bool kCpuCopy = true;
#else
constexpr bool kCpuCopy = false;
#endif

#ifdef CONFIG_BSP_LCD_RGB_BOUNCE_BUFFER_MODE
constexpr std::size_t kBouncePixels = kPanelWidth * CONFIG_BSP_LCD_RGB_BOUNCE_BUFFER_HEIGHT;
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

// The PPA's interrupt: a band's copy is done, its buffer may be drawn into again.
IRAM_ATTR bool on_copied(ppa_client_handle_t, ppa_event_data_t *, void *copied)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(copied), &woken);
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

// The PPA turns a picture counter-clockwise (ppa_types.h): a board turned clockwise needs its
// picture turned the other way, a quarter counter-clockwise.
constexpr ppa_srm_rotation_angle_t ppa_angle(Rotation r)
{
    switch (r) {
    case Rotation::clockwise: return PPA_SRM_ROTATION_ANGLE_90;
    case Rotation::counter_clockwise: return PPA_SRM_ROTATION_ANGLE_270;
    case Rotation::none: break;
    }
    return PPA_SRM_ROTATION_ANGLE_0;
}

// The band, RGB565 as it is, into the block of the panel buffer that shows frame rows
// [first_row, first_row + rows): a run of rows, or of columns when the frame is turned. The
// driver writes the band's cache back itself before the PPA reads it (ppa_srm.c).
ppa_srm_oper_config_t srm_config(const std::uint16_t *band, int rows, std::uint16_t *panel_buffer,
                                 int first_row, SemaphoreHandle_t copied)
{
    const PanelBlock block = band_on_panel(first_row, rows, kRotation);
    ppa_srm_oper_config_t config{};
    config.in.buffer = band;
    config.in.pic_w = config.in.block_w = BandDisplay::kWidth;
    config.in.pic_h = BandDisplay::kBandHeight;
    config.in.block_h = rows;
    config.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    config.out.buffer = panel_buffer;
    config.out.buffer_size = kPanelBufferBytes;
    config.out.pic_w = kPanelWidth;
    config.out.pic_h = kPanelHeight;
    config.out.block_offset_x = block.x;
    config.out.block_offset_y = block.y;
    config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
    config.rotation_angle = ppa_angle(kRotation);
    config.scale_x = config.scale_y = 1.f;
    config.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
    config.mode = PPA_TRANS_MODE_NON_BLOCKING;
    config.user_data = copied;
    return config;
}

// The CPU's copy: unturned, the band's stride is the panel's, so its rows are one contiguous run
// there too; turned, pixel by pixel.
void cpu_copy(const std::uint16_t *band, int first_row, int rows, std::uint16_t *panel_buffer)
{
    if constexpr (kRotation == Rotation::none) {
        std::memcpy(panel_buffer + std::size_t(first_row) * kPanelWidth, band,
                    std::size_t(rows) * kPanelWidth * sizeof(std::uint16_t));
    } else {
        for (int row = 0; row < rows; ++row)
            for (int x = 0; x < BandDisplay::kWidth; ++x) {
                const Pixel at = to_panel({x, first_row + row}, kRotation);
                panel_buffer[std::size_t(at.y) * kPanelWidth + at.x] =
                    band[std::size_t(row) * BandDisplay::kWidth + x];
            }
    }
}

std::uint16_t *alloc_band()
{
    // The DMA capability makes the heap align to the cache line, as the PPA reads it by DMA.
    auto *band = static_cast<std::uint16_t *>(heap_caps_aligned_alloc(
        CONFIG_CACHE_L1_DCACHE_LINE_SIZE, kBandPixels * sizeof(std::uint16_t),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (band)
        std::fill_n(band, kBandPixels, std::uint16_t{0});
    return band;
}

SemaphoreHandle_t free_band_semaphore()
{
    SemaphoreHandle_t s = xSemaphoreCreateBinary();
    if (s)
        xSemaphoreGive(s);   // no copy in flight yet
    return s;
}

// Two pending copies: one per band, so the render thread never waits on the queue itself.
ppa_client_handle_t ppa_copier()
{
    ppa_client_config_t config{};
    config.oper_type = PPA_OPERATION_SRM;
    config.max_pending_trans_num = 2;
    ppa_client_handle_t client = nullptr;
    if (ppa_register_client(&config, &client) != ESP_OK)
        return nullptr;
    ppa_event_callbacks_t callbacks{};
    callbacks.on_trans_done = on_copied;
    if (ppa_client_register_event_callbacks(client, &callbacks) != ESP_OK) {
        ppa_unregister_client(client);
        return nullptr;
    }
    return client;
}

} // namespace

void display_detail::HeapFree::operator()(std::uint16_t *p) const { heap_caps_free(p); }
void display_detail::SemaphoreDelete::operator()(SemaphoreHandle_t s) const { vSemaphoreDelete(s); }
// simplify: a BandDisplay going away does not wait for PPA copies in flight; frame_app never
// returns on this board, so it never goes away mid-copy. Upgrade: take both band semaphores
// before unregistering.
void display_detail::PpaUnregister::operator()(ppa_client_handle_t c) const { ppa_unregister_client(c); }
void display_detail::PanelDelete::operator()(esp_lcd_panel_handle_t p) const { esp_lcd_panel_del(p); }

std::expected<BandDisplay, std::string> BandDisplay::open()
{
    if (!esp_psram_is_initialized())
        return std::unexpected(std::string("no PSRAM found: is this a WROOM-3 module? The display "
                                           "needs 1.5 MB of PSRAM"));
    BandDisplay display;
    for (Band &band : display.bands_) {
        band.reset(alloc_band());
        if (!band)
            return std::unexpected("no room in internal SRAM for the two " +
                                   std::to_string(kBandKiB) + " KB bands: lower "
                                   "CONFIG_PRISM_BAND_HEIGHT");
    }

    display.flipped_.reset(xSemaphoreCreateBinary());
    for (Semaphore &copied : display.copied_)
        copied.reset(free_band_semaphore());
    if (!display.flipped_ || !display.copied_[0] || !display.copied_[1])
        return std::unexpected(std::string("no memory for the display's semaphores"));

    if (!kCpuCopy) {
        display.ppa_.reset(ppa_copier());
        if (!display.ppa_)
            return std::unexpected(std::string("PPA client registration failed: build with "
                                               "CONFIG_PRISM_DISPLAY_CPU_COPY=y to copy "
                                               "on the CPU instead"));
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

    // One copy now, so a PPA that refuses a band says so here rather than every frame.
    display.take_band(0);
    if (const esp_err_t err = display.copy(0, 0, kBandHeight); err != ESP_OK)
        return std::unexpected(failure("band copy failed", err,
                                       "build with CONFIG_PRISM_DISPLAY_CPU_COPY=y to copy "
                                       "on the CPU instead"));
    display.take_band(0);
    if (const esp_err_t err = esp_lcd_panel_disp_on_off(panel, true); err != ESP_OK)
        return std::unexpected(failure("panel power-on failed", err,
                                       "reset the board; if it persists, report it"));
    return display;
}

std::span<std::uint16_t> BandDisplay::band(int index)
{
    take_band(index);
    return {bands_[index].get(), kBandPixels};
}

// The first band of a frame waits for the panel to leave the buffer it is copied into: the
// panel leaves it only when it switches to the buffer the last present() handed it.
void BandDisplay::flush(int index, int first_row, int rows)
{
    if (first_row == 0)
        wait_for_panel();
    report_first_failure(copy(index, first_row, rows));
}

// simplify: if the panel's interrupt fires between the stale-signal drain and the driver arming
// the switch (its cache write-back of the buffer included), `flipped_` is given for the old
// buffer, and the next frame's first band is copied into the buffer the panel still reads: one
// torn frame. In both modes: the link-switch event without bounce buffers, the frame wrap with
// them. Upgrade: have the interrupt report which buffer it switched to.
void BandDisplay::present()
{
    take_band(0);
    take_band(1);
    xSemaphoreTake(flipped_.get(), 0);
    // A buffer of the panel's own: the driver only writes the cache back and switches to it.
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_lcd_panel_draw_bitmap(panel_.get(), 0, 0, kPanelWidth, kPanelHeight,
                                  panel_buffers_[back_index_]));
    flip_pending_ = true;
    back_index_ ^= 1;
}

void BandDisplay::take_band(int index)
{
    if (held_[index])
        return;
    PRISM_PROFILE_SCOPE("wait for band");
    xSemaphoreTake(copied_[index].get(), portMAX_DELAY);
    held_[index] = true;
}

// Hands the band back: `copied_` is given once the copy is done, or now if it never started.
esp_err_t BandDisplay::copy(int index, int first_row, int rows)
{
    held_[index] = false;
    if constexpr (kCpuCopy) {
        cpu_copy(bands_[index].get(), first_row, rows, panel_buffers_[back_index_]);
        xSemaphoreGive(copied_[index].get());
        return ESP_OK;
    } else {
        const ppa_srm_oper_config_t config = srm_config(
            bands_[index].get(), rows, panel_buffers_[back_index_], first_row, copied_[index].get());
        const esp_err_t err = ppa_do_scale_rotate_mirror(ppa_.get(), &config);
        if (err != ESP_OK)
            xSemaphoreGive(copied_[index].get());
        return err;
    }
}

void BandDisplay::wait_for_panel()
{
    if (!flip_pending_)
        return;
    PRISM_PROFILE_SCOPE("wait for panel");
    xSemaphoreTake(flipped_.get(), kFlipTimeout);
    flip_pending_ = false;
}

// simplify: a failed band copy leaves that band's rows stale in the shown frame (only the first
// failure is logged). Upgrade: a per-frame failed flag that makes present() skip draw_bitmap.
void BandDisplay::report_first_failure(esp_err_t err)
{
    if (err == ESP_OK || copy_failed_)
        return;
    copy_failed_ = true;
    ESP_LOGE("display", "%s",
             failure("band copy failed", err,
                     "build with CONFIG_PRISM_DISPLAY_CPU_COPY=y to copy on the CPU "
                     "instead; later failures are not reported")
                 .c_str());
}

} // namespace korvo1
