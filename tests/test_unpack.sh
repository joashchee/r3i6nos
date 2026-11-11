#!/bin/bash
# shellcheck disable=SC2317,SC2329  # the gen_* functions are called by name, through mk
# test_unpack.sh: Commander's own xz and zstd decoders (apps/commander-native/lzma.h, zstd.h) against the real xz and zstd, on
# inputs of every kind: text, binaries, random data, one repeated byte, empty and tiny files, big files (many blocks and chunks),
# every compression level, and files cut short or damaged (which must fail or cut, never crash: built with the sanitizers).
set -u
cd "$(dirname "$0")/.." || exit 1
if ! command -v xz >/dev/null || ! command -v zstd >/dev/null; then echo "NOT RUN: needs xz and zstd"; exit 0; fi
T="$(mktemp -d "${TMPDIR:-/tmp}/r3i6nos-unpack.XXXXXX")"
trap 'rm -rf "$T"' EXIT
cc -std=c11 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -o "$T/unpack" tests/test_unpack.c || exit 1
fail=0; n=0

mk() { # name, generator
    "$2" > "$T/$1"
}
gen_text() { for i in $(seq 1 4000); do echo "line $i: the quick brown fox jumps over the lazy dog $((i * 7919 % 1000))"; done; }
gen_c() { cat apps/commander-native/*.h apps/commander-native/commander.c; }
gen_bin() { head -c 300000 /usr/bin/ssh 2>/dev/null || head -c 300000 /bin/ls; }
gen_rand() { head -c 200000 /dev/urandom; }
gen_same() { head -c 500000 /dev/zero; }
gen_empty() { :; }
gen_one() { printf 'x'; }
gen_tiny() { echo "hello hello hello hello world"; }
gen_small() { gen_text 2>/dev/null | head -c 700; }
gen_kb() { gen_c 2>/dev/null | head -c 20000; }
gen_mixed() { gen_text; head -c 50000 /dev/urandom; gen_text; gen_bin; }
gen_big() { for i in 1 2 3 4 5 6 7 8; do gen_c; gen_bin; done; }
for g in text c bin rand same empty one tiny small kb mixed big; do mk "in_$g" "gen_$g"; done
python3 -I - "$T" <<'P'
import sys, os, random
t = sys.argv[1]
r = random.Random(7)
# long-range matches, short matches, a skewed alphabet (Huffman literals) and lots of small repeats
open(t + "/in_skew", "wb").write(bytes(r.choice(b"aaaaaaaabbbbccd e") for _ in range(400000)))
words = [bytes(r.choice(b"abcdefghij") for _ in range(r.randint(2, 9))) for _ in range(300)]
open(t + "/in_words", "wb").write(b" ".join(r.choice(words) for _ in range(120000)))
P

check() { # label, packed file, original file, kind, [cap]
    n=$((n + 1))
    "$T/unpack" "$4" "$2" > "$T/out" 2> "$T/err"; rc=$?
    if [ $rc -ne 0 ] || ! cmp -s "$T/out" "$3"; then echo "FAIL: $1 (rc $rc) $(head -c 200 "$T/err")"; fail=1; fi
}
for f in "$T"/in_*; do
    b="$(basename "$f")"
    for lvl in 0 1 3 6 9; do
        xz -$lvl -k -c "$f" > "$T/p.xz" && check "xz -$lvl $b" "$T/p.xz" "$f" xz
    done
    xz -1 --check=crc64 -k -c "$f" > "$T/p.xz" && check "xz crc64 $b" "$T/p.xz" "$f" xz
    xz -1 --check=sha256 -k -c "$f" > "$T/p.xz" && check "xz sha256 $b" "$T/p.xz" "$f" xz
    xz -1 --check=none -k -c "$f" > "$T/p.xz" && check "xz nocheck $b" "$T/p.xz" "$f" xz
    xz -3 --block-size=65536 -k -c "$f" > "$T/p.xz" && check "xz many blocks $b" "$T/p.xz" "$f" xz
    xz --lzma2=preset=2,lc=0,lp=2,pb=0 -k -c "$f" > "$T/p.xz" && check "xz lc0 lp2 pb0 $b" "$T/p.xz" "$f" xz
    xz --lzma2=preset=2,lc=4,lp=0,pb=4 -k -c "$f" > "$T/p.xz" && check "xz lc4 pb4 $b" "$T/p.xz" "$f" xz
    cat "$T/p.xz" "$T/p.xz" > "$T/p2.xz"; cat "$f" "$f" > "$T/two"; check "xz two streams $b" "$T/p2.xz" "$T/two" xz
    for lvl in 1 3 9 19; do
        zstd -q -$lvl -c "$f" > "$T/p.zst" && check "zstd -$lvl $b" "$T/p.zst" "$f" zst
    done
    zstd -q -3 --no-check -c "$f" > "$T/p.zst" && check "zstd nocheck $b" "$T/p.zst" "$f" zst
    zstd -q -3 --zstd=wlog=10 -c "$f" > "$T/p.zst" && check "zstd small window $b" "$T/p.zst" "$f" zst
    zstd -q --fast=5 -c "$f" > "$T/p.zst" && check "zstd fast $b" "$T/p.zst" "$f" zst
    zstd -q -3 --single-thread -B4096 -c "$f" > "$T/p.zst" && check "zstd 4k blocks $b" "$T/p.zst" "$f" zst
    zstd -q -3 -c "$f" > "$T/p.zst"; cat "$T/p.zst" "$T/p.zst" > "$T/p2.zst"; check "zstd two frames $b" "$T/p2.zst" "$T/two" zst
done

# a cap cuts the output at the cap (exit 3) and the part written is the start of the file
for kind in xz zst; do
    f="$T/in_mixed"
    if [ $kind = xz ]; then xz -3 --block-size=100000 -c "$f" > "$T/p"; else zstd -q -3 -c "$f" > "$T/p"; fi
    n=$((n + 1))
    "$T/unpack" $kind "$T/p" 70000 > "$T/out" 2>/dev/null; rc=$?
    if [ $rc -ne 3 ] || [ "$(wc -c < "$T/out")" -ne 70000 ] || ! cmp -s "$T/out" <(head -c 70000 "$f"); then echo "FAIL: $kind cap (rc $rc)"; fail=1; fi
done

# damaged and cut-short files: any answer but a crash or a sanitizer report
for kind in xz zst; do
    f="$T/in_c"
    if [ $kind = xz ]; then xz -3 -c "$f" > "$T/good"; else zstd -q -3 -c "$f" > "$T/good"; fi
    size=$(wc -c < "$T/good")
    for cut in 0 1 5 11 12 20 $((size / 3)) $((size / 2)) $((size - 20)) $((size - 1)); do
        if [ "$cut" -eq 0 ]; then : > "$T/bad"; else head -c "$cut" "$T/good" > "$T/bad"; fi; n=$((n + 1))
        "$T/unpack" $kind "$T/bad" > /dev/null 2> "$T/err"; rc=$?
        if grep -q "Sanitizer\|runtime error" "$T/err"; then echo "FAIL: $kind cut at $cut: $(head -3 "$T/err")"; fail=1; fi
    done
    for i in $(seq 1 150); do
        python3 -I - "$T/good" "$T/bad" "$i" <<'P'
import sys, random
d = bytearray(open(sys.argv[1], "rb").read())
r = random.Random(int(sys.argv[3]))
for _ in range(r.choice([1, 1, 2, 5])):
    d[r.randrange(len(d))] = r.randrange(256)
open(sys.argv[2], "wb").write(d)
P
        n=$((n + 1))
        "$T/unpack" $kind "$T/bad" > /dev/null 2> "$T/err"; rc=$?
        if grep -q "Sanitizer\|runtime error" "$T/err"; then echo "FAIL: $kind damaged #$i: $(head -3 "$T/err")"; fail=1; fi
    done
done

if [ $fail -eq 0 ]; then echo "ok: $n xz and zstd cases match the real tools (and damaged files never crash)"; fi
exit $fail
