/* What the OTG / USB port can bring to the R36S, found from /sys, /proc and /dev (the IO Checker's USB and OTG pages and report section).
 *
 * Pure probing, no SDL: every root can be moved (R3I6NOS_SYS, R3I6NOS_PROC, R3I6NOS_DEV) so the self-test builds a fake tree and checks each
 * parser exactly. Nothing here needs root. What it finds:
 *   uio_usb_devices   every USB device: product, vendor:product, speed, what it is (keyboard, mouse, pad, storage, MIDI, audio, network, camera,
 *                     serial, Bluetooth, hub...) from its interface classes and the driver bound to each, and whether any interface has none
 *   uio_connectors    video outputs (DRM connectors): connected or not, the first mode, internal panel or not, on a USB adapter (DisplayLink) or not
 *   uio_usb_disks     USB mass storage: model, size, and where it is mounted if it is
 *   uio_nets          network adapters: Ethernet and Wi-Fi, link state, speed, address, bytes
 *   uio_midi_ports    ALSA raw MIDI ports (/dev/snd/midiC*D*), with the card's name and whether it is on USB
 *   uio_inputs        keyboards, mice and pads the kernel has made input devices for (/proc/bus/input/devices), and whether they are USB
 * MIT, like the rest of this app's source.
 */
#ifndef R3_USB_IO_H
#define R3_USB_IO_H

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <limits.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define UIO_MAX 24
/* Path buffers, each larger than the one that feeds it plus a name (base 4096, then +256 each), so GCC can see that nothing is cut. */
#define UIO_DEV 4400
#define UIO_IFD 4700
#define UIO_P 4800
#define UIO_SUB 5100

static inline const char *uio_root(const char *env, const char *def) {
    const char *v = getenv(env);
    return v && *v ? v : def;
}

/* root/rel into out; 0, or -1 if it does not fit. */
static inline int uio_path(char *out, size_t cap, const char *root, const char *rel) {
    int n = snprintf(out, cap, "%s/%s", root, rel);
    return n < 0 || (size_t)n >= cap ? -1 : 0;
}

/* a/b and a/b/c into out; 0, or -1 when it does not fit (the result is then cut). The return value is what keeps GCC's truncation warning quiet. */
static inline int uio_cat(char *out, size_t cap, const char *a, const char *b) {
    int n = snprintf(out, cap, "%s/%s", a, b);
    return n < 0 || (size_t)n >= cap ? -1 : 0;
}
static inline int uio_cat3(char *out, size_t cap, const char *a, const char *b, const char *c) {
    int n = snprintf(out, cap, "%s/%s/%s", a, b, c);
    return n < 0 || (size_t)n >= cap ? -1 : 0;
}

/* Copy src into out (cap bytes), cut to fit and always ended. Written by hand: GCC warns about snprintf("%s") from a long source into a short field. */
static inline void uio_copy(char *out, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (cap == 0) return;
    if (n >= cap) n = cap - 1;
    memcpy(out, src, n);
    out[n] = 0;
}

/* The first line of a small file, trimmed. 0 when it was read. */
static inline int uio_read(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    out[0] = 0;
    char *got = fgets(out, (int)cap, f);
    fclose(f);
    if (!got) return -1;
    size_t n = strlen(out);
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ' || out[n - 1] == '\t')) out[--n] = 0;
    size_t lead = 0;
    while (out[lead] == ' ' || out[lead] == '\t') lead++;
    if (lead) memmove(out, out + lead, strlen(out + lead) + 1);
    return 0;
}

static inline long uio_read_num(const char *path, long dflt) {
    char v[48];
    if (uio_read(path, v, sizeof v) != 0 || !v[0]) return dflt;
    char *end;
    long x = strtol(v, &end, 10);
    return end == v ? dflt : x;
}

/* Where a symlink in sysfs leads, last part only ("usbhid" for .../drivers/usbhid). "" when there is none. */
static inline void uio_link_base(const char *path, char *out, size_t cap) {
    char target[PATH_MAX];
    out[0] = 0;
    ssize_t n = readlink(path, target, sizeof target - 1);
    if (n <= 0) return;
    target[n] = 0;
    const char *slash = strrchr(target, '/');
    uio_copy(out, cap, slash ? slash + 1 : target);
}

/* Whether a device's real path goes through a USB bus (the card reader and the SD slots are not). */
static inline int uio_is_usb(const char *path) {
    char real[PATH_MAX];
    if (!realpath(path, real)) return 0;
    return strstr(real, "/usb") != NULL;
}

