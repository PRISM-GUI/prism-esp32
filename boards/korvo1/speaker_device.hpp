#pragma once

namespace prism::esp32 {

// The Korvo-1's speaker as a PRISM audio device: `DeviceInfo{"speaker", "Speaker",
// {44100, 48000}, s16, hardware volume}`. The codec and the writer thread open on the first
// Output that plays on it and stay open; between Outputs the writer writes silence.
//
// Registering it twice is harmless: the second call finds it already registered.
void register_speaker();

} // namespace prism::esp32
