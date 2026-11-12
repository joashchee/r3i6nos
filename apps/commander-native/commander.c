/* Commander: a two-panel file manager for the R36S in the style of Norton Commander and Midnight Commander,
 * drawn with the r3i6nos-ansiapps theme (theme/r3i6nos_ansiapps.h, docs/R3I6NOS-ANSIAPPS-THEME.md).
 *
 * Native ARM64 C and SDL2, a default app built into r3i6nos. The screen is 64x24 cells of 10x20: row 0 the top bar, rows 1-20
 * two 32-column panels, rows 21-23 the command line (reply, prompt, status). Nothing scrolls: a long folder turns whole pages.
 *
 * Controls (R36S raw buttons, see r3_pad_action; a keyboard works too):
 *   D-pad up/down   move             D-pad left/right  switch panel          L1/R1  page          L2/R2  top/bottom
 *   A  open a folder, or view a file B  go to the parent folder               X  tag/untag + down  Y  tag all / none
 *   Start  the menu (copy, move, rename, new folder, delete, sort, hidden files, quit)    Select+Start  quit
 *   Keyboard: F2 rename, F3 view, F5 copy, F6 move, F7 new folder, F8 delete, F10 quit, Tab switch panel, Insert/Space tag.
 *
 * The viewer (A on a file) knows what the file is and opens it in tabs, Left/Right (or X/Y) to switch, Up/Down and L1/R1 to page:
 *   Info      the type (by its bytes, as the ansiapps catalogue Diskette identifies files: formats.h) and its details: picture size
 *             and camera, sound format, length and tags, a module's channels, a program's machine, a font's names, an archive's size...
 *   Picture   PNG, JPEG, GIF, BMP, ICO, TGA, QOI, PCX, PNM drawn in the display view (images.h)
 *   Art       ANSI, NFO/DIZ, BIN, XBin, ADF, IDF drawn with the VGA font at 8x16, 25 rows a page (ansiart.h)
 *   Contents  what is inside an archive, package or disk image; or Text out of a document (PDF, RTF, HTML, docx, odt, epub);
 *             a module's instruments, a MIDI's words, a database's tables, a program's libraries (fmt_*.h)
 *   Text      the file itself as text (UTF-8, UTF-16 or code page 437), when it is text
 *   Hex       the bytes
 *
 * Safety: copy and move never overwrite (a name that exists is skipped and counted); delete asks, with No preselected; links are
 * never followed (a link is copied as a link and deleted as a link); a folder is never copied into itself; "/" is never moved or
 * deleted. Work runs in small slices from the main loop with a progress bar, and B cancels it.
 *
 * `--selftest` checks the logic against a temporary folder and prints one R3I6NOS_SMOKE line; `--screenshot out.bmp` draws the
 * real screen once, without a window (`--view FILE [--tab N] [--page N]` first opens the viewer on FILE). `--info FILE` and
 * `--content FILE` print the viewer's Info and Content pages as text. `--report` opens the real window and writes the device report
 * (docs/DEVICE-REPORTS.md) to /roms/Commander/r36s-report.txt: system, card, memory and the power section (CLAUDE.md rule 14: the app
 * idle, then identifying the test corpus). */
#define _GNU_SOURCE
#include <dirent.h>
#include <stdarg.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "r3i6nos_ansiapps.h"
#include "r3i6nos_toast.h"
#include "r3i6nos_report.h"
#include "formats.h"
#include "ansiart.h"
#include "player.h"
#include "selftest_data.h"

#define VERSION "1.1"
#define PANE_W 32
#define PANE_ROWS (R3_DISPLAY_ROWS - 2) /* entries a page */
#define MAX_OPS 500000
#define MAX_DEPTH 64
#define COPY_CHUNK (128 * 1024)

/* ---- small helpers ------------------------------------------------------------------------------------------------- */

static unsigned now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned)(ts.tv_sec * 1000u + (unsigned)(ts.tv_nsec / 1000000));
}

static void copy_str(char *dst, size_t cap, const char *src) {
    snprintf(dst, cap, "%s", src);
}

/* dir + "/" + name into out; 0, or -1 when it does not fit. */
static int path_join(char *out, size_t cap, const char *dir, const char *name) {
    int n = (strcmp(dir, "/") == 0) ? snprintf(out, cap, "/%s", name) : snprintf(out, cap, "%s/%s", dir, name);
    return (n < 0 || (size_t)n >= cap) ? -1 : 0;
}

static const char *base_name(const char *path) {
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

/* The last max cells of a path as UTF-8, starting with "«" when something was cut (never cuts inside a character). */
static void utf8_tail(const char *s, int max, char *out, size_t cap) {
    const unsigned char *p = (const unsigned char *)s, *end = p + strlen(s);
    const unsigned char *starts[1024];
    int n = 0;
    while (p < end && n < 1024) { starts[n++] = p; r3_utf8_next(&p, end); }
    if (n <= max) { snprintf(out, cap, "%s", s); return; }
    snprintf(out, cap, "\xC2\xAB%s", (const char *)starts[n - (max - 1)]);
}

/* 1234 -> "1.2K", 12345678 -> "12M". */
static void fmt_size(unsigned long long n, char *out, size_t cap) {
    static const char unit[] = "KMGT";
    if (n < 1024) { snprintf(out, cap, "%llu", n); return; }
    double v = (double)n;
    int u = -1;
    while (v >= 1024.0 && u < 3) { v /= 1024.0; u++; }
    if (v < 10.0) snprintf(out, cap, "%.1f%c", v, unit[u]);
    else snprintf(out, cap, "%.0f%c", v, unit[u]);
}

/* ---- folders: entries, sorting, loading ---------------------------------------------------------------------------- */

typedef struct {
    char name[256];
    int is_dir, is_link, tagged;
    off_t size;
    time_t mtime;
    mode_t mode;
} Entry;

typedef struct {
    char path[PATH_MAX];
    Entry *e;
    int n, cap, sel;
} Pane;

enum { SORT_NAME, SORT_SIZE, SORT_TIME };
static const char *const SORT_NAMES[] = {"name", "size", "time"};

typedef struct { int sort, hidden; } Opts;

static int g_sort_key;

static int cmp_entries(const void *pa, const void *pb) {
    const Entry *a = pa, *b = pb;
    int ua = strcmp(a->name, "..") == 0, ub = strcmp(b->name, "..") == 0;
    if (ua != ub) return ua ? -1 : 1;
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    if (g_sort_key == SORT_SIZE && !a->is_dir && a->size != b->size) return a->size > b->size ? -1 : 1;
    if (g_sort_key == SORT_TIME && a->mtime != b->mtime) return a->mtime > b->mtime ? -1 : 1;
    return strcasecmp(a->name, b->name) ? strcasecmp(a->name, b->name) : strcmp(a->name, b->name);
}

static void pane_free(Pane *p) {
    free(p->e);
    p->e = NULL;
    p->n = p->cap = 0;
}

static int pane_add(Pane *p, const Entry *e) {
    if (p->n == p->cap) {
        int cap = p->cap ? p->cap * 2 : 64;
        Entry *ne = realloc(p->e, (size_t)cap * sizeof *ne);
        if (!ne) return -1;
        p->e = ne;
        p->cap = cap;
    }
    p->e[p->n++] = *e;
    return 0;
}

static int pane_find(const Pane *p, const char *name) {
    for (int i = 0; i < p->n; i++)
        if (strcmp(p->e[i].name, name) == 0) return i;
    return -1;
}

/* (Re)read p->path. Tags are kept for names still there; the selection stays on keep (a name), else on the same index.
 * Returns 0, or -1 with *err set (the old listing is kept). */
static int pane_load(Pane *p, const Opts *o, const char *keep, char *err, size_t errsz) {
    DIR *d = opendir(p->path);
    if (!d) { snprintf(err, errsz, "%s: %s", base_name(p->path), strerror(errno)); return -1; }
    Pane fresh;
    memset(&fresh, 0, sizeof fresh);
    copy_str(fresh.path, sizeof fresh.path, p->path);
    if (strcmp(p->path, "/") != 0) {
        Entry up;
        memset(&up, 0, sizeof up);
        copy_str(up.name, sizeof up.name, "..");
        up.is_dir = 1;
        pane_add(&fresh, &up);
    }
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (!o->hidden && de->d_name[0] == '.') continue;
        Entry e;
        memset(&e, 0, sizeof e);
        copy_str(e.name, sizeof e.name, de->d_name);
        char full[PATH_MAX];
        struct stat st;
        if (path_join(full, sizeof full, p->path, de->d_name) == 0 && lstat(full, &st) == 0) {
            e.is_link = S_ISLNK(st.st_mode);
            e.size = st.st_size;
            e.mtime = st.st_mtime;
            e.mode = st.st_mode;
            e.is_dir = S_ISDIR(st.st_mode);
            if (e.is_link) {
                struct stat ts;
                if (stat(full, &ts) == 0 && S_ISDIR(ts.st_mode)) e.is_dir = 1;
            }
        }
        int old = pane_find(p, de->d_name);
        if (old >= 0 && p->e[old].tagged) e.tagged = 1;
        if (pane_add(&fresh, &e) != 0) { closedir(d); pane_free(&fresh); snprintf(err, errsz, "out of memory"); return -1; }
    }
    closedir(d);
    g_sort_key = o->sort;
    qsort(fresh.e, (size_t)fresh.n, sizeof *fresh.e, cmp_entries);
    int sel = p->sel;
    pane_free(p);
    *p = fresh;
    int at = keep ? pane_find(p, keep) : -1;
    p->sel = at >= 0 ? at : (sel < p->n ? sel : (p->n ? p->n - 1 : 0));
    return 0;
}

/* Go to a folder (canonical path); keep is the name to select afterwards. */
static int pane_cd(Pane *p, const Opts *o, const char *target, const char *keep, char *err, size_t errsz) {
    char real[PATH_MAX];
    if (!realpath(target, real)) { snprintf(err, errsz, "%s: %s", base_name(target), strerror(errno)); return -1; }
    Pane trial;
    memset(&trial, 0, sizeof trial);
    copy_str(trial.path, sizeof trial.path, real);
    if (pane_load(&trial, o, keep, err, errsz) != 0) return -1;
    pane_free(p);
    *p = trial;
    return 0;
}

static int pane_tagged(const Pane *p) {
    int n = 0;
    for (int i = 0; i < p->n; i++) n += p->e[i].tagged;
    return n;
}

/* ---- work: copy, move and delete as a plan of small operations -------------------------------------------------------- */

enum { OP_MKDIR, OP_COPY, OP_LINK, OP_RMFILE, OP_RMDIR, OP_RENAME };
enum { J_COPY, J_MOVE, J_DELETE };

typedef struct {
    int type, item;
    char *src, *dst;
    off_t size;
    mode_t mode;
    time_t mtime;
} Op;

typedef struct {
    int kind;
    Op *ops;
    int nops, cap, cur;
    int nitems;
    unsigned char *failed;       /* per top-level item: something went wrong, so keep its source */
    off_t total_bytes, done_bytes;
    int fd_in, fd_out;
    char *buf;
    int skipped_exists, skipped_special, errors;
    char first_error[300];
    char current[PATH_MAX];      /* what is being worked on, for the status row */
    int finished, cancelled;
    int force_copy;              /* tests: plan a move as copy + delete even on one filesystem */
    int depth_err;
} Job;

static void job_error(Job *j, const char *what, const char *why) {
    j->errors++;
    if (!j->first_error[0]) snprintf(j->first_error, sizeof j->first_error, "%s: %s", what, why);
}

static int job_add(Job *j, int type, int item, const char *src, const char *dst, off_t size, mode_t mode, time_t mtime) {
    if (j->nops >= MAX_OPS) return -1;
    if (j->nops == j->cap) {
        int cap = j->cap ? j->cap * 2 : 256;
        Op *no = realloc(j->ops, (size_t)cap * sizeof *no);
        if (!no) return -1;
        j->ops = no;
        j->cap = cap;
    }
    Op *op = &j->ops[j->nops];
    memset(op, 0, sizeof *op);
    op->type = type;
    op->item = item;
    op->src = src ? strdup(src) : NULL;
    op->dst = dst ? strdup(dst) : NULL;
    if ((src && !op->src) || (dst && !op->dst)) { free(op->src); free(op->dst); return -1; }
    op->size = size;
    op->mode = mode;
    op->mtime = mtime;
    j->nops++;
    return 0;
}

static void job_free(Job *j) {
    for (int i = 0; i < j->nops; i++) { free(j->ops[i].src); free(j->ops[i].dst); }
    free(j->ops);
    free(j->failed);
    free(j->buf);
    if (j->fd_in >= 0) close(j->fd_in);
    if (j->fd_out >= 0) close(j->fd_out);
    memset(j, 0, sizeof *j);
    j->fd_in = j->fd_out = -1;
}

