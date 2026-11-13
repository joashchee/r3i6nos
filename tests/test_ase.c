/* test_ase.c: Commander's Aseprite reader (apps/commander-native/ase.h) on one file, for tests/test_ase.sh.
 *
 *   test_ase FILE            opens FILE, draws every frame, prints "frames N drawn M", exit 0 (2 when it is not read)
 *   test_ase --fuzz FILE     the same on FILE cut at every length, with every byte set to 0x00, 0xFF, 0x7F and 0x80 one at a time, and
 *                            with 3000 random damages of several bytes: nothing may crash or trip a sanitizer; prints the counts.
 * Built with -fsanitize=address,undefined, so a read past a buffer or an overflow is a failure. */
#include <stdio.h>
#include "../apps/commander-native/ase.h"

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = (uint8_t *)malloc((size_t)len + 1);
    if (!b || fread(b, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(b); return NULL; }
    fclose(f);
    *n = (size_t)len;
    return b;
}

/* Open and draw every frame of a private copy of the bytes (exactly n long, so ASan sees any read past the end). */
static int try_bytes(const uint8_t *src, size_t n, int *frames, int *drawn) {
    uint8_t *b = (uint8_t *)malloc(n ? n : 1);
    if (!b) return -1;
    memcpy(b, src, n);
    Ase a;
    char err[160];
    int rc = ase_open(&a, b, n, err, sizeof err);
    if (rc == 0) {
        *frames = a.nframes;
        *drawn = 0;
        for (int i = 0; i < a.nframes; i++) {
            FImage im;
            if (ase_render(&a, i, &im, err, sizeof err) == 0) { (*drawn)++; img_free(&im); }
        }
        ase_free(&a);
    }
    free(b);
    return rc;
}

int main(int argc, char **argv) {
    int fuzz = argc > 2 && !strcmp(argv[1], "--fuzz");
    if (argc < 2 + fuzz) return 1;
    size_t n;
    uint8_t *b = slurp(argv[1 + fuzz], &n);
    if (!b) return 1;
    int frames = 0, drawn = 0;
    if (!fuzz) {
        int rc = try_bytes(b, n, &frames, &drawn);
        free(b);
        if (rc != 0) { printf("not read\n"); return 2; }
        printf("frames %d drawn %d\n", frames, drawn);
        return 0;
    }
    long tries = 0, opened = 0;
    for (size_t len = 0; len <= n; len++) { tries++; if (try_bytes(b, len, &frames, &drawn) == 0) opened++; }
    uint8_t *d = (uint8_t *)malloc(n);
    static const uint8_t vals[] = {0x00, 0xFF, 0x7F, 0x80};
    for (size_t i = 0; i < n; i++)
        for (size_t v = 0; v < sizeof vals; v++) {
            memcpy(d, b, n);
            d[i] = vals[v];
            tries++;
            if (try_bytes(d, n, &frames, &drawn) == 0) opened++;
        }
    uint32_t seed = 12345;
    for (int k = 0; k < 3000; k++) {
        memcpy(d, b, n);
        seed = seed * 1664525u + 1013904223u;
        int hits = 1 + (int)(seed >> 28) % 6;
        for (int h = 0; h < hits; h++) {
            seed = seed * 1664525u + 1013904223u;
            size_t at = (size_t)(seed >> 8) % n;
            seed = seed * 1664525u + 1013904223u;
            d[at] = (uint8_t)(seed >> 16);
        }
        tries++;
        if (try_bytes(d, n, &frames, &drawn) == 0) opened++;
    }
    free(d);
    free(b);
    printf("tries %ld opened %ld\n", tries, opened);
    return 0;
}
