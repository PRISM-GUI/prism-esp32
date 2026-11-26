#pragma once

// The Korvo-1's two microphones as PRISM's capture device: one device, "mic"/"Microphones", at
// 16 kHz, mono, f32, default. A thread of its own reads blocks of stereo frames from the codec
// (the BSP's esp_codec_dev), averages them to mono and pushes them to the open Input.

namespace korvo1 {

// Starts the codec and registers the microphones as PRISM's capture device. False, with the reason
// printed, when the codec does not start: nothing is registered then, and an app that wants a
// microphone (PRISM's scope) falls back to its own synthetic signal by itself.
bool register_mic_capture();

} // namespace korvo1
