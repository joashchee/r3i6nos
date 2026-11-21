/* arc_members.h: the members of an archive as a list a person can pick from (included by formats.h after fmt_archives.h), and the
 * bytes of one member. The viewer's Contents page uses it to open a file inside an archive, or to extract it to the other panel.
 *
 * Covered: ZIP and what is built on it (.zip, .love, jar/apk, msix), TAR (plain, or inside gzip, xz or zstd), ar, and a .deb (the
 * files of its data part). cpio (newc, crc and old ASCII). With bsdtar (libarchive-tools) on the unit: 7-Zip, RAR, Cabinet, LHA, ISO, lzip and .tar.bz2, listed with
 * `bsdtar -tvf` and read with `bsdtar -xOf` as an unprivileged child (a time limit, a memory limit and an output cap; ARC_BSD below); without it those stay as they were
 * (details only, or the native member list). A plain .bz2 (no tar inside) is not read by bsdtar. A document in a
 * ZIP (docx, odt, EPUB) shows its text instead of its parts. A member is read whole into memory, at most ARC_MEMBER_MAX. */
#ifndef COMMANDER_ARC_MEMBERS_H
#define COMMANDER_ARC_MEMBERS_H

#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>

#define ARC_MEMBER_MAX (48u * 1024u * 1024u)

enum { ARC_ZIP, ARC_TAR, ARC_AR, ARC_BSD };

typedef struct {
    int how;                 /* ARC_* */
    ArcMem *m;
    int n, more;             /* members listed, and how many were left off the end */
    uint8_t *tar;            /* the TAR, unpacked and kept (ARC_TAR) */
    size_t tarn;
    ZList z;                 /* ARC_ZIP: parallel to m */
    FFile f;                 /* ARC_ZIP and ARC_AR read from the file */
    char note[96];           /* a line for the top of the listing: a .deb's data part */
    char path[1024];         /* ARC_BSD: the archive's path, for bsdtar */
} Arc;

static void arc_free(Arc *a) {
    if (a->m && a->f.fd >= 0) ff_close(&a->f); /* (an Arc that was only zeroed has no file open: its fd of 0 is not a file) */
    free(a->m);
    free(a->tar);
    zip_free(&a->z);
    memset(a, 0, sizeof *a);
    a->f.fd = -1;
}


/* ---- bsdtar (libarchive-tools): 7-Zip, RAR, Cabinet, LHA, ISO, lzip, .tar.bz2 ------------------------------------------------- */

#define BSDT_LIST_SECS 30
#define BSDT_READ_SECS 120
#define BSDT_LIST_MAX (4u * 1024u * 1024u)   /* the listing's text */
#define BSDT_AS_MAX (700ull << 20)           /* the child's address space: a big LZMA dictionary must fail, not take the unit's memory */

/* The tool: $R3I6NOS_BSDTAR, else bsdtar in /usr/bin, /usr/local/bin or /opt/homebrew/bin. 1 if found (and out, if given, holds its path). */
static int bsdt_find(char *out, size_t cap) {
    const char *e = getenv("R3I6NOS_BSDTAR");
    char tmp[512];
    if (!out) { out = tmp; cap = sizeof tmp; }
    if (e && *e) { snprintf(out, cap, "%s", e); return access(e, X_OK) == 0; }
    static const char *const dirs[] = { "/usr/bin", "/usr/local/bin", "/opt/homebrew/bin" };
    for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++) {
        snprintf(out, cap, "%s/bsdtar", dirs[i]);
        if (access(out, X_OK) == 0) return 1;
    }
    return 0;
}

/* Runs argv and collects its standard output, at most cap bytes (a 0 byte follows them). Stops the child at the time limit or the cap.
 * Returns 0 when it exited 0; -1 for anything else (*why says: "timed out", "too big", "failed"). */
