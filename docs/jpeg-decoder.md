# The JPEG decoder

The board's chip has a JPEG engine. `register_jpeg_decoder()` makes it PRISM's image decoder, so
album covers and thumbnails come from hardware instead of a software decoder.

    #include "jpeg_decoder.hpp"      // boards/korvo1

    prism::esp32::register_jpeg_decoder();           // once, at boot

After that, `prism::render::decode_image()` finds it like any other registered decoder. There is
no call into the engine from the app.

## What it answers

The engine is asked for RGB888 and the PRISM image is produced in the format the caller asked
for, `rgb565` when the target is this panel:

    const auto image = prism::render::decode_image(
        bytes, {.fit = std::nullopt, .format = prism::render::PixelFormat::rgb565});

- `fit` empty means the picture's own size. A box scales it down, or up to the box, keeping the
  aspect ratio.
- The cap is 1600x1600 pixels, 7.7 MB once decoded to RGB888. A larger picture is refused and the
  caller falls back to whatever it shows instead.
- The picture is decoded whole before it is scaled. That is the reason for the cap: an upgrade
  would decode in strips, or ask the engine for a scaled output where it can.

The engine writes whole MCUs, so its rows and columns round up to 16. The decoded view skips the
padding, and the scaling step reads it in RGB888.

## One engine, many threads

There is one JPEG engine, so decodes are serialised behind a mutex inside the decoder, whichever
thread PRISM asks from. A decode can still take a few milliseconds: the engine's timeout is
200 ms.

## When it refuses

Each failure comes back as the decoder's error string, which `decode_image()` returns:

| Error | Cause |
|---|---|
| `not a JPEG` | the bytes do not start with `FF D8` |
| `no JPEG engine` | the engine could not be created |
| `cannot read the JPEG header` | the header parser refused the file |
| `empty JPEG` | width or height is 0 |
| `JPEG too large` | over 1600x1600 |
| `out of memory for the picture` | no PSRAM left for the RGB888 copy |
| `the JPEG engine could not decode it` | the engine failed on the data |
| `cannot scale the image` | the scaling step failed |

## Registering it

`korvo1_start()` calls `register_jpeg_decoder()` (boards/korvo1/port.cpp), so every firmware on
this board has the hardware decoder. Nothing in this repository's firmwares decodes a picture
today; a `prism::render::decode_image()` call gets the engine unchanged.

The test picture that used to live in the tone app (a 64x64 baseline JPEG, left half red, right
half blue, `px0=0xF800` in RGB565) went with that app; a check again would be a new board tool.
See [apps.md](apps.md).
