#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace prism::esp32::bluetooth {

// The headset's own buttons, as far as the profile carries them: play/pause/stop all mean one
// thing here, and the volume keys are the headset's own.
enum class Remote { play_pause, next, previous };

// Brings Bluetooth up and registers the "headset" device: the bonded headset when there is one,
// otherwise a scan for the first device whose name contains CONFIG_PRISM_BT_HEADSET_NAME. From
// then on the link reconnects by itself, and the device is listed by prism::audio::devices().
//
// Calling it twice is harmless: the second call finds everything up.
void start();

// Play, pause, next and previous from the headset. Called on the Bluetooth task: post it to the
// app's own thread, never touch a UI from here.
void on_remote(std::function<void(Remote)> handler);

// "Off", "Scanning…", "Connecting…", "Connected to <name>": what a settings screen shows.
std::string state_text();

// A number that changes when state_text() would say something else: compare it instead of
// building the string (and taking a lock) every frame.
std::uint32_t state_revision();

} // namespace prism::esp32::bluetooth