static int bsdt_run(char *const argv[], size_t cap, int secs, uint8_t **out, size_t *outn, const char **why) {
    int fds[2];
    /* The child's whole environment, made here: after fork() in a program with threads nothing may allocate. UTF-8 so names come out as they are. */
    static char *const env[] = { (char *)"LC_ALL=C.UTF-8", (char *)"TZ=UTC", (char *)"PATH=/usr/bin:/bin", NULL };
    *out = NULL; *outn = 0; *why = "failed";
    if (pipe(fds) != 0) return -1;
    pid_t c = fork();
    if (c < 0) { close(fds[0]); close(fds[1]); return -1; }
    if (c == 0) {
        dup2(fds[1], 1);
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 2); dup2(nul, 0); }
        for (int i = 3; i < 64; i++) close(i);
        struct rlimit rl = { BSDT_AS_MAX, BSDT_AS_MAX };
        setrlimit(RLIMIT_AS, &rl);
        rl.rlim_cur = rl.rlim_max = (rlim_t)secs + 10;
        setrlimit(RLIMIT_CPU, &rl);
        execve(argv[0], argv, env);
        _exit(127);
    }
    close(fds[1]);
    size_t have = 0, room = 65536;
    uint8_t *b = (uint8_t *)malloc(room + 1);
    time_t end = time(NULL) + secs;
    int killed = 0;
    while (b) {
        struct pollfd pf = { fds[0], POLLIN, 0 };
        int pr = poll(&pf, 1, 250);
        if (pr < 0 && errno == EINTR) continue;
        if (time(NULL) >= end) { *why = "timed out"; killed = 1; break; }
        if (pr <= 0) continue;
        if (have == room) {
            if (room >= cap) { *why = "too big"; killed = 1; break; }
            size_t nr = room * 2 > cap ? cap : room * 2;
            uint8_t *nb = (uint8_t *)realloc(b, nr + 1);
            if (!nb) { *why = "out of memory"; killed = 1; break; }
            b = nb; room = nr;
        }
        ssize_t r = read(fds[0], b + have, room - have);
        if (r < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (r <= 0) break;
        have += (size_t)r;
    }
    close(fds[0]);
    if (killed || !b) kill(c, SIGKILL);
    int st = 0;
    while (waitpid(c, &st, 0) < 0 && errno == EINTR) {}
    if (!killed && b && WIFEXITED(st) && WEXITSTATUS(st) == 0) { b[have] = 0; *out = b; *outn = have; return 0; }
    free(b);
    return -1;
}

/* One line of `bsdtar -tvf`: "-rw-r--r--  0 owner group  size Mon DD HH:MM name" (a link: "name -> target"). 1 if it was a member. */
static int bsdt_parse_line(char *line, ArcMem *m) {
    char *p = line, *tok[8];
    for (int i = 0; i < 8; i++) {
        while (*p == ' ') p++;
        if (!*p) return 0;
        tok[i] = p;
        while (*p && *p != ' ') p++;
        if (!*p) return 0;
        *p++ = 0;
    }
    /* tok: perms, links, owner, group, size, month, day, time-or-year; one space, then the name */
    if (strlen(tok[0]) < 10 || !strchr("-dlhcbps?", tok[0][0])) return 0;
    const char *name = p;
    if (!*name) return 0;
    memset(m, 0, sizeof *m);
    m->size = strtoull(tok[4], NULL, 10);
    char when[32];
    snprintf(when, sizeof when, "%s %2s %s", tok[5], tok[6], tok[7]);
    snprintf(m->when, sizeof m->when, "%-16.16s", when);
    char c = tok[0][0];
    /* '?' is a type libarchive could not tell (a RAR 5 entry made on Windows): a plain file unless its name ends in / */
    m->type = c == '-' || c == '?' ? AM_FILE : c == 'd' ? AM_DIR : c == 'l' ? AM_LINK : AM_OTHER;
    char *arrow = c == 'l' ? strstr(name, " -> ") : NULL;
    char *hard = (c == '-' && m->size == 0) ? strstr(name, " link to ") : NULL;
    if (hard) { *hard = 0; snprintf(m->target, sizeof m->target, "%s", hard + 9); m->type = AM_LINK; }
    if (arrow) { *arrow = 0; snprintf(m->target, sizeof m->target, "%s", arrow + 4); }
    while (name[0] == '.' && name[1] == '/') name += 2;
    if (!strcmp(name, ".") || !*name) return 0;
    if (m->type == AM_FILE && name[strlen(name) - 1] == '/') m->type = AM_DIR;
    snprintf(m->name, sizeof m->name, "%s", name);
    return 1;
}

