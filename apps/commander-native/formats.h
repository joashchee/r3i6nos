/* formats.h: what a file is and what is in it, for Commander's viewer. Written for r3i6nos (MIT), no libraries.
 *
 * The identification follows the same rules as the ansiapps catalogue app Diskette (its file-type table and per-format details,
 * reimplemented here in C for the R36S): magic bytes first, then a few deep or footer signatures gated by the extension, then the
 * extension, then a look at the bytes (text or binary). On top of that it knows the formats r3i6nos's own apps read (tracker
 * modules, MIDI, music as text, LÖVE games, .deb packages, KlamANSI drawings, ANSI art).
 *
 *   ff_open / ff_close    a file opened for reading, with its first FF_HEAD bytes in memory
 *   ff_identify           category and label ("Image", "PNG image")
 *   ff_info               the Info page: label/value rows (size, dimensions, tags, duration, versions, ...)
 *   ff_content            the Content page: a listing (archives, disk images), text taken out of a document, a module's
 *                         messages, or a decompressed .gz; pictures and ANSI art are drawn by images.h and ansiart.h instead
 *
 * Everything reads with bounds checks and caps (FF_MAX_LOAD, FF_MAX_LIST) so a damaged or huge file gives fewer rows, never a crash or
 * the unit's whole memory. Nothing here writes. */
#ifndef COMMANDER_FORMATS_H
#define COMMANDER_FORMATS_H

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "images.h"
#include "inflate.h"
#include "lzma.h"
#include "zstd.h"

#define FF_HEAD 4096
#define FF_MAX_LOAD (32u * 1024u * 1024u)  /* the most read into memory for one page */
#define FF_MAX_UNPACK (16u * 1024u * 1024u) /* the most unpacked from one compressed member */
#define FF_MAX_LIST 5000                    /* the most entries listed */

/* ---- a file to look at --------------------------------------------------------------------------------------------------------- */

typedef struct {
    int fd;
    off_t size;
    uint8_t head[FF_HEAD];
    size_t hn;
    char name[256], ext[16];
} FFile;

static size_t ff_read(const FFile *f, off_t off, void *buf, size_t len) {
    size_t got = 0;
    if (off < 0 || off >= f->size) return 0;
    while (got < len) {
        ssize_t r = pread(f->fd, (char *)buf + got, len - got, off + (off_t)got);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (r == 0) break;
        got += (size_t)r;
    }
    return got;
}

/* len bytes from off (fewer at the end of the file) in a new buffer with a 0 after them; NULL if none. */
static uint8_t *ff_load(const FFile *f, off_t off, size_t len, size_t *got) {
    *got = 0;
    if (off < 0 || off >= f->size) return NULL;
    if ((off_t)len > f->size - off) len = (size_t)(f->size - off);
    if (len > FF_MAX_LOAD) len = FF_MAX_LOAD;
    uint8_t *b = (uint8_t *)malloc(len + 1);
    if (!b) return NULL;
    *got = ff_read(f, off, b, len);
    b[*got] = 0;
    return b;
}

static int ff_open(FFile *f, const char *path, char *err, size_t errsz) {
    memset(f, 0, sizeof *f);
    f->fd = open(path, O_RDONLY);
    if (f->fd < 0) { snprintf(err, errsz, "%s", strerror(errno)); return -1; }
    struct stat st;
    if (fstat(f->fd, &st) != 0 || !S_ISREG(st.st_mode)) { close(f->fd); f->fd = -1; snprintf(err, errsz, "not a plain file"); return -1; }
    f->size = st.st_size;
    const char *slash = strrchr(path, '/');
    snprintf(f->name, sizeof f->name, "%s", slash ? slash + 1 : path);
    const char *dot = strrchr(f->name, '.');
    if (dot && dot != f->name && strlen(dot + 1) < sizeof f->ext) {
        for (size_t i = 0; dot[1 + i]; i++) f->ext[i] = (char)tolower((unsigned char)dot[1 + i]);
    }
    f->hn = ff_read(f, 0, f->head, FF_HEAD);
    return 0;
}

static void ff_close(FFile *f) {
    if (f->fd >= 0) close(f->fd);
    f->fd = -1;
}

static int ff_at(const uint8_t *b, size_t n, size_t off, const void *magic, size_t len) {
    return off + len <= n && memcmp(b + off, magic, len) == 0;
}
#define FF_AT(f, off, lit) ff_at((f)->head, (f)->hn, (off), (lit), sizeof(lit) - 1)

