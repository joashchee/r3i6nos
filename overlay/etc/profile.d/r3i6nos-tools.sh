# shellcheck shell=sh
# Command-line tools installed from native_deb (see deb_watcher.py) are commands in an SSH login too.
case ":$PATH:" in *:/opt/loveos/bin:*) ;; *) PATH="/opt/loveos/bin:$PATH"; export PATH ;; esac
