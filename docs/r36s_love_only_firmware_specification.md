# Minimalist LÖVE & Native Debian Custom Firmware for R36S

A minimal, dedicated firmware specification for RK3326-based R36S handhelds. This OS strips away legacy emulation platforms to run exclusively LÖVE framework games (v11.5 Stable & v12.0 Nightly) and native ARM64 Debian applications.

---

## 1. Scope & Target Hardware Validation

Before flashing or installing, confirm the target board is a genuine-style RK3326 R36S device.

### Hardware Target Specifications
- **SoC:** Rockchip RK3326 (Quad-Core ARM Cortex-A35 @ 1.5 GHz)
- **GPU:** ARM Mali-G31 MP2
- **RAM:** 1 GB DDR3 / DDR3L
- **Display:** 3.5-inch 640×480 IPS panel (KMS/DRM video driver)
- **Storage:** Dual microSD (TF1: OS, TF2 / EASYROMS: Apps & Games)

### Hardware Identification Workflow
1. Insert the original OS microSD into a PC.
2. Check the boot partition:
   - **Supported (Genuine RK3326):** Contains `rk3326-r35s-linux.dtb` or `rk3326-rg351mp-linux.dtb`; partition labeled `EASYROMS`.
   - **Incompatible Clones:** Contains `rk3326-evb-lp3-v12-linux.dtb`, `rf3536k4ka.dtb`, or references `sunxi` / `eGON.BT0` / `Allwinner A33`. Do **not** use standard RK3326 firmware builds on Allwinner clones.

---

## 2. Directory Structure on ROM Partition (`EASYROMS`)

User interaction is simplified to dropping files into three designated folders on the secondary microSD card:

```text
/roms/ (EASYROMS)
├── Love 11.5/       # Drop .love files or project directories for LÖVE 11.5 (Stable)
├── Love 12.0/       # Drop .love files or project directories for LÖVE 12.0 (Nightly)
└── native_deb/      # Drop .deb ARM64 Debian packages for automatic extraction & launching
```

---

## 3. Core Software Stack & Execution Engine

### Pre-installed Engine Runtimes
Isolated LÖVE engine runtimes are stored on the OS partition:
- **LÖVE 11.5 (Stable):** `/opt/love/11.5/bin/love`
- **LÖVE 12.0 (Nightly):** `/opt/love/12.0/bin/love`
- **Extracted Native Apps:** `/opt/native_apps/<app-name>/`

---

## 4. Automation & Launcher Scripts

### A. Automatic `.deb` Package Watcher & Installer
Save to `/usr/local/bin/deb_watcher.py` (`chmod +x`). On boot, this script extracts newly placed ARM64 `.deb` files directly to `/opt/native_apps/` without requiring full `dpkg` database locks, and builds executable launcher shortcuts.

```python
#!/usr/bin/env python3
"""
deb_watcher.py — Extract native .deb packages and generate app launchers.
"""

import os
import subprocess
from pathlib import Path

DEB_DIR = Path("/roms/native_deb")
APP_ROOT = Path("/opt/native_apps")
LAUNCHER_DIR = Path("/opt/launchers/native")

def process_debs():
    APP_ROOT.mkdir(parents=True, exist_ok=True)
    LAUNCHER_DIR.mkdir(parents=True, exist_ok=True)
    
    if not DEB_DIR.exists():
        return

    for deb in DEB_DIR.glob("*.deb"):
        app_name = deb.stem
        app_dest = APP_ROOT / app_name
        
        # Extract package if not already installed
        if not app_dest.exists():
            app_dest.mkdir(parents=True, exist_ok=True)
            try:
                subprocess.run(["dpkg-deb", "-x", str(deb), str(app_dest)], check=True)
            except subprocess.CalledProcessError:
                continue

        # Locate executable binary
        binaries = (
            list((app_dest / "usr" / "bin").glob("*")) +
            list((app_dest / "usr" / "games").glob("*")) +
            list(app_dest.glob("bin/*"))
        )

        if binaries:
            executable = binaries[0]
            launcher_script = LAUNCHER_DIR / f"{app_name}.sh"
            content = f"""#!/bin/bash
export DISPLAY=:0
cd "{app_dest}"
exec "{executable}" "$@"
"""
            launcher_script.write_text(content)
            launcher_script.chmod(0o755)

if __name__ == "__main__":
    process_debs()
```

---

### B. Unified Execution Wrapper
Save to `/usr/local/bin/run_app.sh` (`chmod +x`). Sets up standard SDL gamepad mappings for the R36S handheld controls and boots the targeted binary or LÖVE framework version.

```bash
#!/bin/bash
# Universal Launcher Script for R36S LÖVE OS
# Usage: run_app.sh [love115|love120|native] [path_to_game_or_binary]

TYPE="$1"
TARGET="$2"

if [ -z "$TYPE" ] || [ -z "$TARGET" ]; then
    echo "Usage: $0 [love115|love120|native] [path_to_target]"
    exit 1
fi

# R36S Built-in Gamepad Mapping for SDL2
export SDL_GAMECONTROLLERCONFIG="03000000341200006048000000010000,R36S Handheld,a:b1,b:b0,x:b3,y:b2,leftshoulder:b4,rightshoulder:b5,back:b6,start:b7,dpup:h0.1,dpdown:h0.2,dpleft:h0.4,dpright:h0.8,platform:Linux,"

case "$TYPE" in
    love115)
        LOVE_BIN="/opt/love/11.5/bin/love"
        gpTOkeyb "love" -c "/etc/love_keys.cfg" &
        "$LOVE_BIN" "$TARGET"
        killall -9 gptokeyb
        ;;
    love120)
        LOVE_BIN="/opt/love/12.0/bin/love"
        gpTOkeyb "love" -c "/etc/love_keys.cfg" &
        "$LOVE_BIN" "$TARGET"
        killall -9 gptokeyb
        ;;
    native)
        if [ -f "$TARGET" ]; then
            exec "$TARGET"
        fi
        ;;
    *)
        echo "Unknown application type: $TYPE"
        exit 1
        ;;
esac
```

---

### C. System Boot Initialization Script
Save to `/etc/init.d/S99loveos` (`chmod +x`). Initializes the display stack, processes newly added `.deb` packages, and directly launches the minimalist UI frontend without loading unnecessary background emulation services.

```bash
#!/bin/bash
### BEGIN INIT INFO
# Provides:          loveos
# Required-Start:    $local_fs $syslog
# Default-Start:     5
# Description:       Boot straight to LÖVE OS menu
### END INIT INFO

export DISPLAY=:0
export SDL_NOMOUSE=1

# Scan and extract any new .deb packages dropped in /roms/native_deb
python3 /usr/local/bin/deb_watcher.py

# Launch minimalist game/app launcher UI
exec /usr/local/bin/loveos_menu
```

---

## 5. Architectural Advantages & Resource Optimization

1. **Minimal Memory Footprint:** Bypasses bloated frontends like EmulationStation and RetroArch, preserving maximum RAM (1 GB available on RK3326) for LÖVE games and native applications.
2. **Deterministic Runtimes:** Completely isolates LÖVE 11.5 and 12.0 dependencies to avoid version conflicts.
3. **No Database Dependencies:** Games and apps are automatically scanned on boot directly from standard folders.
4. **Legal Redistribution:** Uses open-source Linux foundations (such as dArkOS) and avoids bundling copyrighted ROMs, proprietary BIOS files, or non-commercial emulation assets.