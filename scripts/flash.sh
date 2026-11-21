#!/bin/bash
# Flash one merged image to the Korvo-1: scripts/flash.sh <image.bin> [port]
# Finds the board's serial port itself (Linux: by the USB bridge's VID:PID; macOS: by name).
# Test hooks: PRISM_FLASH_PORTS replaces the port search, PRISM_ESPTOOL replaces esptool.
set -uo pipefail

# The Korvo-1 guide does not name its USB-UART bridge chip, so match the usual ones:
# CP210x, CH343, CH340, FTDI.
bridges=(10c4:ea60 1a86:55d3 1a86:7523 0403:6001 0403:6010 0403:6015)

usage() { echo "usage: $0 <image.bin> [port]" >&2; exit 2; }
die() { echo "$*" >&2; exit 1; }

find_esptool() {
    local tool
    for tool in "${PRISM_ESPTOOL:-}" esptool.py esptool; do
        [[ -n $tool ]] && command -v "$tool" && return
    done
    echo "esptool is not installed." >&2
    echo "Install it with:  pip install esptool    (then run this script again)" >&2
    return 1
}

usb_id_of_tty() {
    local dir
    dir=$(readlink -f "/sys/class/tty/$(basename "$1")/device" 2>/dev/null) || return
    while [[ -n $dir && $dir != / && ! -e $dir/idVendor ]]; do dir=$(dirname "$dir"); done
    [[ -e $dir/idVendor ]] && echo "$(<"$dir/idVendor"):$(<"$dir/idProduct")"
}

is_bridge() { [[ " ${bridges[*]} " == *" $1 "* ]]; }

linux_ports() {
    local tty
    for tty in /dev/ttyUSB* /dev/ttyACM*; do
        [[ -e $tty ]] && is_bridge "$(usb_id_of_tty "$tty")" && echo "$tty"
    done
}

mac_ports() {
    ls /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* /dev/cu.usbmodem* 2>/dev/null
}

candidate_ports() {
    if [[ -v PRISM_FLASH_PORTS ]]; then tr ' ' '\n' <<<"$PRISM_FLASH_PORTS" | grep .
    elif [[ $(uname) == Darwin ]]; then mac_ports
    else linux_ports
    fi
}

pick_port() {
    local ports
    mapfile -t ports < <(candidate_ports)
    case ${#ports[@]} in
        0) die "no ESP32 board found: is the UART USB-C port (not the power one) plugged in?" ;;
        1) echo "${ports[0]}" ;;
        *) printf 'several boards found:\n' >&2; printf '  %s\n' "${ports[@]}" >&2
           die "run again with the one you want: $0 $image [port]" ;;
    esac
}

[[ $# -ge 1 && $# -le 2 ]] || usage
image=$1
[[ -f $image ]] || die "image not found: $image (build it with scripts/build.sh, or download it from the releases)"
esptool=$(find_esptool) || exit 1
port=${2:-$(pick_port)} || exit 1
echo "flashing $image to $port"
exec "$esptool" --chip esp32s31 -p "$port" -b 921600 write_flash 0x0 "$image"
