# Bluetooth Classic audio (A2DP source) and the headset as a PRISM audio device: the stack's
# callbacks, the link task that reconnects the headset, and the backend that start() registers.
# Moved from prism-mp3-player's board app (and its own btspike before that), so an app calls
# prism::esp32::bluetooth::start() and then plays on the "headset" device like on any other.
#
# Every firmware requires this component (main's REQUIRES are shared), but only the apps built
# with Bluetooth (sdkconfig.<app> with CONFIG_BT_ENABLED) compile its sources: without the stack
# the A2DP headers do not exist. The others get the header only, and must not call start().
# Only the sources depend on the config: IDF resolves REQUIRES before it reads sdkconfig.
set(srcs)
if(CONFIG_BT_ENABLED)
    set(srcs bt_link.cpp bt_callbacks.cpp bt_bringup.cpp headset_device.cpp)
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS .
    REQUIRES prism bt nvs_flash esp_timer freertos heap esp_psram
)
