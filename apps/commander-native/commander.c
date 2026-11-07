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
 * Safety: copy and move never overwrite (a name that exists is skipped and counted); delete asks, with No preselected; links are
 * never followed (a link is copied as a link and deleted as a link); a folder is never copied into itself; "/" is never moved or
 * deleted. Work runs in small slices from the main loop with a progress bar, and B cancels it.
 *
 * `--selftest` checks the logic against a temporary folder and prints one R3I6NOS_SMOKE line; `--screenshot out.bmp` draws the
 * real screen once, without a window. */
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

#define VERSION "1.0"
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

/* ---- the viewer: text or hex, in whole pages ------------------------------------------------------------------------------- */

#define VIEW_W (R3_COLS - 2)
#define VIEW_ROWS (R3_DISPLAY_ROWS - 2)
#define VIEW_MAX_BYTES (4 * 1024 * 1024)
#define VIEW_MAX_LINES 200000

typedef struct {
    unsigned char *cells;      /* lines x VIEW_W code page 437 indices */
    unsigned char *len;        /* cells used on each line */
    int n, cap, hex, page, truncated;
    off_t filesize;
    char title[256];
} View;

static void view_free(View *v) {
    free(v->cells);
    free(v->len);
    memset(v, 0, sizeof *v);
}

static int view_line(View *v) {
    if (v->n >= VIEW_MAX_LINES) { v->truncated = 1; return -1; }
    if (v->n == v->cap) {
        int cap = v->cap ? v->cap * 2 : 256;
        unsigned char *c = realloc(v->cells, (size_t)cap * VIEW_W), *l = realloc(v->len, (size_t)cap);
        if (!c || !l) { free(c ? c : v->cells); v->cells = c ? c : v->cells; if (l) v->len = l; return -1; }
        v->cells = c;
        v->len = l;
        v->cap = cap;
    }
    memset(v->cells + (size_t)v->n * VIEW_W, 32, VIEW_W);
    v->len[v->n] = 0;
    return v->n++;
}

static void view_put(View *v, int line, int ch) {
    if (v->len[line] < VIEW_W) v->cells[(size_t)line * VIEW_W + v->len[line]++] = (unsigned char)ch;
}

static int view_pages(const View *v) { return v->n ? (v->n + VIEW_ROWS - 1) / VIEW_ROWS : 1; }

static int view_load(View *v, const char *path, char *err, size_t errsz) {
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
    size_t probe = got < 4096 ? got : 4096;
    int binary = 0, ctrl = 0;
    for (size_t i = 0; i < probe; i++) {
        if (data[i] == 0) binary = 1;
        if (data[i] < 9 || (data[i] > 13 && data[i] < 32 && data[i] != 27)) ctrl++;
    }
    if (probe && ctrl * 10 > (int)probe) binary = 1;
    v->hex = binary;
    if (binary) {
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
    } else {
        int line = view_line(v);
        const unsigned char *p = data, *end = data + got;
        while (line >= 0 && p < end) {
            if (*p == '\n') { p++; line = view_line(v); continue; }
            if (*p == '\r') { p++; continue; }
            if (*p == '\t') {
                int stop = (v->len[line] / 4 + 1) * 4;
                p++;
                while (v->len[line] < stop) {
                    if (v->len[line] >= VIEW_W) { line = view_line(v); if (line < 0) break; }
                    view_put(v, line, 32);
                }
                continue;
            }
            unsigned cp = r3_utf8_next(&p, end);
            if (v->len[line] >= VIEW_W) { line = view_line(v); if (line < 0) break; }
            view_put(v, line, r3_cp437_of(cp));
        }
    }
    free(data);
    if (v->n == 0) view_line(v);
    return 0;
}

/* ---- the application ---------------------------------------------------------------------------------------------------------- */

enum { M_BROWSE, M_MENU, M_CONFIRM, M_INPUT, M_VIEW, M_JOB };
enum { C_VIEW, C_COPY, C_MOVE, C_RENAME, C_MKDIR, C_DELETE, C_TAGALL, C_UNTAG, C_SORT, C_HIDDEN, C_SWAP, C_SAME, C_QUIT, C_COUNT };

