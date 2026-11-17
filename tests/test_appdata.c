/* theme/r3i6nos_appdata.h, the C frontend of r3i6nos-appdata (CLAUDE.md rule 16, docs/APP-DATA.md), against the real engine on fake cards and
 * against small fake engines (a slow one that can be stopped, a failing one). Nothing is drawn: the screens are read as text.
 * Needs python3 (the engine). Built and run by scripts/check.sh. */
#define _GNU_SOURCE
#include "r3i6nos_ansiapps.h"
#include "r3i6nos_toast.h"
#include "r3i6nos_appdata.h"

static int fails;
#define CHECK(c, m) do { if (!(c)) { fails++; fprintf(stderr, "FAIL: %s\n", m); } } while (0)

static char root[PATH_MAX], sys_at[PATH_MAX], data_at[PATH_MAX], engine[PATH_MAX], seen_text[65536];
static const int *script;
static int script_n, script_i, screens;

static void wf(const char *path, const char *text) {
    char d[PATH_MAX];
    snprintf(d, sizeof d, "%s", path);
    char *sl = strrchr(d, '/');
    if (sl) { *sl = 0; r3_ad_mkdirs(d); }
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}
static char *slurp(const char *path) {
    static char buf[4096];
    FILE *f = fopen(path, "r");
    size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
    if (f) fclose(f);
    buf[n] = 0;
    return buf;
}
static int exists(const char *path) { return access(path, F_OK) == 0; }
static void path_in(char *out, const char *dir, const char *rel) { snprintf(out, PATH_MAX, "%s/%s", dir, rel); }

static void cards(int system, int data, const char *data_fs) {
    char text[2048], p[PATH_MAX];
    snprintf(text, sizeof text, "SYSTEM_AT=%s\nSYSTEM_FS=exfat\nDATA_AT=%s\nDATA_FS=%s\n", system ? sys_at : "none", data ? data_at : "none", data_fs);
    path_in(p, root, "cards");
    wf(p, text);
}

/* the screens, as text, and the person's presses from a script */
static void seen(void *ctx, const R3Screen *s) {
    (void)ctx;
    char rows[R3_ROWS][R3_COLS + 1];
    r3_dump(s, rows);
    screens++;
    size_t at = strlen(seen_text);
    for (int i = 0; i < R3_ROWS && at + R3_COLS + 2 < sizeof seen_text; i++) at += (size_t)snprintf(seen_text + at, sizeof seen_text - at, "%s\n", rows[i]);
}
static int press(void *ctx, int wait_ms) {
    (void)ctx;
    if (script_i >= script_n) return wait_ms > 0 ? R3_BACK : R3_NONE;   /* a dialog waiting for a press gets B (no hang); a bar polling gets nothing */
    return script[script_i++];
}
static void keys(const int *a, int n) { script = a; script_n = n; script_i = 0; seen_text[0] = 0; screens = 0; }
#define KEYS(...) do { static const int k_[] = { __VA_ARGS__ }; keys(k_, (int)(sizeof k_ / sizeof k_[0])); } while (0)
static R3AppDataUI ui = { NULL, NULL, NULL, press, seen, NULL };
static int shows(const char *needle) { return strstr(seen_text, needle) != NULL; }

static void fresh(void) {                    /* new empty cards and engine memory */
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof cmd, "rm -rf '%s/roms' '%s/roms2' '%s/state'", root, root, root);
    if (system(cmd) != 0) fprintf(stderr, "rm failed\n");
    r3_ad_mkdirs(sys_at);
    r3_ad_mkdirs(data_at);
    cards(1, 1, "exfat");
    unsetenv("R3I6NOS_APP_DATA");
    unsetenv("R3I6NOS_DATA_CARD");
    setenv("R3I6NOS_APPDATA", engine, 1);
}

static void put(const char *card, const char *rel, const char *text) {
    char p[PATH_MAX], full[PATH_MAX];
    snprintf(full, sizeof full, "%s/Commander/%s", card, rel);
    snprintf(p, sizeof p, "%s", full);
    wf(p, text);
}