/* Plan the copy of src to dst (which must not exist): folders first, then what is in them. Links are copied as links. */
static int plan_copy_tree(Job *j, int item, const char *src, const char *dst, int depth) {
    struct stat st;
    if (lstat(src, &st) != 0) { job_error(j, base_name(src), strerror(errno)); return 0; }
    if (S_ISDIR(st.st_mode)) {
        if (depth >= MAX_DEPTH) { job_error(j, base_name(src), "folders nested too deeply"); return 0; }
        if (job_add(j, OP_MKDIR, item, src, dst, 0, st.st_mode, st.st_mtime) != 0) return -1;
        DIR *d = opendir(src);
        if (!d) { job_error(j, base_name(src), strerror(errno)); return 0; }
        struct dirent *de;
        int rc = 0;
        while (rc == 0 && (de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char s2[PATH_MAX], d2[PATH_MAX];
            if (path_join(s2, sizeof s2, src, de->d_name) || path_join(d2, sizeof d2, dst, de->d_name)) {
                job_error(j, de->d_name, "path too long");
                continue;
            }
            rc = plan_copy_tree(j, item, s2, d2, depth + 1);
        }
        closedir(d);
        return rc;
    }
    if (S_ISLNK(st.st_mode)) return job_add(j, OP_LINK, item, src, dst, 0, st.st_mode, st.st_mtime) ? -1 : 0;
    if (S_ISREG(st.st_mode)) {
        j->total_bytes += st.st_size;
        return job_add(j, OP_COPY, item, src, dst, st.st_size, st.st_mode, st.st_mtime) ? -1 : 0;
    }
    j->skipped_special++;
    return 0;
}

/* Plan the removal of path: what is inside first, then the folder; links are removed, never followed. */
static int plan_delete_tree(Job *j, int item, const char *path, int depth) {
    struct stat st;
    if (lstat(path, &st) != 0) { job_error(j, base_name(path), strerror(errno)); return 0; }
    if (S_ISDIR(st.st_mode)) {
        if (depth >= MAX_DEPTH) { job_error(j, base_name(path), "folders nested too deeply"); return 0; }
        DIR *d = opendir(path);
        if (!d) { job_error(j, base_name(path), strerror(errno)); return 0; }
        struct dirent *de;
        int rc = 0;
        while (rc == 0 && (de = readdir(d))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            char p2[PATH_MAX];
            if (path_join(p2, sizeof p2, path, de->d_name)) { job_error(j, de->d_name, "path too long"); continue; }
            rc = plan_delete_tree(j, item, p2, depth + 1);
        }
        closedir(d);
        if (rc != 0) return rc;
        return job_add(j, OP_RMDIR, item, path, NULL, 0, 0, 0) ? -1 : 0;
    }
    return job_add(j, OP_RMFILE, item, path, NULL, 0, 0, 0) ? -1 : 0;
}

/* Is dst the same as src or somewhere inside it? */
static int is_inside(const char *src, const char *dst) {
    size_t n = strlen(src);
    return strncmp(dst, src, n) == 0 && (dst[n] == '/' || dst[n] == 0);
}

/* Plan a job for the given absolute paths. dstdir is the target folder for copy and move (NULL for delete).
 * Returns 0 and a plan (possibly empty: see the counters), or -1 with err set. */
static int job_plan(Job *j, int kind, char **srcs, int n, const char *dstdir, char *err, size_t errsz) {
    j->kind = kind;
    j->fd_in = j->fd_out = -1;
    j->failed = calloc((size_t)(n > 0 ? n : 1), 1);
    if (!j->failed) { snprintf(err, errsz, "out of memory"); return -1; }
    struct stat dst_st;
    if (kind != J_DELETE && stat(dstdir, &dst_st) != 0) { snprintf(err, errsz, "%s: %s", base_name(dstdir), strerror(errno)); return -1; }
    for (int i = 0; i < n; i++) {
        const char *src = srcs[i];
        if (!strcmp(src, "/") || !*src) { snprintf(err, errsz, "will not touch the root folder"); return -1; }
        j->nitems++;
        int item = i;
        if (kind == J_DELETE) {
            if (plan_delete_tree(j, item, src, 0) != 0) { snprintf(err, errsz, "too many files"); return -1; }
            continue;
        }
        char dst[PATH_MAX];
        if (path_join(dst, sizeof dst, dstdir, base_name(src))) { job_error(j, base_name(src), "path too long"); j->failed[item] = 1; continue; }
        struct stat sst, dlst;
        if (lstat(src, &sst) != 0) { job_error(j, base_name(src), strerror(errno)); j->failed[item] = 1; continue; }
        if (lstat(dst, &dlst) == 0) { j->skipped_exists++; j->failed[item] = 1; continue; }
        if (S_ISDIR(sst.st_mode) && is_inside(src, dst)) {
            job_error(j, base_name(src), "cannot go into itself");
            j->failed[item] = 1;
            continue;
        }
        if (kind == J_MOVE && !j->force_copy && sst.st_dev == dst_st.st_dev) {
            if (job_add(j, OP_RENAME, item, src, dst, 0, 0, 0) != 0) { snprintf(err, errsz, "too many files"); return -1; }
            continue;
        }
        if (plan_copy_tree(j, item, src, dst, 0) != 0) { snprintf(err, errsz, "too many files"); return -1; }
        if (kind == J_MOVE && plan_delete_tree(j, item, src, 0) != 0) { snprintf(err, errsz, "too many files"); return -1; }
    }
    return 0;
}

static void item_fail(Job *j, const Op *op, const char *why) {
    j->failed[op->item] = 1;
    job_error(j, base_name(op->src ? op->src : op->dst), why);
}

static void close_copy(Job *j, int remove_partial, const Op *op) {
    if (j->fd_in >= 0) close(j->fd_in);
    if (j->fd_out >= 0) close(j->fd_out);
    if (remove_partial && j->fd_out >= 0 && op->dst) unlink(op->dst);
    j->fd_in = j->fd_out = -1;
}

/* Do one slice of work (about budget_ms). Returns 1 while more remains, 0 when the job is over. */
static int job_step(Job *j, unsigned budget_ms) {
    unsigned deadline = now_ms() + budget_ms;
    if (!j->buf) j->buf = malloc(COPY_CHUNK);
    if (!j->buf) { job_error(j, "copy", "out of memory"); j->cur = j->nops; }
    while (j->cur < j->nops) {
        Op *op = &j->ops[j->cur];
        copy_str(j->current, sizeof j->current, base_name(op->src ? op->src : op->dst));
        int advance = 1;
        if (j->failed[op->item] && op->type != OP_COPY && op->type != OP_MKDIR && op->type != OP_LINK) {
            /* a delete after a failed copy: keep the source */
        } else if (j->failed[op->item] && j->kind != J_DELETE && (op->type == OP_COPY || op->type == OP_MKDIR || op->type == OP_LINK)) {
            /* the rest of a copy whose item already failed: stop adding to the mess */
        } else {
            switch (op->type) {
            case OP_MKDIR:
                if (mkdir(op->dst, (op->mode & 0777) | 0700) != 0) item_fail(j, op, strerror(errno));
                break;
            case OP_LINK: {
                char target[PATH_MAX];
                ssize_t n = readlink(op->src, target, sizeof target - 1);
                if (n < 0) { item_fail(j, op, strerror(errno)); break; }
                target[n] = 0;
                if (symlink(target, op->dst) != 0) item_fail(j, op, strerror(errno));
                break;
            }
            case OP_COPY:
                if (j->fd_in < 0) {
                    j->fd_in = open(op->src, O_RDONLY);
                    if (j->fd_in < 0) { item_fail(j, op, strerror(errno)); break; }
                    j->fd_out = open(op->dst, O_WRONLY | O_CREAT | O_EXCL, (op->mode & 0777) | 0600);
                    if (j->fd_out < 0) { int e = errno; close(j->fd_in); j->fd_in = -1; item_fail(j, op, strerror(e)); break; }
                }
                for (;;) {
                    ssize_t r = read(j->fd_in, j->buf, COPY_CHUNK);
                    if (r < 0) { int e = errno; close_copy(j, 1, op); item_fail(j, op, strerror(e)); break; }
                    if (r == 0) {
                        struct timespec ts[2] = {{op->mtime, 0}, {op->mtime, 0}};
                        futimens(j->fd_out, ts);
                        if (close(j->fd_out) != 0) { int e = errno; j->fd_out = -1; close_copy(j, 0, op); unlink(op->dst); item_fail(j, op, strerror(e)); break; }
                        j->fd_out = -1;
                        close_copy(j, 0, op);
                        break;
                    }
                    ssize_t off = 0;
                    while (off < r) {
                        ssize_t w = write(j->fd_out, j->buf + off, (size_t)(r - off));
                        if (w < 0) { if (errno == EINTR) continue; break; }
                        off += w;
                    }
                    if (off < r) { int e = errno ? errno : EIO; close_copy(j, 1, op); item_fail(j, op, strerror(e)); break; }
                    j->done_bytes += r;
                    if (now_ms() >= deadline) { advance = 0; break; }
                }
                break;
            case OP_RMFILE:
                if (unlink(op->src) != 0) item_fail(j, op, strerror(errno));
                break;
            case OP_RMDIR:
                if (rmdir(op->src) != 0) item_fail(j, op, strerror(errno));
                break;
            case OP_RENAME:
                if (rename(op->src, op->dst) != 0) item_fail(j, op, errno == EXDEV ? "different filesystems" : strerror(errno));
                break;
            }
        }
        if (advance) j->cur++;
        if (now_ms() >= deadline) break;
    }
    if (j->cur >= j->nops) {
        if (!j->finished) sync();
        j->finished = 1;
        return 0;
    }
    return 1;
}

/* Stop now: the file being copied is removed, what was done stays. */
static void job_cancel(Job *j) {
    if (j->cur < j->nops && j->ops[j->cur].type == OP_COPY) close_copy(j, 1, &j->ops[j->cur]);
    j->cancelled = 1;
    j->finished = 1;
}

static int job_percent(const Job *j) {
    if (j->kind != J_DELETE && j->total_bytes > 0) return (int)(j->done_bytes * 100 / j->total_bytes);
    return j->nops ? j->cur * 100 / j->nops : 100;
}

static int job_items_ok(const Job *j) {
    int ok = 0;
    for (int i = 0; i < j->nitems; i++) ok += !j->failed[i];
    return ok;
}

/* The one-line result for the reply row, and its style. */
static int job_summary(const Job *j, char *out, size_t cap) {
    static const char *const verb[] = {"Copied", "Moved", "Deleted"};
    char extra[200] = "";
    size_t k = 0;
    if (j->skipped_exists) k += (size_t)snprintf(extra + k, sizeof extra - k, ", %d skipped (name exists)", j->skipped_exists);
    if (j->skipped_special) k += (size_t)snprintf(extra + k, sizeof extra - k, ", %d special files left out", j->skipped_special);
    int ok = job_items_ok(j) - j->skipped_exists;
    if (ok < 0) ok = 0;
    if (j->cancelled) { snprintf(out, cap, "Cancelled after %d of %d items%s", ok, j->nitems, extra); return R3_ST_WARN; }
    if (j->errors) {
        snprintf(out, cap, "%s %d of %d items; %d errors, first: %.160s%s", verb[j->kind], ok, j->nitems, j->errors, j->first_error, extra);
        return R3_ST_ERROR;
    }
    snprintf(out, cap, "%s %d item%s%s", verb[j->kind], ok, ok == 1 ? "" : "s", extra);
    return (j->skipped_exists || j->skipped_special) ? R3_ST_WARN : R3_ST_OK;
}

/* ---- rename and new folder --------------------------------------------------------------------------------------------- */

/* NULL if name is usable as one file name, else why not. */
static const char *name_problem(const char *name) {
    if (!*name) return "the name is empty";
    if (!strcmp(name, ".") || !strcmp(name, "..")) return "that name is reserved";
    if (strchr(name, '/')) return "a name cannot contain /";
    if (strlen(name) > 255) return "the name is too long";
    return NULL;
}

static int do_rename(const char *dir, const char *old, const char *neu, char *err, size_t errsz) {
    const char *bad = name_problem(neu);
    if (bad) { snprintf(err, errsz, "%s", bad); return -1; }
    char a[PATH_MAX], b[PATH_MAX];
    if (path_join(a, sizeof a, dir, old) || path_join(b, sizeof b, dir, neu)) { snprintf(err, errsz, "path too long"); return -1; }
    if (!strcmp(old, neu)) return 0;
    struct stat st;
    if (lstat(b, &st) == 0) { snprintf(err, errsz, "'%s' already exists", neu); return -1; }
    if (rename(a, b) != 0) { snprintf(err, errsz, "%s", strerror(errno)); return -1; }
    return 0;
}

static int do_mkdir(const char *dir, const char *name, char *err, size_t errsz) {
    const char *bad = name_problem(name);
    if (bad) { snprintf(err, errsz, "%s", bad); return -1; }
    char p[PATH_MAX];
    if (path_join(p, sizeof p, dir, name)) { snprintf(err, errsz, "path too long"); return -1; }
    if (mkdir(p, 0755) != 0) { snprintf(err, errsz, "%s", errno == EEXIST ? "that name already exists" : strerror(errno)); return -1; }
    return 0;
}

/* ---- the viewer: whole pages in tabs (Info, the file's content, its text, hex) ------------------------------------------------- */

#define VIEW_W (R3_COLS - 2)
#define VIEW_ROWS (R3_DISPLAY_ROWS - 2)
#define VIEW_MAX_BYTES (4 * 1024 * 1024)
#define VIEW_MAX_LINES 200000
#define PIC_W R3_W                                /* a picture or art page: the whole display view, no frame */
#define PIC_H (R3_DISPLAY_ROWS * R3_CELL_H)
#define PIC_Y (R3_DISPLAY_TOP * R3_CELL_H)

/* Lines of cells: one tab's text. */
typedef struct {
    unsigned char *cells;      /* lines x VIEW_W code page 437 indices */
    unsigned char *fg;         /* lines x VIEW_W colours (0: the body style) */
    unsigned char *len;        /* cells used on each line */
    int n, cap, hex, page, truncated;
    off_t filesize;
    char title[256];
} View;

static void view_free(View *v) {
    free(v->cells);
    free(v->fg);
    free(v->len);
    memset(v, 0, sizeof *v);
}

static int view_line(View *v) {
    if (v->n >= VIEW_MAX_LINES) { v->truncated = 1; return -1; }
    if (v->n == v->cap) {
        int cap = v->cap ? v->cap * 2 : 256;
        unsigned char *c = realloc(v->cells, (size_t)cap * VIEW_W);
        if (c) v->cells = c;
        unsigned char *f = realloc(v->fg, (size_t)cap * VIEW_W);
        if (f) v->fg = f;
        unsigned char *l = realloc(v->len, (size_t)cap);
        if (l) v->len = l;
        if (!c || !f || !l) return -1;
        v->cap = cap;
    }
    memset(v->cells + (size_t)v->n * VIEW_W, 32, VIEW_W);
    memset(v->fg + (size_t)v->n * VIEW_W, 0, VIEW_W);
    v->len[v->n] = 0;
    return v->n++;
}

static void view_put_c(View *v, int line, int ch, int fg) {
    if (v->len[line] < VIEW_W) {
        v->cells[(size_t)line * VIEW_W + v->len[line]] = (unsigned char)ch;
        v->fg[(size_t)line * VIEW_W + v->len[line]++] = (unsigned char)fg;
    }
}

static void view_put(View *v, int line, int ch) { view_put_c(v, line, ch, 0); }

static int view_pages(const View *v) { return v->n ? (v->n + VIEW_ROWS - 1) / VIEW_ROWS : 1; }

enum { AS_AUTO, AS_TEXT, AS_HEX };

/* Bytes as lines: text (UTF-8, UTF-16 with a byte-order mark, or else code page 437 byte for byte) wrapped at VIEW_W, or hex. */
static void view_from_bytes(View *v, const unsigned char *data, size_t got, int as) {
    size_t probe = got < 4096 ? got : 4096;
    int binary = 0, ctrl = 0;
    int utf16 = got >= 2 && ((data[0] == 0xFF && data[1] == 0xFE) || (data[0] == 0xFE && data[1] == 0xFF));
    for (size_t i = 0; i < probe && !utf16; i++) {
        if (data[i] == 0) binary = 1;
        if (data[i] < 9 || (data[i] > 13 && data[i] < 32 && data[i] != 27 && data[i] != 26)) ctrl++;
    }
    if (probe && ctrl * 10 > (int)probe) binary = 1;
    v->hex = as == AS_HEX || (as == AS_AUTO && binary);
    if (v->hex) {
        for (size_t off = 0; off < got; off += 8) {
            int line = view_line(v);
            if (line < 0) break;
            char txt[80];
            int k = snprintf(txt, sizeof txt, "%06zX ", off);
            for (size_t b = 0; b < 8; b++) {
                if (off + b < got) k += snprintf(txt + k, sizeof txt - (size_t)k, " %02X", data[off + b]);
                else k += snprintf(txt + k, sizeof txt - (size_t)k, "   ");
            }
            k += snprintf(txt + k, sizeof txt - (size_t)k, "  |");
            for (size_t b = 0; b < 8 && off + b < got; b++) txt[k++] = (data[off + b] >= 32 && data[off + b] < 127) ? (char)data[off + b] : '.';
            txt[k++] = '|';
            for (int c = 0; c < k; c++) view_put(v, line, (unsigned char)txt[c]);
        }
        if (v->n == 0) view_line(v);
        return;
    }
    char *conv = NULL;
    if (utf16) { /* to UTF-8 first */
        size_t cap = got * 2 + 4;
        conv = malloc(cap);
        if (conv) { utf16_to_utf8(conv, cap, data, got, data[0] == 0xFE); data = (const unsigned char *)conv; got = strlen(conv); }
    }
    int non_ascii, utf8 = utf8_valid(data, got, &non_ascii);
    int line = view_line(v);
    const unsigned char *p = data, *end = data + got;
    if (got >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) p += 3;
    while (line >= 0 && p < end) {
        if (*p == '\n') { p++; line = view_line(v); continue; }
        if (*p == '\r') { p++; continue; }
        if (*p == 26 && !utf8) break; /* DOS end of file: a SAUCE record may follow */
        if (*p == '\t') {
            int stop = (v->len[line] / 4 + 1) * 4;
            p++;
            while (v->len[line] < stop) {
                if (v->len[line] >= VIEW_W) { line = view_line(v); if (line < 0) break; }
                view_put(v, line, 32);
            }
            continue;
        }
        int ch;
        if (utf8) ch = r3_cp437_of(r3_utf8_next(&p, end));
        else { ch = *p < 32 || *p == 127 ? 32 : *p; p++; } /* not UTF-8: the bytes are code page 437, as DOS wrote them */
        if (v->len[line] >= VIEW_W) { line = view_line(v); if (line < 0) break; }
        view_put(v, line, ch);
    }
    free(conv);
    if (v->n == 0) view_line(v);
}

/* A file as text or hex (as), at most VIEW_MAX_BYTES of it. */
static int view_load_as(View *v, const char *path, int as, char *err, size_t errsz) {
    memset(v, 0, sizeof *v);
    int fd = open(path, O_RDONLY);
    if (fd < 0) { snprintf(err, errsz, "%s: %s", base_name(path), strerror(errno)); return -1; }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { close(fd); snprintf(err, errsz, "%s is not a plain file", base_name(path)); return -1; }
    v->filesize = st.st_size;
    copy_str(v->title, sizeof v->title, base_name(path));
    size_t want = st.st_size > VIEW_MAX_BYTES ? VIEW_MAX_BYTES : (size_t)st.st_size;
    unsigned char *data = malloc(want + 1);
    if (!data) { close(fd); snprintf(err, errsz, "out of memory"); return -1; }
    size_t got = 0;
    while (got < want) {
        ssize_t r = read(fd, data + got, want - got);
        if (r < 0) { if (errno == EINTR) continue; int e = errno; close(fd); free(data); snprintf(err, errsz, "%s", strerror(e)); return -1; }
        if (r == 0) break;
        got += (size_t)r;
    }
    close(fd);
    if ((off_t)got < st.st_size) v->truncated = 1;
    int cut = v->truncated;
    view_from_bytes(v, data, got, as);
    v->truncated |= cut;
    free(data);
    return 0;
}

static int view_load(View *v, const char *path, char *err, size_t errsz) { return view_load_as(v, path, AS_AUTO, err, errsz); }

/* The Info rows as lines: the label in the hint colour, the value after it, wrapped under itself. */
static void view_from_info(View *v, const FInfo *in) {
    enum { KEY_W = 15 };
    for (int i = 0; i < in->n; i++) {
        int line = view_line(v);
        if (line < 0) return;
        unsigned char key[KEY_W + 1], val[600];
        int kn = r3_fit(in->r[i].k, KEY_W, key), vn = r3_decode(in->r[i].v, val, (int)sizeof val);
        for (int c = 0; c < kn; c++) view_put_c(v, line, key[c], R3_LIGHT_CYAN);
        while (v->len[line] < KEY_W + 1) view_put(v, line, ' ');
        for (int c = 0; c < vn; c++) {
            if (v->len[line] >= VIEW_W) {
                line = view_line(v);
                if (line < 0) return;
                while (v->len[line] < KEY_W + 1) view_put(v, line, ' ');
            }
            view_put(v, line, val[c]);
        }
    }
    if (v->n == 0) view_line(v);
}

enum { TAB_LINES, TAB_PICTURE, TAB_ART, TAB_PLAY };
enum { LOAD_READY, LOAD_TEXT, LOAD_HEX };
#define MAX_TABS 4

typedef struct {
    int kind, load;            /* what it shows; LOAD_TEXT/LOAD_HEX: read the file when first shown */
    char name[16];
    View v;
} Tab;

typedef struct {
    char path[PATH_MAX], title[256];
    FType type;
    off_t filesize;
    Tab tab[MAX_TABS];
    int ntabs, cur;
    FImage pic;                /* fitted to PIC_W x PIC_H */
    int pic_w, pic_h;          /* as decoded */
    Art art;
    int art_page;
    Player pl;                 /* the Play tab: a Rec demo, a WAV or a video (player.h) */
    unsigned gen;              /* changes whenever the pixels shown change */
    Arc arc;                   /* the members of an archive, when the Contents tab is a list to pick from */
    int has_arc, arc_tab, arc_line0, sel; /* the tab, the line the first member is on, and the member picked */
} Viewer;

static void viewer_free(Viewer *w) {
    for (int i = 0; i < w->ntabs; i++) view_free(&w->tab[i].v);
    img_free(&w->pic);
    art_free(&w->art);
    pl_close(&w->pl);
    arc_free(&w->arc);
    unsigned gen = w->gen;
    memset(w, 0, sizeof *w);
    w->gen = gen + 1;
}

static Tab *viewer_tab(Viewer *w) { return &w->tab[w->cur]; }

static Tab *viewer_add(Viewer *w, int kind, const char *name) {
    if (w->ntabs >= MAX_TABS) return NULL;
    Tab *t = &w->tab[w->ntabs++];
    memset(t, 0, sizeof *t);
    t->kind = kind;
    copy_str(t->name, sizeof t->name, name);
    copy_str(t->v.title, sizeof t->v.title, w->title);
    return t;
}

/* Show tab i, reading the file for it the first time. */
static void viewer_show(Viewer *w, int i, char *err, size_t errsz) {
    if (i < 0 || i >= w->ntabs) return;
    if (w->tab[w->cur].kind == TAB_PLAY && w->cur != i) pl_pause(&w->pl, 1);   /* leaving the Play tab stops the sound */
    w->cur = i;
    w->gen++;
    Tab *t = &w->tab[i];
    if (t->load == LOAD_READY) return;
    int as = t->load == LOAD_HEX ? AS_HEX : AS_TEXT;
    t->load = LOAD_READY;
    if (view_load_as(&t->v, w->path, as, err, errsz) != 0) { view_line(&t->v); return; }
}

static int pages_of(const Viewer *w) {
    const Tab *t = &w->tab[w->cur];
    if (t->kind == TAB_ART) return art_pages(&w->art, PIC_H);
    if (t->kind == TAB_PICTURE || t->kind == TAB_PLAY) return 1;
    return view_pages(&t->v);
}

static int page_of(const Viewer *w) {
    const Tab *t = &w->tab[w->cur];
    return t->kind == TAB_ART ? w->art_page : (t->kind == TAB_PICTURE || t->kind == TAB_PLAY) ? 0 : t->v.page;
}

static void set_page(Viewer *w, int page) {
    int pages = pages_of(w);
    if (page < 0) page = 0;
    if (page > pages - 1) page = pages - 1;
    Tab *t = &w->tab[w->cur];
    if (t->kind == TAB_ART) w->art_page = page;
    else if (t->kind == TAB_LINES) t->v.page = page;
    w->gen++;
}

static int is_picture_kind(int k) {
    return k == K_PNG || k == K_JPEG || k == K_GIF || k == K_BMP || k == K_ICO || k == K_TGA || k == K_QOI || k == K_PCX || k == K_PNM;
}

/* How a kind plays (player.h): a Rec demo's frames and a PCM WAV by themselves, other sound and video through ffmpeg. */
static int play_mode(int k) {
    if (k == K_R3F) return PL_R3F;
    if (k == K_WAV) return PL_WAV;
    if (k == K_MP4 || k == K_AVI || k == K_MKV || k == K_ASF || k == K_FLV || k == K_MPEGPS || k == K_MPEGTS) return PL_VIDEO;
    if (k == K_AIFF || k == K_FLAC || k == K_MP3 || k == K_OGG || k == K_WAVPACK) return PL_AUDIO;
    return PL_NONE;
}

/* Pictures drawn by ffmpeg (player.h pl_still) when the unit has one: Info reads their size from the header, ffmpeg gives the pixels. */
static int is_ffmpeg_still_kind(int k) { return k == K_WEBP || k == K_TIFF || k == K_PSD; }

/* The picture ffmpeg decodes, at the size Info read from its header: full.px (w*h ARGB) is allocated, 0; else -1 and `*why`. */
static int still_via_ffmpeg(const char *path, const FInfo *info, FImage *full, const char **why) {
    int sw = 0, sh = 0;
    for (int i = 0; i < info->n; i++) if (!strcmp(info->r[i].k, "Dimensions")) sscanf(info->r[i].v, "%d x %d", &sw, &sh);
    memset(full, 0, sizeof *full);
    if (sw <= 0 || sh <= 0 || (long long)sw * sh > 24000000LL) { *why = "its size is not known or is over 24 megapixels"; return -1; }
    full->px = (uint32_t *)malloc((size_t)sw * sh * sizeof(uint32_t));
    if (!full->px) { *why = "out of memory"; return -1; }
    *why = NULL;
    if (pl_still(path, sw, sh, full->px, why) != 0) { free(full->px); full->px = NULL; return -1; }
    full->w = sw;
    full->h = sh;
    return 0;
}

static int is_art_kind(int k) { return k == K_ANSI || k == K_BINART || k == K_XBIN || k == K_ADF || k == K_IDF; }

/* An archive's members as lines, one each (a long name keeps its end, after "..."), so the line a member is on is its number plus the
 * lines before it. A .deb's control file and a heading come first. Returns the line the first member is on. */
static int view_from_members(View *v, const Arc *arc, const FText *head) {
    if (head && head->s && head->n) view_from_bytes(v, (const unsigned char *)head->s, head->n, AS_TEXT);
    if (v->n == 1 && v->len[0] == 0) v->n = 0; /* view_from_bytes makes one empty line for nothing */
    if (arc->note[0]) {
        int line = view_line(v);
        if (line >= 0) for (const char *c = arc->note; *c; c++) view_put(v, line, (unsigned char)*c);
    }
    int line0 = v->n;
    for (int i = 0; i < arc->n; i++) {
        const ArcMem *m = &arc->m[i];
        int line = view_line(v);
        if (line < 0) break;
        char sz[16], head_s[48];
        if (m->type == AM_DIR) snprintf(sz, sizeof sz, "<DIR>");
        else if (m->type == AM_LINK) snprintf(sz, sizeof sz, "link");
        else fmt_bytes(m->size, sz, sizeof sz);
        int hn = snprintf(head_s, sizeof head_s, "%9s  %-16.16s  ", sz, m->when);
        for (int c = 0; c < hn; c++) view_put(v, line, (unsigned char)head_s[c]);
        unsigned char cells[420];
        int nn = r3_decode(m->name, cells, (int)sizeof cells);
        int room = VIEW_W - hn;
        if (nn <= room) { for (int c = 0; c < nn; c++) view_put(v, line, cells[c]); }
        else {
            for (int c = 0; c < 3; c++) view_put(v, line, '.');
            for (int c = nn - (room - 3); c < nn; c++) view_put(v, line, cells[c]);
        }
    }
    if (arc->more > 0) {
        int line = view_line(v);
        char more[48];
        snprintf(more, sizeof more, "... and %d more", arc->more);
        if (line >= 0) for (const char *c = more; *c; c++) view_put(v, line, (unsigned char)*c);
    }
    if (v->n == 0) view_line(v);
    return line0;
}

/* Open a file in the viewer: work out what it is, then make its tabs. Returns -1 (and err) only when it cannot be read at all. */
static int viewer_open_as(Viewer *w, const char *path, const char *title, char *err, size_t errsz) {
    viewer_free(w);
    FFile f;
    if (ff_open(&f, path, err, errsz) != 0) {
        char why[200];
        copy_str(why, sizeof why, err);
        snprintf(err, errsz, "%s: %s", base_name(path), why);
        return -1;
    }
    copy_str(w->path, sizeof w->path, path);
    copy_str(w->title, sizeof w->title, title ? title : base_name(path));
    w->filesize = f.size;
    w->type = ff_identify(&f);
    int k = w->type.kind;
    FInfo info = {0, 0, 0};
    ff_info(&f, w->type, &info);

    Tab *t = viewer_add(w, TAB_LINES, "Info");
    int content = -1;
    if (is_picture_kind(k)) {
        size_t n;
        uint8_t *b = ff_load(&f, 0, FF_MAX_LOAD, &n);
        FImage full;
        char why[160] = "";
        if (b && img_decode(img_kind(b, n, f.ext), b, n, &full, why, sizeof why) == 0) {
            if (k == K_JPEG) img_orient(&full, ff_jpeg_orientation(&f));
            w->pic_w = full.w;
            w->pic_h = full.h;
            if (img_fit(&full, PIC_W, PIC_H, &w->pic) == 0) { content = w->ntabs; viewer_add(w, TAB_PICTURE, "Picture"); }
            img_free(&full);
        } else {
            /* a progressive JPEG (not drawn by images.h) goes to ffmpeg, which reads it exactly as a baseline one; arithmetic coding fails there too */
            const char *fwhy = NULL;
            FImage via;
            if (k == K_JPEG && still_via_ffmpeg(path, &info, &via, &fwhy) == 0) {
                img_orient(&via, ff_jpeg_orientation(&f));
                w->pic_w = via.w;
                w->pic_h = via.h;
                if (img_fit(&via, PIC_W, PIC_H, &w->pic) == 0) { content = w->ntabs; viewer_add(w, TAB_PICTURE, "Picture"); }
                img_free(&via);
            }
            if (content < 0) {
                if (k == K_JPEG && fwhy) info_add(&info, "Picture", "not shown: %s; ffmpeg: %s", why[0] ? why : "the file could not be read", fwhy);
                else info_add(&info, "Picture", "not shown: %s", why[0] ? why : "the file could not be read");
            }
        }
        free(b);
    } else if (is_ffmpeg_still_kind(k)) {
        const char *why = NULL;
        FImage full;
        if (still_via_ffmpeg(path, &info, &full, &why) == 0) {
            w->pic_w = full.w;
            w->pic_h = full.h;
            if (img_fit(&full, PIC_W, PIC_H, &w->pic) == 0) { content = w->ntabs; viewer_add(w, TAB_PICTURE, "Picture"); }
            img_free(&full);
        }
        if (content < 0) info_add(&info, "Picture", "not shown: %s", why ? why : "it could not be drawn");
    } else if (is_art_kind(k)) {
        char why[160];
        if (art_load(&w->art, &f, k, why, sizeof why) == 0) { content = w->ntabs; viewer_add(w, TAB_ART, "Art"); }
        else info_add(&info, "Art", "not shown: %s", why);
    }
    if (content < 0 && play_mode(k) != PL_NONE) {
        const char *why = NULL;
        int mode = play_mode(k);
        if (pl_open(&w->pl, path, mode, (double)SDL_GetTicks(), &why) != 0 && mode == PL_WAV) {
            const char *via = NULL;    /* a WAV that is not plain PCM may still play through ffmpeg */
            if (pl_open(&w->pl, path, PL_AUDIO, (double)SDL_GetTicks(), &via) == 0) why = NULL;
        }
        if (w->pl.mode) { content = w->ntabs; viewer_add(w, TAB_PLAY, "Play"); }
        else info_add(&info, "Play", "not played: %s", why ? why : "it could not be opened");
    }
    if (content < 0 && arc_kind_listable(k) && arc_open(&w->arc, path, k) == 0 && w->arc.n > 0) {
        FText head = {0, 0, 0, 0, 0};
        if (k == K_DEB) deb_control_ex(&f, NULL, &head, 0);
        content = w->ntabs;
        w->has_arc = 1;
        w->arc_tab = content;
        Tab *c = viewer_add(w, TAB_LINES, "Contents");
        w->arc_line0 = view_from_members(&c->v, &w->arc, &head);
        text_free(&head);
    } else if (content < 0) {
        arc_free(&w->arc);
    }
    if (content < 0) {
        FText txt = {0, 0, 0, 0, 0};
        uint8_t *raw = NULL;
        size_t rawn = 0;
        char name[32];
        int fc = ff_content(&f, w->type, &txt, &raw, &rawn, name, sizeof name);
        if (fc != FC_NONE) {
            content = w->ntabs;
            Tab *c = viewer_add(w, TAB_LINES, name);
            if (fc == FC_BYTES) view_from_bytes(&c->v, raw, rawn, AS_AUTO);
            else view_from_bytes(&c->v, (const unsigned char *)(txt.s ? txt.s : ""), txt.n, AS_TEXT);
            if (txt.cut) c->v.truncated = 1;
        }
        free(raw);
        text_free(&txt);
    }
    int text = -1;
    if (ff_looks_text(f.head, f.hn) && k != K_EMPTY) {
        text = w->ntabs;
        Tab *s = viewer_add(w, TAB_LINES, content >= 0 && !strcmp(w->tab[content].name, "Text") ? "Source" : "Text");
        s->load = LOAD_TEXT;
    }
    int hex = w->ntabs;
    Tab *h = viewer_add(w, TAB_LINES, "Hex");
    h->load = LOAD_HEX;
    ff_close(&f);
    t = &w->tab[0];
    view_from_info(&t->v, &info);
    info_free(&info);
    for (int i = 0; i < w->ntabs; i++) w->tab[i].v.filesize = w->filesize;
    int first = content >= 0 ? content : text >= 0 ? text : (k == K_UNKNOWN || k == K_EMPTY) ? hex : 0;
    viewer_show(w, first, err, errsz);
    if (w->has_arc) set_page(w, w->arc_line0 / VIEW_ROWS);
    return 0;
}

static int viewer_open(Viewer *w, const char *path, char *err, size_t errsz) { return viewer_open_as(w, path, NULL, err, errsz); }

/* The pixels over the display view while a picture or art tab shows. */
static void viewer_overlay(const Viewer *w, uint32_t *px) {
    const Tab *t = &w->tab[w->cur];
    if (w->ntabs == 0) return;
    if (t->kind == TAB_ART && w->art.ch) {
        art_draw(&w->art, w->art_page, px, PIC_Y, PIC_H);
    } else if (t->kind == TAB_PLAY && w->pl.has_video && w->pl.px) {
        memcpy(px + (size_t)PIC_Y * R3_W, w->pl.px, (size_t)PIC_W * PIC_H * sizeof(uint32_t));
    } else if (t->kind == TAB_PICTURE && w->pic.px) {
        for (int y = 0; y < PIC_H; y++) memset(px + (size_t)(PIC_Y + y) * R3_W, 0, R3_W * sizeof(uint32_t));
        int x0 = (PIC_W - w->pic.w) / 2, y0 = PIC_Y + (PIC_H - w->pic.h) / 2;
        for (int y = 0; y < w->pic.h; y++) {
            uint32_t *dst = px + (size_t)(y0 + y) * R3_W + x0;
            memcpy(dst, w->pic.px + (size_t)y * w->pic.w, (size_t)w->pic.w * sizeof(uint32_t));
        }
    }
}

/* ---- the application ---------------------------------------------------------------------------------------------------------- */

enum { M_BROWSE, M_MENU, M_CONFIRM, M_INPUT, M_VIEW, M_JOB };
enum { C_VIEW, C_COPY, C_MOVE, C_RENAME, C_MKDIR, C_DELETE, C_TAGALL, C_UNTAG, C_SORT, C_HIDDEN, C_SWAP, C_SAME, C_QUIT, C_COUNT };

static const struct { const char *label, *key; } MENU[C_COUNT] = {
    {"View", "F3"}, {"Copy", "F5"}, {"Move", "F6"}, {"Rename", "F2"}, {"Make directory", "F7"}, {"Delete", "F8"},
    {"Tag all", ""}, {"Untag all", ""}, {"Sort by", ""}, {"Hidden files", ""}, {"Swap panels", ""},
    {"Other panel: this folder", ""}, {"Quit", "F10"},
};

#define VIEW_DEPTH 4
typedef struct {
    char path[PATH_MAX], title[256], tmpfile[PATH_MAX], tmpdir[PATH_MAX];
    int sel, tab, page;
} ViewFrame;

typedef struct {
    Pane p[2];
    int act;
    Opts opt;
    int mode, quit;
    char reply[512];
    int reply_style;
    int menu_sel;
    /* confirmation */
    int conf_sel, conf_cmd;
    char conf_title[128], conf_yes[192];
    /* a line being typed */
    char buf[256];
    int in_cmd;
    char in_prompt[200], in_old[256];
    /* the viewer and the running job */
    Viewer view;
    ViewFrame vstack[VIEW_DEPTH]; /* the archives above the file in the viewer, which B goes back to */
    int vdepth;
    char vtmpfile[PATH_MAX], vtmpdir[PATH_MAX]; /* the viewer's file when it was taken out of an archive */
    Job job;
    int job_active;
    char **job_srcs;
    int job_nsrcs;
    char job_dst[PATH_MAX];
    int select_held;
} App;

static void say(App *a, int style, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void say(App *a, int style, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->reply, sizeof a->reply, fmt, ap);
    va_end(ap);
    a->reply_style = style;
}

static Pane *cur(App *a) { return &a->p[a->act]; }
static Pane *other(App *a) { return &a->p[1 - a->act]; }

static void refresh(App *a, int which, const char *keep) {
    char err[256];
    if (pane_load(&a->p[which], &a->opt, keep, err, sizeof err) != 0) say(a, R3_ST_ERROR, "%s", err);
}

static void refresh_all(App *a) {
    refresh(a, 0, NULL);
    refresh(a, 1, NULL);
}

/* ---- opening and extracting an archive's members from the viewer's Contents list ---------------------------------------------- */

static void tmp_remove(char *file, char *dir) {
    if (file[0]) unlink(file);
    if (dir[0]) rmdir(dir);
    file[0] = dir[0] = 0;
}

/* Close the viewer and every archive above it, taking away the files made to show members. */
static void viewer_close_all(App *a) {
    viewer_free(&a->view);
    tmp_remove(a->vtmpfile, a->vtmpdir);
    for (int i = 0; i < a->vdepth; i++) tmp_remove(a->vstack[i].tmpfile, a->vstack[i].tmpdir);
    a->vdepth = 0;
}

/* B in the viewer: back to the archive a member was opened from, or out of the viewer. */
static void viewer_up(App *a) {
    if (a->vdepth == 0) { a->mode = M_BROWSE; viewer_free(&a->view); return; }
    ViewFrame *fr = &a->vstack[--a->vdepth];
    tmp_remove(a->vtmpfile, a->vtmpdir);
    char err[256];
    if (viewer_open_as(&a->view, fr->path, fr->title, err, sizeof err) != 0) {
        say(a, R3_ST_ERROR, "%s", err);
        tmp_remove(fr->tmpfile, fr->tmpdir);
        viewer_close_all(a);
        a->mode = M_BROWSE;
        return;
    }
    copy_str(a->vtmpfile, sizeof a->vtmpfile, fr->tmpfile);
    copy_str(a->vtmpdir, sizeof a->vtmpdir, fr->tmpdir);
    fr->tmpfile[0] = fr->tmpdir[0] = 0;
    char e2[64] = "";
    viewer_show(&a->view, fr->tab, e2, sizeof e2);
    if (a->view.has_arc) {
        a->view.sel = fr->sel < a->view.arc.n ? fr->sel : 0;
        set_page(&a->view, fr->page);
    }
}

/* A member's name as a plain file name: what follows the last /, with nothing a file system would refuse for control characters. */
static void member_file_name(const char *name, char *out, size_t cap) {
    const char *b = strrchr(name, '/');
    b = b ? b + 1 : name;
    size_t k = 0;
    for (; *b && k + 1 < cap && k < 200; b++) out[k++] = (unsigned char)*b < 32 || *b == 127 ? '_' : *b;
    out[k] = 0;
    if (!k || !strcmp(out, ".") || !strcmp(out, "..")) copy_str(out, cap, "member");
}

static int write_file_excl(const char *path, const uint8_t *b, size_t n, mode_t mode) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, mode);
    if (fd < 0) return -1;
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(fd, b + done, n - done);
        if (w < 0) { if (errno == EINTR) continue; int e = errno; close(fd); unlink(path); errno = e; return -1; }
        done += (size_t)w;
    }
    if (close(fd) != 0) { int e = errno; unlink(path); errno = e; return -1; }
    return 0;
}