static const struct { const char *label, *key; } MENU[C_COUNT] = {
    {"View", "F3"}, {"Copy", "F5"}, {"Move", "F6"}, {"Rename", "F2"}, {"Make directory", "F7"}, {"Delete", "F8"},
    {"Tag all", ""}, {"Untag all", ""}, {"Sort by", ""}, {"Hidden files", ""}, {"Swap panels", ""},
    {"Other panel: this folder", ""}, {"Quit", "F10"},
};

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
    View view;
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
    view_free(&a->view);
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
    if (a->mode != M_JOB || !a->job_active) return;
    if (!job_step(&a->job, budget_ms) || a->job.finished) finish_job(a);
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
        view_free(&a->view);
        if (view_load(&a->view, full, err, sizeof err) != 0) { say(a, R3_ST_ERROR, "%s", err); break; }
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
        int pages = view_pages(&a->view);
        if (action == R3_BACK || action == R3_ACCEPT) { a->mode = M_BROWSE; view_free(&a->view); break; }
        if (action == R3_UP || action == R3_PAGEUP || action == R3_LEFT) a->view.page = a->view.page > 0 ? a->view.page - 1 : 0;
        else if (action == R3_DOWN || action == R3_PAGEDOWN || action == R3_RIGHT) a->view.page = a->view.page < pages - 1 ? a->view.page + 1 : pages - 1;
        else if (action == R3_HOME) a->view.page = 0;
        else if (action == R3_END) a->view.page = pages - 1;
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
        const View *v = &a->view;
        int pages = view_pages(v);
        char pg[32];
        snprintf(pg, sizeof pg, "Page %d/%d", v->page + 1, pages);
        R3Box b = {1, v->title, pages > 1 ? pg : NULL, "B close  Up/Down page", 0};
        r3_box(s, 0, R3_DISPLAY_TOP, R3_COLS, R3_DISPLAY_ROWS, &b);
        for (int r = 0; r < VIEW_ROWS; r++) {
            int ln = v->page * VIEW_ROWS + r;
            if (ln >= v->n) break;
            r3_put_cells(s, 1, R3_DISPLAY_TOP + 1 + r, v->cells + (size_t)ln * VIEW_W, v->len[ln], R3_ST(R3_ST_BODY));
        }
        char sz[16];
        fmt_size((unsigned long long)v->filesize, sz, sizeof sz);
        snprintf(left, sizeof left, "%s view, %s bytes", v->hex ? "Hex" : "Text", sz);
        c.reply = v->truncated ? "Only the first part of this file is shown" : "B closes the viewer";
        c.reply_style = v->truncated ? R3_ST_WARN : R3_ST_HINT;
        c.line = v->title;
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
static void shot(const R3Screen *s, const char *name) {
    const char *dir = getenv("R3I6NOS_FM_SHOTS");
    if (!dir || !*dir) return;
    uint32_t *px = malloc((size_t)R3_W * R3_H * 4);
    if (!px) return;
    r3_render(s, px);
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
    shot(&s, "1-browse");
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
    shot(&s, "2-menu");
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
    shot(&s, "3-confirm-copy");
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_JOB, "A starts the job");
    app_compose(&a, &s);
    CHECK(row_has(&s, 21, "B cancels"), "job tip");
    shot(&s, "4-job");
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
    shot(&s, "5-rename");
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
    CHECK(a.mode == M_VIEW && !a.view.hex && a.view.n >= 3, "text view (%d lines)", a.view.n);
    app_compose(&a, &s);
    CHECK(dump_has(&s, "line one") && dump_has(&s, "    tabbed") && !dump_has(&s, "Page 1/1"), "text and tab shown, no page number for one page");
    shot(&s, "6-view-text");
    app_action(&a, R3_BACK);
    CHECK(a.mode == M_BROWSE, "B closes the viewer");
    a.p[0].sel = pane_find(&a.p[0], "data.bin");
    app_action(&a, R3_ACCEPT);
    CHECK(a.mode == M_VIEW && a.view.hex, "binary shows as hex");
    app_compose(&a, &s);
    CHECK(dump_has(&s, "000000  00 01 02 41 42 43 FF FE  |...ABC..|"), "hex line");
    shot(&s, "7-view-hex");
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
    rm_rf(root);
    if (real) { rm_rf(real); free(real); }
    return g_fail;
}