/* a new launch: an app is a new process, so what the last one exported is not there */
static int init(R3AppData *d) {
    unsetenv("R3I6NOS_APP_DATA");
    unsetenv("R3I6NOS_DATA_CARD");
    return r3_appdata_init(d, "Commander", &ui);
}

static void fake_engine(const char *name, const char *body) {
    char p[PATH_MAX];
    path_in(p, root, name);
    wf(p, body);
    chmod(p, 0755);
}

int main(void) {
    char tmpl[PATH_MAX], p[PATH_MAX], q[PATH_MAX];
    snprintf(tmpl, sizeof tmpl, "%s/r3-appdata-XXXXXX", getenv("TMPDIR") && *getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 1; }
    snprintf(root, sizeof root, "%s", tmpl);
    snprintf(sys_at, sizeof sys_at, "%s/roms", root);
    snprintf(data_at, sizeof data_at, "%s/roms2", root);
    const char *src = getenv("R3I6NOS_APPDATA_ENGINE");
    snprintf(engine, sizeof engine, "%s", src && *src ? src : "overlay/usr/local/bin/r3i6nos-appdata");
    path_in(p, root, "cards");
    setenv("R3I6NOS_CARDS_FILE", p, 1);
    path_in(p, root, "state");
    setenv("R3I6NOS_APPDATA_STATE", p, 1);
    path_in(p, root, "nopower");
    setenv("R3I6NOS_POWER_SUPPLY", p, 1);
    setenv("R3I6NOS_APPDATA_MIN_FREE_MB", "0", 1);
    R3AppData d;

    /* a caller that has decided: used as it is, no engine, no question */
    fresh();
    setenv("R3I6NOS_APPDATA", "/nonexistent/engine", 1);
    path_in(p, root, "decided");
    setenv("R3I6NOS_APP_DATA", p, 1);
    KEYS(R3_ACCEPT);
    CHECK(r3_appdata_init(&d, "Commander", &ui) == 0 && !strcmp(d.use, p) && exists(p), "a preset R3I6NOS_APP_DATA is used and made");
    CHECK(screens == 0, "and nothing is asked");

    /* no engine at all: the old place, said so */
    fresh();
    setenv("R3I6NOS_APPDATA", "/nonexistent/engine", 1);
    setenv("R3I6NOS_ROMS", sys_at, 1);
    CHECK(init(&d) == 0, "no engine: init still answers");
    path_in(p, sys_at, "Commander");
    CHECK(!strcmp(d.use, p) && !d.engine && strstr(d.notice, "not available"), "no engine: the old place and a notice");
    CHECK(!strcmp(getenv("R3I6NOS_APP_DATA"), p), "no engine: exported");
    unsetenv("R3I6NOS_ROMS");

    /* nothing anywhere, a valid data card: start there, no question */
    fresh();
    KEYS(R3_ACCEPT);
    CHECK(init(&d) == 0, "init");
    path_in(p, data_at, "Commander");
    CHECK(!strcmp(d.use, p) && exists(p) && d.engine && screens == 0, "no data anywhere: the data card, no dialog");
    CHECK(!strcmp(d.data_card, data_at), "the data card's root is exported");
    CHECK(!strcmp(getenv("R3I6NOS_DATA_CARD"), data_at), "R3I6NOS_DATA_CARD");

    /* quiet: the same answer with no dialog even when there is a question to ask */
    fresh();
    put(sys_at, "save.dat", "mine");
    KEYS(R3_ACCEPT);
    CHECK(r3_appdata_quiet(&d, "Commander") == 0 && screens == 0, "quiet: no dialog");
    path_in(p, sys_at, "Commander");
    CHECK(!strcmp(d.use, p), "quiet: data only on the system card is used where it is, never moved");
    path_in(q, sys_at, "Commander/save.dat");
    CHECK(exists(q), "quiet: nothing moved");

    /* data only on the system card: the offer starts on Not now */
    fresh();
    put(sys_at, "save.dat", "twelve bytes");
    KEYS(R3_ACCEPT);
    CHECK(init(&d) == 0, "init");
    CHECK(shows("Move Commander's data to the data card?") && shows("Move all") && shows("Keep on the system card") && shows("Not now"), "the move offer is worded");
    CHECK(shows("12 B in 1 files"), "it says how much");
    path_in(p, sys_at, "Commander");
    CHECK(!strcmp(d.use, p), "Not now (the default): the system card is used");
    path_in(q, sys_at, "Commander/save.dat");
    CHECK(exists(q), "Not now moves nothing");

    /* B is Not now too */
    KEYS(R3_BACK);
    init(&d);
    CHECK(!strcmp(d.use, p) && exists(q), "B: not now");

    /* Move all: up twice from Not now */
    KEYS(R3_UP, R3_UP, R3_ACCEPT);
    CHECK(init(&d) == 0, "init");
    path_in(p, data_at, "Commander");
    CHECK(!strcmp(d.use, p), "after Move all the data card is used");
    path_in(q, data_at, "Commander/save.dat");
    CHECK(exists(q) && !strcmp(slurp(q), "twelve bytes"), "the file is on the data card, whole");
    path_in(q, sys_at, "Commander");
    CHECK(!exists(q), "and gone from the system card");
    CHECK(shows("Moving Commander's data") && shows("1 of 1 files"), "a progress screen showed (step, bar, count)");

    /* a fresh launch afterwards: no question */
    KEYS(R3_ACCEPT);
    init(&d);
    CHECK(screens == 0 && !strcmp(d.use, p), "settled: nothing is asked again");

    /* Keep on the system card, and not asked again */
    fresh();
    put(sys_at, "save.dat", "keep me");
    KEYS(R3_UP, R3_ACCEPT);
    init(&d);
    path_in(p, sys_at, "Commander");
    CHECK(!strcmp(d.use, p), "keep: the system card is used");
    KEYS(R3_ACCEPT);
    init(&d);
    CHECK(screens == 0 && !strcmp(d.use, p), "keep: not asked again");

    /* both cards: the merge dialog, and merge into the data card */
    fresh();
    put(sys_at, "a.txt", "from system");
    put(data_at, "b.txt", "from data");
    put(sys_at, "same.txt", "system version");
    put(data_at, "same.txt", "data version!!");
    KEYS(R3_ACCEPT);
    init(&d);
    CHECK(shows("Commander has data on both cards:") && shows("Merge into the data card (recommended)") && shows("Merge into the system card") &&
          shows("Don't ask again"), "the merge offer is worded");
    CHECK(shows("1 files are on both and differ"), "it counts the files that differ");
    path_in(p, data_at, "Commander");
    CHECK(!strcmp(d.use, p), "not now: the data card is used meanwhile");
    path_in(q, sys_at, "Commander/a.txt");
    CHECK(exists(q), "not now merges nothing");
    KEYS(R3_UP, R3_UP, R3_ACCEPT);
    init(&d);
    path_in(q, data_at, "Commander/a.txt");
    CHECK(exists(q), "merged: the system card's file is on the data card");
    path_in(q, sys_at, "Commander");
    CHECK(!exists(q), "merged: the system card's folder is gone");
    CHECK(shows("Merging Commander's data"), "merging has its own title");
    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof cmd, "ls '%s/Commander/.merge-conflicts'/*/same.txt >/dev/null 2>&1", data_at);
    CHECK(system(cmd) == 0, "the older of the two different files was kept, not deleted");

    /* merge into the system card: second choice */
    fresh();
    put(sys_at, "a.txt", "s");
    put(data_at, "b.txt", "d");
    KEYS(R3_UP, R3_ACCEPT);
    init(&d);
    path_in(p, sys_at, "Commander");
    path_in(q, sys_at, "Commander/b.txt");
    CHECK(!strcmp(d.use, p) && exists(q), "merged into the system card");

    /* Don't ask again */
    fresh();
    put(sys_at, "a.txt", "s");
    put(data_at, "b.txt", "d");
    KEYS(R3_DOWN, R3_ACCEPT);
    init(&d);
    KEYS(R3_ACCEPT);
    init(&d);
    CHECK(screens == 0, "don't ask again: not asked");

    /* the data card is out after being used: Needs the data card */
    fresh();
    put(data_at, "save.dat", "on the data card");
    KEYS(R3_ACCEPT);
    init(&d);
    cards(1, 0, "exfat");
    KEYS(R3_ACCEPT);
    CHECK(init(&d) == 0, "init");
    CHECK(shows("Needs the data card.") && shows("Use the system card (new, separate data)") && shows("Quit"), "needs-card is worded");
    CHECK(d.quit, "the default is Quit");
    KEYS(R3_UP, R3_ACCEPT);
    init(&d);
    path_in(p, sys_at, "Commander");
    CHECK(!d.quit && !strcmp(d.use, p), "the system card for now");

    /* a card that cannot be used: a notice, no dialog */
    fresh();
    put(sys_at, "save.dat", "x");
    setenv("R3I6NOS_APPDATA_MIN_FREE_MB", "999999999", 1);
    KEYS(R3_ACCEPT);
    init(&d);
    setenv("R3I6NOS_APPDATA_MIN_FREE_MB", "0", 1);
    path_in(p, sys_at, "Commander");
    CHECK(screens == 0 && !strcmp(d.use, p) && strstr(d.notice, "free") && strstr(d.notice, "system card"), "a full data card: the system card, with a notice");
    cards(1, 1, "ntfs");
    init(&d);
    CHECK(strstr(d.notice, "filesystem"), "an unsupported filesystem is said");

    /* a failing engine: the reason is shown, the app goes on */
    fresh();
    fake_engine("fail-engine",
        "#!/bin/sh\n"
        "case \"$1\" in\n"
        "  state) printf 'card=valid\\naction=offer-move\\nuse=%s/Commander\\ns_at=%s/Commander\\nd_at=%s/Commander\\ns_files=3\\ns_bytes=3000\\n' \"$R3_T_SYS\" \"$R3_T_SYS\" \"$R3_T_DATA\" ;;\n"
        "  move) echo 'STEP Planning'; echo 'FAIL The target card has 1 KB free'; exit 1 ;;\n"
        "  *) exit 0 ;;\n"
        "esac\n");
    path_in(p, root, "fail-engine");
    setenv("R3I6NOS_APPDATA", p, 1);
    setenv("R3_T_SYS", sys_at, 1);
    setenv("R3_T_DATA", data_at, 1);
    KEYS(R3_UP, R3_UP, R3_ACCEPT, R3_ACCEPT);
    CHECK(init(&d) == 0, "init after a failed move");
    CHECK(shows("It did not finish") && shows("The target card has 1 KB free"), "the engine's reason is shown");

    /* B stops the work: the engine is asked to, and the person is told */
    fresh();
    fake_engine("slow-engine",
        "#!/bin/sh\n"
        "case \"$1\" in\n"
        "  state) printf 'card=valid\\naction=offer-move\\nuse=%s/Commander\\ns_at=%s/Commander\\nd_at=%s/Commander\\ns_files=3\\ns_bytes=3000\\n' \"$R3_T_SYS\" \"$R3_T_SYS\" \"$R3_T_DATA\" ;;\n"
        "  move) trap 'echo \"FAIL stopped; what is done stays done\"; exit 1' TERM\n"
        "        echo 'STEP Copying'; echo 'PROGRESS 1/3'\n"
        "        i=0; while [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done; echo 'DONE copied=3 deleted=3 conflicts=0 skipped=0' ;;\n"
        "  *) exit 0 ;;\n"
        "esac\n");
    path_in(p, root, "slow-engine");
    setenv("R3I6NOS_APPDATA", p, 1);
    KEYS(R3_UP, R3_UP, R3_ACCEPT, R3_NONE, R3_BACK, R3_ACCEPT);
    CHECK(init(&d) == 0, "init after a stopped move");
    CHECK(shows("1 of 3 files") && shows("Stopping after this file"), "the bar shows the work and the stop");
    CHECK(shows("It did not finish"), "a stopped move is told");

    path_in(p, root, "x");
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", root);
    if (fails) { fprintf(stderr, "test_appdata: %d failure(s)\n", fails); return 1; }
    puts("test_appdata: ok");
    return 0;
}