/* Append word to a space-separated list unless it is already there (and there is room). */
static inline void uio_add_word(char *list, size_t cap, const char *word) {
    size_t n = strlen(word);
    for (const char *p = list; *p;) {
        while (*p == ' ') p++;
        const char *e = p;
        while (*e && *e != ' ') e++;
        if ((size_t)(e - p) == n && !strncmp(p, word, n)) return;
        p = e;
    }
    size_t u = strlen(list);
    if (u + n + 2 >= cap) return;
    snprintf(list + u, cap - u, "%s%s", u ? " " : "", word);
}

static inline int uio_cmp_names(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* ---- USB devices -------------------------------------------------------------------------------------------------------------- */

typedef struct {
    char name[24];      /* sysfs name: 1-1, 1-1.2 */
    char vidpid[12];
    char product[48];
    char speed[12];     /* Mb/s, as the kernel says it */
    char kinds[72];     /* space separated: keyboard mouse pad hid storage midi audio network wifi serial camera bluetooth hub printer other */
    char drivers[72];   /* the drivers bound to its interfaces */
    int interfaces, unbound;
    int hub;
} UioDev;

static inline unsigned uio_hex(const char *s) { return (unsigned)strtoul(s, NULL, 16); }

/* What one interface is, from its class, subclass and protocol, and its driver when the class says only "vendor specific". */
static inline const char *uio_iface_kind(unsigned cls, unsigned sub, unsigned proto, const char *driver) {
    switch (cls) {
    case 0x01: return sub == 0x03 ? "midi" : "audio";
    case 0x02: return sub == 0x06 || sub == 0x0d || sub == 0x0e ? "network" : "serial";
    case 0x03: return proto == 1 ? "keyboard" : proto == 2 ? "mouse" : "hid";
    case 0x06: return "camera";     /* still image */
    case 0x07: return "printer";
    case 0x08: return "storage";
    case 0x09: return "hub";
    case 0x0e: return "camera";
    case 0x11: return "billboard";  /* a dock or cable saying an alternate mode (video out) it could not start: a notice, no driver is meant to bind */
    case 0x0a: return NULL;         /* CDC data: the other half of a network or serial pair */
    case 0xe0: return sub == 0x01 ? "bluetooth" : "wifi";
    default: break;
    }
    static const char *const net[] = {"asix", "ax88", "r8152", "r8153", "cdc_ether", "cdc_ncm", "smsc", "lan78", "pegasus", "rtl8150", "dm9601", "mcs7830", "usbnet", "qmi_wwan", "cdc_mbim", NULL};
    static const char *const ser[] = {"ftdi", "cp210", "ch341", "pl2303", "usbserial", "cdc_acm", "option", NULL};
    static const char *const wifi[] = {"rtl8", "mt76", "ath9k", "ath10k", "rt2800", "rt73", "8821", "8188", "rtw", "carl9170", "brcm", "iwlwifi", NULL};
    for (int i = 0; net[i]; i++) if (strstr(driver, net[i])) return "network";
    for (int i = 0; ser[i]; i++) if (strstr(driver, ser[i])) return "serial";
    for (int i = 0; wifi[i]; i++) if (strstr(driver, wifi[i])) return "wifi";
    return "other";
}

/* Every real USB device (not the root hubs, not the interfaces). Returns the count. */
static inline int uio_usb_devices(UioDev *out, int cap) {
    char base[PATH_MAX];
    if (uio_path(base, sizeof base, uio_root("R3I6NOS_SYS", "/sys"), "bus/usb/devices") != 0) return 0;
    DIR *d = opendir(base);
    if (!d) return 0;
    char *names[96];
    int nn = 0;
    struct dirent *e;
    while ((e = readdir(d)) && nn < 96) {
        if (e->d_name[0] == '.' || strchr(e->d_name, ':') || !strncmp(e->d_name, "usb", 3)) continue;
        names[nn++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)nn, sizeof names[0], uio_cmp_names);      /* a directory is not sorted: 1-1, 1-2, 1-2.1... */
    int n = 0;
    for (int i = 0; i < nn && n < cap; i++) {
        char dev[UIO_DEV], p[UIO_P], v[96];
        uio_cat(dev, sizeof dev, base, names[i]);
        UioDev *u = &out[n];
        memset(u, 0, sizeof *u);
        snprintf(u->name, sizeof u->name, "%s", names[i]);
        char vid[8] = "????", pid[8] = "????";
        uio_cat(p, sizeof p, dev, "idVendor");
        if (uio_read(p, v, sizeof v) == 0) snprintf(vid, sizeof vid, "%.4s", v);
        uio_cat(p, sizeof p, dev, "idProduct");
        if (uio_read(p, v, sizeof v) == 0) snprintf(pid, sizeof pid, "%.4s", v);
        snprintf(u->vidpid, sizeof u->vidpid, "%s:%s", vid, pid);
        uio_cat(p, sizeof p, dev, "product");
        if (uio_read(p, v, sizeof v) != 0) snprintf(v, sizeof v, "%s", u->vidpid);
        snprintf(u->product, sizeof u->product, "%.47s", v);
        uio_cat(p, sizeof p, dev, "speed");
        if (uio_read(p, v, sizeof v) == 0) snprintf(u->speed, sizeof u->speed, "%.11s", v);
        /* the interfaces: directories "<name>:1.0" */
        DIR *dd = opendir(dev);
        struct dirent *f;
        while (dd && (f = readdir(dd))) {
            if (!strchr(f->d_name, ':') || strncmp(f->d_name, names[i], strlen(names[i])) != 0) continue;
            char ifd[UIO_IFD], drv[40];
            uio_cat(ifd, sizeof ifd, dev, f->d_name);
            unsigned cls = 0, sub = 0, proto = 0;
            uio_cat(p, sizeof p, ifd, "bInterfaceClass");
            if (uio_read(p, v, sizeof v) == 0) cls = uio_hex(v);
            uio_cat(p, sizeof p, ifd, "bInterfaceSubClass");
            if (uio_read(p, v, sizeof v) == 0) sub = uio_hex(v);
            uio_cat(p, sizeof p, ifd, "bInterfaceProtocol");
            if (uio_read(p, v, sizeof v) == 0) proto = uio_hex(v);
            uio_cat(p, sizeof p, ifd, "driver");
            uio_link_base(p, drv, sizeof drv);
            u->interfaces++;
            if (!drv[0]) { if (cls != 0x11) u->unbound++; } else uio_add_word(u->drivers, sizeof u->drivers, drv);
            const char *k = uio_iface_kind(cls, sub, proto, drv);
            if (cls == 0x03) {  /* a HID that the kernel gave a joystick node is a pad */
                char *jsglob = NULL;
                DIR *inp = NULL;
                uio_cat(p, sizeof p, ifd, "input");
                inp = opendir(p);
                struct dirent *g;
                while (inp && (g = readdir(inp))) {
                    if (strncmp(g->d_name, "input", 5) != 0) continue;
                    char sub2[UIO_SUB];
                    uio_cat(sub2, sizeof sub2, p, g->d_name);
                    DIR *ii = opendir(sub2);
                    struct dirent *h;
                    while (ii && (h = readdir(ii))) if (!strncmp(h->d_name, "js", 2) && isdigit((unsigned char)h->d_name[2])) jsglob = h->d_name;
                    if (ii) closedir(ii);
                }
                if (inp) closedir(inp);
                if (jsglob) k = "pad";
            }
            if (k) uio_add_word(u->kinds, sizeof u->kinds, k);
            if (k && !strcmp(k, "hub")) u->hub = 1;
        }
        if (dd) closedir(dd);
        if (!u->kinds[0]) uio_add_word(u->kinds, sizeof u->kinds, "other");
        n++;
    }
    for (int i = 0; i < nn; i++) free(names[i]);
    return n;
}

/* ---- video outputs ------------------------------------------------------------------------------------------------------------ */

typedef struct {
    char name[40];      /* HDMI-A-1, DSI-1, DVI-I-1 */
    char status[16];    /* connected, disconnected, unknown */
    char mode[24];      /* the first (preferred) mode: 1920x1080 */
    int internal;       /* the unit's own panel */
    int usb;            /* behind a USB display adapter */
} UioConn;

static inline int uio_connectors(UioConn *out, int cap) {
    char base[PATH_MAX];
    if (uio_path(base, sizeof base, uio_root("R3I6NOS_SYS", "/sys"), "class/drm") != 0) return 0;
    DIR *d = opendir(base);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < cap) {
        if (strncmp(e->d_name, "card", 4) != 0) continue;
        const char *dash = strchr(e->d_name, '-');
        if (!dash) continue;        /* "card0" itself, not a connector */
        UioConn *c = &out[n];
        memset(c, 0, sizeof *c);
        snprintf(c->name, sizeof c->name, "%.39s", dash + 1);
        char p[PATH_MAX + 300], v[64];
        uio_cat3(p, sizeof p, base, e->d_name, "status");
        if (uio_read(p, v, sizeof v) != 0) snprintf(v, sizeof v, "unknown");
        snprintf(c->status, sizeof c->status, "%.15s", v);
        uio_cat3(p, sizeof p, base, e->d_name, "modes");
        if (uio_read(p, v, sizeof v) == 0) snprintf(c->mode, sizeof c->mode, "%.23s", v);
        c->internal = strstr(c->name, "DSI") || strstr(c->name, "LVDS") || strstr(c->name, "eDP") || strstr(c->name, "DPI") || strstr(c->name, "Virtual");
        char card[24];
        snprintf(card, sizeof card, "%.*s", (int)(dash - e->d_name), e->d_name);
        uio_cat3(p, sizeof p, base, card, "device");
        c->usb = uio_is_usb(p);
        n++;
    }
    closedir(d);
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && strcmp(out[j].name, out[j - 1].name) < 0; j--) { UioConn t = out[j]; out[j] = out[j - 1]; out[j - 1] = t; }
    return n;
}

