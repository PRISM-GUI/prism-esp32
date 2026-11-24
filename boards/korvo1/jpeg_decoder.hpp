#pragma once

// The ESP32-S31's JPEG engine as a PRISM image decoder, so covers and thumbnails come from the
// hardware decoder instead of a software one. Idempotent: call it once at boot, before the first
// picture is asked for.
namespace prism::esp32 {

void register_jpeg_decoder();

} // namespace prism::esp32