static unsigned rd16le(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned rd16be(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static uint32_t rd32le(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rd32be(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t rd64le(const uint8_t *p) { return rd32le(p) | (uint64_t)rd32le(p + 4) << 32; }
static uint64_t rd64be(const uint8_t *p) { return (uint64_t)rd32be(p) << 32 | rd32be(p + 4); }

/* ---- what Info and Content are made of ---------------------------------------------------------------------------------------- */

typedef struct { char k[28]; char v[232]; } FRow;
typedef struct { FRow *r; int n, cap; } FInfo;

static void info_free(FInfo *in) { free(in->r); memset(in, 0, sizeof *in); }

static void info_add(FInfo *in, const char *k, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void info_add(FInfo *in, const char *k, const char *fmt, ...) {
    if (in->n >= 400) return;
    if (in->n == in->cap) {
        int cap = in->cap ? in->cap * 2 : 32;
        FRow *r = (FRow *)realloc(in->r, (size_t)cap * sizeof *r);
        if (!r) return;
        in->r = r;
        in->cap = cap;
    }
    FRow *row = &in->r[in->n];
    snprintf(row->k, sizeof row->k, "%s", k);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(row->v, sizeof row->v, fmt, ap);
    va_end(ap);
    for (char *c = row->v; *c; c++) if ((unsigned char)*c < 32) *c = ' ';
    if (row->v[0]) in->n++;
}

/* A field from a file: printable bytes only, trimmed (len may run past a 0). */
static void ff_clean(char *out, size_t cap, const uint8_t *s, size_t len) {
    size_t k = 0;
    for (size_t i = 0; i < len && s[i] && k + 1 < cap; i++) out[k++] = (s[i] < 32 || s[i] == 127) ? ' ' : (char)s[i];
    while (k && out[k - 1] == ' ') k--;
    out[k] = 0;
    size_t lead = 0;
    while (out[lead] == ' ') lead++;
    if (lead) memmove(out, out + lead, k - lead + 1);
}

static void info_field(FInfo *in, const char *k, const uint8_t *s, size_t len) {
    char v[232];
    ff_clean(v, sizeof v, s, len);
    if (v[0]) info_add(in, k, "%s", v);
}

typedef struct { char *s; size_t n, cap; int lines, cut; } FText;

static void text_free(FText *t) { free(t->s); memset(t, 0, sizeof *t); }

static void text_add(FText *t, const char *s, size_t n) {
    if (t->cut) return;
    if (t->n + n > FF_MAX_UNPACK) { t->cut = 1; return; }
    if (t->n + n + 1 > t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 4096;
        while (cap < t->n + n + 1) cap *= 2;
        char *g = (char *)realloc(t->s, cap);
        if (!g) { t->cut = 1; return; }
        t->s = g;
        t->cap = cap;
    }
    memcpy(t->s + t->n, s, n);
    t->n += n;
    t->s[t->n] = 0;
    for (size_t i = 0; i < n; i++) if (s[i] == '\n') t->lines++;
}

static void text_line(FText *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void text_line(FText *t, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 2) n = (int)sizeof buf - 2;
    buf[n++] = '\n';
    text_add(t, buf, (size_t)n);
}

static void fmt_bytes(unsigned long long n, char *out, size_t cap) {
    static const char *u[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double v = (double)n;
    int i = 0;
    while (v >= 1024 && i < 4) { v /= 1024; i++; }
    if (i == 0) snprintf(out, cap, "%llu B", n);
    else snprintf(out, cap, "%.1f %s", v, u[i]);
}

static void fmt_duration(double secs, char *out, size_t cap) {
    if (secs < 0 || secs != secs) { snprintf(out, cap, "?"); return; }
    long s = (long)(secs + 0.5);
    if (s >= 3600) snprintf(out, cap, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
    else if (secs < 10) snprintf(out, cap, "%.2f s", secs);
    else snprintf(out, cap, "%ld:%02ld", s / 60, s % 60);
}

static void fmt_time(time_t t, char *out, size_t cap) {
    struct tm tmv;
    if (t <= 0 || !gmtime_r(&t, &tmv)) { snprintf(out, cap, "?"); return; }
    strftime(out, cap, "%Y-%m-%d %H:%M UTC", &tmv);
}

/* ---- identification ---------------------------------------------------------------------------------------------------------------- */

enum {
    K_UNKNOWN, K_TEXT, K_EMPTY,
    /* pictures */ K_PNG, K_JPEG, K_GIF, K_BMP, K_ICO, K_TIFF, K_WEBP, K_PSD, K_HEIF, K_TGA, K_QOI, K_PCX, K_PNM, K_SVG,
    /* text art */ K_ANSI, K_XBIN, K_BINART, K_ADF, K_IDF, K_TND, K_KAN,
    /* audio */ K_WAV, K_AIFF, K_FLAC, K_MP3, K_OGG, K_MIDI, K_RMID, K_WAVPACK, K_MOD, K_S3M, K_XM, K_IT, K_MTM, K_STM, K_669, K_MED,
    K_OKT, K_TRACKER, K_UMF, K_MUSICTEXT,
    /* video */ K_MP4, K_AVI, K_MKV, K_ASF, K_FLV, K_MPEGPS, K_MPEGTS,
    /* documents */ K_PDF, K_OLE, K_RTF, K_OOXML, K_ODF, K_EPUB, K_HTML, K_EML, K_MBOX, K_XML, K_JSON, K_CSV,
    /* archives */ K_ZIP, K_LOVE, K_JAR, K_RAR, K_7Z, K_GZIP, K_BZIP2, K_XZ, K_ZSTD, K_LZIP, K_CAB, K_TAR, K_AR, K_DEB, K_RPM, K_CPIO,
    K_OLDARC, K_LZH, K_MSIX,
    /* disk images */ K_ISO, K_DMG, K_VHD, K_DISK,
    /* programs */ K_ELF, K_PE, K_MACHO, K_FAT, K_CLASS, K_WASM, K_LUAC, K_SCRIPT, K_APPIMAGE,
    /* fonts */ K_TTF, K_TTC, K_WOFF, K_WOFF2, K_PSF,
    /* data */ K_SQLITE, K_DBF, K_PARQUET, K_TORRENT, K_LNK, K_REGF,
    /* 3D */ K_OBJ, K_STL, K_GLB, K_GLTF,
};

typedef struct { const char *cat, *label; int kind; } FType;

typedef struct { size_t off; const char *magic; size_t len; const char *cat, *label; int kind; } FSig;
#define SIG(off, lit, cat, label, kind) {off, lit, sizeof(lit) - 1, cat, label, kind}

/* The magic-byte table, in the order they are tried (a longer or deeper signature before a shorter one that it starts with). */
static const FSig FF_SIGS[] = {
    SIG(0, "\x89PNG\r\n\x1a\n", "Image", "PNG image", K_PNG),
    SIG(0, "\xFF\xD8\xFF", "Image", "JPEG image", K_JPEG),
    SIG(0, "GIF87a", "Image", "GIF image", K_GIF),
    SIG(0, "GIF89a", "Image", "GIF image", K_GIF),
    SIG(0, "II*\x00", "Image", "TIFF image", K_TIFF),
    SIG(0, "MM\x00*", "Image", "TIFF image", K_TIFF),
    SIG(0, "8BPS", "Image", "Photoshop document", K_PSD),
    SIG(0, "XBIN\x1a", "Image", "XBin art", K_XBIN),
    SIG(0, "qoif", "Image", "QOI image", K_QOI),
    SIG(0, "fLaC", "Audio", "FLAC audio", K_FLAC),
    SIG(28, "OpusHead", "Audio", "Opus audio", K_OGG),
    SIG(28, "\x01vorbis", "Audio", "Ogg Vorbis audio", K_OGG),
    SIG(28, "\x7f" "FLAC", "Audio", "Ogg FLAC audio", K_OGG),
    SIG(28, "\x80theora", "Video", "Ogg Theora video", K_OGG),
    SIG(0, "OggS", "Audio", "Ogg audio/video", K_OGG),
    SIG(0, "MThd", "Audio", "MIDI", K_MIDI),
    SIG(0, "wvpk", "Audio", "WavPack audio", K_WAVPACK),
    SIG(0, "Extended Module: ", "Audio", "FastTracker 2 module (XM)", K_XM),
    SIG(0, "IMPM", "Audio", "Impulse Tracker module (IT)", K_IT),
    SIG(44, "SCRM", "Audio", "Scream Tracker 3 module (S3M)", K_S3M),
    SIG(0, "MTM", "Audio", "MultiTracker module (MTM)", K_MTM),
    SIG(20, "!Scream!", "Audio", "Scream Tracker 2 module (STM)", K_STM),
    SIG(0, "OKTASONG", "Audio", "Oktalyzer module", K_OKT),
    SIG(0, "MMD0", "Audio", "OctaMED module (MMD0)", K_MED),
    SIG(0, "MMD1", "Audio", "OctaMED module (MMD1)", K_MED),
    SIG(0, "MMD2", "Audio", "OctaMED module (MMD2)", K_MED),
    SIG(0, "MMD3", "Audio", "OctaMED module (MMD3)", K_MED),
    SIG(0, "THX\x00", "Audio", "AHX module", K_TRACKER),
    SIG(0, "THX\x01", "Audio", "AHX module", K_TRACKER),
    SIG(0, "HVL", "Audio", "HivelyTracker module", K_TRACKER),
    SIG(0, "SWTD", "Audio", "Sawteeth module", K_TRACKER),
    SIG(0, "cyd!song", "Audio", "Klystrack module", K_TRACKER),
    SIG(0, "MLEDMODL", "Audio", "Musicline Editor module", K_TRACKER),
    SIG(0, "PROTREK", "Audio", "ProTrekkr module", K_TRACKER),
    SIG(0, "DBM0", "Audio", "DigiBooster Pro module", K_TRACKER),
    SIG(44, "PTMF", "Audio", "PolyTracker module (PTM)", K_TRACKER),
    SIG(0, "FAR\xFE", "Audio", "Farandole module", K_TRACKER),
    SIG(0, "MAS_UTrack_V00", "Audio", "UltraTracker module", K_TRACKER),
    SIG(0, "AMF", "Audio", "DSMI module (AMF)", K_TRACKER),
    SIG(0, "UMF\x1a", "Audio", "Neumetik tracker song (UMF)", K_UMF),
    SIG(0, "\x1A\x45\xDF\xA3", "Video", "Matroska/WebM video", K_MKV),
    SIG(0, "\x30\x26\xB2\x75\x8E\x66\xCF\x11\xA6\xD9\x00\xAA\x00\x62\xCE\x6C", "Video", "Windows Media (ASF)", K_ASF),
    SIG(0, "FLV\x01", "Video", "Flash video", K_FLV),
    SIG(0, "\x00\x00\x01\xBA", "Video", "MPEG program stream", K_MPEGPS),
    SIG(0, "%PDF-", "Document", "PDF document", K_PDF),
    SIG(0, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", "Document", "Legacy MS Office document (or MSI)", K_OLE),
    SIG(0, "{\\rtf", "Document", "Rich Text Format document", K_RTF),
    SIG(0, "PK\x03\x04", "Archive", "ZIP archive", K_ZIP),
    SIG(0, "PK\x05\x06", "Archive", "ZIP archive (empty)", K_ZIP),
    SIG(0, "Rar!\x1a\x07\x01\x00", "Archive", "RAR 5 archive", K_RAR),
    SIG(0, "Rar!\x1a\x07\x00", "Archive", "RAR archive", K_RAR),
    SIG(0, "7z\xBC\xAF\x27\x1C", "Archive", "7-Zip archive", K_7Z),
    SIG(0, "\x1F\x8B\x08", "Archive", "Gzip compressed", K_GZIP),
    SIG(0, "BZh", "Archive", "Bzip2 compressed", K_BZIP2),
    SIG(0, "\xFD" "7zXZ\x00", "Archive", "XZ compressed", K_XZ),
    SIG(0, "\x28\xB5\x2F\xFD", "Archive", "Zstandard compressed", K_ZSTD),
    SIG(0, "LZIP", "Archive", "Lzip compressed", K_LZIP),
    SIG(0, "MSCF", "Archive", "Microsoft Cabinet archive", K_CAB),
    SIG(257, "ustar", "Archive", "TAR archive", K_TAR),
    SIG(0, "!<arch>\ndebian-binary", "Archive", "Debian package (.deb)", K_DEB),
    SIG(0, "!<arch>\n", "Archive", "Unix ar archive", K_AR),
    SIG(0, "\xED\xAB\xEE\xDB", "Archive", "RPM package", K_RPM),
    SIG(0, "070701", "Archive", "cpio archive (newc)", K_CPIO),
    SIG(0, "070702", "Archive", "cpio archive (newc, CRC)", K_CPIO),
    SIG(0, "070707", "Archive", "cpio archive (portable)", K_CPIO),
    SIG(0, "\x1A\x08", "Archive", "ARC archive (SEA ARC, 1985)", K_OLDARC),
    SIG(0, "\x60\xEA", "Archive", "ARJ archive", K_OLDARC),
    SIG(0, "ZOO", "Archive", "ZOO archive", K_OLDARC),
    SIG(2, "-lh", "Archive", "LZH/LHA archive", K_LZH),
    SIG(2, "-lz", "Archive", "LZH/LHA archive (LArc)", K_LZH),
    SIG(0, "\x1F\x9D", "Archive", "Unix compress (.Z)", K_OLDARC),
    SIG(7, "**ACE**", "Archive", "ACE archive", K_OLDARC),
    SIG(0, "DMS!", "Archive", "Amiga DiskMasher disk image", K_OLDARC),
    SIG(0, "\x7F" "ELF", "Executable", "ELF program or library", K_ELF),
    SIG(0, "MZ", "Executable", "Windows or DOS program", K_PE),
    SIG(0, "\xFE\xED\xFA\xCE", "Executable", "Mach-O program (32-bit)", K_MACHO),
    SIG(0, "\xFE\xED\xFA\xCF", "Executable", "Mach-O program (64-bit)", K_MACHO),
    SIG(0, "\xCE\xFA\xED\xFE", "Executable", "Mach-O program (32-bit)", K_MACHO),
    SIG(0, "\xCF\xFA\xED\xFE", "Executable", "Mach-O program (64-bit)", K_MACHO),
    SIG(0, "\x00" "asm", "Executable", "WebAssembly module", K_WASM),
    SIG(0, "\x1bLua", "Executable", "Lua bytecode", K_LUAC),
    SIG(0, "\x1bLJ", "Executable", "LuaJIT bytecode", K_LUAC),
    SIG(0, "\x4C\x00\x00\x00\x01\x14\x02\x00", "Shortcut", "Windows shortcut (.lnk)", K_LNK),
    SIG(0, "regf", "Database", "Windows Registry hive", K_REGF),
    SIG(0, "\x00\x01\x00\x00\x00", "Font", "TrueType font", K_TTF),
    SIG(0, "OTTO", "Font", "OpenType font", K_TTF),
    SIG(0, "true", "Font", "TrueType font (Apple)", K_TTF),
    SIG(0, "wOFF", "Font", "WOFF font", K_WOFF),
    SIG(0, "wOF2", "Font", "WOFF2 font", K_WOFF2),
    SIG(0, "ttcf", "Font", "TrueType collection", K_TTC),
    SIG(0, "\x36\x04", "Font", "PC Screen Font (PSF 1)", K_PSF),
    SIG(0, "\x72\xb5\x4a\x86", "Font", "PC Screen Font (PSF 2)", K_PSF),
    SIG(0, "SQLite format 3\x00", "Database", "SQLite database", K_SQLITE),
    SIG(0, "PAR1", "Database", "Parquet data file", K_PARQUET),
    SIG(0, "glTF", "Model", "glTF binary (GLB) 3D model", K_GLB),
    SIG(0, "klam ", "Image", "KlamANSI drawing", K_KAN),
};

typedef struct { const char *ext, *cat, *label; int kind; } FExt;

/* Formats with no signature, known by their extension: text, code, text art and the music-as-text formats Neumetik reads. */
static const FExt FF_EXTS[] = {
    {"txt", "Text", "Plain text", K_TEXT}, {"md", "Text", "Markdown document", K_TEXT}, {"json", "Text", "JSON data", K_JSON},
    {"xml", "Text", "XML data", K_XML}, {"html", "Document", "HTML document", K_HTML}, {"htm", "Document", "HTML document", K_HTML},
    {"xhtml", "Document", "XHTML document", K_HTML}, {"css", "Text", "CSS stylesheet", K_TEXT}, {"js", "Text", "JavaScript source", K_TEXT},
    {"mjs", "Text", "JavaScript module", K_TEXT}, {"ts", "Text", "TypeScript source", K_TEXT}, {"py", "Text", "Python source", K_TEXT},
    {"rs", "Text", "Rust source", K_TEXT}, {"go", "Text", "Go source", K_TEXT}, {"java", "Text", "Java source", K_TEXT},
    {"c", "Text", "C source", K_TEXT}, {"h", "Text", "C header", K_TEXT}, {"cpp", "Text", "C++ source", K_TEXT},
    {"hpp", "Text", "C++ header", K_TEXT}, {"cs", "Text", "C# source", K_TEXT}, {"rb", "Text", "Ruby source", K_TEXT},
    {"php", "Text", "PHP source", K_TEXT}, {"sh", "Text", "Shell script", K_SCRIPT}, {"bash", "Text", "Shell script", K_SCRIPT},
    {"lua", "Text", "Lua source", K_TEXT}, {"yaml", "Text", "YAML data", K_TEXT}, {"yml", "Text", "YAML data", K_TEXT},
    {"toml", "Text", "TOML data", K_TEXT}, {"ini", "Text", "INI settings", K_TEXT}, {"cfg", "Text", "Settings file", K_TEXT},
    {"conf", "Text", "Settings file", K_TEXT}, {"csv", "Text", "CSV table", K_CSV}, {"tsv", "Text", "TSV table", K_CSV},
    {"sql", "Text", "SQL script", K_TEXT}, {"log", "Text", "Log file", K_TEXT}, {"srt", "Text", "SubRip subtitles", K_TEXT},
    {"vtt", "Text", "WebVTT subtitles", K_TEXT}, {"m3u", "Text", "M3U playlist", K_TEXT}, {"m3u8", "Text", "M3U playlist", K_TEXT},
    {"pls", "Text", "PLS playlist", K_TEXT}, {"cue", "Text", "CD cue sheet", K_TEXT}, {"ics", "Text", "iCalendar", K_TEXT},
    {"vcf", "Text", "vCard contacts", K_TEXT}, {"eml", "Document", "Email message", K_EML}, {"mbox", "Document", "Mbox mail archive", K_MBOX},
    {"tex", "Text", "TeX document", K_TEXT}, {"bib", "Text", "BibTeX references", K_TEXT}, {"diff", "Text", "Patch", K_TEXT},
    {"patch", "Text", "Patch", K_TEXT}, {"bat", "Text", "DOS batch file", K_TEXT}, {"ps1", "Text", "PowerShell script", K_TEXT},
    {"pem", "Text", "PEM key or certificate", K_TEXT}, {"desktop", "Text", "Desktop entry", K_TEXT}, {"service", "Text", "systemd unit", K_TEXT},
    {"svg", "Image", "SVG image", K_SVG}, {"bdf", "Font", "BDF bitmap font", K_TEXT},
    {"nfo", "Image", "NFO (release info, often ANSI art)", K_ANSI}, {"diz", "Image", "FILE_ID.DIZ description", K_ANSI},
    {"ans", "Image", "ANSI art", K_ANSI}, {"ice", "Image", "ANSI art (iCE colours)", K_ANSI}, {"asc", "Text", "ASCII text or art", K_TEXT},
    {"bin", "Image", "Binary text art (BIN)", K_BINART}, {"adf", "Image", "ArtWorx art (ADF)", K_ADF}, {"idf", "Image", "iCE Draw art (IDF)", K_IDF},
    {"tnd", "Image", "TundraDraw art", K_TND}, {"kan", "Image", "KlamANSI drawing", K_KAN},
    {"tga", "Image", "Targa image", K_TGA}, {"pcx", "Image", "PCX image", K_PCX}, {"pbm", "Image", "Portable bitmap", K_PNM},
    {"pgm", "Image", "Portable greymap", K_PNM}, {"ppm", "Image", "Portable pixmap", K_PNM}, {"pnm", "Image", "Portable anymap", K_PNM},
    {"obj", "Model", "Wavefront OBJ 3D model", K_OBJ}, {"stl", "Model", "STL 3D model", K_STL}, {"gltf", "Model", "glTF JSON 3D model", K_GLTF},
    {"dbf", "Database", "dBase table", K_DBF}, {"torrent", "Data", "BitTorrent file", K_TORRENT},
    /* music as text (Neumetik, Composer) */
    {"abc", "Music", "ABC notation", K_MUSICTEXT}, {"musicxml", "Music", "MusicXML score", K_MUSICTEXT}, {"krn", "Music", "Humdrum **kern score", K_MUSICTEXT},
    {"cho", "Music", "ChordPro song", K_MUSICTEXT}, {"chordpro", "Music", "ChordPro song", K_MUSICTEXT}, {"alda", "Music", "Alda score", K_MUSICTEXT},
    {"mini", "Music", "Strudel/Tidal mini-notation", K_MUSICTEXT}, {"rtttl", "Music", "RTTTL ringtone", K_MUSICTEXT},
    {"tmf", "Music", "TMF (text MIDI)", K_MUSICTEXT}, {"flmml", "Music", "FlMML", K_MUSICTEXT}, {"sion", "Music", "SiON MML", K_MUSICTEXT},
    {"mml", "Music", "MML (music macro language)", K_MUSICTEXT}, {"ms2mml", "Music", "MapleStory 2 MML", K_MUSICTEXT},
    {"cmp", "Music", "Composer notation", K_MUSICTEXT}, {"ly", "Music", "LilyPond score", K_MUSICTEXT},
};

/* Signature-less binaries named by extension when their bytes do not say otherwise. */
static const FExt FF_EXTS_BIN[] = {
    {"iso", "Disk Image", "ISO 9660 disk image", K_ISO}, {"img", "Disk Image", "Disk image", K_DISK}, {"ima", "Disk Image", "Disk image", K_DISK},
    {"dmg", "Disk Image", "Apple disk image (DMG)", K_DMG}, {"vhd", "Disk Image", "Virtual hard disk (VHD)", K_VHD},
    {"ptk", "Audio", "ProTrekkr module", K_TRACKER}, {"v2m", "Audio", "V2 (farbrausch) module", K_TRACKER},
    {"669", "Audio", "Composer 669 module", K_669}, {"sid", "Audio", "C64 SID tune", K_TRACKER}, {"nsf", "Audio", "NES sound file", K_TRACKER},
};

static int ff_looks_text(const uint8_t *b, size_t n) {
    if (n == 0) return 1;
    if (n >= 2 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF))) return 1; /* UTF-16 with a BOM */
    int ctrl = 0;
    for (size_t i = 0; i < n; i++) {
        if (b[i] == 0) return 0;
        if (b[i] < 9 || (b[i] > 13 && b[i] < 32 && b[i] != 26 && b[i] != 27)) ctrl++;
    }
    return ctrl * 20 < (int)n;
}

/* An ANSI-art file (escape sequences that set colours or move the cursor), whatever its name. */
static int ff_has_ansi(const uint8_t *b, size_t n) {
    int esc = 0;
    for (size_t i = 0; i + 2 < n; i++)
        if (b[i] == 27 && b[i + 1] == '[') { esc++; if (esc >= 3) return 1; }
    return 0;
}

/* The 128-byte SAUCE record at the end (with an optional 0x1A before it); fills rec. */
static int ff_sauce(const FFile *f, uint8_t rec[128]) {
    if (f->size < 128) return 0;
    if (ff_read(f, f->size - 128, rec, 128) != 128) return 0;
    return !memcmp(rec, "SAUCE00", 7);
}

/* MOD: 31 samples and a tag at 1080 that names the channels; 0 if none. */
static int ff_mod_channels(const uint8_t *b, size_t n) {
    if (n < 1084) return 0;
    const uint8_t *t = b + 1080;
    if (!memcmp(t, "M.K.", 4) || !memcmp(t, "M!K!", 4) || !memcmp(t, "FLT4", 4) || !memcmp(t, "4CHN", 4) || !memcmp(t, "M&K!", 4) || !memcmp(t, "N.T.", 4)) return 4;
    if (!memcmp(t, "FLT8", 4) || !memcmp(t, "OKTA", 4) || !memcmp(t, "CD81", 4)) return 8;
    if (isdigit(t[0]) && !memcmp(t + 1, "CHN", 3)) return t[0] - '0';
    if (isdigit(t[0]) && isdigit(t[1]) && (!memcmp(t + 2, "CH", 2) || !memcmp(t + 2, "CN", 2))) return (t[0] - '0') * 10 + t[1] - '0';
    if (!memcmp(t, "TDZ", 3) && isdigit(t[3])) return t[3] - '0';
    return 0;
}

static FType ff_type(int kind, const char *cat, const char *label) { FType t = {cat, label, kind}; return t; }

static int ff_is_mbr(const FFile *f) {
    if (f->hn < 512 || f->head[510] != 0x55 || f->head[511] != 0xAA) return 0;
    int used = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = f->head + 446 + i * 16;
        if (e[0] != 0 && e[0] != 0x80) return 0;
        if (e[4]) used++;
    }
    return used > 0;
}

static FType ff_identify(const FFile *f) {
    const uint8_t *h = f->head;
    const char *ext = f->ext;
    if (f->size == 0) return ff_type(K_EMPTY, "Empty", "Empty file");
    if (FF_AT(f, 0, "RIFF")) {
        if (FF_AT(f, 8, "WAVE")) return ff_type(K_WAV, "Audio", "WAV audio");
        if (FF_AT(f, 8, "AVI ")) return ff_type(K_AVI, "Video", "AVI video");
        if (FF_AT(f, 8, "WEBP")) return ff_type(K_WEBP, "Image", "WebP image");
        if (FF_AT(f, 8, "RMID")) return ff_type(K_RMID, "Audio", "RIFF MIDI (RMID)");
        if (FF_AT(f, 8, "sfbk")) return ff_type(K_UNKNOWN, "Audio", "SoundFont 2 bank");
    }
    if (FF_AT(f, 0, "FORM") && (FF_AT(f, 8, "AIFF") || FF_AT(f, 8, "AIFC"))) return ff_type(K_AIFF, "Audio", "AIFF audio");
    if (FF_AT(f, 4, "ftyp")) {
        if (FF_AT(f, 8, "heic") || FF_AT(f, 8, "heix") || FF_AT(f, 8, "mif1") || FF_AT(f, 8, "msf1")) return ff_type(K_HEIF, "Image", "HEIC/HEIF image");
        if (FF_AT(f, 8, "avif") || FF_AT(f, 8, "avis")) return ff_type(K_HEIF, "Image", "AVIF image");
        if (FF_AT(f, 8, "qt  ")) return ff_type(K_MP4, "Video", "QuickTime video");
        if (FF_AT(f, 8, "M4A ") || FF_AT(f, 8, "M4B ")) return ff_type(K_MP4, "Audio", "MPEG-4 audio (M4A)");
        if (FF_AT(f, 8, "3gp")) return ff_type(K_MP4, "Video", "3GPP video");
        return ff_type(K_MP4, "Video", "MPEG-4 video");
    }
    if (f->hn >= 8 && FF_AT(f, 4, "moov")) return ff_type(K_MP4, "Video", "QuickTime video");
    if (FF_AT(f, 0, "PK\x03\x04") || FF_AT(f, 0, "PK\x05\x06")) {
        static const FExt zips[] = {
            {"docx", "Document", "Word document (docx)", K_OOXML}, {"xlsx", "Document", "Excel spreadsheet (xlsx)", K_OOXML},
            {"pptx", "Document", "PowerPoint presentation (pptx)", K_OOXML}, {"odt", "Document", "OpenDocument text (odt)", K_ODF},
            {"ods", "Document", "OpenDocument spreadsheet (ods)", K_ODF}, {"odp", "Document", "OpenDocument presentation (odp)", K_ODF},
            {"epub", "Document", "EPUB e-book", K_EPUB}, {"love", "Archive", "LÖVE game (.love)", K_LOVE},
            {"jar", "Archive", "Java archive (jar)", K_JAR}, {"apk", "Archive", "Android package (apk)", K_JAR},
            {"msix", "Archive", "Windows app package (msix)", K_MSIX}, {"appx", "Archive", "Windows app package (appx)", K_MSIX},
            {"mxl", "Music", "MusicXML, compressed (.mxl)", K_ZIP}, {"cbz", "Archive", "Comic book (cbz)", K_ZIP},
            {"kmz", "Archive", "Google Earth KMZ", K_ZIP}, {"xpi", "Archive", "Firefox add-on", K_ZIP}, {"3mf", "Model", "3MF 3D model", K_ZIP},
        };
        for (size_t i = 0; i < sizeof zips / sizeof zips[0]; i++)
            if (!strcmp(ext, zips[i].ext)) return ff_type(zips[i].kind, zips[i].cat, zips[i].label);
        if (FF_AT(f, 30, "mimetypeapplication/epub+zip")) return ff_type(K_EPUB, "Document", "EPUB e-book");
        if (FF_AT(f, 30, "mimetypeapplication/vnd.oasis.opendocument")) return ff_type(K_ODF, "Document", "OpenDocument file");
    }
    if (FF_AT(f, 0, "\x7F" "ELF") && FF_AT(f, 8, "AI\x02")) return ff_type(K_APPIMAGE, "Executable", "AppImage (type 2)");
    if (FF_AT(f, 0, "\xCA\xFE\xBA\xBE") && f->hn >= 8) {
        if (rd32be(h + 4) < 40) return ff_type(K_FAT, "Executable", "Mach-O universal program");
        return ff_type(K_CLASS, "Executable", "Java class");
    }
    if (h[0] == 'B' && h[1] == 'M' && f->hn >= 26 && rd32le(h + 14) >= 12 && rd32le(h + 14) <= 124) return ff_type(K_BMP, "Image", "BMP image");
    if (f->hn >= 6 && h[0] == 0 && h[1] == 0 && (h[2] == 1 || h[2] == 2) && h[3] == 0 && rd16le(h + 4) > 0 && rd16le(h + 4) < 256 &&
        (!strcmp(ext, "ico") || !strcmp(ext, "cur") || (f->hn >= 22 && h[6 + 3] == 0)))
        return ff_type(K_ICO, "Image", h[2] == 1 ? "ICO icon" : "CUR cursor");
    int modch = ff_mod_channels(h, f->hn);
    if (modch) return ff_type(K_MOD, "Audio", "ProTracker-style module (MOD)");
    for (size_t i = 0; i < sizeof FF_SIGS / sizeof FF_SIGS[0]; i++) {
        const FSig *s = &FF_SIGS[i];
        if (ff_at(h, f->hn, s->off, s->magic, s->len)) {
            if (s->kind == K_PE && f->hn >= 64) {
                uint32_t pe = rd32le(h + 60);
                if (pe + 4 <= f->hn && !memcmp(h + pe, "PE\0\0", 4)) return ff_type(K_PE, "Executable", "Windows program (PE)");
                if (!strcmp(ext, "exe") || !strcmp(ext, "com")) return ff_type(K_PE, "Executable", "DOS program (MZ)");
                continue;
            }
            if (s->kind == K_TTF && s->magic[0] == 0 && strcmp(ext, "ttf") && strcmp(ext, "otf") && f->hn >= 12 && (rd16be(h + 4) == 0 || rd16be(h + 4) > 64)) continue;
            if (s->kind == K_PSF && s->len == 2 && strcmp(ext, "psf") && strcmp(ext, "psfu")) continue;
            if (s->kind == K_OLDARC && s->len == 2 && strcmp(ext, "arc") && strcmp(ext, "arj") && strcmp(ext, "z")) continue;
            if (s->kind == K_MPEGPS && strcmp(ext, "mpg") && strcmp(ext, "mpeg") && strcmp(ext, "vob")) return ff_type(K_MPEGPS, "Video", "MPEG video");
            if (s->kind == K_KAN && !FF_AT(f, 0, "klam 1")) continue;
            if (s->kind == K_GZIP && (!strcmp(ext, "tgz") || strstr(f->name, ".tar.gz"))) return ff_type(K_GZIP, "Archive", "TAR archive, gzip compressed");
            return ff_type(s->kind, s->cat, s->label);
        }
    }
    if (f->hn >= 189 && h[0] == 0x47 && h[188] == 0x47) return ff_type(K_MPEGTS, "Video", "MPEG transport stream");
    if (f->hn >= 4 && h[0] == 0xFF && (h[1] & 0xE0) == 0xE0 && ((h[1] >> 1) & 3) != 0 && (h[2] >> 4) != 15 && ((h[2] >> 2) & 3) != 3)
        return ff_type(K_MP3, "Audio", ((h[1] >> 1) & 3) == 1 ? "MP3 audio" : "MPEG audio");
    if (FF_AT(f, 0, "ID3")) return ff_type(K_MP3, "Audio", "MP3 audio");
    /* deep and footer signatures, only when the name hints */
    uint8_t buf[512];
    if (!strcmp(ext, "iso") || !strcmp(ext, "img") || !strcmp(ext, "bin") || !strcmp(ext, "cdr")) {
        if (ff_read(f, 32769, buf, 5) == 5 && !memcmp(buf, "CD001", 5)) return ff_type(K_ISO, "Disk Image", "ISO 9660 disk image");
    }
    if ((!strcmp(ext, "dmg") || !strcmp(ext, "vhd")) && f->size >= 512 && ff_read(f, f->size - 512, buf, 512) == 512) {
        if (!memcmp(buf, "koly", 4)) return ff_type(K_DMG, "Disk Image", "Apple disk image (DMG)");
        if (!memcmp(buf, "conectix", 8)) return ff_type(K_VHD, "Disk Image", "Virtual hard disk (VHD)");
    }
    if (ff_is_mbr(f)) return ff_type(K_DISK, "Disk Image", "Disk image (partitioned)");
    if (!strcmp(ext, "tga") || !strcmp(ext, "pcx") || !strcmp(ext, "pbm") || !strcmp(ext, "pgm") || !strcmp(ext, "ppm") || !strcmp(ext, "pnm")) {
        for (size_t i = 0; i < sizeof FF_EXTS / sizeof FF_EXTS[0]; i++)
            if (!strcmp(ext, FF_EXTS[i].ext)) return ff_type(FF_EXTS[i].kind, FF_EXTS[i].cat, FF_EXTS[i].label);
    }
    if (!strcmp(ext, "stl")) return ff_type(K_STL, "Model", FF_AT(f, 0, "solid") ? "STL 3D model (text)" : "STL 3D model (binary)");
    if (!strcmp(ext, "dbf")) return ff_type(K_DBF, "Database", "dBase table");
    for (size_t i = 0; i < sizeof FF_EXTS_BIN / sizeof FF_EXTS_BIN[0]; i++)
        if (!strcmp(ext, FF_EXTS_BIN[i].ext)) return ff_type(FF_EXTS_BIN[i].kind, FF_EXTS_BIN[i].cat, FF_EXTS_BIN[i].label);
    /* text art: BIN, ADF, IDF, TND are raw cells, ANS is text with escapes */
    if (!strcmp(ext, "bin") || !strcmp(ext, "adf") || !strcmp(ext, "idf") || !strcmp(ext, "tnd")) {
        uint8_t rec[128];
        int sauce = ff_sauce(f, rec);
        if (!strcmp(ext, "adf") && f->hn > 0 && h[0] == 1) return ff_type(K_ADF, "Image", "ArtWorx art (ADF)");
        if (!strcmp(ext, "idf") && FF_AT(f, 0, "\x04" "1.4")) return ff_type(K_IDF, "Image", "iCE Draw art (IDF)");
        if (!strcmp(ext, "tnd") && FF_AT(f, 1, "TUNDRA24")) return ff_type(K_TND, "Image", "TundraDraw art");
        if (!strcmp(ext, "bin") && (sauce ? rec[94] == 5 : (f->size % 160) == 0) && f->size >= 160 && f->size < 4 * 1024 * 1024)
            return ff_type(K_BINART, "Image", "Binary text art (BIN)");
    }
    /* text */
    size_t probe = f->hn;
    if (ff_looks_text(h, probe)) {
        for (size_t i = 0; i < sizeof FF_EXTS / sizeof FF_EXTS[0]; i++)
            if (!strcmp(ext, FF_EXTS[i].ext)) {
                if (FF_EXTS[i].kind == K_TEXT && ff_has_ansi(h, probe)) return ff_type(K_ANSI, "Image", "ANSI art");
                return ff_type(FF_EXTS[i].kind, FF_EXTS[i].cat, FF_EXTS[i].label);
            }
        uint8_t rec[128];
        if (ff_has_ansi(h, probe) || (ff_sauce(f, rec) && rec[94] == 1)) return ff_type(K_ANSI, "Image", "ANSI art");
        if (h[0] == '#' && h[1] == '!') return ff_type(K_SCRIPT, "Text", "Script");
        if (!strncasecmp((const char *)h, "<!doctype html", 14) || !strncasecmp((const char *)h, "<html", 5)) return ff_type(K_HTML, "Document", "HTML document");
        if (!strncmp((const char *)h, "<?xml", 5)) {
            if (strstr((const char *)h, "<svg")) return ff_type(K_SVG, "Image", "SVG image");
            if (strstr((const char *)h, "score-partwise") || strstr((const char *)h, "score-timewise")) return ff_type(K_MUSICTEXT, "Music", "MusicXML score");
            return ff_type(K_XML, "Text", "XML data");
        }
        if (!strncmp((const char *)h, "From ", 5)) return ff_type(K_MBOX, "Document", "Mbox mail archive");
        if (h[0] == '{' || h[0] == '[') return ff_type(K_JSON, "Text", "JSON data (probably)");
        if (h[0] == 'd' && f->hn > 12 && (!memcmp(h, "d8:announce", 11) || !memcmp(h, "d4:info", 7))) return ff_type(K_TORRENT, "Data", "BitTorrent file");
        if (!strcmp(ext, "") || !strcmp(ext, "text")) return ff_type(K_TEXT, "Text", "Plain text");
        return ff_type(K_TEXT, "Text", "Text");
    }
    for (size_t i = 0; i < sizeof FF_EXTS / sizeof FF_EXTS[0]; i++)
        if (!strcmp(ext, FF_EXTS[i].ext) && FF_EXTS[i].kind != K_TEXT && FF_EXTS[i].kind != K_BINART && FF_EXTS[i].kind != K_ADF &&
            FF_EXTS[i].kind != K_IDF && FF_EXTS[i].kind != K_TND && FF_EXTS[i].kind != K_KAN)
            return ff_type(FF_EXTS[i].kind, FF_EXTS[i].cat, FF_EXTS[i].label);
    return ff_type(K_UNKNOWN, "Unknown", "Unknown (binary data)");
}

#include "fmt_media.h"
#include "fmt_archives.h"
#include "arc_members.h"
#include "fmt_misc.h"

#endif