/* ---- USB mass storage --------------------------------------------------------------------------------------------------------- */

typedef struct {
    char dev[16];       /* sda */
    char model[56];
    unsigned long long bytes;
    char mount[160];    /* "" when it is not mounted */
    char fstype[16];
    char part[16];      /* the mounted partition: sda1 */
} UioDisk;

/* "\040" and friends in /proc/mounts paths. */
static inline void uio_unescape(char *s) {
    char *w = s;
    for (const char *r = s; *r; r++) {
        if (r[0] == '\\' && isdigit((unsigned char)r[1]) && isdigit((unsigned char)r[2]) && isdigit((unsigned char)r[3])) {
            *w++ = (char)((r[1] - '0') * 64 + (r[2] - '0') * 8 + (r[3] - '0'));
            r += 3;
        } else *w++ = *r;
    }
    *w = 0;
}

static inline int uio_usb_disks(UioDisk *out, int cap) {
    char base[PATH_MAX], mounts[PATH_MAX];
    if (uio_path(base, sizeof base, uio_root("R3I6NOS_SYS", "/sys"), "block") != 0) return 0;
    DIR *d = opendir(base);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < cap) {
        if (strncmp(e->d_name, "sd", 2) != 0) continue;
        char p[PATH_MAX + 300], v[96];
        uio_cat3(p, sizeof p, base, e->d_name, "device");
        if (!uio_is_usb(p)) continue;
        UioDisk *u = &out[n];
        memset(u, 0, sizeof *u);
        snprintf(u->dev, sizeof u->dev, "%.15s", e->d_name);
        char vendor[40] = "", model[48] = "";
        snprintf(p, sizeof p, "%s/%s/device/vendor", base, e->d_name);
        uio_read(p, vendor, sizeof vendor);
        snprintf(p, sizeof p, "%s/%s/device/model", base, e->d_name);
        uio_read(p, model, sizeof model);
        snprintf(u->model, sizeof u->model, "%s%s%s", vendor, vendor[0] && model[0] ? " " : "", model);
        if (!u->model[0]) snprintf(u->model, sizeof u->model, "USB disk");
        uio_cat3(p, sizeof p, base, e->d_name, "size");
        if (uio_read(p, v, sizeof v) == 0) u->bytes = strtoull(v, NULL, 10) * 512ULL;
        n++;
    }
    closedir(d);
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && strcmp(out[j].dev, out[j - 1].dev) < 0; j--) { UioDisk t = out[j]; out[j] = out[j - 1]; out[j - 1] = t; }
    if (uio_path(mounts, sizeof mounts, uio_root("R3I6NOS_PROC", "/proc"), "mounts") == 0) {
        FILE *f = fopen(mounts, "r");
        char line[640];
        while (f && fgets(line, sizeof line, f)) {
            char dev[96], mp[256], fs[32];
            if (sscanf(line, "%95s %255s %31s", dev, mp, fs) != 3 || strncmp(dev, "/dev/", 5) != 0) continue;
            for (int i = 0; i < n; i++) {
                size_t l = strlen(out[i].dev);
                if (strncmp(dev + 5, out[i].dev, l) == 0 && !out[i].mount[0]) {
                    uio_unescape(mp);
                    snprintf(out[i].mount, sizeof out[i].mount, "%.159s", mp);
                    snprintf(out[i].fstype, sizeof out[i].fstype, "%.15s", fs);
                    snprintf(out[i].part, sizeof out[i].part, "%.15s", dev + 5);
                }
            }
        }
        if (f) fclose(f);
    }
    return n;
}