/* bsdtar names a member by a pattern, so a name's glob characters are escaped; a path that starts with "-" is made "./-...". */
static void bsdt_pattern(const char *name, char *out, size_t cap) {
    size_t o = 0;
    for (const char *q = name; *q && o + 3 < cap; q++) {
        if (strchr("*?[]\\", *q)) out[o++] = '\\';
        out[o++] = *q;
    }
    out[o] = 0;
}

static void bsdt_archive_arg(const char *path, char *out, size_t cap) {
    snprintf(out, cap, "%s%s", path[0] == '/' ? "" : "./", path);
}

static int arc_open_bsd(Arc *a, const char *path) {
    char tool[512], arg[1100];
    if (!bsdt_find(tool, sizeof tool)) return -1;
    snprintf(a->path, sizeof a->path, "%s", path);
    bsdt_archive_arg(path, arg, sizeof arg);
    char *const argv[] = { tool, (char *)"-tvf", arg, NULL };
    uint8_t *txt; size_t n; const char *why;
    if (bsdt_run(argv, BSDT_LIST_MAX, BSDT_LIST_SECS, &txt, &n, &why) != 0) return -1;
    a->how = ARC_BSD;
    int seen = 0;
    for (char *save = NULL, *line = strtok_r((char *)txt, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        ArcMem m;
        if (!bsdt_parse_line(line, &m)) continue;
        seen++;
        if (a->n < FF_MAX_LIST) a->m[a->n++] = m;
    }
    free(txt);
    a->more = seen > a->n ? seen - a->n : 0;
    snprintf(a->note, sizeof a->note, "-- files (bsdtar) --");
    return a->n > 0 ? 0 : -1;
}

static uint8_t *bsdt_read(const Arc *a, const ArcMem *m, size_t *outn, char *why, size_t whysz) {
    char tool[512], arg[1100], pat[900];
    if (!bsdt_find(tool, sizeof tool)) { snprintf(why, whysz, "bsdtar is not installed"); return NULL; }
    bsdt_archive_arg(a->path, arg, sizeof arg);
    bsdt_pattern(m->name, pat, sizeof pat);
    char *const argv[] = { tool, (char *)"-xOf", arg, (char *)"--", pat, NULL };
    uint8_t *b; size_t n; const char *w;
    if (bsdt_run(argv, ARC_MEMBER_MAX, BSDT_READ_SECS, &b, &n, &w) != 0) {
        snprintf(why, whysz, !strcmp(w, "failed") ? "could not be unpacked (encrypted, damaged or not supported)" : "%s", w);
        return NULL;
    }
    if (n < m->size) { free(b); snprintf(why, whysz, "cut short"); return NULL; }
    *outn = n;
    return b;
}

static int arc_kind_listable(int kind) {
    switch (kind) {
    case K_ZIP: case K_LOVE: case K_JAR: case K_MSIX: case K_TAR: case K_GZIP: case K_XZ: case K_ZSTD: case K_AR: case K_DEB: case K_CPIO: return 1;
    case K_7Z: case K_RAR: case K_CAB: case K_LZH: case K_ISO: case K_LZIP: case K_BZIP2: return bsdt_find(NULL, 0);  /* only when bsdtar is installed */
    default: return 0;
    }
}

/* cpio members: names, kinds and the offsets of their bytes in the file (the same walk as cpio_list). Returns how many were seen. */
static int cpio_members(const FFile *f, ArcMem *out, int max, int *seen) {
    off_t p = 0;
    int n = 0;
    *seen = 0;
    for (int guard = 0; p + 76 <= f->size && guard < 200000; guard++) {
        uint8_t h[110];
        if (ff_read(f, p, h, 110) < 76) break;
        unsigned long long size, mode, mt;
        size_t nl, hs;
        char num[16];
        if (!memcmp(h, "070701", 6) || !memcmp(h, "070702", 6)) {
            memcpy(num, h + 14, 8); num[8] = 0; mode = strtoull(num, NULL, 16);
            memcpy(num, h + 46, 8); num[8] = 0; mt = strtoull(num, NULL, 16);
            memcpy(num, h + 54, 8); num[8] = 0; size = strtoull(num, NULL, 16);
            memcpy(num, h + 94, 8); num[8] = 0; nl = strtoul(num, NULL, 16);
            hs = 110;
        } else if (!memcmp(h, "070707", 6)) {
            memcpy(num, h + 18, 6); num[6] = 0; mode = strtoull(num, NULL, 8);
            memcpy(num, h + 48, 11); num[11] = 0; mt = strtoull(num, NULL, 8);
            memcpy(num, h + 59, 6); num[6] = 0; nl = strtoul(num, NULL, 8);
            memcpy(num, h + 65, 11); num[11] = 0; size = strtoull(num, NULL, 8);
            hs = 76;
        } else break;
        char name[400];
        size_t got = ff_read(f, p + (off_t)hs, name, nl < sizeof name - 1 ? nl : sizeof name - 1);
        name[got] = 0;
        if (!strcmp(name, "TRAILER!!!")) break;
        int newc = hs == 110;
        off_t data = p + (off_t)hs + (off_t)nl;
        if (newc) data = (data + 3) & ~(off_t)3;
        (*seen)++;
        if (n < max) {
            ArcMem *m = &out[n++];
            const char *nm = name;
            while (nm[0] == '.' && nm[1] == '/') nm += 2;
            snprintf(m->name, sizeof m->name, "%s", nm);
            unix_time_short((long long)mt, m->when, sizeof m->when);
            m->size = size;
            m->off = (uint64_t)data;
            unsigned long long ty = mode & 0170000;
            m->type = ty == 0040000 ? AM_DIR : ty == 0100000 ? AM_FILE : ty == 0120000 ? AM_LINK : AM_OTHER;
        }
        p = data + (off_t)size;
        if (newc) p = (p + 3) & ~(off_t)3;
    }
    return n;
}

/* Lists the members of the file at path (of the given kind). 0, or -1 if this is not an archive it can list (arc is then empty). */
static int arc_open(Arc *a, const char *path, int kind) {
    memset(a, 0, sizeof *a);
    a->f.fd = -1;
    if (!arc_kind_listable(kind)) return -1;
    char err[128];
    if (ff_open(&a->f, path, err, sizeof err) != 0) { a->f.fd = -1; return -1; }
    a->m = (ArcMem *)calloc(FF_MAX_LIST, sizeof *a->m);
    if (!a->m) { ff_close(&a->f); memset(a, 0, sizeof *a); a->f.fd = -1; return -1; }

    if (kind == K_ZIP || kind == K_LOVE || kind == K_JAR || kind == K_MSIX) {
        if (zip_open(&a->f, &a->z) != 0) { arc_free(a); return -1; }
        a->how = ARC_ZIP;
        for (int i = 0; i < a->z.n && i < FF_MAX_LIST; i++) {
            const ZEnt *e = &a->z.e[i];
            ArcMem *m = &a->m[a->n++];
            snprintf(m->name, sizeof m->name, "%s", e->name);
            dos_time(e->date, e->time, m->when, sizeof m->when);
            m->size = e->usize;
            size_t l = strlen(e->name);
            m->type = l && e->name[l - 1] == '/' ? AM_DIR : AM_FILE;
        }
        a->more = a->z.total > a->z.n ? a->z.total - a->z.n : 0;
        return 0;
    }

    if (kind == K_AR) {
        ArEnt *ents = (ArEnt *)malloc(sizeof(ArEnt) * FF_MAX_LIST);
        if (!ents) { arc_free(a); return -1; }
        int n = ar_list(&a->f, ents, FF_MAX_LIST);
        a->how = ARC_AR;
        for (int i = 0; i < n; i++) {
            ArcMem *m = &a->m[a->n++];
            snprintf(m->name, sizeof m->name, "%s", ents[i].name);
            unix_time_short(ents[i].mtime, m->when, sizeof m->when);
            m->size = ents[i].size;
            m->off = (uint64_t)ents[i].off;
            m->type = AM_FILE;
        }
        free(ents);
        return 0;
    }

    if (kind == K_CPIO) {
        int seen = 0;
        a->how = ARC_AR; /* members read from the file at their offsets */
        cpio_members(&a->f, a->m, FF_MAX_LIST, &seen);
        a->n = seen < FF_MAX_LIST ? seen : FF_MAX_LIST;
        a->more = seen > a->n ? seen - a->n : 0;
        if (a->n == 0) { arc_free(a); return -1; }
        return 0;
    }

    if (kind == K_7Z || kind == K_RAR || kind == K_CAB || kind == K_LZH || kind == K_ISO || kind == K_LZIP || kind == K_BZIP2) {
        if (arc_open_bsd(a, path) != 0) { arc_free(a); return -1; }
        return 0;
    }

    /* a TAR, plain or packed; a .deb's data part */
    size_t n = 0;
    uint8_t *tar = NULL;
    int cut = 0;
    if (kind == K_TAR) {
        tar = ff_load(&a->f, 0, FF_MAX_LOAD, &n);
    } else if (kind == K_DEB) {
        ArEnt ents[16];
        int ne = ar_list(&a->f, ents, 16);
        const char *how;
        tar = deb_member(&a->f, ents, ne, "data.tar", &n, &how);
        snprintf(a->note, sizeof a->note, "-- files (data.tar, %s) --", how);
    } else {
        tar = unpack_file(&a->f, kind, FF_LIST_UNPACK, &n, &cut);
    }
    if (!tar || n < 512 || (kind != K_TAR && kind != K_DEB && memcmp(tar + 257, "ustar", 5))) { /* not a TAR after all (or no data part) */
        free(tar);
        arc_free(a);
        return -1;
    }
    ArcIdx ix = {a->m, 0, FF_MAX_LIST};
    int count = tar_walk_ix(tar, n, NULL, NULL, NULL, NULL, NULL, &ix);
    a->how = ARC_TAR;
    a->tar = tar;
    a->tarn = n;
    a->n = ix.n;
    a->more = count > ix.n ? count - ix.n : 0;
    return 0;
}

/* One member's bytes (0-terminated, at most ARC_MEMBER_MAX), or NULL with *why saying what is wrong. */
static uint8_t *arc_read(const Arc *a, int i, size_t *outn, char *why, size_t whysz) {
    *outn = 0;
    if (i < 0 || i >= a->n) { snprintf(why, whysz, "no such member"); return NULL; }
    const ArcMem *m = &a->m[i];
    if (m->type == AM_DIR) { snprintf(why, whysz, "a folder"); return NULL; }
    if (m->type == AM_LINK) { snprintf(why, whysz, "a link%s%s", m->target[0] ? " to " : "", m->target); return NULL; }
    if (m->type != AM_FILE) { snprintf(why, whysz, "not a plain file"); return NULL; }
    if (m->size > ARC_MEMBER_MAX) { snprintf(why, whysz, "bigger than %u MiB", ARC_MEMBER_MAX >> 20); return NULL; }
    uint8_t *b = NULL;
    if (a->how == ARC_ZIP) {
        const ZEnt *e = &a->z.e[i];
        if (e->flags & 1) { snprintf(why, whysz, "encrypted"); return NULL; }
        if (e->method != 0 && e->method != 8) { snprintf(why, whysz, "packed in a way not supported here (method %u)", e->method); return NULL; }
        b = zip_read(&a->f, e, ARC_MEMBER_MAX, outn);
        if (!b) snprintf(why, whysz, "damaged");
    } else if (a->how == ARC_BSD) {
        b = bsdt_read(a, m, outn, why, whysz);
    } else if (a->how == ARC_TAR) {
        if (m->off > a->tarn || m->size > a->tarn - m->off) { snprintf(why, whysz, "cut short"); return NULL; }
        b = (uint8_t *)malloc((size_t)m->size + 1);
        if (!b) { snprintf(why, whysz, "out of memory"); return NULL; }
        memcpy(b, a->tar + m->off, (size_t)m->size);
        b[m->size] = 0;
        *outn = (size_t)m->size;
    } else {
        b = ff_load(&a->f, (off_t)m->off, (size_t)m->size, outn);
        if (!b) snprintf(why, whysz, "could not be read");
        else if (*outn < m->size) { free(b); b = NULL; *outn = 0; snprintf(why, whysz, "cut short"); }
    }
    return b;
}

#endif
