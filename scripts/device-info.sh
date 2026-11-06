#!/bin/sh
# device-info.sh - run ON the R36S itself (over SSH or its terminal, with the
# stock OS booted) to record what the card cannot tell us: the kernel's real
# command line, the model, the input devices and the controller mapping.
#
#   ssh ark@<ip> 'sh -s' < scripts/device-info.sh > local_hardware_assets/info/device-info.txt
#
# Read-only: it only prints files and command output. POSIX sh, no extras.

section() { printf '\n== %s\n' "$1"; }
show() { if [ -r "$1" ]; then cat "$1" 2>/dev/null; echo; else echo "(no $1)"; fi; }

echo "r3i6nos device info, $(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null)"

section "model"
tr -d '\0' < /proc/device-tree/model 2>/dev/null; echo
tr -d '\0' < /proc/device-tree/compatible 2>/dev/null | tr '\n' ' '; echo

section "kernel"
uname -a
show /proc/cmdline

section "os-release"
show /etc/os-release

section "memory and cpu"
grep -E 'MemTotal|MemFree' /proc/meminfo 2>/dev/null
grep -E 'model name|Hardware|CPU part' /proc/cpuinfo 2>/dev/null | sort -u

section "mounts and block devices"
cat /proc/mounts 2>/dev/null
cat /proc/partitions 2>/dev/null

section "input devices (the gamepad's name, vendor and product: the SDL GUID comes from these)"
show /proc/bus/input/devices
ls -l /dev/input/by-path 2>/dev/null

section "controller mapping the stock frontend uses"
for f in /etc/emulationstation/es_input.cfg /home/ark/.emulationstation/es_input.cfg; do
    if [ -r "$f" ]; then echo "-- $f"; cat "$f"; fi
done
for f in /etc/sdl_controllers.txt /opt/inttools/sdl_controllers.txt; do
    [ -r "$f" ] && { echo "-- $f"; cat "$f"; }
done

section "graphics and display"
ls -l /dev/dri 2>/dev/null
for f in /sys/class/drm/*/modes /sys/class/drm/*/status; do
    [ -r "$f" ] && { echo "-- $f"; cat "$f"; }
done
show /sys/class/graphics/fb0/virtual_size

section "audio"
cat /proc/asound/cards 2>/dev/null
show /proc/asound/devices

section "loaded modules"
cut -d' ' -f1 /proc/modules 2>/dev/null | sort | tr '\n' ' '; echo

section "boot messages that matter (mmc, mali/panfrost, rk817, panel, joypad)"
dmesg 2>/dev/null | grep -i -E 'mmc[0-9]|mali|panfrost|rk817|rk3326|panel|joypad|adc-joystick|pwm-backlight|drm' | head -80

echo
echo "done"