/* ---- network adapters --------------------------------------------------------------------------------------------------------- */

typedef struct {
    char name[20];
    int wifi, usb;
    char state[16];     /* up, down, unknown (operstate) */
    int carrier;        /* -1: not told */
    int speed;          /* Mb/s, -1: not told */
    char mac[20];
    char ip[20];
    unsigned long long rx, tx;
} UioNet;

static inline int uio_nets(UioNet *out, int cap) {
    char base[PATH_MAX];
    if (uio_path(base, sizeof base, uio_root("R3I6NOS_SYS", "/sys"), "class/net") != 0) return 0;
    DIR *d = opendir(base);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < cap) {
        if (e->d_name[0] == '.' || !strcmp(e->d_name, "lo")) continue;
        UioNet *u = &out[n];
        memset(u, 0, sizeof *u);
        snprintf(u->name, sizeof u->name, "%.19s", e->d_name);
        char p[PATH_MAX + 300], v[64];
        uio_cat3(p, sizeof p, base, e->d_name, "wireless");
        u->wifi = access(p, F_OK) == 0;
        uio_cat3(p, sizeof p, base, e->d_name, "phy80211");
        if (access(p, F_OK) == 0) u->wifi = 1;
        uio_cat3(p, sizeof p, base, e->d_name, "device");
        u->usb = uio_is_usb(p);
        uio_cat3(p, sizeof p, base, e->d_name, "operstate");
        if (uio_read(p, v, sizeof v) != 0) snprintf(v, sizeof v, "unknown");
        snprintf(u->state, sizeof u->state, "%.15s", v);
        uio_cat3(p, sizeof p, base, e->d_name, "carrier");
        u->carrier = (int)uio_read_num(p, -1);
        uio_cat3(p, sizeof p, base, e->d_name, "speed");
        u->speed = (int)uio_read_num(p, -1);
        uio_cat3(p, sizeof p, base, e->d_name, "address");
        uio_read(p, u->mac, sizeof u->mac);
        snprintf(p, sizeof p, "%s/%s/statistics/rx_bytes", base, e->d_name);
        u->rx = (unsigned long long)uio_read_num(p, 0);
        snprintf(p, sizeof p, "%s/%s/statistics/tx_bytes", base, e->d_name);
        u->tx = (unsigned long long)uio_read_num(p, 0);
        n++;
    }
    closedir(d);
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && strcmp(out[j].name, out[j - 1].name) < 0; j--) { UioNet t = out[j]; out[j] = out[j - 1]; out[j - 1] = t; }
    struct ifaddrs *ifa = NULL;
    if (n > 0 && getifaddrs(&ifa) == 0) {
        for (struct ifaddrs *i = ifa; i; i = i->ifa_next) {
            if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
            for (int k = 0; k < n; k++)
                if (!strcmp(out[k].name, i->ifa_name) && !out[k].ip[0])
                    inet_ntop(AF_INET, &((struct sockaddr_in *)(void *)i->ifa_addr)->sin_addr, out[k].ip, sizeof out[k].ip);
        }
        freeifaddrs(ifa);
    }
    return n;
}