/* A on a member: take it out into a temporary file and open that in the viewer; B comes back to the list. */
static void member_open(App *a) {
    Viewer *w = &a->view;
    if (w->sel < 0 || w->sel >= w->arc.n) return;
    const ArcMem *m = &w->arc.m[w->sel];
    char fname[256], why[160];
    member_file_name(m->name, fname, sizeof fname);
    if (a->vdepth >= VIEW_DEPTH) { say(a, R3_ST_WARN, "Archives inside archives open %d deep at most", VIEW_DEPTH); return; }
    size_t n;
    uint8_t *b = arc_read(&w->arc, w->sel, &n, why, sizeof why);
    if (!b) { say(a, R3_ST_WARN, "%s: %s", fname, why); return; }
    const char *root = getenv("TMPDIR");
    char dir[PATH_MAX], file[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/commander-XXXXXX", root && *root ? root : "/tmp");
    if (!mkdtemp(dir)) { say(a, R3_ST_ERROR, "Cannot make a temporary folder: %s", strerror(errno)); free(b); return; }
    path_join(file, sizeof file, dir, fname);
    if (write_file_excl(file, b, n, 0600) != 0) {
        say(a, R3_ST_ERROR, "Cannot write a temporary file: %s", strerror(errno));
        rmdir(dir);
        free(b);
        return;
    }
    free(b);
    char title[256];
    const char *parent = strstr(w->title, " > ");
    while (parent && strstr(parent + 3, " > ")) parent = strstr(parent + 3, " > ");
    snprintf(title, sizeof title, "%.60s > %.150s", parent ? parent + 3 : w->title, m->name);
    ViewFrame *fr = &a->vstack[a->vdepth];
    copy_str(fr->path, sizeof fr->path, w->path);
    copy_str(fr->title, sizeof fr->title, w->title);
    copy_str(fr->tmpfile, sizeof fr->tmpfile, a->vtmpfile);
    copy_str(fr->tmpdir, sizeof fr->tmpdir, a->vtmpdir);
    fr->sel = w->sel;
    fr->tab = w->cur;
    fr->page = page_of(w);
    a->vdepth++;
    copy_str(a->vtmpfile, sizeof a->vtmpfile, file);
    copy_str(a->vtmpdir, sizeof a->vtmpdir, dir);
    char err[256];
    if (viewer_open_as(w, file, title, err, sizeof err) != 0) {
        viewer_up(a); /* back to the list, and the file made for it is removed */
        say(a, R3_ST_ERROR, "%s", err);
    }
}

/* Start on a member: write it into the other panel's folder, under its own name; never over a file that is there. */
static void member_extract(App *a) {
    Viewer *w = &a->view;
    if (w->sel < 0 || w->sel >= w->arc.n) return;
    const ArcMem *m = &w->arc.m[w->sel];
    char fname[256], why[160], dest[PATH_MAX];
    member_file_name(m->name, fname, sizeof fname);
    size_t n;
    uint8_t *b = arc_read(&w->arc, w->sel, &n, why, sizeof why);
    if (!b) { say(a, R3_ST_WARN, "%s: %s", fname, why); return; }
    Pane *to = other(a);
    path_join(dest, sizeof dest, to->path, fname);
    struct stat st;
    if (lstat(dest, &st) == 0) { say(a, R3_ST_WARN, "%s is already in the other panel: not overwritten", fname); free(b); return; }
    if (write_file_excl(dest, b, n, 0644) != 0) { say(a, R3_ST_ERROR, "Cannot extract %s: %s", fname, strerror(errno)); free(b); return; }
    free(b);
    char sz[16];
    fmt_size(n, sz, sizeof sz);
    say(a, R3_ST_OK, "Extracted %s (%s bytes) to the other panel", fname, sz);
    refresh(a, 1 - a->act, fname);
}

/* Up/Down pick the member above or below; L1/R1 a page. The page follows the pick. */
static void list_move(Viewer *w, int action) {
    int n = w->arc.n, sel = w->sel;
    if (action == R3_UP) sel--;
    else if (action == R3_DOWN) sel++;
    else if (action == R3_PAGEUP) sel -= VIEW_ROWS;
    else if (action == R3_PAGEDOWN) sel += VIEW_ROWS;
    else if (action == R3_HOME) sel = 0;
    else if (action == R3_END) sel = n - 1;
    if (sel < 0) sel = 0;
    if (sel > n - 1) sel = n - 1;
    w->sel = sel;
    if (action == R3_HOME) set_page(w, 0);
    else set_page(w, (w->arc_line0 + sel) / VIEW_ROWS);
}

static void app_init(App *a, const char *left, const char *right) {
    char err[256];
    memset(a, 0, sizeof *a);
    a->job.fd_in = a->job.fd_out = -1;
    const char *start[2] = {left, right};
    for (int i = 0; i < 2; i++) {
        if (pane_cd(&a->p[i], &a->opt, start[i], NULL, err, sizeof err) != 0 && pane_cd(&a->p[i], &a->opt, "/", NULL, err, sizeof err) != 0)
            fprintf(stderr, "commander: cannot read %s: %s\n", start[i], err);
    }
}

static void app_free(App *a) {
    pane_free(&a->p[0]);
    pane_free(&a->p[1]);
    viewer_close_all(a);
    job_free(&a->job);
    for (int i = 0; i < a->job_nsrcs; i++) free(a->job_srcs[i]);
    free(a->job_srcs);
}

/* The full paths to work on: the tagged entries of the active panel, else the one under the cursor. Never "..". */
static int targets(App *a, char ***out) {
    Pane *p = cur(a);
    int tagged = pane_tagged(p), n = 0;
    char **list = calloc((size_t)(p->n > 0 ? p->n : 1), sizeof *list);
    if (!list) return -1;
    for (int i = 0; i < p->n; i++) {
        int use = tagged ? p->e[i].tagged : (i == p->sel);
        if (!use || !strcmp(p->e[i].name, "..")) continue;
        char full[PATH_MAX];
        if (path_join(full, sizeof full, p->path, p->e[i].name)) continue;
        list[n] = strdup(full);
        if (!list[n]) break;
        n++;
    }
    *out = list;
    return n;
}

static void free_list(char **l, int n) {
    for (int i = 0; i < n; i++) free(l[i]);
    free(l);
}

static void open_confirm(App *a, int cmd, const char *title, const char *yes, int sel) {
    a->mode = M_CONFIRM;
    a->conf_cmd = cmd;
    a->conf_sel = sel;
    copy_str(a->conf_title, sizeof a->conf_title, title);
    copy_str(a->conf_yes, sizeof a->conf_yes, yes);
}

static void open_input(App *a, int cmd, const char *prompt, const char *initial, const char *old) {
    a->mode = M_INPUT;
    a->in_cmd = cmd;
    copy_str(a->in_prompt, sizeof a->in_prompt, prompt);
    copy_str(a->buf, sizeof a->buf, initial);
    copy_str(a->in_old, sizeof a->in_old, old ? old : "");
}

/* Start the job that was confirmed (srcs is kept for the job's lifetime only to free it). */
static void start_job(App *a, int kind) {
    char **srcs;
    int n = targets(a, &srcs);
    if (n <= 0) { if (n == 0) free(srcs); say(a, R3_ST_WARN, "Nothing selected"); a->mode = M_BROWSE; return; }
    job_free(&a->job);
    a->job.fd_in = a->job.fd_out = -1;
    char err[256] = "";
    const char *dst = kind == J_DELETE ? NULL : other(a)->path;
    if (job_plan(&a->job, kind, srcs, n, dst, err, sizeof err) != 0) {
        free_list(srcs, n);
        job_free(&a->job);
        say(a, R3_ST_ERROR, "%s", err);
        a->mode = M_BROWSE;
        return;
    }
    for (int i = 0; i < a->job_nsrcs; i++) free(a->job_srcs[i]);
    free(a->job_srcs);
    a->job_srcs = srcs;
    a->job_nsrcs = n;
    a->job_active = 1;
    a->mode = M_JOB;
}

static void finish_job(App *a) {
    char msg[512];
    int style = job_summary(&a->job, msg, sizeof msg);
    say(a, style, "%s", msg);
    a->job_active = 0;
    a->mode = M_BROWSE;
    for (int i = 0; i < 2; i++) refresh(a, i, NULL);
    if (a->job.kind != J_DELETE || a->job.errors == 0)
        for (int i = 0; i < a->p[a->act].n; i++) a->p[a->act].e[i].tagged = 0;
    job_free(&a->job);
}

static void run_cmd(App *a, int cmd);

/* One slice of a running job; called from the main loop. */
static void app_tick(App *a, unsigned budget_ms) {
    if (a->mode == M_VIEW && a->view.ntabs && a->view.tab[a->view.cur].kind == TAB_PLAY) {
        if (pl_pump(&a->view.pl, (double)SDL_GetTicks())) a->view.gen++;
        return;
    }
    if (a->mode != M_JOB || !a->job_active) return;
    if (!job_step(&a->job, budget_ms) || a->job.finished) finish_job(a);
}

/* A Play tab is playing: the loop wakes about every frame. */
static int app_playing(const App *a) {
    return a->mode == M_VIEW && a->view.ntabs && a->view.tab[a->view.cur].kind == TAB_PLAY && a->view.pl.playing && !a->view.pl.ended;
}

static void run_cmd(App *a, int cmd) {
    Pane *p = cur(a);
    Entry *e = (p->n > 0) ? &p->e[p->sel] : NULL;
    char title[128], yes[192];
    char **t;
    int n;
    switch (cmd) {
    case C_VIEW: {
        if (!e || !strcmp(e->name, "..")) { say(a, R3_ST_WARN, "Pick a file to view"); break; }
        if (e->is_dir) { say(a, R3_ST_WARN, "%s is a folder: A opens it", e->name); break; }
        char full[PATH_MAX], err[256];
        path_join(full, sizeof full, p->path, e->name);
        if (viewer_open(&a->view, full, err, sizeof err) != 0) { say(a, R3_ST_ERROR, "%s", err); break; }
        a->mode = M_VIEW;
        break;
    }
    case C_COPY:
    case C_MOVE:
        n = targets(a, &t);
        if (n <= 0) { if (n == 0) free(t); say(a, R3_ST_WARN, "Nothing selected: pick a file or tag some with X"); break; }
        free_list(t, n);
        if (!strcmp(p->path, other(a)->path)) { say(a, R3_ST_WARN, "Both panels show the same folder: change the other one first"); break; }
        snprintf(title, sizeof title, "%s %d item%s?", cmd == C_COPY ? "Copy" : "Move", n, n == 1 ? "" : "s");
        {
            char dest[128];
            utf8_tail(other(a)->path, 26, dest, sizeof dest);
            snprintf(yes, sizeof yes, "Yes, %s to %s", cmd == C_COPY ? "copy" : "move", dest);
        }
        open_confirm(a, cmd, title, yes, 0);
        break;
    case C_DELETE: {
        n = targets(a, &t);
        if (n <= 0) { if (n == 0) free(t); say(a, R3_ST_WARN, "Nothing selected: pick a file or tag some with X"); break; }
        int dirs = 0;
        for (int i = 0; i < n; i++) {
            struct stat st;
            if (lstat(t[i], &st) == 0 && S_ISDIR(st.st_mode)) dirs++;
        }
        free_list(t, n);
        snprintf(title, sizeof title, "Delete %d item%s?", n, n == 1 ? "" : "s");
        snprintf(yes, sizeof yes, dirs ? "Yes, delete (folders and all inside)" : "Yes, delete (cannot be undone)");
        open_confirm(a, cmd, title, yes, 1);
        break;
    }
    case C_RENAME:
        if (!e || !strcmp(e->name, "..")) { say(a, R3_ST_WARN, "Pick something to rename"); break; }
        snprintf(title, sizeof title, "Rename %.100s to:", e->name);
        open_input(a, cmd, title, e->name, e->name);
        break;
    case C_MKDIR:
        open_input(a, cmd, "New folder name:", "New folder", NULL);
        break;
    case C_TAGALL:
        for (int i = 0; i < p->n; i++) p->e[i].tagged = strcmp(p->e[i].name, "..") != 0;
        say(a, R3_ST_HINT, "%d tagged", pane_tagged(p));
        break;
    case C_UNTAG:
        for (int i = 0; i < p->n; i++) p->e[i].tagged = 0;
        say(a, R3_ST_HINT, "Nothing tagged");
        break;
    case C_SORT:
        a->opt.sort = (a->opt.sort + 1) % 3;
        refresh_all(a);
        say(a, R3_ST_HINT, "Sorted by %s", SORT_NAMES[a->opt.sort]);
        break;
    case C_HIDDEN:
        a->opt.hidden = !a->opt.hidden;
        refresh_all(a);
        say(a, R3_ST_HINT, "Hidden files %s", a->opt.hidden ? "shown" : "hidden");
        break;
    case C_SWAP: {
        Pane tmp = a->p[0];
        a->p[0] = a->p[1];
        a->p[1] = tmp;
        say(a, R3_ST_HINT, "Panels swapped");
        break;
    }
    case C_SAME: {
        char err[256];
        if (pane_cd(other(a), &a->opt, p->path, NULL, err, sizeof err) != 0) say(a, R3_ST_ERROR, "%s", err);
        else say(a, R3_ST_HINT, "Other panel is here too");
        break;
    }
    case C_QUIT:
        a->quit = 1;
        break;
    }
}

static const char CHARS[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ._-()[]+,&'~";

/* Step the last character of the typed line through CHARS (by delta), or start with one. */
static void input_cycle(App *a, int delta) {
    size_t n = strlen(a->buf);
    int idx = -1;
    if (n > 0) {
        const char *at = strchr(CHARS, a->buf[n - 1]);
        if (at && a->buf[n - 1]) idx = (int)(at - CHARS);
    }
    int total = (int)strlen(CHARS);
    if (n == 0 || idx < 0) {
        if (n + 1 < sizeof a->buf) { a->buf[n] = CHARS[0]; a->buf[n + 1] = 0; }
        return;
    }
    a->buf[n - 1] = CHARS[((idx + delta) % total + total) % total];
}

static void input_backspace(App *a) {
    size_t n = strlen(a->buf);
    while (n > 0 && ((unsigned char)a->buf[n - 1] & 0xC0) == 0x80) n--;
    if (n > 0) n--;
    a->buf[n] = 0;
}

/* Typed text (a keyboard); only a line being typed takes it. */
static void app_text(App *a, const char *utf8) {
    if (a->mode != M_INPUT) return;
    size_t n = strlen(a->buf), m = strlen(utf8);
    if (n + m < sizeof a->buf) memcpy(a->buf + n, utf8, m + 1);
}

static void accept_input(App *a) {
    char err[256];
    Pane *p = cur(a);
    if (a->in_cmd == C_RENAME) {
        if (do_rename(p->path, a->in_old, a->buf, err, sizeof err) != 0) { say(a, R3_ST_ERROR, "Not renamed: %s", err); a->mode = M_BROWSE; return; }
        refresh(a, a->act, a->buf);
        refresh(a, 1 - a->act, NULL);
        say(a, R3_ST_OK, "Renamed to %s", a->buf);
    } else {
        if (do_mkdir(p->path, a->buf, err, sizeof err) != 0) { say(a, R3_ST_ERROR, "No folder made: %s", err); a->mode = M_BROWSE; return; }
        refresh(a, a->act, a->buf);
        refresh(a, 1 - a->act, NULL);
        say(a, R3_ST_OK, "Made folder %s", a->buf);
    }
    a->mode = M_BROWSE;
}

static void browse_action(App *a, int action) {
    Pane *p = cur(a);
    Entry *e = p->n > 0 ? &p->e[p->sel] : NULL;
    char err[256], full[PATH_MAX];
    switch (action) {
    case R3_UP: case R3_DOWN: case R3_PAGEUP: case R3_PAGEDOWN: case R3_HOME: case R3_END:
        p->sel = r3_list_key(p->sel, p->n, PANE_ROWS, action);
        break;
    case R3_LEFT:
    case R3_RIGHT:
        a->act = action == R3_LEFT ? 0 : 1;
        break;
    case R3_ACCEPT:
        if (!e) break;
        if (e->is_dir) {
            char old[PATH_MAX];
            copy_str(old, sizeof old, base_name(p->path));
            path_join(full, sizeof full, p->path, e->name);
            if (pane_cd(p, &a->opt, full, !strcmp(e->name, "..") ? old : NULL, err, sizeof err) != 0) say(a, R3_ST_ERROR, "%s", err);
            else a->reply[0] = 0;
        } else {
            run_cmd(a, C_VIEW);
        }
        break;
    case R3_BACK:
        if (strcmp(p->path, "/") != 0) {
            char old[PATH_MAX];
            copy_str(old, sizeof old, base_name(p->path));
            path_join(full, sizeof full, p->path, "..");
            if (pane_cd(p, &a->opt, full, old, err, sizeof err) != 0) say(a, R3_ST_ERROR, "%s", err);
            else a->reply[0] = 0;
        }
        break;
    case R3_X:
        if (e && strcmp(e->name, "..") != 0) e->tagged = !e->tagged;
        if (p->n) p->sel = (p->sel + 1) % p->n == 0 ? p->sel : p->sel + 1;
        break;
    case R3_Y: {
        int all_tagged = 1;
        for (int i = 0; i < p->n; i++)
            if (strcmp(p->e[i].name, "..") != 0 && !p->e[i].tagged) all_tagged = 0;
        run_cmd(a, all_tagged ? C_UNTAG : C_TAGALL);
        break;
    }
    case R3_MORE:
        a->mode = M_MENU;
        break;
    default:
        break;
    }
}

static void menu_values(const App *a, R3Item *items) {
    for (int i = 0; i < C_COUNT; i++) {
        copy_str(items[i].label, sizeof items[i].label, MENU[i].label);
        items[i].value[0] = 0;
        if (MENU[i].key[0]) copy_str(items[i].value, sizeof items[i].value, MENU[i].key);
    }
    copy_str(items[C_SORT].value, sizeof items[C_SORT].value, SORT_NAMES[a->opt.sort]);
    copy_str(items[C_HIDDEN].value, sizeof items[C_HIDDEN].value, a->opt.hidden ? "shown" : "hidden");
}

/* One action (a button or a key already mapped) in whatever mode the app is in. */
static void app_action(App *a, int action) {
    switch (a->mode) {
    case M_BROWSE:
        browse_action(a, action);
        break;
    case M_MENU:
        if (action == R3_BACK || action == R3_MORE) { a->mode = M_BROWSE; break; }
        if (action == R3_ACCEPT) { int c = a->menu_sel; a->mode = M_BROWSE; run_cmd(a, c); break; }
        a->menu_sel = r3_list_key(a->menu_sel, C_COUNT, r3_list_rows(C_COUNT + 2), action);
        break;
    case M_CONFIRM:
        if (action == R3_BACK) { a->mode = M_BROWSE; break; }
        if (action == R3_ACCEPT) {
            if (a->conf_sel == 0) start_job(a, a->conf_cmd == C_COPY ? J_COPY : (a->conf_cmd == C_MOVE ? J_MOVE : J_DELETE));
            else a->mode = M_BROWSE;
            break;
        }
        if (action == R3_UP || action == R3_DOWN) a->conf_sel = 1 - a->conf_sel;
        break;
    case M_INPUT:
        switch (action) {
        case R3_ACCEPT: accept_input(a); break;
        case R3_BACK: a->mode = M_BROWSE; say(a, R3_ST_HINT, "Cancelled"); break;
        case R3_UP: input_cycle(a, 1); break;
        case R3_DOWN: input_cycle(a, -1); break;
        case R3_PAGEUP: input_cycle(a, 10); break;
        case R3_PAGEDOWN: input_cycle(a, -10); break;
        case R3_RIGHT: if (strlen(a->buf) + 1 < sizeof a->buf) { size_t n = strlen(a->buf); a->buf[n] = CHARS[0]; a->buf[n + 1] = 0; } break;
        case R3_LEFT: input_backspace(a); break;
        case R3_HOME: a->buf[0] = 0; break;
        default: break;
        }
        break;
    case M_VIEW: {
        Viewer *w = &a->view;
        char err[256] = "";
        int on_list = w->has_arc && w->cur == w->arc_tab;
        if (on_list && action == R3_ACCEPT) { member_open(a); break; }
        if (on_list && action == R3_MORE) { member_extract(a); break; }
        if (w->tab[w->cur].kind == TAB_PLAY && action == R3_ACCEPT) {   /* A: pause, resume, or play again from the start */
            Player *pl = &w->pl;
            if (pl->ended) {
                char path[sizeof pl->path];
                const char *why = NULL;
                int mode = pl->mode;
                copy_str(path, sizeof path, pl->path);
                if (pl_open(pl, path, mode, (double)SDL_GetTicks(), &why) != 0) say(a, R3_ST_ERROR, "Cannot play again: %s", why ? why : "?");
            } else {
                pl_pause(pl, pl->playing);
            }
            w->gen++;
            break;
        }
        if (action == R3_BACK || action == R3_ACCEPT) { viewer_up(a); break; }
        if (on_list && (action == R3_UP || action == R3_DOWN || action == R3_PAGEUP || action == R3_PAGEDOWN || action == R3_HOME || action == R3_END)) {
            list_move(w, action);
            break;
        }
        if (action == R3_LEFT || action == R3_Y) viewer_show(w, (w->cur + w->ntabs - 1) % w->ntabs, err, sizeof err);
        else if (action == R3_RIGHT || action == R3_X) viewer_show(w, (w->cur + 1) % w->ntabs, err, sizeof err);
        else if (action == R3_UP || action == R3_PAGEUP) set_page(w, page_of(w) - 1);
        else if (action == R3_DOWN || action == R3_PAGEDOWN) set_page(w, page_of(w) + 1);
        else if (action == R3_HOME) set_page(w, 0);
        else if (action == R3_END) set_page(w, pages_of(w) - 1);
        if (err[0]) say(a, R3_ST_ERROR, "%s", err);
        break;
    }
    case M_JOB:
        if (action == R3_BACK) { job_cancel(&a->job); finish_job(a); }
        break;
    }
}

/* Keyboard keys that are not buttons: the function keys, Tab, Insert, Backspace. Returns 1 if used. */
static int app_key(App *a, SDL_Keycode k) {
    if (a->mode == M_INPUT && k == SDLK_BACKSPACE) { input_backspace(a); return 1; }
    if (a->mode != M_BROWSE) return 0;
    switch (k) {
    case SDLK_F2: run_cmd(a, C_RENAME); return 1;
    case SDLK_F3: run_cmd(a, C_VIEW); return 1;
    case SDLK_F5: run_cmd(a, C_COPY); return 1;
    case SDLK_F6: run_cmd(a, C_MOVE); return 1;
    case SDLK_F7: run_cmd(a, C_MKDIR); return 1;
    case SDLK_F8: run_cmd(a, C_DELETE); return 1;
    case SDLK_F10: run_cmd(a, C_QUIT); return 1;
    case SDLK_TAB: a->act = 1 - a->act; return 1;
    case SDLK_INSERT: case SDLK_SPACE: browse_action(a, R3_X); return 1;
    case SDLK_BACKSPACE: browse_action(a, R3_BACK); return 1;
    case SDLK_F9: app_action(a, R3_MORE); return 1;
    default: return 0;
    }
}

/* A keyboard key as an action; while a line is being typed, letters belong to the line. */
static int key_action(const App *a, SDL_Keycode k) {
    if (a->mode == M_INPUT) {
        switch (k) {
        case SDLK_RETURN: case SDLK_KP_ENTER: return R3_ACCEPT;
        case SDLK_ESCAPE: return R3_BACK;
        case SDLK_UP: return R3_UP;
        case SDLK_DOWN: return R3_DOWN;
        default: return R3_NONE;
        }
    }
    int act = r3_key_action(k);
    if (act == R3_MORE && (k == SDLK_TAB || k == SDLK_s)) return R3_NONE; /* Tab is the panel switch here, S is not a command */
    return act;
}

/* ---- drawing ------------------------------------------------------------------------------------------------------------------ */

static void mode_string(mode_t m, char out[11]) {
    out[0] = S_ISDIR(m) ? 'd' : (S_ISLNK(m) ? 'l' : '-');
    const char *rwx = "rwxrwxrwx";
    for (int i = 0; i < 9; i++) out[1 + i] = (m & (1u << (8 - i))) ? rwx[i] : '-';
    out[10] = 0;
}

static void draw_pane(const App *a, R3Screen *s, int idx) {
    const Pane *p = &a->p[idx];
    int col = idx * PANE_W, active = (idx == a->act);
    int page, pages;
    r3_list_window(p->n, p->sel, PANE_ROWS, &page, &pages);
    char right[32], foot[48];
    snprintf(right, sizeof right, "%d/%d", page, pages);
    foot[0] = 0;
    int tagged = pane_tagged(p);
    if (tagged) {
        unsigned long long bytes = 0;
        for (int i = 0; i < p->n; i++)
            if (p->e[i].tagged && !p->e[i].is_dir) bytes += (unsigned long long)p->e[i].size;
        char sz[16];
        fmt_size(bytes, sz, sizeof sz);
        snprintf(foot, sizeof foot, "%d tagged %s", tagged, sz);
    }
    char title[PATH_MAX + 4];
    utf8_tail(p->path, PANE_W - 8 - (int)strlen(right) - 1, title, sizeof title);
    R3Box b = {active, title, right, foot[0] ? foot : NULL, 0};
    r3_box(s, col, R3_DISPLAY_TOP, PANE_W, R3_DISPLAY_ROWS, &b);
    int first = (page - 1) * PANE_ROWS;
    for (int i = first; i < p->n && i < first + PANE_ROWS; i++) {
        const Entry *e = &p->e[i];
        int y = R3_DISPLAY_TOP + 1 + (i - first), x = col + 1;
        int st = R3_ST_BODY;
        if (i == p->sel) st = active ? R3_ST_SELECT : R3_ST_DIM;
        else if (e->tagged) st = R3_ST_WARN;
        else if (e->is_dir) st = R3_ST_STRONG;
        r3_fill(s, x, y, PANE_W - 2, 1, 32, R3_ST(st));
        r3_put(s, x, y, e->tagged ? '*' : ' ', R3_ST(st));
        char label[300];
        snprintf(label, sizeof label, "%s%s", e->is_dir ? "/" : "", e->name);
        r3_text_fit(s, x + 1, y, label, 21, R3_ST(st));
        char size[24];
        if (!strcmp(e->name, "..")) copy_str(size, sizeof size, "UP--DIR");
        else if (e->is_dir) copy_str(size, sizeof size, "<DIR>");
        else fmt_size((unsigned long long)e->size, size, sizeof size);
        r3_text_right(s, x + PANE_W - 3, y, size, R3_ST(st));
    }
}

static void free_space(const char *path, char *out, size_t cap) {
    struct statvfs sv;
    out[0] = 0;
    if (statvfs(path, &sv) == 0) {
        char sz[16];
        fmt_size((unsigned long long)sv.f_bavail * sv.f_frsize, sz, sizeof sz);
        snprintf(out, cap, "%s free", sz);
    }
}

static void centered_list(R3Screen *s, const char *title, const R3Item *items, int n, int sel, const char *foot, int w) {
    int h = n + 2;
    R3List l = {(R3_COLS - w) / 2, R3_DISPLAY_TOP + (R3_DISPLAY_ROWS - h) / 2, w, h, title, foot, items, n, sel, {NULL}};
    r3_list_dialog(s, &l);
}

/* The whole screen for the app as it is now. */
static void app_compose(const App *a, R3Screen *s) {
    r3_clear(s);
    char free_txt[48];
    free_space(a->p[a->act].path, free_txt, sizeof free_txt);
    r3_top_bar(s, "Commander", free_txt);

    R3Cmd c;
    memset(&c, 0, sizeof c);
    c.reply = a->reply[0] ? a->reply : "A open  B up  X tag  Start menu  Select+Start quit";
    c.reply_style = a->reply[0] ? a->reply_style : R3_ST_HINT;
    char line[PATH_MAX + 4], left[160], right[64];
    left[0] = right[0] = 0;

    if (a->mode == M_VIEW) {
        const Viewer *w = &a->view;
        const Tab *t = &w->tab[w->cur];
        int pages = pages_of(w), page = page_of(w);
        char pg[32], tabs[80];
        snprintf(pg, sizeof pg, "Page %d/%d", page + 1, pages);
        /* the tab strip: the one shown in brackets */
        int k = 0;
        for (int i = 0; i < w->ntabs && k < (int)sizeof tabs - 20; i++)
            k += snprintf(tabs + k, sizeof tabs - (size_t)k, i == w->cur ? "[%s]" : " %s ", w->tab[i].name);
        if (t->kind == TAB_LINES) {
            const View *v = &t->v;
            R3Box b = {1, w->title, pages > 1 ? pg : NULL, tabs, 0};
            r3_box(s, 0, R3_DISPLAY_TOP, R3_COLS, R3_DISPLAY_ROWS, &b);
            for (int r = 0; r < VIEW_ROWS; r++) {
                int ln = v->page * VIEW_ROWS + r;
                if (ln >= v->n) break;
                const unsigned char *cells = v->cells + (size_t)ln * VIEW_W, *fg = v->fg + (size_t)ln * VIEW_W;
                if (w->has_arc && w->cur == w->arc_tab && ln == w->arc_line0 + w->sel) { /* the member picked */
                    r3_fill(s, 1, R3_DISPLAY_TOP + 1 + r, VIEW_W, 1, 32, R3_ST(R3_ST_SELECT));
                    for (int c = 0; c < v->len[ln]; c++) r3_put(s, 1 + c, R3_DISPLAY_TOP + 1 + r, cells[c], r3_styles[R3_ST_SELECT][0], r3_styles[R3_ST_SELECT][1]);
                    continue;
                }
                for (int c = 0; c < v->len[ln]; c++) r3_put(s, 1 + c, R3_DISPLAY_TOP + 1 + r, cells[c], fg[c] ? fg[c] : R3_LIGHT_GRAY, R3_BLACK);
            }
        } else {
            r3_fill(s, 0, R3_DISPLAY_TOP, R3_COLS, R3_DISPLAY_ROWS, 32, R3_ST(R3_ST_BODY)); /* the pixels go over this */
            if (t->kind == TAB_PLAY && !w->pl.has_video) {   /* sound only: a title, the state and a bar, in cells */
                const Player *pl = &w->pl;
                R3Box b = {1, w->title, NULL, tabs, 0};
                r3_box(s, 0, R3_DISPLAY_TOP, R3_COLS, R3_DISPLAY_ROWS, &b);
                char now[16], tot[16], line[96];
                pl_time(now, sizeof now, pl->now);
                pl_time(tot, sizeof tot, pl->total);
                snprintf(line, sizeof line, "%s  %s / %s", pl->ended ? "Finished" : pl->playing ? "Playing" : "Paused", now, pl->total > 0 ? tot : "?:??");
                r3_text(s, 4, R3_DISPLAY_TOP + 8, line, R3_LIGHT_GRAY, R3_BLACK);
                int cells = R3_COLS - 8, done = pl->total > 0 ? (int)(cells * (pl->now > pl->total ? 1.0 : pl->now / pl->total)) : 0;
                for (int i = 0; i < cells; i++) r3_put(s, 4 + i, R3_DISPLAY_TOP + 10, i < done ? 219 : 176, R3_LIGHT_GREEN, R3_BLACK);
            }
        }
        char sz[16];
        fmt_size((unsigned long long)w->filesize, sz, sizeof sz);
        if (t->kind == TAB_PICTURE)
            snprintf(left, sizeof left, "%s, %dx%d%s", w->type.label, w->pic_w, w->pic_h,
                     w->pic.w > w->pic_w ? " (enlarged)" : w->pic.w < w->pic_w ? " (made smaller)" : "");
        else if (t->kind == TAB_PLAY) {
            char now[16], tot[16];
            pl_time(now, sizeof now, w->pl.now);
            pl_time(tot, sizeof tot, w->pl.total);
            snprintf(left, sizeof left, "%s  %s / %s", w->type.label, now, w->pl.total > 0 ? tot : "?:??");
        } else if (t->kind == TAB_ART) snprintf(left, sizeof left, "%s, %d columns  %s", w->type.label, w->art.w, pages > 1 ? pg : "");
        else snprintf(left, sizeof left, "%s, %s bytes", w->type.label, sz);
        static char reply[160];
        if (t->kind == TAB_PLAY) snprintf(reply, sizeof reply, "%s", w->pl.note[0] ? w->pl.note : w->pl.ended ? "A play again  Left/Right tab  B close" : "A pause or play  Left/Right tab  B close");
        else if (t->kind != TAB_LINES) snprintf(reply, sizeof reply, "%s  Left/Right tab", tabs);
        else if (w->has_arc && w->cur == w->arc_tab) snprintf(reply, sizeof reply, "Up/Down pick  A open  Start extract  B %s", a->vdepth ? "back" : "close");
        else snprintf(reply, sizeof reply, "%s", t->v.truncated ? "Only the first part is shown" : a->vdepth ? "Left/Right tab  Up/Down page  B back" : "Left/Right tab  Up/Down page  B close");
        c.reply = a->reply[0] && a->reply_style == R3_ST_ERROR ? a->reply : reply;
        c.reply_style = a->reply[0] && a->reply_style == R3_ST_ERROR ? R3_ST_ERROR : (t->kind == TAB_LINES && t->v.truncated ? R3_ST_WARN : R3_ST_HINT);
        c.line = w->title;
        c.left = left;
        r3_command_line(s, &c);
        return;
    }

    draw_pane(a, s, 0);
    draw_pane(a, s, 1);
    const Pane *p = &a->p[a->act];
    snprintf(line, sizeof line, "%s", p->path);
    c.line = line;

    if (p->n > 0) {
        const Entry *e = &p->e[p->sel];
        if (strcmp(e->name, "..") != 0) {
            char ms[11], sz[16], when[24] = "";
            mode_string(e->mode, ms);
            fmt_size((unsigned long long)e->size, sz, sizeof sz);
            struct tm tmv;
            if (localtime_r(&e->mtime, &tmv)) strftime(when, sizeof when, "%Y-%m-%d %H:%M", &tmv);
            snprintf(left, sizeof left, "%s %s%s  %s", ms, e->is_dir ? "" : sz, e->is_dir ? "" : "  ", when);
        } else {
            snprintf(left, sizeof left, "Parent folder");
        }
        snprintf(right, sizeof right, "%d/%d", p->sel + 1, p->n);
        c.left = left;
        c.right = right;
    }

    if (a->mode == M_JOB) {
        static const char *const verb[] = {"Copying", "Moving", "Deleting"};
        snprintf(left, sizeof left, "%s %.100s", verb[a->job.kind], a->job.current);
        char pct[8];
        snprintf(pct, sizeof pct, "%d%%", job_percent(&a->job));
        r3_text_fit(s, 1, R3_COMMAND_TOP + 2, left, 26, R3_ST(R3_ST_BODY));
        c.left = NULL;
        c.right = NULL;
        c.reply = "Working... B cancels";
        c.reply_style = R3_ST_WARN;
        r3_command_line(s, &c);
        r3_text_fit(s, 1, R3_COMMAND_TOP + 2, left, 26, R3_ST(R3_ST_BODY));
        r3_progress(s, 29, R3_COMMAND_TOP + 2, 29, job_percent(&a->job));
        r3_text_right(s, R3_COLS - 2, R3_COMMAND_TOP + 2, pct, R3_ST(R3_ST_HINT));
        return;
    }

    if (a->mode == M_INPUT) {
        c.reply = a->in_prompt;
        c.reply_style = R3_ST_HINT;
        c.line = a->buf;
        c.caret = 1;
        c.left = "Up/Down letter  Right new  Left del  A ok  B cancel";
        c.right = NULL;
    }
    r3_command_line(s, &c);

    if (a->mode == M_MENU) {
        R3Item items[C_COUNT];
        menu_values(a, items);
        centered_list(s, "Commander", items, C_COUNT, a->menu_sel, "A Choose  B Close", 40);
    } else if (a->mode == M_CONFIRM) {
        R3Item items[2];
        memset(items, 0, sizeof items);
        copy_str(items[0].label, sizeof items[0].label, a->conf_yes);
        copy_str(items[1].label, sizeof items[1].label, "No");
        centered_list(s, a->conf_title, items, 2, a->conf_sel, "A Choose  B Back", 48);
    }
}

/* The screen's pixels: the cells, then a picture or art page over the display view. */
static void app_render(const App *a, const R3Screen *s, uint32_t *px) {
    r3_render(s, px);
    if (a->mode == M_VIEW) viewer_overlay(&a->view, px);
}

/* r3_video_draw, with the viewer's pixels: a change of picture or page redraws even when the cells stay the same. */
static void app_video_draw(R3Video *v, SDL_Renderer *r, const R3Screen *s, const App *a, int force) {
    static unsigned last_gen = ~0u;
    unsigned gen = a->mode == M_VIEW ? a->view.gen * 2 + 1 : 0;
    int changed = force || !v->have_prev || gen != last_gen || memcmp(&v->prev, s, sizeof *s) != 0;
    if (changed) {
        app_render(a, s, v->px);
        SDL_UpdateTexture(v->tex, NULL, v->px, R3_W * (int)sizeof(uint32_t));
        v->prev = *s;
        v->have_prev = 1;
        last_gen = gen;
    }
    if (r3_video_due(v, changed)) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        SDL_RenderCopy(r, v->tex, NULL, NULL);
        SDL_RenderPresent(r);
    }
}

/* Open the viewer on a file at start (--view), on tab number `tab` (1 is Info; 0 keeps the one it opens on) and page `page`. */
static int start_viewer(App *a, const char *file, int tab, int page) {
    char err[256];
    if (viewer_open(&a->view, file, err, sizeof err) != 0) { fprintf(stderr, "commander: %s\n", err); return -1; }
    a->mode = M_VIEW;
    if (tab > 0) viewer_show(&a->view, tab - 1, err, sizeof err);
    if (page > 1) set_page(&a->view, page - 1);
    return 0;
}

/* ---- the tests that need no display ----------------------------------------------------------------------------------------------- */

static int g_fail;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; fprintf(stderr, "FAIL line %d: ", __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static int write_file(const char *path, const char *data, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(data, 1, n, f);
    return fclose(f) == 0 && w == n ? 0 : -1;
}

/* mkdir -p, without a shell (the smoke test runs under qemu, where /bin/sh is another architecture). */
static int mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    copy_str(tmp, sizeof tmp, path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    return (mkdir(tmp, 0755) != 0 && errno != EEXIST) ? -1 : 0;
}

/* rm -rf, never following links; makes folders writable first (the tests leave an unreadable file behind on purpose). */
static void rm_rf(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        chmod(path, 0700);
        DIR *d = opendir(path);
        struct dirent *de;
        while (d && (de = readdir(d))) {
            char sub[PATH_MAX];
            if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..") && path_join(sub, sizeof sub, path, de->d_name) == 0) rm_rf(sub);
        }
        if (d) closedir(d);
        rmdir(path);
    } else {
        unlink(path);
    }
}

static int exists(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0;
}

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = malloc(1 << 20);
    *n = buf ? fread(buf, 1, 1 << 20, f) : 0;
    fclose(f);
    return buf;
}

