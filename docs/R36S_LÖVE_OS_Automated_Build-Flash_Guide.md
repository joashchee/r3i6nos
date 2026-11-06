# Automated Build and Flashing Workflow for R36S LÖVE OS

This guide details the end-to-end automated pipeline to build and flash a minimal, legally compliant, Debian-based LÖVE framework operating system for the R36S handheld.

## Workflow Overview

```
 ┌────────────────────────────────┐
 │     Claude / Developer         │ (Edits scripts & OS configuration)
 └───────────────┬────────────────┘
                 │ git push
                 ▼
 ┌────────────────────────────────┐
 │    GitHub Actions (CI/CD)      │ (Debootstraps Debian ARM64 rootfs & packages raw .img)
 └───────────────┬────────────────┘
                 │ Download r36s-loveos-base.img.gz
                 ▼
 ┌────────────────────────────────┐
 │     Local MacBook Terminal     │ (Injects legal/hardware .dtb assets & flashes to SD)
 └────────────────────────────────┘
```

---

## 1. Repository Setup

Create the public repository structure on your build host or local machine:

```text
my-r36s-loveos/
├── .github/
│   └── workflows/
│       └── build.yml
├── scripts/
│   ├── build_rootfs.sh
│   └── assemble_image.sh
├── local_hardware_assets/   # Gitignored local directory for working SD files
│   └── boot/                # .dtb files, uEnv.txt, boot.ini from working R36S SD
└── .gitignore
```

### `.gitignore` configuration

Ensure proprietary board configurations or local raw images are not pushed to public repositories:

```text
local_hardware_assets/
*.img
*.img.gz
*.iso
```

---

## 2. GitHub Actions CI Workflow

Save to `.github/workflows/build.yml`. This handles automated debootstrapping and disk image creation in the cloud.

```yaml
name: Build R36S LoveOS Base Image

on:
  push:
    branches: [ "main", "master" ]
  workflow_dispatch:

jobs:
  build-image:
    runs-on: ubuntu-latest

    steps:
      - name: Checkout Code
        uses: actions/checkout@v4

      - name: Install Build Tools
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            debootstrap \
            qemu-user-static \
            binfmt-support \
            parted \
            dosfstools \
            e2fsprogs \
            rsync

      - name: Build Rootfs & Assemble Image
        run: |
          chmod +x ./scripts/*.sh
          sudo ./scripts/build_rootfs.sh
          sudo ./scripts/assemble_image.sh

      - name: Compress Image Artifact
        run: |
          gzip -k r36s-loveos-custom.img
          sha256sum r36s-loveos-custom.img.gz > r36s-loveos-custom.img.gz.sha256

      - name: Upload Build Artifacts
        uses: actions/upload-artifact@v4
        with:
          name: loveos-base-image
          path: |
            r36s-loveos-custom.img.gz
            r36s-loveos-custom.img.gz.sha256
```

---

## 3. Build Scripts

### A. Rootfs Debootstrap Script (`scripts/build_rootfs.sh`)

```bash
#!/bin/bash
set -e

ROOTFS_DIR="./r36s-debian-rootfs"
echo "[*] Debootstrapping Debian Bookworm ARM64..."

mkdir -p "$ROOTFS_DIR"

debootstrap \
  --arch=arm64 \
  --variant=minbase \
  --include=systemd,systemd-sysv,udev,kmod,sudo,bash-completion,python3,libsdl2-2.0-0,libsdl2-image-2.0-0,libsdl2-mixer-2.0-0,libsdl2-ttf-2.0-0 \
  bookworm \
  "$ROOTFS_DIR" \
  https://deb.debian.org/debian/

echo "[*] Rootfs created successfully."
```

### B. Image Assembly Script (`scripts/assemble_image.sh`)

```bash
#!/bin/bash
set -e

IMG_NAME="r36s-loveos-custom.img"
ROOTFS_DIR="./r36s-debian-rootfs"

echo "[*] Creating 4GB disk container..."
dd if=/dev/zero of="$IMG_NAME" bs=1M count=4096 status=progress

echo "[*] Partitioning MBR layout..."
parted "$IMG_NAME" --script mklabel msdos
parted "$IMG_NAME" --script mkpart primary fat32 16MiB 528MiB
parted "$IMG_NAME" --script mkpart primary ext4 528MiB 100%

LOOP_DEV=$(losetup --find --show --partscan "$IMG_NAME")
BOOT_PART="${LOOP_DEV}p1"
ROOT_PART="${LOOP_DEV}p2"

mkfs.vfat -F 32 -n "BOOT" "$BOOT_PART"
mkfs.ext4 -L "ROOTFS" "$ROOT_PART"

mkdir -p /mnt/r36s_boot /mnt/r36s_root
mount "$BOOT_PART" /mnt/r36s_boot
mount "$ROOT_PART" /mnt/r36s_root

echo "[*] Copying rootfs..."
rsync -aAXv "$ROOTFS_DIR/" /mnt/r36s_root/

# Inject local boot assets if building locally
if [ -d "./local_hardware_assets/boot" ]; then
    echo "[*] Injecting hardware boot assets..."
    cp -rv ./local_hardware_assets/boot/* /mnt/r36s_boot/
fi

umount /mnt/r36s_boot
umount /mnt/r36s_root
losetup -d "$LOOP_DEV"
rmdir /mnt/r36s_boot /mnt/r36s_root

echo "[*] Image ready: $IMG_NAME"
```

---

## 4. Local macOS Flashing Workflow

Follow these terminal steps on your MacBook to complete the hardware flash process using your working SD card assets.

### Step 1: Backup Boot Assets from Working SD Card
Insert your existing, working R36S SD card into your Mac and copy its boot files:

```bash
# Locate your mounted working SD card boot volume
mkdir -p ./local_hardware_assets/boot
cp -rv /Volumes/BOOT/* ./local_hardware_assets/boot/
```

### Step 2: Download & Extract Cloud Artifact
Download `loveos-base-image.img.gz` from the GitHub Actions release tab into your project folder and extract it:

```bash
gunzip -k loveos-base-image.img.gz
```

### Step 3: Inject Your Working DTB/Boot Files into the Image (Optional step if mounting locally on macOS)
Alternatively, flash the raw image directly, then copy your backed-up `.dtb` files directly into the FAT32 `BOOT` partition of the new SD card using Finder or macOS Terminal.

### Step 4: Flash Image to Target MicroSD Card via Terminal

1. Identify your target MicroSD card disk identifier:
   ```bash
   diskutil list
   ```
   *(Identify your SD card disk number, e.g., `/dev/disk4`).*

2. Unmount the SD card:
   ```bash
   diskutil unmountDisk /dev/diskN
   ```
   *(Replace `N` with your actual disk number).*

3. Write the image using raw disk speed access (`rdisk`):
   ```bash
   sudo dd if=r36s-loveos-custom.img of=/dev/rdiskN bs=1M status=progress
   ```

4. Eject the formatted card:
   ```bash
   diskutil eject /dev/diskN
   ```

5. Insert the newly flashed MicroSD card into TF1 on your R36S and power on.