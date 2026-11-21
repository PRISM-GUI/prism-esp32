#!/bin/bash
# Checks scripts/flash.sh against a fake esptool and fake port lists. Run: scripts/test_flash.sh
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
printf '#!/bin/sh\necho "$@" > "%s/args"\n' "$tmp" > "$tmp/esptool.py"
chmod +x "$tmp/esptool.py"
touch "$tmp/img.bin"
failures=0

# macOS ships bash 3.2 and no newer shell: flash.sh is POSIX sh, run under the plainest sh here.
shell=$(command -v dash || command -v sh)
bash4_only='mapfile|readarray|declare -A|\$\{[a-z_]+(,,|\^\^)\}|\[\[ -v|&>>|\|&'
if grep -E "$bash4_only|\[\[|=\(" "$here/flash.sh" | grep -v '^#'; then
    echo "FAIL flash.sh uses a bash-only construct"; failures=$((failures+1))
else echo "PASS flash.sh has no bash-only constructs"; fi

run() { PRISM_ESPTOOL="$tmp/esptool.py" PRISM_FLASH_PORTS="$1" "$shell" "$here/flash.sh" "${@:2}" 2>&1; }
check() {
    if [[ $2 == *"$3"* ]]; then echo "PASS $1"; else echo "FAIL $1: wanted '$3', got '$2'"; failures=$((failures+1)); fi
}

rm -f "$tmp/args"; out=$(run "" "$tmp/img.bin"); rc=$?
check "no board: message" "$out" "no ESP32 board found: is the UART USB-C port (not the power one) plugged in?"
check "no board: exit 1" "rc=$rc" "rc=1"
check "no board: esptool not run" "$([ -e "$tmp/args" ] && echo ran || echo idle)" idle

out=$(run "/dev/ttyUSB0" "$tmp/img.bin"); rc=$?
check "one board: exit 0" "rc=$rc" "rc=0"
check "one board: command" "$(cat "$tmp/args")" "--chip esp32s31 -p /dev/ttyUSB0 -b 921600 write_flash 0x0 $tmp/img.bin"

rm -f "$tmp/args"; out=$(run "/dev/ttyUSB0 /dev/ttyUSB1" "$tmp/img.bin"); rc=$?
check "two boards: exit 1" "rc=$rc" "rc=1"
check "two boards: lists them" "$out" "/dev/ttyUSB1"
check "two boards: asks for [port]" "$out" "[port]"
check "two boards: esptool not run" "$([ -e "$tmp/args" ] && echo ran || echo idle)" idle

out=$(run "/dev/ttyUSB0 /dev/ttyUSB1" "$tmp/img.bin" /dev/ttyUSB1); rc=$?
check "explicit port wins" "$(cat "$tmp/args")" "-p /dev/ttyUSB1 "

out=$(run "/dev/ttyUSB0" "$tmp/missing.bin"); rc=$?
check "missing image: exit 1" "rc=$rc" "rc=1"

out=$(run "/dev/ttyUSB0"); rc=$?
check "no image: usage" "$out" "usage:"

[[ $failures -eq 0 ]] && echo "all passed" || { echo "$failures failed"; exit 1; }