static void run_job_to_end(Job *j) {
    for (int guard = 0; guard < 100000 && job_step(j, 50); guard++) {}
}

static int dump_has(const R3Screen *s, const char *text) {
    char d[R3_ROWS][R3_COLS + 1];
    r3_dump(s, d);
    for (int r = 0; r < R3_ROWS; r++)
        if (strstr(d[r], text)) return 1;
    return 0;
}

static int row_has(const R3Screen *s, int row, const char *text) {
    char d[R3_ROWS][R3_COLS + 1];
    r3_dump(s, d);
    return strstr(d[row], text) != NULL;
}

/* With R3I6NOS_FM_SHOTS set to a folder, the self-test saves the screen in each state there as <name>.bmp. */
static void shot(const App *a, const R3Screen *s, const char *name) {
    const char *dir = getenv("R3I6NOS_FM_SHOTS");
    if (!dir || !*dir) return;
    uint32_t *px = malloc((size_t)R3_W * R3_H * 4);
    if (!px) return;
    app_render(a, s, px);
    SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(px, R3_W, R3_H, 32, R3_W * 4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/%s.bmp", dir, name);
    if (surf) { SDL_SaveBMP(surf, path); SDL_FreeSurface(surf); }
    free(px);
}

static void test_theme(void) {
    CHECK(r3_glyph_hash() == R3_GLYPH_HASH, "glyph hash %08x differs from the Lua theme's %08x", r3_glyph_hash(), R3_GLYPH_HASH);
    unsigned char g[32];
    CHECK(r3_decode("A\xC3\x96z", g, 32) == 3 && g[0] == 'A' && g[1] == 153 && g[2] == 'z', "decode: A, O-umlaut, z");
    CHECK(r3_decode("\xE2\x94\x80", g, 32) == 1 && g[0] == 196, "decode: box line");
    CHECK(r3_decode("\xE6\x97\xA5", g, 32) == 1 && g[0] == '?', "decode: unknown is ?");
    CHECK(r3_decode("\xFF", g, 32) == 1 && g[0] == '?', "decode: bad byte is ?");
    CHECK(r3_decode("a\tb", g, 32) == 3 && g[1] == 32, "decode: tab is a space");
    int n = r3_fit("abcdef", 4, g);
    CHECK(n == 4 && g[3] == 175, "fit ends in >>");
    n = r3_fit_tail("/aaa/bbb/ccc", 6, g);
    CHECK(n == 6 && g[0] == 174 && g[1] == 'b', "tail starts with <<");
    for (int i = 0; i < R3_ST_COUNT; i++)
        CHECK(r3_contrast(r3_styles[i][0], r3_styles[i][1]) >= 4.5, "style %d fails 4.5:1", i);
    CHECK(r3_contrast(R3_DARK_GRAY, R3_BLACK) < 4.5, "dark gray on black must fail");
    CHECK(r3_pad_action(8) == R3_UP && r3_pad_action(9) == R3_DOWN && r3_pad_action(10) == R3_LEFT && r3_pad_action(11) == R3_RIGHT, "d-pad");
    CHECK(r3_pad_action(1) == R3_ACCEPT && r3_pad_action(0) == R3_BACK && r3_pad_action(13) == R3_MORE, "A, B, Start");
    CHECK(r3_pad_action(2) == R3_X && r3_pad_action(3) == R3_Y && r3_pad_action(12) == R3_SELECT, "X, Y, Select");
    CHECK(r3_list_key(0, 40, 18, R3_UP) == 39 && r3_list_key(39, 40, 18, R3_DOWN) == 0, "list wraps");
    CHECK(r3_list_key(5, 40, 18, R3_PAGEDOWN) == 23 && r3_list_key(30, 40, 18, R3_PAGEDOWN) == 39, "page clamps");
    CHECK(r3_list_key(5, 40, 18, R3_PAGEUP) == 0 && r3_list_key(20, 40, 18, R3_HOME) == 0 && r3_list_key(20, 40, 18, R3_END) == 39, "page up, home, end");
}

static void test_files(const char *root) {
    char p[PATH_MAX], q[PATH_MAX], err[256];
    const char *tree[] = {"src/sub", "dst", "outside"};
    for (int i = 0; i < 3; i++) {
        path_join(p, sizeof p, root, tree[i]);
        CHECK(mkdir_p(p) == 0, "mkdir %s", tree[i]);
    }
    path_join(p, sizeof p, root, "src/a.txt");
    write_file(p, "hello\n", 6);
    path_join(p, sizeof p, root, "src/sub/b.bin");
    char *big = malloc(300000);
    for (int i = 0; i < 300000; i++) big[i] = (char)(i * 7);
    write_file(p, big, 300000);
    path_join(p, sizeof p, root, "outside/keep.txt");
    write_file(p, "keep", 4);
    path_join(p, sizeof p, root, "src/sub/link");
    path_join(q, sizeof q, root, "outside");
    CHECK(symlink(q, p) == 0, "make a link to a folder outside");
    path_join(p, sizeof p, root, "src/alink");
    CHECK(symlink("a.txt", p) == 0, "make a relative link");

    char src[PATH_MAX], dst[PATH_MAX];
    path_join(src, sizeof src, root, "src");
    path_join(dst, sizeof dst, root, "dst");

    /* copy a folder: contents, link kept as a link, bytes equal */
    Job j;
    memset(&j, 0, sizeof j);
    char *list[1] = {src};
    CHECK(job_plan(&j, J_COPY, list, 1, dst, err, sizeof err) == 0, "plan copy: %s", err);
    run_job_to_end(&j);
    CHECK(j.errors == 0 && j.skipped_exists == 0, "copy errors=%d exists=%d %s", j.errors, j.skipped_exists, j.first_error);
    path_join(p, sizeof p, root, "dst/src/sub/b.bin");
    size_t n = 0;
    char *back = slurp(p, &n);
    CHECK(back && n == 300000 && memcmp(back, big, 300000) == 0, "copied bytes are equal (%zu)", n);
    free(back);
    path_join(p, sizeof p, root, "dst/src/alink");
    char target[64] = "";
    ssize_t tl = readlink(p, target, sizeof target - 1);
    CHECK(tl == 5 && !strncmp(target, "a.txt", 5), "a link is copied as a link");
    path_join(p, sizeof p, root, "dst/src/sub/link");
    struct stat ls;
    CHECK(lstat(p, &ls) == 0 && S_ISLNK(ls.st_mode), "a link to a folder is copied as a link, not followed");
    path_join(p, sizeof p, root, "src/a.txt");
    path_join(q, sizeof q, root, "dst/src/a.txt");
    struct stat s1, s2;
    CHECK(stat(p, &s1) == 0 && stat(q, &s2) == 0 && s1.st_mtime == s2.st_mtime, "modification time kept");
    job_free(&j);

    /* again: nothing is overwritten */
    memset(&j, 0, sizeof j);
    CHECK(job_plan(&j, J_COPY, list, 1, dst, err, sizeof err) == 0, "plan again");
    CHECK(j.skipped_exists == 1 && j.nops == 0, "second copy skips (exists=%d ops=%d)", j.skipped_exists, j.nops);
    job_free(&j);

    /* a folder cannot go into itself */
    memset(&j, 0, sizeof j);
    char inside[PATH_MAX];
    path_join(inside, sizeof inside, root, "src/sub");
    CHECK(job_plan(&j, J_COPY, list, 1, inside, err, sizeof err) == 0 && j.errors == 1 && j.nops == 0, "refuses to copy into itself");
    job_free(&j);

    /* move on one filesystem is a rename; forced copy+delete removes the source after a good copy */
    char mv[PATH_MAX];
    path_join(mv, sizeof mv, root, "dst/src");
    char *mlist[1] = {mv};
    char mdst[PATH_MAX];
    path_join(mdst, sizeof mdst, root, "outside");
    memset(&j, 0, sizeof j);
    CHECK(job_plan(&j, J_MOVE, mlist, 1, mdst, err, sizeof err) == 0 && j.nops == 1 && j.ops[0].type == OP_RENAME, "move on one filesystem is one rename");
    run_job_to_end(&j);
    path_join(p, sizeof p, root, "outside/src/a.txt");
    CHECK(j.errors == 0 && exists(p) && !exists(mv), "renamed: it is there and not here");
    job_free(&j);
    path_join(mv, sizeof mv, root, "outside/src");
    mlist[0] = mv;
    path_join(mdst, sizeof mdst, root, "dst");
    memset(&j, 0, sizeof j);
    j.force_copy = 1;
    CHECK(job_plan(&j, J_MOVE, mlist, 1, mdst, err, sizeof err) == 0 && j.nops > 5, "forced move plans copy + delete");
    run_job_to_end(&j);
    path_join(p, sizeof p, root, "dst/src/sub/b.bin");
    CHECK(j.errors == 0 && exists(p) && !exists(mv), "copy + delete move: source gone, copy there (errors=%d %s)", j.errors, j.first_error);
    path_join(p, sizeof p, root, "outside/keep.txt");
    CHECK(exists(p), "deleting a link did not follow it into the folder it points at");
    job_free(&j);

    /* a failed copy keeps its source (not as root: root can read anything) */
    if (geteuid() != 0) {
        path_join(p, sizeof p, root, "src/secret.txt");
        write_file(p, "x", 1);
        chmod(p, 0);
        path_join(mv, sizeof mv, root, "src");
        mlist[0] = mv;
        char dst2[PATH_MAX];
        path_join(dst2, sizeof dst2, root, "outside");
        memset(&j, 0, sizeof j);
        j.force_copy = 1;
        CHECK(job_plan(&j, J_MOVE, mlist, 1, dst2, err, sizeof err) == 0, "plan move with an unreadable file");
        run_job_to_end(&j);
        CHECK(j.errors >= 1 && exists(p), "an unreadable file is reported and its source is kept (errors=%d)", j.errors);
        path_join(q, sizeof q, root, "outside/src/secret.txt");
        CHECK(!exists(q), "no partial copy of the unreadable file");
        job_free(&j);
        chmod(p, 0644);
    }

    /* delete: never follows links, never the root */
    path_join(p, sizeof p, root, "dst/src");
    char *dlist[1] = {p};
    memset(&j, 0, sizeof j);
    CHECK(job_plan(&j, J_DELETE, dlist, 1, NULL, err, sizeof err) == 0, "plan delete");
    run_job_to_end(&j);
    CHECK(j.errors == 0 && !exists(p), "deleted the tree (errors=%d %s)", j.errors, j.first_error);
    path_join(q, sizeof q, root, "outside/keep.txt");
    CHECK(exists(q), "delete did not follow the link out of the tree");
    job_free(&j);
    char *rootl[1] = {"/"};
    memset(&j, 0, sizeof j);
    CHECK(job_plan(&j, J_DELETE, rootl, 1, NULL, err, sizeof err) != 0, "refuses to delete /");
    job_free(&j);
    memset(&j, 0, sizeof j);
    CHECK(job_plan(&j, J_MOVE, rootl, 1, dst, err, sizeof err) != 0, "refuses to move /");
    job_free(&j);

    /* cancel removes the half-copied file */
    path_join(p, sizeof p, root, "big.bin");
    write_file(p, big, 300000);
    path_join(dst, sizeof dst, root, "outside");
    char *clist[1] = {p};
    memset(&j, 0, sizeof j);
    CHECK(job_plan(&j, J_COPY, clist, 1, dst, err, sizeof err) == 0, "plan cancel test");
    job_step(&j, 0);
    job_cancel(&j);
    path_join(q, sizeof q, root, "outside/big.bin");
    CHECK(j.cancelled && !exists(q), "a cancelled copy leaves no partial file");
    job_free(&j);

    /* rename and new folder */
    path_join(p, sizeof p, root, "one.txt");
    write_file(p, "1", 1);
    path_join(p, sizeof p, root, "two.txt");
    write_file(p, "2", 1);
    CHECK(do_rename(root, "one.txt", "uno.txt", err, sizeof err) == 0, "rename ok: %s", err);
    CHECK(do_rename(root, "uno.txt", "two.txt", err, sizeof err) != 0 && strstr(err, "exists"), "rename onto an existing name is refused: %s", err);
    CHECK(do_rename(root, "uno.txt", "a/b", err, sizeof err) != 0, "rename with / refused");
    CHECK(do_rename(root, "uno.txt", "..", err, sizeof err) != 0, "rename to .. refused");
    CHECK(do_rename(root, "uno.txt", "", err, sizeof err) != 0, "rename to nothing refused");
    CHECK(do_mkdir(root, "New folder", err, sizeof err) == 0, "mkdir ok: %s", err);
    CHECK(do_mkdir(root, "New folder", err, sizeof err) != 0, "mkdir on an existing name refused");
    free(big);
}

static void test_ui(const char *root) {
    char p[PATH_MAX], err[256];
    const char *tree[] = {"ui/left/Zeta", "ui/left/alpha", "ui/right"};
    for (int i = 0; i < 3; i++) {
        path_join(p, sizeof p, root, tree[i]);
        CHECK(mkdir_p(p) == 0, "mkdir %s", tree[i]);
    }
    char left[PATH_MAX], right[PATH_MAX];
    path_join(left, sizeof left, root, "ui/left");
    path_join(right, sizeof right, root, "ui/right");
    for (int i = 0; i < 40; i++) {
        char name[32];
        snprintf(name, sizeof name, "file%02d.txt", i);
        path_join(p, sizeof p, left, name);
        write_file(p, "0123456789", 10);
    }
    path_join(p, sizeof p, left, ".hidden");
    write_file(p, "h", 1);
    path_join(p, sizeof p, left, "Notes.txt");
    write_file(p, "line one\nline two\n\ttabbed\n", 26);
    path_join(p, sizeof p, left, "data.bin");
    write_file(p, "\x00\x01\x02" "ABC" "\xff\xfe", 8);

    App a;
    app_init(&a, left, right);
    R3Screen s;
    app_compose(&a, &s);
    CHECK(row_has(&s, 0, "Commander"), "top bar names the app");
    CHECK(row_has(&s, 0, "free"), "top bar shows free space");
    /* entries: .. then folders (alpha, Zeta) then files, case-insensitively */
    CHECK(!strcmp(a.p[0].e[0].name, ".."), "first entry is ..");
    CHECK(!strcmp(a.p[0].e[1].name, "alpha") && !strcmp(a.p[0].e[2].name, "Zeta"), "folders first, case-insensitive: %s %s", a.p[0].e[1].name, a.p[0].e[2].name);
    CHECK(pane_find(&a.p[0], ".hidden") < 0, "hidden files are hidden");
    CHECK(a.p[0].n == 1 + 2 + 40 + 2, "entry count %d", a.p[0].n);
    CHECK(row_has(&s, 3, "/alpha") || row_has(&s, 2, "/alpha"), "a folder shows with a slash");
    CHECK(dump_has(&s, "<DIR>") && dump_has(&s, "UP--DIR"), "folder markers");
    CHECK(dump_has(&s, "10") && dump_has(&s, "1/3"), "sizes and page 1/3 (45 entries, 18 a page)");
    CHECK(row_has(&s, 22, "ui/left") || row_has(&s, 22, "left"), "prompt shows the folder");
    CHECK(row_has(&s, 21, "A open"), "idle tip");
    shot(&a, &s, "1-browse");
    /* the three rows of the command line and the top row are the only things outside the panels */
    for (int c = 0; c < R3_COLS; c++) CHECK(s.bg[c] == R3_BLUE, "top bar is blue");

    /* paging: whole pages */
    app_action(&a, R3_PAGEDOWN);
    CHECK(a.p[0].sel == 18, "page down moves 18");
    app_compose(&a, &s);
    CHECK(dump_has(&s, "2/3"), "page 2 of 3");
    app_action(&a, R3_END);
    CHECK(a.p[0].sel == a.p[0].n - 1, "end");
    app_action(&a, R3_HOME);
    CHECK(a.p[0].sel == 0, "home");

    /* every cell with text passes 4.5:1 */
    for (int i = 0; i < R3_COLS * R3_ROWS; i++)
        if (s.ch[i] != 32 && s.ch[i] != 0) CHECK(r3_contrast(s.fg[i], s.bg[i]) >= 4.5, "low contrast at cell %d (%d on %d)", i, s.fg[i], s.bg[i]);

    /* tagging and the status row */
    a.p[0].sel = pane_find(&a.p[0], "file00.txt");
    app_action(&a, R3_X);
    app_action(&a, R3_X);
    CHECK(pane_tagged(&a.p[0]) == 2, "X tags and moves down (%d)", pane_tagged(&a.p[0]));
    app_compose(&a, &s);
    CHECK(dump_has(&s, "2 tagged 20"), "foot shows the tagged count and bytes");
    app_action(&a, R3_Y);
    CHECK(pane_tagged(&a.p[0]) == a.p[0].n - 1, "Y tags all but ..");
    app_action(&a, R3_Y);
    CHECK(pane_tagged(&a.p[0]) == 0, "Y again untags all");

    /* switch panel with left/right */
    app_action(&a, R3_RIGHT);
    CHECK(a.act == 1, "right switches to the right panel");
    app_action(&a, R3_LEFT);
    CHECK(a.act == 0, "left switches back");

    /* enter a folder and come back, the folder stays selected */
    a.p[0].sel = pane_find(&a.p[0], "alpha");
    app_action(&a, R3_ACCEPT);
    CHECK(strstr(a.p[0].path, "alpha") != NULL, "A enters the folder");
    app_action(&a, R3_BACK);
    CHECK(!strcmp(a.p[0].e[a.p[0].sel].name, "alpha"), "B goes up and keeps the folder selected");

    /* the menu */
    app_action(&a, R3_MORE);
    CHECK(a.mode == M_MENU, "Start opens the menu");
    app_compose(&a, &s);
    CHECK(dump_has(&s, "Make directory") && dump_has(&s, "Quit") && dump_has(&s, "Hidden files"), "menu items");
    shot(&a, &s, "2-menu");
    app_action(&a, R3_BACK);
    CHECK(a.mode == M_BROWSE, "B closes the menu");

    /* hidden files and sorting from the menu */
    run_cmd(&a, C_HIDDEN);
    CHECK(pane_find(&a.p[0], ".hidden") >= 0, "hidden files shown");
    run_cmd(&a, C_HIDDEN);
    run_cmd(&a, C_SORT);
    CHECK(a.opt.sort == SORT_SIZE, "sort by size");
    run_cmd(&a, C_SORT);
    run_cmd(&a, C_SORT);
    CHECK(a.opt.sort == SORT_NAME, "sort cycles back to name");

    /* copy needs two different folders and asks first, with Yes preselected */
    a.p[0].sel = pane_find(&a.p[0], "file01.txt");
    run_cmd(&a, C_COPY);
    CHECK(a.mode == M_CONFIRM && a.conf_sel == 0, "copy asks, Yes first");
    app_compose(&a, &s);
    CHECK(dump_has(&s, "Copy 1 item?") && dump_has(&s, "Yes, copy to"), "copy confirmation text");
    shot(&a, &s, "3-confirm-copy");
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_JOB, "A starts the job");
    app_compose(&a, &s);
    CHECK(row_has(&s, 21, "B cancels"), "job tip");
    shot(&a, &s, "4-job");
    for (int g = 0; g < 1000 && a.mode == M_JOB; g++) app_tick(&a, 50);
    CHECK(a.mode == M_BROWSE && strstr(a.reply, "Copied 1 item"), "copy finished: %s", a.reply);
    path_join(p, sizeof p, right, "file01.txt");
    CHECK(exists(p), "copied into the other panel");
    CHECK(pane_find(&a.p[1], "file01.txt") >= 0, "the other panel was refreshed");
    /* again: skipped, shown as a warning */
    run_cmd(&a, C_COPY);
    app_action(&a, R3_ACCEPT);
    for (int g = 0; g < 1000 && a.mode == M_JOB; g++) app_tick(&a, 50);
    CHECK(strstr(a.reply, "skipped") && a.reply_style == R3_ST_WARN, "second copy is reported as skipped: %s", a.reply);

    /* delete asks with No preselected: A alone deletes nothing */
    a.p[0].sel = pane_find(&a.p[0], "file02.txt");
    run_cmd(&a, C_DELETE);
    CHECK(a.mode == M_CONFIRM && a.conf_sel == 1, "delete asks, No first");
    app_action(&a, R3_ACCEPT);
    path_join(p, sizeof p, left, "file02.txt");
    CHECK(a.mode == M_BROWSE && exists(p), "A on the default No keeps the file");
    run_cmd(&a, C_DELETE);
    app_action(&a, R3_UP);
    CHECK(a.conf_sel == 0, "up moves to Yes");
    app_action(&a, R3_ACCEPT);
    for (int g = 0; g < 1000 && a.mode == M_JOB; g++) app_tick(&a, 50);
    CHECK(!exists(p) && strstr(a.reply, "Deleted 1 item"), "delete done: %s", a.reply);

    /* B cancels a running job */
    a.p[0].sel = pane_find(&a.p[0], "file03.txt");
    run_cmd(&a, C_COPY);
    app_action(&a, R3_ACCEPT);
    app_action(&a, R3_BACK);
    CHECK(a.mode == M_BROWSE && strstr(a.reply, "Cancelled"), "B cancels: %s", a.reply);

    /* rename with the pad: Up/Down cycle the last letter, Right adds one, Left deletes */
    a.p[0].sel = pane_find(&a.p[0], "file04.txt");
    run_cmd(&a, C_RENAME);
    CHECK(a.mode == M_INPUT && !strcmp(a.buf, "file04.txt"), "rename starts with the old name");
    app_action(&a, R3_LEFT);
    app_action(&a, R3_LEFT);
    app_action(&a, R3_LEFT);
    app_action(&a, R3_LEFT);
    CHECK(!strcmp(a.buf, "file04"), "Left deletes: %s", a.buf);
    app_action(&a, R3_UP);
    CHECK(!strcmp(a.buf, "file05"), "Up steps the last character: %s", a.buf);
    app_action(&a, R3_RIGHT);
    CHECK(!strcmp(a.buf, "file05a"), "Right adds a letter: %s", a.buf);
    app_action(&a, R3_DOWN);
    CHECK(!strcmp(a.buf, "file05~"), "Down steps back through the list (a wraps to the end of it): '%s'", a.buf);
    app_text(&a, "x.txt");
    CHECK(!strcmp(a.buf, "file05~x.txt"), "typed text is added: %s", a.buf);
    app_compose(&a, &s);
    CHECK(row_has(&s, 21, "Rename file04.txt to:") && row_has(&s, 22, "file05~x.txt"), "the line is shown on the prompt row");
    shot(&a, &s, "5-rename");
    app_action(&a, R3_BACK);
    CHECK(a.mode == M_BROWSE, "B cancels typing");
    path_join(p, sizeof p, left, "file04.txt");
    CHECK(exists(p), "a cancelled rename changes nothing");
    run_cmd(&a, C_RENAME);
    a.buf[0] = 0;
    app_text(&a, "renamed.txt");
    app_action(&a, R3_ACCEPT);
    path_join(p, sizeof p, left, "renamed.txt");
    CHECK(exists(p) && !strcmp(a.p[0].e[a.p[0].sel].name, "renamed.txt"), "renamed and selected: %s", a.reply);
    run_cmd(&a, C_RENAME);
    a.buf[0] = 0;
    app_text(&a, "file05.txt");
    app_action(&a, R3_ACCEPT);
    CHECK(strstr(a.reply, "already exists") && a.reply_style == R3_ST_ERROR, "an existing name is refused: %s", a.reply);

    /* new folder */
    run_cmd(&a, C_MKDIR);
    CHECK(a.mode == M_INPUT && !strcmp(a.buf, "New folder"), "new folder starts with a name");
    app_action(&a, R3_ACCEPT);
    path_join(p, sizeof p, left, "New folder");
    CHECK(exists(p), "folder made: %s", a.reply);

    /* the viewer: text wraps into pages, a binary file is hex */
    a.p[0].sel = pane_find(&a.p[0], "Notes.txt");
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_VIEW && !viewer_tab(&a.view)->v.hex && viewer_tab(&a.view)->v.n >= 3, "text view (%d lines)", viewer_tab(&a.view)->v.n);
    app_compose(&a, &s);
    CHECK(dump_has(&s, "line one") && dump_has(&s, "    tabbed") && !dump_has(&s, "Page 1/1"), "text and tab shown, no page number for one page");
    shot(&a, &s, "6-view-text");
    app_action(&a, R3_BACK);
    CHECK(a.mode == M_BROWSE, "B closes the viewer");
    a.p[0].sel = pane_find(&a.p[0], "data.bin");
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_VIEW && viewer_tab(&a.view)->v.hex, "binary shows as hex");
    app_compose(&a, &s);
    CHECK(dump_has(&s, "000000  00 01 02 41 42 43 FF FE  |...ABC..|"), "hex line");
    shot(&a, &s, "7-view-hex");
    app_action(&a, R3_BACK);

    /* a long line wraps at 62 and pages by 18 */
    char *longtxt = malloc(62 * 40 + 1);
    memset(longtxt, 'w', 62 * 40);
    path_join(p, sizeof p, left, "long.txt");
    write_file(p, longtxt, 62 * 40);
    free(longtxt);
    View v;
    CHECK(view_load(&v, p, err, sizeof err) == 0 && v.n == 40 && view_pages(&v) == 3, "62 columns a line, 18 lines a page (%d lines)", v.n);
    view_free(&v);

    /* quit */
    run_cmd(&a, C_QUIT);
    CHECK(a.quit, "quit");
    app_free(&a);
}