/* ---- MIDI --------------------------------------------------------------------------------------------------------------------- */

typedef struct {
    char node[PATH_MAX > 120 ? 120 : PATH_MAX];     /* /dev/snd/midiC1D0 */
    int card, device;
    char name[56];      /* the card's id or name */
    int usb;
} UioMidi;

static inline int uio_midi_ports(UioMidi *out, int cap) {
    char dir[PATH_MAX];
    if (uio_path(dir, sizeof dir, uio_root("R3I6NOS_DEV", "/dev"), "snd") != 0) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < cap) {
        int c, dv;
        if (sscanf(e->d_name, "midiC%dD%d", &c, &dv) != 2) continue;
        UioMidi *m = &out[n];
        memset(m, 0, sizeof *m);
        snprintf(m->node, sizeof m->node, "%.60s/%.40s", dir, e->d_name);
        m->card = c;
        m->device = dv;
        const char *proc = uio_root("R3I6NOS_PROC", "/proc");
        char p[PATH_MAX + 64], rel[64];
        snprintf(rel, sizeof rel, "asound/card%d/id", c);
        uio_path(p, sizeof p, proc, rel);
        if (uio_read(p, m->name, sizeof m->name) != 0) snprintf(m->name, sizeof m->name, "card %d", c);
        snprintf(rel, sizeof rel, "asound/card%d/usbbus", c);
        uio_path(p, sizeof p, proc, rel);
        m->usb = access(p, F_OK) == 0;
        n++;
    }
    closedir(d);
    /* in card, then device order */
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && (out[j].card < out[j - 1].card || (out[j].card == out[j - 1].card && out[j].device < out[j - 1].device)); j--) {
            UioMidi t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
        }
    return n;
}

