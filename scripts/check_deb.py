#!/usr/bin/env python3
"""check_deb.py <file.deb>: read a .deb with the standard library only (an ar archive of control.tar.* and data.tar.*) and refuse one that
is not safe to put in the image: no Package line, no copyright file, or a copyright file that still carries a DRAFT GRANT marker.
Prints the package name on success. Used by fetch_app_debs.sh on the debs it downloads (they are built in other repos)."""
import io
import sys
import tarfile


def ar_members(data):
    if data[:8] != b"!<arch>\n":
        raise ValueError("not an ar archive (not a .deb)")
    pos, out = 8, {}
    while pos + 60 <= len(data):
        name = data[pos:pos + 16].decode().strip().rstrip("/")
        size = int(data[pos + 48:pos + 58].decode().strip())
        out[name] = data[pos + 60:pos + 60 + size]
        pos += 60 + size + (size & 1)
    return out


def tar_of(members, prefix):
    for name, blob in members.items():
        if name.startswith(prefix):
            return tarfile.open(fileobj=io.BytesIO(blob))     # gz, xz and bz2 are read by tarfile
    raise ValueError(f"no {prefix}* in the .deb")


def check(path):
    members = ar_members(open(path, "rb").read())
    control = tar_of(members, "control.tar")
    f = next((control.extractfile(m) for m in control.getmembers() if m.name.lstrip("./") == "control"), None)
    if f is None:
        raise ValueError("no control file")
    package = next((l.split(":", 1)[1].strip() for l in f.read().decode().splitlines() if l.startswith("Package:")), "")
    if not package:
        raise ValueError("no Package line")
    data = tar_of(members, "data.tar")
    texts = [data.extractfile(m).read().decode("utf-8", "replace") for m in data.getmembers()
             if m.isfile() and m.name.lstrip("./").startswith("usr/share/doc/") and m.name.endswith("/copyright")]
    if not texts or not any(t.strip() for t in texts):
        raise ValueError("no usr/share/doc/*/copyright: a package must ship its licence")
    if any("DRAFT GRANT" in t for t in texts):
        raise ValueError("its copyright still has a DRAFT GRANT marker")
    return package


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: check_deb.py <file.deb>")
    try:
        print(check(sys.argv[1]))
    except (ValueError, OSError, tarfile.TarError) as e:
        sys.exit(f"check_deb: {sys.argv[1]}: {e}")