/* ---- main ----------------------------------------------------------------------------------------------------------------------- */

static int save_screenshot(const char *path, const char *left, const char *right) {
    App a;
    app_init(&a, left, right);
    R3Screen s;
    app_compose(&a, &s);
    uint32_t *px = malloc((size_t)R3_W * R3_H * 4);
    if (!px) return 1;
    r3_render(&s, px);
    SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(px, R3_W, R3_H, 32, R3_W * 4, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    int rc = surf ? SDL_SaveBMP(surf, path) : 1;
    if (surf) SDL_FreeSurface(surf);
    free(px);
    app_free(&a);
    return rc;
}

static void open_pads(void) {
    for (int i = 0; i < SDL_NumJoysticks(); i++) SDL_JoystickOpen(i);
}

int main(int argc, char **argv) {
    int selftest = 0;
    const char *shot = NULL;
    const char *left = getenv("R3I6NOS_FM_LEFT"), *right = getenv("R3I6NOS_FM_RIGHT");
    if (!left || !*left) left = access("/roms", R_OK) == 0 ? "/roms" : (getenv("HOME") ? getenv("HOME") : "/");
    if (!right || !*right) right = getenv("HOME") && *getenv("HOME") ? getenv("HOME") : "/";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--selftest")) selftest = 1;
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--version")) { puts("commander " VERSION); return 0; }
        else { fprintf(stderr, "usage: %s [--selftest | --screenshot out.bmp | --version]\n", argv[0]); return 2; }
    }
    if (shot) {
        int rc = save_screenshot(shot, left, right);
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
    if (!r3_video_init(&video, r)) { fprintf(stderr, "commander: texture: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

    App app;
    app_init(&app, left, right);
    R3Screen screen;

    if (selftest) {
        app_compose(&app, &screen);
        r3_video_draw(&video, r, &screen, 1);
        SDL_version v;
        SDL_GetVersion(&v);
        printf("R3I6NOS_SMOKE ok commander=%s sdl=%d.%d.%d joysticks=%d renderer=%s\n", VERSION, v.major, v.minor, v.patch,
               SDL_NumJoysticks(), SDL_GetCurrentVideoDriver());
        app_free(&app);
        r3_video_free(&video);
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    open_pads();
    int typing = 0, force = 1;
    while (!app.quit) {
        SDL_Event e;
        int have = (app.mode == M_JOB) ? SDL_PollEvent(&e) : SDL_WaitEventTimeout(&e, 500);
        while (have) {
            switch (e.type) {
            case SDL_QUIT: app.quit = 1; break;
            case SDL_WINDOWEVENT: force = 1; break;
            case SDL_JOYDEVICEADDED: SDL_JoystickOpen(e.jdevice.which); break;
            case SDL_JOYBUTTONUP:
                if (r3_pad_action(e.jbutton.button) == R3_SELECT) app.select_held = 0;
                break;
            case SDL_JOYBUTTONDOWN: {
                int act = r3_pad_action(e.jbutton.button);
                if (act == R3_SELECT) { app.select_held = 1; break; }
                if (act == R3_MORE && app.select_held) { app.quit = 1; break; } /* Select+Start leaves, as in the input checker */
                if (act != R3_NONE) app_action(&app, act);
                break;
            }
            case SDL_KEYDOWN: {
                SDL_Keycode k = e.key.keysym.sym;
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
        app_compose(&app, &screen);
        r3_video_draw(&video, r, &screen, force);
        force = 0;
    }
    app_free(&app);
    r3_video_free(&video);
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