/* ---- the formats: identification, Info, Content, pictures and art (fixtures from selftest_data.h) ------------------------------ */

static char g_fxdir[PATH_MAX];

static const Fixture *fx_find(const char *id) {
    for (size_t i = 0; i < sizeof FIXTURES / sizeof FIXTURES[0]; i++)
        if (!strcmp(FIXTURES[i].id, id)) return &FIXTURES[i];
    return NULL;
}

/* The fixture written to the test folder; its path. */
static const char *fx_path(const char *id) {
    static char p[PATH_MAX];
    const Fixture *fx = fx_find(id);
    if (!fx) return "";
    path_join(p, sizeof p, g_fxdir, fx->name);
    if (!exists(p)) write_file(p, (const char *)fx->data, fx->len);
    return p;
}

static FType fx_type(const char *id) {
    FFile f;
    char err[256];
    FType t = {"", "", -1};
    if (ff_open(&f, fx_path(id), err, sizeof err) != 0) return t;
    t = ff_identify(&f);
    ff_close(&f);
    return t;
}

/* Whether the Info page has row `key` whose value contains `want`. */
static int fx_info(const char *id, const char *key, const char *want) {
    FFile f;
    char err[256];
    if (ff_open(&f, fx_path(id), err, sizeof err) != 0) return 0;
    FInfo in = {0, 0, 0};
    ff_info(&f, ff_identify(&f), &in);
    ff_close(&f);
    int ok = 0;
    for (int i = 0; i < in.n; i++)
        if (!strcmp(in.r[i].k, key) && strstr(in.r[i].v, want)) ok = 1;
    if (!ok) {
        fprintf(stderr, "  %s Info has:\n", id);
        for (int i = 0; i < in.n; i++) fprintf(stderr, "    %s: %s\n", in.r[i].k, in.r[i].v);
    }
    info_free(&in);
    return ok;
}

