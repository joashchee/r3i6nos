/* test_unpack.c: unpacks a .xz or .zst file with Commander's own decoders (lzma.h, zstd.h) to stdout, for tests/test_unpack.sh to compare
 * with the real xz and zstd. Usage: test_unpack xz|zst FILE [CAP]. Exit 0; 2 when the decoder says the file is damaged; 3 when cut at CAP. */
#include <stdio.h>
#include "../apps/commander-native/lzma.h"
#include "../apps/commander-native/zstd.h"

int main(int argc, char **argv) {
    if (argc < 3) return 1;
    FILE *f = fopen(argv[2], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *in = malloc((size_t)n + 1);
    if (!in || fread(in, 1, (size_t)n, f) != (size_t)n) return 1;
    fclose(f);
    size_t cap = argc > 3 ? (size_t)strtoull(argv[3], NULL, 10) : (size_t)64 << 20;
    uint8_t *out = malloc(cap + FI_XZ_SLACK + 1);
    if (!out) return 1;
    int cut = 0;
    long got = !strcmp(argv[1], "xz") ? fi_unxz(in, (size_t)n, out, cap, &cut) : fi_unzstd(in, (size_t)n, out, cap, &cut);
    if (got < 0) { fprintf(stderr, "damaged\n"); free(in); free(out); return 2; }
    fwrite(out, 1, (size_t)got, stdout);
    free(in);
    free(out);
    return cut ? 3 : 0;
}