/* A MIDI message as words: "note on C4 vel 100", "note off", "control 7 = 90", or the raw bytes. */
static inline void uio_midi_text(const unsigned char *b, int n, char *out, size_t cap) {
    static const char *const nn[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (n >= 3 && (b[0] & 0xF0) == 0x90 && b[2] > 0) snprintf(out, cap, "ch%d note on %s%d vel %d", (b[0] & 15) + 1, nn[b[1] % 12], b[1] / 12 - 1, b[2]);
    else if (n >= 3 && ((b[0] & 0xF0) == 0x80 || (b[0] & 0xF0) == 0x90)) snprintf(out, cap, "ch%d note off %s%d", (b[0] & 15) + 1, nn[b[1] % 12], b[1] / 12 - 1);
    else if (n >= 3 && (b[0] & 0xF0) == 0xB0) snprintf(out, cap, "ch%d control %d = %d", (b[0] & 15) + 1, b[1], b[2]);
    else if (n >= 2 && (b[0] & 0xF0) == 0xC0) snprintf(out, cap, "ch%d program %d", (b[0] & 15) + 1, b[1]);
    else if (n >= 3 && (b[0] & 0xF0) == 0xE0) snprintf(out, cap, "ch%d pitch bend %d", (b[0] & 15) + 1, b[1] + b[2] * 128 - 8192);
    else {
        size_t u = 0;
        out[0] = 0;
        for (int i = 0; i < n && u + 4 < cap; i++) u += (size_t)snprintf(out + u, cap - u, "%s%02X", i ? " " : "", b[i]);
    }
}

/* ---- keyboards, mice and pads the kernel made input devices for ---------------------------------------------------------------- */

typedef struct {
    char name[64];
    int keyboard, mouse, pad, usb;
} UioInput;

static inline int uio_inputs(UioInput *out, int cap) {
    char path[PATH_MAX];
    if (uio_path(path, sizeof path, uio_root("R3I6NOS_PROC", "/proc"), "bus/input/devices") != 0) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[512], name[64] = "", handlers[160] = "";
    int usb = 0, have = 0, n = 0, rel_xy = 0;
    for (;;) {
        char *got = fgets(line, sizeof line, f);
        if (!got || line[0] == '\n' || line[0] == '\r') {
            if (have && n < cap) {
                UioInput *u = &out[n];
                memset(u, 0, sizeof *u);
                snprintf(u->name, sizeof u->name, "%s", name);
                u->keyboard = strstr(handlers, "kbd") && strstr(handlers, "leds");
                /* this kernel (4.4.189) may have no mousedev: then a mouse has only an event node, told by its relative X and Y axes */
                u->mouse = strstr(handlers, "mouse") != NULL || rel_xy;
                u->pad = strstr(handlers, "js") != NULL;
                u->usb = usb;
                if (u->keyboard || u->mouse || u->pad) n++;
            }
            name[0] = handlers[0] = 0;
            usb = have = rel_xy = 0;
            if (!got) break;
            continue;
        }
        have = 1;
        if (!strncmp(line, "N: Name=\"", 9)) {
            uio_copy(name, sizeof name, line + 9);
            char *q = strrchr(name, '"');
            if (q) *q = 0;
        } else if (!strncmp(line, "H: Handlers=", 12)) {
            uio_copy(handlers, sizeof handlers, line + 12);
        } else if (!strncmp(line, "B: REL=", 7)) {
            const char *w = strrchr(line, ' ');   /* the last word holds the low bits: REL_X is bit 0, REL_Y bit 1 */
            if (w && !strncmp(w + 1, "REL=", 4)) w += 4;   /* one word: "B: REL=903" has no space after the "=" */
            rel_xy = w && (strtoul(w + 1, NULL, 16) & 3) == 3;
        } else if (!strncmp(line, "S: Sysfs=", 9)) {
            usb = strstr(line, "/usb") != NULL;
        }
    }
    fclose(f);
    return n;
}

#endif /* R3_USB_IO_H */