/* Whether the Content page (lines or unpacked bytes) contains `want` (and says what it has when not, unless quiet). */
static int fx_content_q(const char *id, const char *want, int quiet) {
    FFile f;
    char err[256], name[32];
    if (ff_open(&f, fx_path(id), err, sizeof err) != 0) return 0;
    FText t = {0, 0, 0, 0, 0};
    uint8_t *raw = NULL;
    size_t rawn = 0;
    int fc = ff_content(&f, ff_identify(&f), &t, &raw, &rawn, name, sizeof name);
    ff_close(&f);
    int ok = (fc == FC_LINES && t.s && strstr(t.s, want)) || (fc == FC_BYTES && raw && memmem(raw, rawn, want, strlen(want)));
    if (!ok && !quiet) fprintf(stderr, "  %s Content (%s): %s\n", id, name, fc == FC_LINES && t.s ? t.s : "(none)");
    free(raw);
    text_free(&t);
    return ok;
}

static int fx_content(const char *id, const char *want) { return fx_content_q(id, want, 0); }

static void test_formats(const char *root) {
    path_join(g_fxdir, sizeof g_fxdir, root, "formats");
    CHECK(mkdir_p(g_fxdir) == 0, "fixtures folder");

    /* identification */
    static const struct { const char *id; int kind; } kinds[] = {
        {"jpeg_base", K_JPEG}, {"jpeg_prog", K_JPEG}, {"png_rgba", K_PNG}, {"png_adam7", K_PNG}, {"gif", K_GIF}, {"bmp8", K_BMP}, {"tga", K_TGA},
        {"pcx", K_PCX}, {"ppm", K_PNM}, {"ico", K_ICO}, {"qoi", K_QOI}, {"wav", K_WAV}, {"midi", K_MIDI}, {"mod", K_MOD}, {"mp3", K_MP3},
        {"flac", K_FLAC}, {"docx", K_OOXML}, {"targz", K_GZIP}, {"gz", K_GZIP}, {"tarxz", K_XZ}, {"xz", K_XZ}, {"tarzst", K_ZSTD}, {"zst", K_ZSTD}, {"deb_xz", K_DEB}, {"deb_zst", K_DEB}, {"sqlite", K_SQLITE}, {"pdf", K_PDF}, {"rtf", K_RTF},
        {"html", K_HTML}, {"mbr", K_DISK}, {"ansi", K_ANSI}, {"xbin", K_XBIN},
    };
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        FType t = fx_type(kinds[i].id);
        CHECK(t.kind == kinds[i].kind, "%s is identified as %s (kind %d, want %d)", kinds[i].id, t.label, t.kind, kinds[i].kind);
    }

    /* Info */
    CHECK(fx_info("jpeg_base", "Dimensions", "24 x 16") && fx_info("jpeg_base", "Coding", "baseline"), "JPEG size and coding");
    CHECK(fx_info("jpeg_prog", "Coding", "progressive"), "progressive JPEG is named");
    CHECK(fx_info("png_adam7", "Colour", "palette, 2 bits") && fx_info("png_adam7", "Colour", "interlaced") && fx_info("png_adam7", "Title", "Adam7 test"), "PNG header and text");
    CHECK(fx_info("gif", "Dimensions", "10 x 6"), "GIF size");
    CHECK(fx_info("wav", "Sound", "mono, 8000 Hz, 16-bit") && fx_info("wav", "Duration", "0.05 s") && fx_info("wav", "Coding", "PCM"), "WAV");
    CHECK(fx_info("midi", "Title", "Test song") && fx_info("midi", "Duration", "2.00 s") && fx_info("midi", "Notes", "4 on 1 channel"), "MIDI: name, length, notes");
    CHECK(fx_info("midi", "Tempo", "120 BPM"), "MIDI tempo");
    CHECK(fx_info("mod", "Title", "commander test") && fx_info("mod", "Channels", "4"), "MOD");
    CHECK(fx_info("mp3", "Title", "Mp3 title") && fx_info("mp3", "Bit rate", "128 kbit/s") && fx_info("mp3", "Sound", "44100 Hz"), "MP3 tag and frame");
    CHECK(fx_info("flac", "Sound", "stereo, 44100 Hz, 16-bit") && fx_info("flac", "Title", "Flacky"), "FLAC");
    CHECK(fx_info("docx", "Title", "Docx title") && fx_info("docx", "Author", "Commander"), "docx properties");
    CHECK(fx_info("targz", "Contents", "a TAR archive, 2 entries"), "tar.gz holds a TAR");
    CHECK(fx_info("gz", "Original name", "notes.txt") && fx_info("gz", "Unpacked", "64 B"), "gzip name and size");
    CHECK(fx_info("tarxz", "Contents", "a TAR archive, 2 entries") && fx_info("xz", "Contents", "text") && fx_info("xz", "Check", "CRC64"), ".tar.xz holds a TAR; an .xz of text");
    CHECK(fx_info("tarzst", "Contents", "a TAR archive, 2 entries") && fx_info("zst", "Contents", "text"), ".tar.zst holds a TAR; a .zst of text");
    CHECK(fx_info("deb_xz", "Package", "hello-test") && fx_info("deb_xz", "Control part", "xz") && fx_info("deb_zst", "Package", "hello-test") && fx_info("deb_zst", "Control part", "zstd"), "a .deb's control part in xz and in zstd");
    CHECK(fx_info("sqlite", "Tables", "songs") && fx_info("sqlite", "Schema", "1 tables, 1 indexes"), "SQLite schema");
    CHECK(fx_info("pdf", "Pages", "2") && fx_info("pdf", "Title", "PDF title") && fx_info("pdf", "Author", "Ann"), "PDF pages and info");
    CHECK(fx_info("rtf", "Title", "RTF title"), "RTF title");
    CHECK(fx_info("html", "Title", "Page title"), "HTML title");
    CHECK(fx_info("mbr", "Partition 1", "FAT32") && fx_info("mbr", "Partition 1", "R36S CARD") && fx_info("mbr", "Partition 1", "boot"), "MBR partition and label");
    CHECK(fx_info("xbin", "Characters", "3 x 1"), "XBin size");

    /* Content */
    CHECK(fx_content("docx", "Hello from a docx") && fx_content("docx", "Second & last"), "docx text");
    CHECK(fx_content("targz", "game/main.lua") && fx_content("targz", "game/readme.txt"), "tar.gz listing");
    CHECK(fx_content("gz", "gzip says hello"), "gzip unpacked");
    CHECK(fx_content("tarxz", "game/main.lua") && fx_content("tarxz", "game/readme.txt") && fx_content("xz", "xz says hello"), ".tar.xz listed, .xz unpacked");
    CHECK(fx_content("tarzst", "game/main.lua") && fx_content("tarzst", "game/readme.txt") && fx_content("zst", "zstd says hello"), ".tar.zst listed, .zst unpacked");
    CHECK(fx_content("deb_xz", "usr/bin/hello") && fx_content("deb_xz", "Version: 1.2-3") && fx_content("deb_zst", "usr/bin/hello") && fx_content("deb_zst", "Version: 1.2-3"), "a .deb's files and control, xz and zstd");
    CHECK(fx_content("pdf", "Hello PDF") && fx_content("pdf", "Second page"), "PDF text, plain and Flate");
    CHECK(fx_content("rtf", "Hello RTF") && fx_content("rtf", "Caf\xC3\xA9") && !fx_content_q("rtf", "Arial;", 1), "RTF text without the font table");
    CHECK(fx_content("html", "One & two") && fx_content("html", "Heading"), "HTML text");
    CHECK(fx_content("midi", "la la") && fx_content("midi", "Test song"), "MIDI names and lyrics");
    CHECK(fx_content("mod", "bass drum"), "MOD sample names");
    CHECK(fx_content("sqlite", "table    songs") && fx_content("sqlite", "index    by_plays"), "SQLite schema list");
    CHECK(fx_content("mbr", "FAT32"), "MBR partition list");

    /* pictures: the decoders against Pillow */
    for (size_t i = 0; i < sizeof FIXTURE_PIXELS / sizeof FIXTURE_PIXELS[0]; i++) {
        const FixturePixel *px = &FIXTURE_PIXELS[i];
        const Fixture *fx = fx_find(px->id);
        FImage im;
        char err[160] = "";
        const char *dot = strrchr(fx->name, '.');
        int rc = img_decode(img_kind(fx->data, fx->len, dot ? dot + 1 : ""), fx->data, fx->len, &im, err, sizeof err);
        CHECK(rc == 0, "%s decodes: %s", px->id, err);
        if (rc != 0) continue;
        uint32_t c = im.px[(size_t)px->y * im.w + px->x];
        int r = (int)(c >> 16 & 255), g = (int)(c >> 8 & 255), b = (int)(c & 255), al = (int)(c >> 24);
        int tol = !strncmp(px->id, "jpeg", 4) ? 10 : 0;
        CHECK(abs(r - px->r) <= tol && abs(g - px->g) <= tol && abs(b - px->b) <= tol && abs(al - px->a) <= tol,
              "%s at %d,%d is %d,%d,%d,%d; Pillow says %d,%d,%d,%d", px->id, px->x, px->y, r, g, b, al, px->r, px->g, px->b, px->a);
        img_free(&im);
    }
    {
        const Fixture *fx = fx_find("qoi");
        FImage im;
        char err[160];
        CHECK(img_decode(IMG_QOI, fx->data, fx->len, &im, err, sizeof err) == 0 && im.w == 2 && im.px[1] == 0xFFC86432u && im.px[3] == 0x80FFFFFFu, "QOI pixels");
        img_free(&im);
        fx = fx_find("jpeg_prog");
        CHECK(img_decode(IMG_JPEG, fx->data, fx->len, &im, err, sizeof err) != 0 && strstr(err, "progressive"), "a progressive JPEG says why it is not drawn");
        FImage small = {2, 1, NULL}, fit;
        uint32_t two[2] = {0xFFFF0000u, 0x00000000u};
        small.px = two;
        CHECK(img_fit(&small, 640, 400, &fit) == 0 && fit.w == 640 && fit.h == 320 && fit.px[0] == 0xFFFF0000u, "a small picture grows by a whole number");
        CHECK(fit.px && (fit.px[639] & 0xFFFFFF) != 0 && (fit.px[639] & 0xFFFFFF) < 0x505050, "transparency shows the checkerboard");
        img_free(&fit);
        unsigned char zbuf[64];
        static const unsigned char bad[] = {0x78, 0x9C, 0xFF, 0xFF, 0xFF};
        CHECK(fi_zlib(bad, sizeof bad, zbuf, sizeof zbuf, NULL) < 0, "damaged DEFLATE is refused");
    }

    /* art */
    {
        FFile f;
        char err[160];
        Art art;
        CHECK(ff_open(&f, fx_path("ansi"), err, sizeof err) == 0 && art_load(&art, &f, K_ANSI, err, sizeof err) == 0, "ANSI loads");
        CHECK(art.ch[0] == 'H' && art.at[0] == 0x0E && art.ch[2] == ' ' && art.at[2] == 0x17 && art.ch[3] == 'X' && art.at[3] == 0x14, "ANSI colours: bold yellow, blue background, red");
        CHECK(art.h == 2 && art.ch[art.w + 2] == 'Z', "ANSI cursor move to the right (%d rows)", art.h);
        art_free(&art);
        ff_close(&f);
        CHECK(ff_open(&f, fx_path("xbin"), err, sizeof err) == 0 && art_load(&art, &f, K_XBIN, err, sizeof err) == 0 && art.w == 3 && art.ch[2] == 'C' && art.at[2] == 0x4A, "XBin cells");
        art_free(&art);
        ff_close(&f);
    }

