# Layout constants shared by assemble_image.sh, check_image.sh and inject-boot.sh.
# Sourced, not run. Changing a value here changes images: say so in the CHANGELOG.

BOOT_START=32768                                  # sectors: 16 MiB; room for the bootloader before it
LOADER_SECTOR=64                                  # Rockchip idbloader (stock card confirmed: 2026-11-06)
DISK_SIGNATURE=0x52333669                         # MBR disk id
ROOT_UUID=52333669-0000-4000-8000-000000000002    # ext4 UUID of ROOTFS: boot.ini says root=UUID='...'
