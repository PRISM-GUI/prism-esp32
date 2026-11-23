# Bluetooth Classic audio (A2DP source) and the headset as a PRISM audio device: the stack's
# callbacks, the link task that reconnects the headset, and the backend that start() registers.
# Moved from prism-mp3-player's board app (and its own btspike before that), so an app calls
# prism::esp32::bluetooth::start() and then plays on the "headset" device like on any other.
#
# Registered for every firmware, as main's own REQUIRES are: with CONFIG_BT_ENABLED off nothing
# references this archive, so it is built and never linked. Only the apps built with Bluetooth
# (sdkconfig.<app>) call start().
idf_component_register(
    SRCS bt_link.cpp bt_callbacks.cpp bt_bringup.cpp headset_device.cpp
    INCLUDE_DIRS .
    REQUIRES prism bt nvs_flash esp_timer freertos heap esp_psram
)