#ifdef __linux__
    {
        FFile f;
        char err[160];
        if (ff_open(&f, "/proc/self/exe", err, sizeof err) == 0) {
            FType t = ff_identify(&f);
            CHECK(t.kind == K_ELF, "this program is an ELF (%s)", t.label);
            FInfo in = {0, 0, 0};
            ff_info(&f, t, &in);
            int kind_row = 0;
            for (int i = 0; i < in.n; i++) if (!strcmp(in.r[i].k, "Kind")) kind_row = 1;
            CHECK(kind_row, "ELF details");
#ifdef __aarch64__
            int r36s = 0;
            for (int i = 0; i < in.n; i++) if (!strcmp(in.r[i].k, "On the R36S") && strstr(in.r[i].v, "right kind")) r36s = 1;
            CHECK(r36s, "an ARM64 program is the R36S's kind");
#endif
            info_free(&in);
            ff_close(&f);
        }
    }
#endif
}

/* Opening a file inside an archive from the viewer's Contents list, and extracting it to the other panel. */
static void test_archive_members(const char *root) {
    char out[PATH_MAX], dest[PATH_MAX], body[256];
    path_join(out, sizeof out, root, "extracted");
    CHECK(mkdir_p(out) == 0, "extract folder");
    App a;
    app_init(&a, g_fxdir, out);
    R3Screen s;

    /* a tar.gz: the list, picking, opening a member, and back */
    CHECK(start_viewer(&a, fx_path("targz"), 0, 0) == 0 && a.view.has_arc && a.view.arc.n == 2, "tar.gz opens as a list of 2 members");
    CHECK(!strcmp(a.view.arc.m[0].name, "game/main.lua") && a.view.sel == 0, "the first member is picked");
    app_compose(&a, &s);
    CHECK(dump_has(&s, "game/main.lua") && dump_has(&s, "Up/Down pick  A open  Start extract  B close"), "the list and its hint are drawn");
    app_action(&a, R3_DOWN);
    CHECK(a.view.sel == 1, "Down picks the next member");
    app_action(&a, R3_DOWN);
    CHECK(a.view.sel == 1, "Down stops at the last member");
    app_action(&a, R3_UP);
    CHECK(a.view.sel == 0, "Up picks the one above");
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_VIEW && a.vdepth == 1 && !a.view.has_arc && strstr(a.view.title, "game/main.lua"), "A opens the member (title %s)", a.view.title);
    char tmpfile[PATH_MAX];
    copy_str(tmpfile, sizeof tmpfile, a.vtmpfile);
    CHECK(tmpfile[0] && exists(tmpfile), "the member is a temporary file");
    app_compose(&a, &s);
    CHECK(dump_has(&s, "print('hi')") && dump_has(&s, "B back"), "the member's text is shown, and B goes back");
    app_action(&a, R3_BACK);
    CHECK(a.mode == M_VIEW && a.vdepth == 0 && a.view.has_arc && a.view.cur == a.view.arc_tab && a.view.sel == 0, "B returns to the list");
    CHECK(!exists(tmpfile) && !a.vtmpfile[0], "the temporary file is gone");

    /* extract the second member to the other panel */
    app_action(&a, R3_DOWN);
    app_action(&a, R3_MORE);
    path_join(dest, sizeof dest, out, "readme.txt");
    CHECK(exists(dest), "Start extracts into the other panel's folder");
    FILE *f = fopen(dest, "rb");
    size_t got = f ? fread(body, 1, sizeof body - 1, f) : 0;
    if (f) fclose(f);
    body[got] = 0;
    CHECK(got == 80 && !strncmp(body, "read me\nread me\n", 16), "the extracted file has the member's bytes (%zu)", got);
    CHECK(a.reply_style == R3_ST_OK && strstr(a.reply, "Extracted readme.txt"), "it says so: %s", a.reply);
    int found = 0;
    for (int i = 0; i < other(&a)->n; i++) if (!strcmp(other(&a)->e[i].name, "readme.txt")) found = 1;
    CHECK(found, "the other panel lists it");
    write_file(dest, "mine", 4);
    app_action(&a, R3_MORE);
    f = fopen(dest, "rb");
    got = f ? fread(body, 1, sizeof body - 1, f) : 0;
    if (f) fclose(f);
    CHECK(got == 4 && a.reply_style == R3_ST_WARN && strstr(a.reply, "not overwritten"), "a name that is there is never overwritten: %s", a.reply);
    app_action(&a, R3_BACK);
    CHECK(a.mode == M_BROWSE, "B closes the viewer");

    /* a ZIP holding a folder, a tar.gz and a text file: a folder says so, and archives open inside archives */
    CHECK(start_viewer(&a, fx_path("zip_nested"), 0, 0) == 0 && a.view.has_arc && a.view.arc.n == 3, "a ZIP lists 3 members");
    app_action(&a, R3_ACCEPT);
    CHECK(a.vdepth == 0 && a.reply_style == R3_ST_WARN && strstr(a.reply, "a folder"), "a folder member is not opened: %s", a.reply);
    app_action(&a, R3_DOWN);
    app_action(&a, R3_ACCEPT);
    CHECK(a.vdepth == 1 && a.view.has_arc && a.view.arc.n == 2 && strstr(a.view.title, "bundle.zip > inner/game.tar.gz"), "a tar.gz inside the ZIP opens as a list (title %s)", a.view.title);
    char outer[PATH_MAX];
    copy_str(outer, sizeof outer, a.vtmpfile);
    app_action(&a, R3_ACCEPT);
    CHECK(a.vdepth == 2 && !a.view.has_arc && strstr(a.view.title, "bundle.zip > inner/game.tar.gz > game/main.lua") == NULL && strstr(a.view.title, "game/main.lua"), "and a member of that opens too (title %s)", a.view.title);
    char innerf[PATH_MAX];
    copy_str(innerf, sizeof innerf, a.vtmpfile);
    app_action(&a, R3_BACK);
    CHECK(a.vdepth == 1 && a.view.has_arc && a.view.sel == 0 && !exists(innerf) && exists(outer), "B goes up one level at a time");
    app_action(&a, R3_BACK);
    CHECK(a.vdepth == 0 && a.view.has_arc && a.view.sel == 1 && !exists(outer), "back in the ZIP at the member left from");
    app_action(&a, R3_ACCEPT);
    app_action(&a, R3_ACCEPT);
    CHECK(a.vdepth == 2, "opened again");
    app_free(&a);
    CHECK(!exists(outer) && !exists(innerf), "closing the app removes the temporary files");

    /* xz, zstd and .deb members */
    app_init(&a, g_fxdir, out);
    CHECK(start_viewer(&a, fx_path("tarxz"), 0, 0) == 0 && a.view.has_arc && a.view.arc.n == 2, ".tar.xz is a list");
    app_action(&a, R3_ACCEPT);
    app_compose(&a, &s);
    CHECK(a.vdepth == 1 && dump_has(&s, "print('hi')"), "a member of a .tar.xz opens");
    app_action(&a, R3_BACK);
    CHECK(start_viewer(&a, fx_path("tarzst"), 0, 0) == 0 && a.view.has_arc && a.view.arc.n == 2, ".tar.zst is a list");
    app_action(&a, R3_DOWN);
    app_action(&a, R3_ACCEPT);
    app_compose(&a, &s);
    CHECK(a.vdepth == 1 && dump_has(&s, "read me"), "a member of a .tar.zst opens");
    app_action(&a, R3_BACK);
    CHECK(start_viewer(&a, fx_path("deb_xz"), 0, 0) == 0 && a.view.has_arc && a.view.arc.n == 2 && !strcmp(a.view.arc.m[0].name, "./usr/bin/hello"), "a .deb (xz) lists its files");
    app_compose(&a, &s);
    CHECK(dump_has(&s, "-- files (data.tar, xz) --") && dump_has(&s, "usr/bin/hello"), "with its heading");
    app_action(&a, R3_ACCEPT);
    app_compose(&a, &s);
    CHECK(a.vdepth == 1 && dump_has(&s, "echo hello"), "a file from a .deb opens");
    app_action(&a, R3_BACK);
    CHECK(start_viewer(&a, fx_path("deb_zst"), 0, 0) == 0 && a.view.has_arc && a.view.arc.n == 2, "a .deb (zstd) lists its files");

    CHECK(start_viewer(&a, fx_path("cpio_newc"), 0, 0) == 0 && a.view.has_arc && a.view.arc.n == 3 && a.view.arc.m[0].type == AM_DIR, "a cpio lists 3 members");
    app_action(&a, R3_DOWN);
    app_action(&a, R3_ACCEPT);
    app_compose(&a, &s);
    CHECK(a.vdepth == 1 && dump_has(&s, "echo hello from cpio"), "a file from a cpio opens");
    app_action(&a, R3_BACK);

    /* many members: paging with the pick, a long name keeps its end */
    CHECK(start_viewer(&a, fx_path("tar_many"), 0, 0) == 0 && a.view.has_arc && a.view.arc.n == 41, "41 members");
    int rows = VIEW_ROWS;
    for (int i = 0; i < rows; i++) app_action(&a, R3_DOWN);
    CHECK(a.view.sel == rows && viewer_tab(&a.view)->v.page == (a.view.arc_line0 + rows) / rows && viewer_tab(&a.view)->v.page == 1, "the page follows the pick (sel %d page %d)", a.view.sel, viewer_tab(&a.view)->v.page);
    app_action(&a, R3_PAGEDOWN);
    CHECK(a.view.sel == 2 * rows && viewer_tab(&a.view)->v.page == 2, "R1 pages down");
    app_action(&a, R3_END);
    app_compose(&a, &s);
    CHECK(a.view.sel == 40 && dump_has(&s, "...") && dump_has(&s, "long-member-name.txt"), "a long name is cut at its start, so its end shows");
    app_action(&a, R3_HOME);
    CHECK(a.view.sel == 0 && viewer_tab(&a.view)->v.page == 0, "Home goes to the first member");
    app_action(&a, R3_LEFT);
    CHECK(a.view.cur != a.view.arc_tab, "Left still switches tabs");
    app_action(&a, R3_BACK);
    CHECK(a.mode == M_BROWSE && a.vdepth == 0, "B on another tab closes the viewer at the top level");
    app_free(&a);
}

/* The viewer's tabs on real files, through the app. */
static void test_viewer_tabs(const char *root) {
    (void)root;
    App a;
    app_init(&a, g_fxdir, g_fxdir);
    R3Screen s;
    char err[256];

    CHECK(start_viewer(&a, fx_path("png_rgba"), 0, 0) == 0 && a.mode == M_VIEW, "picture opens");
    CHECK(viewer_tab(&a.view)->kind == TAB_PICTURE && a.view.ntabs == 3 && !strcmp(a.view.tab[0].name, "Info"), "a picture opens on its Picture tab");
    app_compose(&a, &s);
    CHECK(dump_has(&s, "[Picture]") && dump_has(&s, "5x3 (enlarged)"), "picture status line");
    uint32_t *px = malloc((size_t)R3_W * R3_H * 4);
    if (px) {
        app_render(&a, &s, px);
        uint32_t mid = px[(size_t)(PIC_Y + PIC_H / 2) * R3_W + R3_W / 2];
        CHECK((mid & 0xFFFFFF) != 0, "the picture is drawn over the display view (%08x)", mid);
    }
    shot(&a, &s, "8-view-picture");
    app_action(&a, R3_LEFT);
    CHECK(a.view.cur == 0, "Left goes to Info");
    app_compose(&a, &s);
    CHECK(dump_has(&s, "Dimensions") && dump_has(&s, "5 x 3 pixels") && dump_has(&s, "[Info]"), "Info page");
    shot(&a, &s, "9-view-info");
    app_action(&a, R3_LEFT);
    CHECK(a.view.cur == 2 && viewer_tab(&a.view)->v.hex && viewer_tab(&a.view)->v.n > 0, "Left again wraps to Hex, read when first shown");
    app_action(&a, R3_BACK);
    CHECK(a.mode == M_BROWSE, "B closes");

    CHECK(start_viewer(&a, fx_path("ansi"), 0, 0) == 0 && viewer_tab(&a.view)->kind == TAB_ART, "ANSI opens on Art");
    app_compose(&a, &s);
    if (px) {
        app_render(&a, &s, px);
        int cw, chh;
        art_cell(&a.view.art, &cw, &chh);
        uint32_t c = px[(size_t)(PIC_Y + chh / 2) * R3_W + (R3_W - a.view.art.w * cw) / 2 + 1]; /* the left stroke of the H */
        CHECK((c & 0xFFFFFF) != 0, "art drawn (%08x)", c);
    }
    shot(&a, &s, "10-view-art");
    app_action(&a, R3_BACK);

    CHECK(start_viewer(&a, fx_path("targz"), 0, 0) == 0 && !strcmp(viewer_tab(&a.view)->name, "Contents"), "an archive opens on Contents");
    app_compose(&a, &s);
    CHECK(dump_has(&s, "game/main.lua"), "listing shown");
    shot(&a, &s, "11-view-contents");
    app_action(&a, R3_BACK);

    CHECK(start_viewer(&a, fx_path("html"), 0, 0) == 0 && !strcmp(viewer_tab(&a.view)->name, "Text") && a.view.ntabs == 4 && !strcmp(a.view.tab[2].name, "Source"), "HTML: Text, then Source");
    app_action(&a, R3_RIGHT);
    app_compose(&a, &s);
    CHECK(dump_has(&s, "<!DOCTYPE html>"), "Source shows the markup");
    app_action(&a, R3_BACK);

    setenv("R3I6NOS_FFMPEG", "/nonexistent/ffmpeg", 1);   /* the host may have an ffmpeg: this checks the picture without one */
    CHECK(start_viewer(&a, fx_path("jpeg_prog"), 0, 0) == 0 && a.view.cur == 0, "a picture that cannot be drawn opens on Info");
    app_compose(&a, &s);
    CHECK(dump_has(&s, "progressive"), "Info says why");
    app_action(&a, R3_BACK);
    /* with ffmpeg (a stand-in that writes the picture's w*h*4 bytes) the progressive JPEG is drawn, as a baseline one is */
    {
        FFile jf;
        FInfo ji = {0, 0, 0};
        int jw = 0, jh = 0;
        if (ff_open(&jf, fx_path("jpeg_prog"), err, sizeof err) == 0) { ff_info(&jf, ff_identify(&jf), &ji); ff_close(&jf); }
        for (int i = 0; i < ji.n; i++) if (!strcmp(ji.r[i].k, "Dimensions")) sscanf(ji.r[i].v, "%d x %d", &jw, &jh);
        info_free(&ji);
        char ffs[PATH_MAX], script[256];
        path_join(ffs, sizeof ffs, root, "fake-ffmpeg-jpeg");
        snprintf(script, sizeof script, "#!/bin/sh\nexport LC_ALL=C\nhead -c %d /dev/zero | tr '\\000' '\\377'\n", jw * jh * 4);
        write_file(ffs, script, strlen(script));
        chmod(ffs, 0755);
        setenv("R3I6NOS_FFMPEG", ffs, 1);
        CHECK(jw > 0 && jh > 0 && start_viewer(&a, fx_path("jpeg_prog"), 0, 0) == 0 && viewer_tab(&a.view)->kind == TAB_PICTURE, "a progressive JPEG opens on Picture with ffmpeg (%dx%d)", jw, jh);
        CHECK(a.view.pic_w == jw && a.view.pic_h == jh && a.view.pic.px && a.view.pic.px[0] == 0xFFFFFFFFu, "drawn from ffmpeg's pixels");
        app_action(&a, R3_BACK);
        setenv("R3I6NOS_FFMPEG", "/nonexistent/ffmpeg", 1);
        CHECK(start_viewer(&a, fx_path("jpeg_prog"), 0, 0) == 0 && viewer_tab(&a.view)->kind != TAB_PICTURE, "without ffmpeg it is Info again");
        app_compose(&a, &s);
        CHECK(dump_has(&s, "ffmpeg"), "Info says ffmpeg could not draw it");
        app_action(&a, R3_BACK);
    }
    unsetenv("R3I6NOS_FFMPEG");

    CHECK(viewer_open(&a.view, "/nonexistent/file", err, sizeof err) != 0 && strstr(err, "file"), "a missing file is an error");
    free(px);
    app_free(&a);
}

/* ---- Play: what Rec demo makes, a WAV, and video through a stand-in ffmpeg ------------------------------------------------------- */

static void put_le(unsigned char *p, unsigned long v, int n) { for (int i = 0; i < n; i++) p[i] = (unsigned char)(v >> (8 * i)); }

/* A mono 16-bit 8000 Hz WAV of `seconds` of silence. */
static void write_wav(const char *path, int seconds) {
    unsigned long data = 8000ul * 2 * (unsigned long)seconds;
    unsigned char h[44] = "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\1\0\1\0\0\0\0\0\0\0\0\0\2\0\x10\0data";
    put_le(h + 4, 36 + data, 4);
    put_le(h + 24, 8000, 4);
    put_le(h + 28, 16000, 4);
    put_le(h + 40, data, 4);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(h, 1, 44, f);
    for (unsigned long i = 0; i < data; i++) fputc(0, f);
    fclose(f);
}

/* Rec demo's kept frames: three screens that stay 30, 15 and 45 frames (3.0 s). */
static void write_r3f(const char *path) {
    static const struct { uint32_t run; const char *text; } rec[3] = {{30, "ONE"}, {15, "TWO"}, {45, "THREE"}};
    FILE *f = fopen(path, "wb");
    if (!f) return;
    for (int i = 0; i < 3; i++) {
        R3Screen s;
        r3_clear(&s);
        r3_text(&s, 2, 2, rec[i].text, R3_WHITE, R3_BLUE);
        fwrite(&rec[i].run, sizeof rec[i].run, 1, f);
        fwrite(&s, sizeof s, 1, f);
    }
    fclose(f);
}

static FType fx_type_of(const char *path) {
    FFile f;
    char err[256];
    FType t = {"", "", -1};
    if (ff_open(&f, path, err, sizeof err) != 0) return t;
    t = ff_identify(&f);
    ff_close(&f);
    return t;
}

