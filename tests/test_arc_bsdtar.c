/* test_arc_bsdtar.c: Commander's bsdtar members (apps/commander-native/arc_members.h, ARC_BSD) on one archive, for tests/test_arc_bsdtar.sh.
 * Usage: test_arc_bsdtar KIND FILE   (KIND: 7z, bz2, iso, rar, cab, lzip). Prints one line per member, "type size name", then the bytes
 * of every plain file as "== name" and the text. Exit 0; 2 when the archive was not listed (no bsdtar, or not an archive). */
#include <SDL.h>
#include "r3i6nos_ansiapps.h"
#include "../apps/commander-native/formats.h"

int main(int argc, char **argv) {
    if (argc < 3) return 1;
    int kind = !strcmp(argv[1], "7z") ? K_7Z : !strcmp(argv[1], "bz2") ? K_BZIP2 : !strcmp(argv[1], "iso") ? K_ISO
             : !strcmp(argv[1], "rar") ? K_RAR : !strcmp(argv[1], "cab") ? K_CAB : K_LZIP;
    Arc a;
    if (arc_open(&a, argv[2], kind) != 0) return 2;
    printf("how=%d n=%d more=%d\n", a.how, a.n, a.more);
    for (int i = 0; i < a.n; i++) printf("%d %llu %s\n", a.m[i].type, (unsigned long long)a.m[i].size, a.m[i].name);
    for (int i = 0; i < a.n; i++) {
        if (a.m[i].type != AM_FILE) continue;
        char why[128] = "";
        size_t n = 0;
        uint8_t *b = arc_read(&a, i, &n, why, sizeof why);
        printf("== %s\n", a.m[i].name);
        if (b) { fwrite(b, 1, n, stdout); free(b); } else printf("(%s)\n", why);
    }
    arc_free(&a);
    return 0;
}