static void test_play(const char *root) {
    char p[PATH_MAX], wav[PATH_MAX], err[256];
    App a;
    app_init(&a, root, root);
    setenv("R3I6NOS_FM_NOAUDIO", "1", 1);   /* silent, on the test's own clock */
    path_join(p, sizeof p, root, "demo-20261111-143005-frames.r3f");
    path_join(wav, sizeof wav, root, "demo-20261111-143005.wav");
    write_r3f(p);
    write_wav(wav, 3);

    CHECK(fx_type_of(p).kind == K_R3F, "a kept frames file is recognised");
    {
        FFile f;
        FInfo in = {0, 0, 0};
        int ok = ff_open(&f, p, err, sizeof err) == 0;
        if (ok) { ff_info(&f, ff_identify(&f), &in); ff_close(&f); }
        int screens = 0, dur = 0;
        for (int i = 0; i < in.n; i++) {
            if (!strcmp(in.r[i].k, "Screens") && strstr(in.r[i].v, "3 different, 90 frames")) screens = 1;
            if (!strcmp(in.r[i].k, "Duration") && strstr(in.r[i].v, "3.0 s")) dur = 1;
        }
        CHECK(ok && screens && dur, "frames Info: screens and duration");
        info_free(&in);
    }
    CHECK(start_viewer(&a, p, 0, 0) == 0 && viewer_tab(&a.view)->kind == TAB_PLAY, "a Rec demo's frames open on Play");
    Player *pl = &a.view.pl;
    CHECK(pl->mode == PL_R3F && pl->has_video && pl->has_audio && pl->playing && !pl->ended, "frames play, with the .wav beside them");
    CHECK(pl->total > 2.99 && pl->total < 3.01, "length 3 s (%.2f)", pl->total);
    CHECK(app_playing(&a), "the loop wakes while it plays");
    double t0 = pl->last_ms;
    CHECK(pl->px && pl->px[(size_t)(2 * R3_CELL_H + 4) * PL_W + 20 + (PL_W - PL_W * PL_H / R3_H) / 2] != 0, "the first screen is drawn");
    pl_pump(pl, t0 + 100);
    CHECK(pl->frame_end == 30 && pl->rec_left == 2, "0.1 s: still the first screen");
    CHECK(pl_pump(pl, t0 + 1100) && pl->frame_end == 45 && pl->rec_left == 1, "1.1 s: the second screen");
    pl_pump(pl, t0 + 1600);
    CHECK(pl->frame_end == 90 && pl->rec_left == 0 && !pl->ended, "1.6 s: the third");
    app_action(&a, R3_ACCEPT);
    CHECK(!pl->playing && a.mode == M_VIEW, "A pauses");
    double held = pl->now;
    pl_pump(pl, t0 + 2600);
    CHECK(pl->now == held, "a paused recording does not move");
    app_action(&a, R3_ACCEPT);
    CHECK(pl->playing, "A plays on");
    pl_pump(pl, t0 + 2700);
    pl_pump(pl, t0 + 4800);
    CHECK(pl->ended && pl->now > 2.99, "it ends at 3 s");
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_VIEW && pl->playing && !pl->ended && pl->now == 0 && pl->rec_left == 2, "A after the end plays again from the start");
    app_action(&a, R3_RIGHT);
    CHECK(!pl->playing && viewer_tab(&a.view)->kind != TAB_PLAY, "leaving the Play tab pauses");
    app_action(&a, R3_BACK);
    CHECK(a.mode == M_BROWSE, "B closes");

    /* a damaged frames file does not play, and Info says so */
    char bad[PATH_MAX];
    path_join(bad, sizeof bad, root, "demo-20261111-150000-frames.r3f");
    write_file(bad, "xyz", 3);
    CHECK(start_viewer(&a, bad, 0, 0) == 0 && viewer_tab(&a.view)->kind != TAB_PLAY, "a damaged frames file opens on Info");
    app_action(&a, R3_BACK);

    /* a WAV, by itself */
    char plain[PATH_MAX];
    path_join(plain, sizeof plain, root, "plain.wav");
    write_wav(plain, 2);
    CHECK(start_viewer(&a, plain, 0, 0) == 0 && viewer_tab(&a.view)->kind == TAB_PLAY, "a WAV opens on Play");
    pl = &a.view.pl;
    CHECK(pl->mode == PL_WAV && !pl->has_video && pl->total > 1.99 && pl->total < 2.01, "WAV: sound only, 2 s");
    t0 = pl->last_ms;
    pl_pump(pl, t0 + 1000);
    CHECK(!pl->ended && pl->now > 0.9, "WAV: one second in");
    pl_pump(pl, t0 + 2500);
    CHECK(pl->ended, "WAV: ends");
    R3Screen s;
    app_compose(&a, &s);
    CHECK(dump_has(&s, "Finished"), "a sound-only Play tab says it finished");
    app_action(&a, R3_BACK);

    /* the sound device itself (SDL's dummy driver in CI drains in real time; with no driver the note says so and it plays silently) */
    unsetenv("R3I6NOS_FM_NOAUDIO");
    CHECK(start_viewer(&a, plain, 0, 0) == 0 && viewer_tab(&a.view)->kind == TAB_PLAY, "a WAV opens with the sound device");
    pl = &a.view.pl;
    t0 = pl->last_ms;
    for (int i = 0; i < 10; i++) { usleep(50000); pl_pump(pl, t0 + 50.0 * (i + 1)); }
    CHECK(pl->dev ? (pl->fed > 0 && pl->primed && pl->now > 0) : pl->note[0] != 0, "sound: queued and heard, or a note says why not (fed %llu, now %.2f, %s)", pl->fed, pl->now, pl->note);
    app_action(&a, R3_BACK);
    setenv("R3I6NOS_FM_NOAUDIO", "1", 1);

    /* a video, with a stand-in ffmpeg that writes ten white frames and a second of sound */
    char mp4[PATH_MAX], ff[PATH_MAX];
    path_join(mp4, sizeof mp4, root, "demo-20261111-143005.mp4");
    write_file(mp4, "\0\0\0\x18" "ftypisom\0\0\0\0isommp41", 24);
    path_join(ff, sizeof ff, root, "fake-ffmpeg");
    FILE *fx = fopen(ff, "w");
    if (fx) {
        fputs("#!/bin/sh\nexport LC_ALL=C\ncase \"$*\" in\n *-an*) head -c $((640*400*4*10)) /dev/zero | tr '\\000' '\\377' ;;\n *) head -c 176400 /dev/zero ;;\nesac\n", fx);
        fclose(fx);
        chmod(ff, 0755);
    }
    setenv("R3I6NOS_FFMPEG", "/nonexistent/ffmpeg", 1);
    CHECK(start_viewer(&a, mp4, 0, 0) == 0 && viewer_tab(&a.view)->kind != TAB_PLAY, "no ffmpeg: a video opens on Info");
    CHECK(strstr(a.view.tab[0].name, "Info") != NULL, "Info is the first tab");
    app_compose(&a, &s);
    CHECK(dump_has(&s, "needs ffmpeg"), "Info says playing needs ffmpeg");
    app_action(&a, R3_BACK);
    setenv("R3I6NOS_FFMPEG", ff, 1);
    CHECK(start_viewer(&a, mp4, 0, 0) == 0 && viewer_tab(&a.view)->kind == TAB_PLAY, "with ffmpeg a video opens on Play");
    pl = &a.view.pl;
    CHECK(pl->mode == PL_VIDEO && pl->has_video, "video through ffmpeg");
    t0 = pl->last_ms;
    for (int i = 0; i < 200 && pl->vidx < 3; i++) { usleep(20000); pl_pump(pl, t0 + 400 + 50.0 * i); }
    CHECK(pl->vidx >= 3, "frames arrive from the pipe (%ld)", pl->vidx);
    CHECK(pl->px[0] == 0xFFFFFFFFu && pl->px[PL_FRAME_BYTES / 4 - 1] == 0xFFFFFFFFu, "and are drawn (%08x)", pl->px[0]);
    for (int i = 0; i < 200 && !pl->ended; i++) { usleep(20000); pl_pump(pl, t0 + 400 + 50.0 * (200 + i)); }
    CHECK(pl->ended, "the video ends when ffmpeg's output does");
    app_action(&a, R3_BACK);
    CHECK(a.view.pl.vpid == 0 && a.view.pl.vfd <= 0, "closing leaves no ffmpeg running");

    /* a Photoshop file (only its header matters here: 4 x 2 pixels), drawn by a stand-in ffmpeg that writes 4 x 2 x 4 white bytes */
    char psd[PATH_MAX], ffs[PATH_MAX];
    path_join(psd, sizeof psd, root, "pic.psd");
    path_join(ffs, sizeof ffs, root, "fake-ffmpeg-still");
    {
        static const unsigned char hdr[] = "8BPS\0\1\0\0\0\0\0\0\0\3\0\0\0\2\0\0\0\4\0\10\0\3\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
        write_file(psd, (const char *)hdr, sizeof hdr - 1);
        FILE *fs = fopen(ffs, "w");
        if (fs) { fputs("#!/bin/sh\nexport LC_ALL=C\nhead -c 32 /dev/zero | tr '\\000' '\\377'\n", fs); fclose(fs); chmod(ffs, 0755); }
    }
    setenv("R3I6NOS_FFMPEG", "/nonexistent/ffmpeg", 1);
    CHECK(start_viewer(&a, psd, 0, 0) == 0 && viewer_tab(&a.view)->kind != TAB_PICTURE, "no ffmpeg: a Photoshop file opens on Info");
    app_compose(&a, &s);
    CHECK(dump_has(&s, "needs ffmpeg"), "Info says the picture needs ffmpeg");
    app_action(&a, R3_BACK);
    setenv("R3I6NOS_FFMPEG", ffs, 1);
    CHECK(start_viewer(&a, psd, 0, 0) == 0 && viewer_tab(&a.view)->kind == TAB_PICTURE, "with ffmpeg a Photoshop file opens on Picture");
    CHECK(a.view.pic_w == 4 && a.view.pic_h == 2 && a.view.pic.px && a.view.pic.px[0] == 0xFFFFFFFFu, "drawn from ffmpeg's pixels (%dx%d %08x)", a.view.pic_w, a.view.pic_h, a.view.pic.px ? a.view.pic.px[0] : 0);
    app_action(&a, R3_BACK);
    /* UTF-16 text with a byte-order mark (FF FE) is text, not an MPEG audio frame (found by the test corpus) */
    {
        char u16[PATH_MAX];
        path_join(u16, sizeof u16, root, "utf16.txt");
        write_file(u16, "\xff\xfeU\0T\0F\0-\0" "1\0" "6\0 \0t\0e\0x\0t\0\n\0", 24);
        CHECK(fx_type_of(u16).kind == K_TEXT, "a UTF-16 text file with a BOM is text, not MPEG audio (%s)", fx_type_of(u16).label);
    }
    unsetenv("R3I6NOS_FFMPEG");
    unsetenv("R3I6NOS_FM_NOAUDIO");
    app_free(&a);
}

static int selftest_logic(void) {
    char templ[PATH_MAX];
    const char *tmp = getenv("TMPDIR");
    snprintf(templ, sizeof templ, "%s/r3i6nos-commander.XXXXXX", tmp && *tmp ? tmp : "/tmp");
    char *root = mkdtemp(templ);
    if (!root) { perror("commander: mkdtemp"); return 1; }
    char *real = realpath(root, NULL); /* macOS: /var is a link to /private/var, and the app shows real paths */
    test_theme();
    test_files(real ? real : root);
    test_ui(real ? real : root);
    test_formats(real ? real : root);
    test_viewer_tabs(real ? real : root);
    test_archive_members(real ? real : root);
    test_play(real ? real : root);
    rm_rf(root);
    if (real) { rm_rf(real); free(real); }
    return g_fail;
}

/* ---- main ----------------------------------------------------------------------------------------------------------------------- */

static int save_screenshot(const char *path, const char *left, const char *right, const char *file, int tab, int page) {
    App a;
    app_init(&a, left, right);
    if (file && start_viewer(&a, file, tab, page) != 0) { app_free(&a); return 1; }
    R3Screen s;
    app_compose(&a, &s);
    uint32_t *px = malloc((size_t)R3_W * R3_H * 4);
    if (!px) return 1;
    app_render(&a, &s, px);
    SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(px, R3_W, R3_H, 32, R3_W * 4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    int rc = surf ? SDL_SaveBMP(surf, path) : 1;
    if (surf) SDL_FreeSurface(surf);
    free(px);
    app_free(&a);
    return rc;
}

/* --info FILE: what the viewer's Info page says, as text (and --content: its Content page). For trying the formats on a laptop. */
static int print_info(const char *file, int content) {
    char err[256];
    FFile f;
    if (ff_open(&f, file, err, sizeof err) != 0) { fprintf(stderr, "commander: %s: %s\n", file, err); return 1; }
    FType ty = ff_identify(&f);
    if (!content) {
        FInfo in = {0, 0, 0};
        ff_info(&f, ty, &in);
        for (int i = 0; i < in.n; i++) printf("%-15s %s\n", in.r[i].k, in.r[i].v);
        info_free(&in);
    } else {
        FText t = {0, 0, 0, 0, 0};
        uint8_t *raw = NULL;
        size_t rawn = 0;
        char name[32];
        int fc = ff_content(&f, ty, &t, &raw, &rawn, name, sizeof name);
        printf("== %s\n", fc == FC_NONE ? "(no content page)" : name);
        if (fc == FC_LINES && t.s) fwrite(t.s, 1, t.n, stdout);
        if (fc == FC_BYTES) fwrite(raw, 1, rawn, stdout);
        free(raw);
        text_free(&t);
    }
    ff_close(&f);
    return 0;
}

static void open_pads(void) {
    for (int i = 0; i < SDL_NumJoysticks(); i++) SDL_JoystickOpen(i);
}

/* ---- the device report: `commander --report` (docs/DEVICE-REPORTS.md; the power section is CLAUDE.md rule 14, docs/POWER.md) ---------- */

typedef struct { App *app; SDL_Renderer *r; R3Video *video; R3Toast *toast; R3Screen screen; } ReportCtx;

/* One pass of the idle loop as main runs it, minus input (a report in progress takes none): wait for an event, a quarter of a second
 * when nothing is changing, and draw. */
static void report_idle_turn(void *arg) {
    ReportCtx *c = arg;
    SDL_Event e;
    if (SDL_WaitEventTimeout(&e, (c->video->settle > 0 || r3_toast_active(c->toast)) ? 16 : 250)) {
        while (SDL_PollEvent(&e)) { /* dropped */ }
    }
    app_tick(c->app, 25);
    r3_toast_poll(c->toast, SDL_GetTicks());
    app_compose(c->app, &c->screen);
    r3_toast_draw(c->toast, &c->screen);
    app_video_draw(c->video, c->r, &c->screen, c->app, 0);
}

/* Every regular file under dir (up to four levels, up to `cap`, none over 8 MB: the report times the identifying, not the card). */
static int corpus_walk(const char *dir, int depth, char **out, int n, int cap) {
    DIR *d = opendir(dir);
    if (!d) return n;
    struct dirent *e;
    while ((e = readdir(d)) && n < cap) {
        if (e->d_name[0] == '.') continue;
        char path[PATH_MAX];
        struct stat st;
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path || lstat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode) && depth < 4) n = corpus_walk(path, depth + 1, out, n, cap);
        else if (S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= 8 * 1024 * 1024) out[n++] = strdup(path);
    }
    closedir(d);
    return n;
}

#define REPORT_IDENTIFY_PER_S 20.0   /* files a second the viewer's identify and info may take: a first guess, set before any measurement */

static int commander_report(App *app, SDL_Renderer *r, R3Video *video, R3Toast *toast, int quiet) {
    const char *roms = getenv("R3I6NOS_ROMS") && *getenv("R3I6NOS_ROMS") ? getenv("R3I6NOS_ROMS") : "/roms";
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s/Commander", roms);
    R3Report rep;
    memset(&rep, 0, sizeof rep);
    rep.quiet = quiet;
    if (r3_report_open_named(&rep, dir, "r36s-report.txt", "Commander", VERSION, "r36s-commander-native", "commander --report", "r3i6nos") != 0) {
        fprintf(stderr, "commander: cannot write the report in %s: %s\n", dir, strerror(errno));
        return 1;
    }
    r3_report_system(&rep);
    r3_report_card(&rep, dir);
    r3_report_memory(&rep);
    r3_report_power_section(&rep);
    ReportCtx c;
    memset(&c, 0, sizeof c);
    c.app = app;
    c.r = r;
    c.video = video;
    c.toast = toast;
    R3PowerResult idle, work;
    memset(&work, 0, sizeof work);
    r3_report_power_idle(&rep, r3_report_power_seconds(10), report_idle_turn, &c, &idle);
    /* working: identify the files of the test corpus as the viewer's Info page does, as fast as it will, for a few seconds */
    const char *corpus = getenv("R3I6NOS_TEST_FILES") && *getenv("R3I6NOS_TEST_FILES") ? getenv("R3I6NOS_TEST_FILES") : "/opt/loveos/test-files";
    char **list = calloc(600, sizeof *list);
    int n = list ? corpus_walk(corpus, 0, list, 0, 600) : 0;
    if (n == 0) {
        r3_report_line(&rep, R3_SKIP, "working", "no test files in %s: the image has them (corpus/README.md)", corpus);
    } else {
        R3Power p;
        r3_power_begin(&p);
        double t0 = r3_report_ms(), secs = r3_report_power_seconds(5);
        long done = 0;
        for (int i = 0; r3_report_ms() - t0 < secs * 1000.0; i = (i + 1) % n) {
            char err[256];
            FFile f;
            if (ff_open(&f, list[i], err, sizeof err) == 0) {
                FInfo in = {0, 0, 0};
                ff_info(&f, ff_identify(&f), &in);
                info_free(&in);
                ff_close(&f);
                done++;
            }
            if ((done & 15) == 0) r3_power_sample(&p);
        }
        double wall = (r3_report_ms() - t0) / 1000.0;
        r3_report_power_end(&rep, &p, "working", 0, 0, &work);
        double rate = wall > 0 ? (double)done / wall : 0;
        r3_report_line(&rep, rate >= REPORT_IDENTIFY_PER_S ? R3_PASS : R3_FAIL, "identify speed", "%.0f files a second over %d files (goal at least %.0f)", rate, n, REPORT_IDENTIFY_PER_S);
    }
    for (int i = 0; i < n; i++) free(list[i]);
    free(list);
    r3_report_power_advice(&rep, &idle, &work);
    int pass = rep.pass, fail = rep.fail, skip = rep.skip;
    if (r3_report_close(&rep) != 0) { fprintf(stderr, "commander: cannot write the report in %s\n", dir); return 1; }
    if (!quiet) printf("Written: %s/r36s-report.txt (PASS %d FAIL %d SKIP %d)\n", dir, pass, fail, skip);
    return 0;
}

int main(int argc, char **argv) {
    int selftest = 0, report = 0, tab = 0, page = 0;
    const char *shot = NULL, *view = NULL;
    const char *left = getenv("R3I6NOS_FM_LEFT"), *right = getenv("R3I6NOS_FM_RIGHT");
    if (!left || !*left) left = access("/roms", R_OK) == 0 ? "/roms" : (getenv("HOME") ? getenv("HOME") : "/");
    if (!right || !*right) right = getenv("HOME") && *getenv("HOME") ? getenv("HOME") : "/";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) selftest = 1;
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--report")) report = 1;
        else if (!strcmp(argv[i], "--version")) { puts("commander " VERSION); return 0; }
        else if ((!strcmp(argv[i], "--info") || !strcmp(argv[i], "--content")) && i + 1 < argc) return print_info(argv[i + 1], argv[i][2] == 'c');
        else if (!strcmp(argv[i], "--view") && i + 1 < argc) view = argv[++i];
        else if (!strcmp(argv[i], "--tab") && i + 1 < argc) tab = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--page") && i + 1 < argc) page = atoi(argv[++i]);
        else {
            fprintf(stderr, "usage: %s [--selftest | --report | --version | --info FILE | --content FILE | [--view FILE [--tab N] [--page N]] [--screenshot out.bmp]]\n", argv[0]);
            return 2;
        }
    }
    if (shot) {
        int rc = save_screenshot(shot, left, right, view, tab, page);
        return rc;
    }
    if (selftest) {
        int bad = selftest_logic();
        if (bad) { fprintf(stderr, "commander: %d selftest check(s) failed\n", bad); return 1; }
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "commander: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_ShowCursor(SDL_DISABLE);
    SDL_Window *win = SDL_CreateWindow("Commander", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, R3_W, R3_H,
                                       selftest ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!win) { fprintf(stderr, "commander: window: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, selftest ? SDL_RENDERER_SOFTWARE : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE); /* no GPU: the software path still draws */
    if (!r) { fprintf(stderr, "commander: renderer: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    R3Video video;
    R3Toast toast;                  /* the hot-plug toast over the top bar (docs/HOTPLUG.md) */
    r3_toast_init(&toast);
    if (!r3_video_init(&video, r)) { fprintf(stderr, "commander: texture: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    App app;
    app_init(&app, left, right);
    if (view && !selftest && start_viewer(&app, view, tab, page) != 0) { app_free(&app); SDL_Quit(); return 1; }
    R3Screen screen;

    if (selftest) {
        app_compose(&app, &screen);
        app_video_draw(&video, r, &screen, &app, 1);
        SDL_version v;
        SDL_GetVersion(&v);
        printf("R3I6NOS_SMOKE ok commander=%s sdl=%d.%d.%d joysticks=%d renderer=%s\n", VERSION, v.major, v.minor, v.patch,
               SDL_NumJoysticks(), SDL_GetCurrentVideoDriver());
        /* the report, with short power windows, into a throwaway folder: the power section must be in it */
        char tmpl[PATH_MAX];
        snprintf(tmpl, sizeof tmpl, "%s/commander-selftest-XXXXXX", getenv("TMPDIR") && *getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
        char *tdir = mkdtemp(tmpl);
        int rbad = 1;
        if (tdir) {
            setenv("R3I6NOS_ROMS", tdir, 1);
            setenv("R3I6NOS_REPORT_POWER_S", "0.2", 1);
            R3Toast ttoast;
            r3_toast_init(&ttoast);
            char path[PATH_MAX + 64], text[24576] = "";
            if (commander_report(&app, r, &video, &ttoast, 1) == 0) {
                snprintf(path, sizeof path, "%s/Commander/r36s-report.txt", tdir);
                FILE *f = fopen(path, "r");
                size_t got = f ? fread(text, 1, sizeof text - 1, f) : 0;
                text[got] = 0;
                if (f) fclose(f);
                rbad = !(strstr(text, "== power ==") && strstr(text, "idle cpu: ") && strstr(text, "cut first: ") && strstr(text, "\nend\n"));
            }
            char cmd[PATH_MAX + 16];
            snprintf(cmd, sizeof cmd, "rm -rf '%s'", tdir);
            if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", tdir);
        }
        printf("R3I6NOS_SMOKE power-report %s\n", rbad ? "missing" : "ok");
        if (rbad) { fprintf(stderr, "commander: the report has no power section\n"); app_free(&app); r3_video_free(&video); SDL_DestroyRenderer(r); SDL_DestroyWindow(win); SDL_Quit(); return 1; }
        app_free(&app);
        r3_video_free(&video);
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    if (report) {
        open_pads();
        int rc = commander_report(&app, r, &video, &toast, 0);
        app_free(&app);
        r3_video_free(&video);
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return rc;
    }
    open_pads();
    int typing = 0, force = 1;
    while (!app.quit) {
        SDL_Event e;
        /* a job polls; so does a screen still being presented (app_video_draw owes presents after a change); else wait */
        int have = (app.mode == M_JOB) ? SDL_PollEvent(&e) : SDL_WaitEventTimeout(&e, (force || video.settle > 0 || app_playing(&app) || r3_toast_active(&toast)) ? 16 : 250);
        while (have) {
            switch (e.type) {
            case SDL_QUIT: app.quit = 1; break;
            case SDL_WINDOWEVENT: force = 1; break;
            case SDL_JOYDEVICEADDED: SDL_JoystickOpen(e.jdevice.which); break;
            case SDL_JOYBUTTONUP:
                if (r3_pad_action(e.jbutton.button) == R3_SELECT) app.select_held = 0;
                break;
            case SDL_JOYBUTTONDOWN: {
                if (r3_toast_key(&toast)) break; /* the key that clears a failure toast is used up */
                int act = r3_pad_action(e.jbutton.button);
                if (act == R3_SELECT) { app.select_held = 1; break; }
                if (act == R3_MORE && app.select_held) { app.quit = 1; break; } /* Select+Start leaves, as in the input checker */
                if (act != R3_NONE) app_action(&app, act);
                break;
            }
            case SDL_KEYDOWN: {
                SDL_Keycode k = e.key.keysym.sym;
                if (r3_toast_key(&toast)) break;
                if (app_key(&app, k)) break;
                int act = key_action(&app, k);
                if (act != R3_NONE) app_action(&app, act);
                break;
            }
            case SDL_TEXTINPUT: app_text(&app, e.text.text); break;
            default: break;
            }
            have = SDL_PollEvent(&e);
        }
        app_tick(&app, 25);
        if ((app.mode == M_INPUT) != typing) {
            typing = app.mode == M_INPUT;
            if (typing) SDL_StartTextInput();
            else SDL_StopTextInput();
        }
        r3_toast_poll(&toast, SDL_GetTicks());
        app_compose(&app, &screen);
        r3_toast_draw(&toast, &screen);
        app_video_draw(&video, r, &screen, &app, force);
        force = 0;
    }
    app_free(&app);
    r3_video_free(&video);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
