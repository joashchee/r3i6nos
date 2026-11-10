/* fmt_archives.h: archives, packages and disk images for Commander (included by formats.h): what is inside them, listed on the
 * Content page, and their details on the Info page. ZIP (and everything built on it: LÖVE games, Office and OpenDocument files, EPUB,
 * jar/apk, msix, .mxl), TAR (plain or gzip), gzip, ar and Debian packages, RAR 4 and 5, Microsoft Cabinet, cpio, LHA, ISO 9660, RPM,
 * 7-Zip/bzip2/xz/zstd/lzip (details only: their data is not unpacked here), and partitioned disk images (MBR and GPT, with each
 * partition's filesystem and label). */
#ifndef COMMANDER_FMT_ARCHIVES_H
#define COMMANDER_FMT_ARCHIVES_H

#define FF_LIST_UNPACK (48u * 1024u * 1024u) /* the most unpacked to list a .tar.gz */

static void dos_time(unsigned date, unsigned tm, char *out, size_t cap) {
    if (!date) { snprintf(out, cap, "%16s", ""); return; }
    snprintf(out, cap, "%04u-%02u-%02u %02u:%02u", 1980 + (date >> 9), (date >> 5) & 15, date & 31, tm >> 11, (tm >> 5) & 63);
}

static void unix_time_short(long long t, char *out, size_t cap) {
    struct tm tmv;
    time_t tt = (time_t)t;
    if (t <= 0 || !gmtime_r(&tt, &tmv)) { snprintf(out, cap, "%16s", ""); return; }
    strftime(out, cap, "%Y-%m-%d %H:%M", &tmv);
}

/* One listing line: size, date, name (a folder ends in /). */
static void list_line(FText *t, unsigned long long size, const char *when, const char *name, int dir) {
    char sz[16];
    if (dir) snprintf(sz, sizeof sz, "<DIR>");
    else fmt_bytes(size, sz, sizeof sz);
    text_line(t, "%10s  %s  %s%s", sz, when, name, dir && name[0] && name[strlen(name) - 1] != '/' ? "/" : "");
}

/* ---- ZIP ---------------------------------------------------------------------------------------------------------------------- */

typedef struct {
    char name[400];
    uint64_t csize, usize, off;
    unsigned method, flags, date, time;
} ZEnt;

typedef struct {
    ZEnt *e;
    int n, total;
    char comment[232];
} ZList;

static void zip_free(ZList *z) { free(z->e); memset(z, 0, sizeof *z); }

static int zip_open(const FFile *f, ZList *z) {
    memset(z, 0, sizeof *z);
    size_t tn;
    off_t from = f->size > 65557 ? f->size - 65557 : 0;
    uint8_t *t = ff_load(f, from, 65557, &tn);
    if (!t) return -1;
    long eocd = -1;
    for (long i = (long)tn - 22; i >= 0; i--)
        if (!memcmp(t + i, "PK\x05\x06", 4)) { eocd = i; break; }
    if (eocd < 0) { free(t); return -1; }
    const uint8_t *e = t + eocd;
    uint64_t count = rd16le(e + 10), cdoff = rd32le(e + 16);
    unsigned cl = rd16le(e + 20);
    if (cl && (size_t)eocd + 22 + cl <= tn) ff_clean(z->comment, sizeof z->comment, e + 22, cl);
    if ((count == 0xFFFF || cdoff == 0xFFFFFFFFu) && eocd >= 20 && !memcmp(e - 20, "PK\x06\x07", 4)) {
        uint8_t z64[56];
        if (ff_read(f, (off_t)rd64le(e - 20 + 8), z64, 56) == 56 && !memcmp(z64, "PK\x06\x06", 4)) { count = rd64le(z64 + 32); cdoff = rd64le(z64 + 48); }
    }
    free(t);
    z->total = count > 1000000 ? 1000000 : (int)count;
    int keep = z->total < FF_MAX_LIST ? z->total : FF_MAX_LIST;
    z->e = (ZEnt *)calloc((size_t)(keep ? keep : 1), sizeof(ZEnt));
    if (!z->e) return -1;
    off_t p = (off_t)cdoff;
    uint8_t h[46];
    for (int i = 0; i < keep; i++) {
        if (ff_read(f, p, h, 46) != 46 || memcmp(h, "PK\x01\x02", 4)) break;
        ZEnt *ze = &z->e[z->n];
        unsigned nl = rd16le(h + 28), xl = rd16le(h + 30), kl = rd16le(h + 32);
        ze->flags = rd16le(h + 8);
        ze->method = rd16le(h + 10);
        ze->time = rd16le(h + 12);
        ze->date = rd16le(h + 14);
        ze->csize = rd32le(h + 20);
        ze->usize = rd32le(h + 24);
        ze->off = rd32le(h + 42);
        size_t got = ff_read(f, p + 46, ze->name, nl < sizeof ze->name - 1 ? nl : sizeof ze->name - 1);
        ze->name[got] = 0;
        if (xl && (ze->usize == 0xFFFFFFFFu || ze->csize == 0xFFFFFFFFu || ze->off == 0xFFFFFFFFu)) {
            uint8_t x[256];
            size_t xg = ff_read(f, p + 46 + nl, x, xl < sizeof x ? xl : sizeof x);
            for (size_t q = 0; q + 4 <= xg;) {
                unsigned id = rd16le(x + q), sz = rd16le(x + q + 2);
                if (id == 1) {
                    size_t r = q + 4;
                    if (ze->usize == 0xFFFFFFFFu && r + 8 <= xg) { ze->usize = rd64le(x + r); r += 8; }
                    if (ze->csize == 0xFFFFFFFFu && r + 8 <= xg) { ze->csize = rd64le(x + r); r += 8; }
                    if (ze->off == 0xFFFFFFFFu && r + 8 <= xg) ze->off = rd64le(x + r);
                }
                q += 4 + sz;
            }
        }
        z->n++;
        p += 46 + nl + xl + kl;
    }
    return 0;
}

static const ZEnt *zip_find(const ZList *z, const char *name) {
    for (int i = 0; i < z->n; i++) if (!strcmp(z->e[i].name, name)) return &z->e[i];
    for (int i = 0; i < z->n; i++) if (!strcasecmp(z->e[i].name, name)) return &z->e[i];
    return NULL;
}

/* A member's bytes (at most cap, 0-terminated), or NULL (encrypted, another method, damaged). */
static uint8_t *zip_read(const FFile *f, const ZEnt *ze, size_t cap, size_t *outn) {
    *outn = 0;
    if (!ze || (ze->flags & 1) || (ze->method != 0 && ze->method != 8)) return NULL;
    uint8_t lh[30];
    if (ff_read(f, (off_t)ze->off, lh, 30) != 30 || memcmp(lh, "PK\x03\x04", 4)) return NULL;
    off_t data = (off_t)ze->off + 30 + rd16le(lh + 26) + rd16le(lh + 28);
    size_t want = ze->usize < cap ? (size_t)ze->usize : cap;
    if (ze->method == 0) {
        size_t n;
        uint8_t *b = ff_load(f, data, want, &n);
        *outn = n;
        return b;
    }
    size_t cn;
    uint8_t *c = ff_load(f, data, ze->csize, &cn);
    if (!c) return NULL;
    uint8_t *out = (uint8_t *)malloc(want + 1);
    if (!out) { free(c); return NULL; }
    long got = fi_inflate(c, cn, out, want, NULL, NULL);
    free(c);
    if (got < 0) { free(out); return NULL; }
    out[got] = 0;
    *outn = (size_t)got;
    return out;
}

static char *zip_text(const FFile *f, const ZList *z, const char *name, size_t cap) {
    size_t n;
    return (char *)zip_read(f, zip_find(z, name), cap, &n);
}

/* ---- a little XML ------------------------------------------------------------------------------------------------------------- */

/* Decode one entity at s (after '&'); returns the bytes used (0 if not one) and puts the code point in *cp. */
static int xml_entity(const char *s, unsigned *cp) {
    static const struct { const char *n; unsigned c; } ents[] = {{"amp;", '&'}, {"lt;", '<'}, {"gt;", '>'}, {"quot;", '"'}, {"apos;", '\''},
                                                                 {"nbsp;", ' '}, {"mdash;", 0x2014}, {"ndash;", 0x2013}, {"hellip;", 0x2026},
                                                                 {"copy;", 0xA9}, {"rsquo;", 0x2019}, {"lsquo;", 0x2018}, {"rdquo;", 0x201D}, {"ldquo;", 0x201C}};
    if (s[0] == '#') {
        char *end;
        unsigned long v = s[1] == 'x' || s[1] == 'X' ? strtoul(s + 2, &end, 16) : strtoul(s + 1, &end, 10);
        if (*end != ';' || v == 0 || v > 0x10FFFF) return 0;
        *cp = (unsigned)v;
        return (int)(end - s) + 1;
    }
    for (size_t i = 0; i < sizeof ents / sizeof ents[0]; i++) {
        size_t l = strlen(ents[i].n);
        if (!strncmp(s, ents[i].n, l)) { *cp = ents[i].c; return (int)l; }
    }
    return 0;
}

/* The text inside the first <tag ...>...</tag> (tags inside it dropped, entities decoded). */
static int xml_tag_text(const char *xml, const char *tag, char *out, size_t cap) {
    char open[64];
    snprintf(open, sizeof open, "<%s", tag);
    const char *s = xml;
    size_t ol = strlen(open);
    while ((s = strstr(s, open)) != NULL) {
        char c = s[ol];
        if (c == '>' || c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
        s += ol;
    }
    if (!s) return 0;
    s = strchr(s, '>');
    if (!s || s[-1] == '/') return 0;
    s++;
    char close[64];
    snprintf(close, sizeof close, "</%s>", tag);
    const char *e = strstr(s, close);
    if (!e) return 0;
    size_t k = 0;
    int intag = 0;
    for (const char *p = s; p < e && k + 5 < cap; p++) {
        if (*p == '<') { intag = 1; continue; }
        if (*p == '>') { intag = 0; continue; }
        if (intag) continue;
        unsigned cp;
        int used;
        if (*p == '&' && (used = xml_entity(p + 1, &cp)) > 0) { k = utf8_put(out, cap, k, cp); p += used; continue; }
        out[k++] = (*p == '\n' || *p == '\r' || *p == '\t') ? ' ' : *p;
    }
    out[k] = 0;
    ff_clean(out, cap, (const uint8_t *)out, k);
    return out[0] != 0;
}

static int xml_attr(const char *xml, const char *tag, const char *attr, char *out, size_t cap) {
    char open[64], pat[64];
    snprintf(open, sizeof open, "<%s", tag);
    snprintf(pat, sizeof pat, "%s=\"", attr);
    const char *s = xml;
    size_t ol = strlen(open);
    while ((s = strstr(s, open)) != NULL && !(s[ol] == ' ' || s[ol] == '\t' || s[ol] == '\n' || s[ol] == '\r' || s[ol] == '/' || s[ol] == '>')) s += ol;
    if (!s) return 0;
    const char *end = strchr(s, '>');
    const char *a = strstr(s, pat);
    if (!a || (end && a > end)) return 0;
    a += strlen(pat);
    const char *e = strchr(a, '"');
    if (!e) return 0;
    snprintf(out, cap, "%.*s", (int)(e - a), a);
    return 1;
}

static void info_xml_tag(FInfo *in, const char *k, const char *xml, const char *tag) {
    char v[232];
    if (xml && xml_tag_text(xml, tag, v, sizeof v)) info_add(in, k, "%s", v);
}

/* Text out of XML or HTML: tags dropped, block tags become line breaks, whitespace runs one space, script and style skipped. */
static void xml_to_text(const char *s, size_t n, FText *t) {
    static const char *blocks[] = {"p", "w:p", "text:p", "text:h", "a:p", "br", "div", "li", "tr", "h1", "h2", "h3", "h4", "h5", "h6",
                                   "title", "blockquote", "pre", "table", "w:br", "text:line-break", "dt", "dd", "section", "si", NULL};
    char line[4096];
    size_t k = 0;
    int space = 0;
    for (size_t i = 0; i < n && !t->cut;) {
        if (s[i] == '<') {
            size_t j = i + 1;
            int closing = j < n && s[j] == '/';
            if (closing) j++;
            char name[32];
            size_t nl = 0;
            while (j < n && nl + 1 < sizeof name && s[j] != '>' && s[j] != ' ' && s[j] != '/' && s[j] != '\n' && s[j] != '\t') name[nl++] = (char)tolower((unsigned char)s[j++]);
            name[nl] = 0;
            if (!strncmp(s + i, "<!--", 4)) { const char *e = strstr(s + i, "-->"); i = e ? (size_t)(e - s) + 3 : n; continue; }
            const char *e = memchr(s + i, '>', n - i);
            size_t after = e ? (size_t)(e - s) + 1 : n;
            if (!closing && (!strcmp(name, "script") || !strcmp(name, "style"))) {
                char endtag[16];
                snprintf(endtag, sizeof endtag, "</%s", name);
                const char *x = strstr(s + after, endtag);
                if (!x) { char up[16]; snprintf(up, sizeof up, "</%c%s", toupper((unsigned char)name[0]), name + 1); x = strstr(s + after, up); }
                i = x ? (size_t)(x - s) : n;
                continue;
            }
            int block = 0;
            for (int b = 0; blocks[b]; b++) if (!strcmp(name, blocks[b])) block = 1;
            if (!strcmp(name, "w:tab") || !strcmp(name, "text:tab") || !strcmp(name, "td") || !strcmp(name, "th")) { if (k + 1 < sizeof line) line[k++] = ' '; }
            if (block && (closing || !strcmp(name, "br") || !strcmp(name, "w:br") || !strcmp(name, "text:line-break"))) {
                while (k && line[k - 1] == ' ') k--;
                line[k] = 0;
                text_line(t, "%s", line);
                k = 0;
                space = 0;
            }
            i = after;
            continue;
        }
        unsigned char c = (unsigned char)s[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            if (k && !space && k + 1 < sizeof line) line[k++] = ' ';
            space = 1;
            i++;
            continue;
        }
        space = 0;
        unsigned cp;
        int used;
        if (c == '&' && (used = xml_entity(s + i + 1, &cp)) > 0) { k = utf8_put(line, sizeof line, k, cp); i += 1 + (size_t)used; }
        else { if (k + 1 < sizeof line) line[k++] = (char)c; i++; }
        if (k + 8 >= sizeof line) { line[k] = 0; text_line(t, "%s", line); k = 0; }
    }
    if (k) { line[k] = 0; text_line(t, "%s", line); }
}

/* ---- ZIP-based files ---------------------------------------------------------------------------------------------------------- */

static void info_zip(const FFile *f, int kind, FInfo *in) {
    ZList z;
    if (zip_open(f, &z) != 0) { info_add(in, "Contents", "no central directory found (damaged or cut short?)"); return; }
    unsigned long long u = 0, c = 0;
    int dirs = 0, enc = 0, other = 0;
    for (int i = 0; i < z.n; i++) {
        size_t l = strlen(z.e[i].name);
        if (l && z.e[i].name[l - 1] == '/') dirs++;
        u += z.e[i].usize;
        c += z.e[i].csize;
        if (z.e[i].flags & 1) enc++;
        if (z.e[i].method != 0 && z.e[i].method != 8) other++;
    }
    char a[16], b[16];
    fmt_bytes(u, a, sizeof a);
    fmt_bytes(c, b, sizeof b);
    info_add(in, "Contents", "%d files, %d folders%s", z.total - dirs, dirs, z.total > z.n ? " (listing cut)" : "");
    info_add(in, "Unpacked", "%s (packed %s, %.0f%%)", a, b, u ? 100.0 * (double)c / (double)u : 100.0);
    if (enc) info_add(in, "Encrypted", "%d entries", enc);
    if (other) info_add(in, "Note", "%d entries use a method this viewer cannot unpack", other);
    if (z.comment[0]) info_add(in, "Comment", "%s", z.comment);
    char *x = NULL, *y = NULL;
    if (kind == K_OOXML) {
        x = zip_text(f, &z, "docProps/core.xml", 1 << 20);
        info_xml_tag(in, "Title", x, "dc:title");
        info_xml_tag(in, "Author", x, "dc:creator");
        info_xml_tag(in, "Last saved by", x, "cp:lastModifiedBy");
        info_xml_tag(in, "Created", x, "dcterms:created");
        info_xml_tag(in, "Modified", x, "dcterms:modified");
        y = zip_text(f, &z, "docProps/app.xml", 1 << 20);
        info_xml_tag(in, "Application", y, "Application");
        info_xml_tag(in, "Pages", y, "Pages");
        info_xml_tag(in, "Words", y, "Words");
        info_xml_tag(in, "Slides", y, "Slides");
    } else if (kind == K_ODF) {
        x = zip_text(f, &z, "meta.xml", 1 << 20);
        info_xml_tag(in, "Title", x, "dc:title");
        info_xml_tag(in, "Author", x, "meta:initial-creator");
        info_xml_tag(in, "Last saved by", x, "dc:creator");
        info_xml_tag(in, "Created", x, "meta:creation-date");
        info_xml_tag(in, "Application", x, "meta:generator");
        char v[64];
        if (x && xml_attr(x, "meta:document-statistic", "meta:page-count", v, sizeof v)) info_add(in, "Pages", "%s", v);
        if (x && xml_attr(x, "meta:document-statistic", "meta:word-count", v, sizeof v)) info_add(in, "Words", "%s", v);
    } else if (kind == K_EPUB || !strcmp(f->ext, "mxl")) {
        x = zip_text(f, &z, "META-INF/container.xml", 1 << 20);
        char root[400];
        if (x && xml_attr(x, "rootfile", "full-path", root, sizeof root)) {
            y = zip_text(f, &z, root, 4 << 20);
            if (kind == K_EPUB) {
                info_xml_tag(in, "Title", y, "dc:title");
                info_xml_tag(in, "Author", y, "dc:creator");
                info_xml_tag(in, "Language", y, "dc:language");
                info_xml_tag(in, "Publisher", y, "dc:publisher");
                info_xml_tag(in, "Date", y, "dc:date");
            } else {
                info_add(in, "Score", "%s", root);
                info_xml_tag(in, "Title", y, "work-title");
                info_xml_tag(in, "Movement", y, "movement-title");
                info_xml_tag(in, "Composer", y, "creator");
                info_xml_tag(in, "Software", y, "software");
                if (y) { int parts = 0; for (const char *s = y; (s = strstr(s, "<score-part ")) != NULL; s++) parts++; info_add(in, "Parts", "%d", parts); }
            }
        }
    } else if (kind == K_LOVE) {
        x = zip_text(f, &z, "conf.lua", 1 << 20);
        info_add(in, "main.lua", "%s", zip_find(&z, "main.lua") ? "yes" : "missing: LÖVE will not start it");
        if (x) {
            static const char *keys[][2] = {{"title", "Title"}, {"version", "LÖVE version"}, {"identity", "Save folder"}};
            for (int i = 0; i < 3; i++) {
                char pat[32];
                snprintf(pat, sizeof pat, ".%s", keys[i][0]);
                for (const char *s = x; (s = strstr(s, pat)) != NULL; s++) {
                    const char *q = s + strlen(pat);
                    while (*q == ' ' || *q == '\t') q++;
                    if (*q != '=') continue;
                    q++;
                    while (*q == ' ' || *q == '\t') q++;
                    if (*q != '"' && *q != '\'') continue;
                    char quote = *q++;
                    const char *e = strchr(q, quote);
                    if (e && e - q < 200) info_add(in, keys[i][1], "%.*s", (int)(e - q), q);
                    break;
                }
            }
        } else {
            info_add(in, "conf.lua", "none (LÖVE's defaults)");
        }
    } else if (kind == K_JAR) {
        x = zip_text(f, &z, "META-INF/MANIFEST.MF", 1 << 20);
        static const char *keys[] = {"Main-Class", "Created-By", "Implementation-Title", "Implementation-Version", "Bundle-Name", NULL};
        for (int i = 0; x && keys[i]; i++) {
            char pat[64];
            snprintf(pat, sizeof pat, "%s: ", keys[i]);
            const char *s = strstr(x, pat);
            if (s) { s += strlen(pat); size_t l = strcspn(s, "\r\n"); info_add(in, keys[i], "%.*s", (int)(l < 200 ? l : 200), s); }
        }
        if (zip_find(&z, "AndroidManifest.xml")) info_add(in, "Android", "has AndroidManifest.xml (binary XML, not shown)");
    } else if (kind == K_MSIX) {
        x = zip_text(f, &z, "AppxManifest.xml", 1 << 20);
        char v[200];
        if (x && xml_attr(x, "Identity", "Name", v, sizeof v)) info_add(in, "Name", "%s", v);
        if (x && xml_attr(x, "Identity", "Version", v, sizeof v)) info_add(in, "Version", "%s", v);
        if (x && xml_attr(x, "Identity", "Publisher", v, sizeof v)) info_add(in, "Publisher", "%s", v);
        info_xml_tag(in, "Display name", x, "DisplayName");
    }
    free(x);
    free(y);
    zip_free(&z);
}

static int zip_name_cmp(const void *a, const void *b) {
    const char *x = *(const char *const *)a, *y = *(const char *const *)b;
    /* slide2 before slide10 */
    while (*x && *y) {
        if (isdigit((unsigned char)*x) && isdigit((unsigned char)*y)) {
            long u = strtol(x, (char **)&x, 10), v = strtol(y, (char **)&y, 10);
            if (u != v) return u < v ? -1 : 1;
            continue;
        }
        if (*x != *y) return (unsigned char)*x - (unsigned char)*y;
        x++;
        y++;
    }
    return (unsigned char)*x - (unsigned char)*y;
}

/* The text of a document in a ZIP (docx, pptx, xlsx, odt/ods/odp, epub); 0 if this is not one. */
static int zip_document_text(const FFile *f, int kind, const ZList *z, FText *t) {
    const char *names[2048];
    int n = 0;
    if (kind == K_OOXML) {
        if (zip_find(z, "word/document.xml")) names[n++] = "word/document.xml";
        else if (zip_find(z, "xl/sharedStrings.xml")) names[n++] = "xl/sharedStrings.xml";
        else
            for (int i = 0; i < z->n && n < 2048; i++)
                if (!strncmp(z->e[i].name, "ppt/slides/slide", 16) && strstr(z->e[i].name, ".xml") && !strstr(z->e[i].name, "_rels")) names[n++] = z->e[i].name;
    } else if (kind == K_ODF) {
        if (zip_find(z, "content.xml")) names[n++] = "content.xml";
    } else if (kind == K_EPUB) {
        for (int i = 0; i < z->n && n < 2048; i++) {
            const char *dot = strrchr(z->e[i].name, '.');
            if (dot && (!strcasecmp(dot, ".xhtml") || !strcasecmp(dot, ".html") || !strcasecmp(dot, ".htm")) && !strstr(z->e[i].name, "nav.")) names[n++] = z->e[i].name;
        }
    } else {
        return 0;
    }
    if (kind == K_OOXML && n > 1) qsort(names, (size_t)n, sizeof names[0], zip_name_cmp);
    for (int i = 0; i < n && !t->cut; i++) {
        size_t len;
        uint8_t *x = zip_read(f, zip_find(z, names[i]), FF_MAX_UNPACK, &len);
        if (!x) continue;
        if (kind == K_OOXML && !strncmp(names[i], "ppt/", 4)) text_line(t, "-- Slide %d --", i + 1);
        xml_to_text((const char *)x, len, t);
        if (n > 1) text_line(t, "%s", "");
        free(x);
    }
    return 1;
}

static void content_zip(const FFile *f, int kind, FText *t) {
    ZList z;
    if (zip_open(f, &z) != 0) { text_line(t, "No list of contents: the archive is damaged or cut short."); return; }
    if (zip_document_text(f, kind, &z, t) && t->n) { zip_free(&z); return; }
    for (int i = 0; i < z.n; i++) {
        char when[24];
        dos_time(z.e[i].date, z.e[i].time, when, sizeof when);
        size_t l = strlen(z.e[i].name);
        list_line(t, z.e[i].usize, when, z.e[i].name, l && z.e[i].name[l - 1] == '/');
    }
    if (z.total > z.n) text_line(t, "... and %d more", z.total - z.n);
    zip_free(&z);
}

/* ---- TAR, gzip ---------------------------------------------------------------------------------------------------------------- */

static unsigned long long tar_num(const uint8_t *s, size_t len) {
    if (s[0] & 0x80) { /* base-256 */
        unsigned long long v = s[0] & 0x7F;
        for (size_t i = 1; i < len; i++) v = v << 8 | s[i];
        return v;
    }
    unsigned long long v = 0;
    size_t i = 0;
    while (i < len && (s[i] == ' ' || s[i] == 0)) i++;
    for (; i < len && s[i] >= '0' && s[i] <= '7'; i++) v = v * 8 + (unsigned)(s[i] - '0');
    return v;
}

/* A TAR in memory: each entry to t (if given); `want` names a member whose bytes are copied out (a .deb's control). */
static int tar_walk(const uint8_t *b, size_t n, FText *t, const char *want, char **found, size_t *foundn, unsigned long long *total) {
    size_t p = 0;
    int count = 0;
    char longname[1024] = "";
    while (p + 512 <= n) {
        const uint8_t *h = b + p;
        if (!h[0]) break;
        unsigned long long size = tar_num(h + 124, 12);
        char type = (char)h[156];
        char name[1100];
        if (longname[0]) { snprintf(name, sizeof name, "%s", longname); longname[0] = 0; }
        else if (!memcmp(h + 257, "ustar", 5) && h[345]) snprintf(name, sizeof name, "%.155s/%.100s", (const char *)h + 345, (const char *)h + 0);
        else snprintf(name, sizeof name, "%.100s", (const char *)h);
        size_t data = p + 512;
        if (type == 'L' || type == 'x') {
            size_t l = size < sizeof longname - 1 ? (size_t)size : sizeof longname - 1;
            if (data + l <= n) {
                if (type == 'L') { memcpy(longname, b + data, l); longname[l] = 0; }
                else { /* pax: "len path=value\n" records */
                    for (size_t q = data; q < data + l;) {
                        char *sp = memchr(b + q, ' ', data + l - q);
                        long rl = strtol((const char *)b + q, NULL, 10);
                        if (!sp || rl <= 0) break;
                        if (!strncmp(sp + 1, "path=", 5)) {
                            size_t vl = (size_t)rl - (size_t)(sp + 6 - (const char *)(b + q)) - 1;
                            if (vl < sizeof longname) { memcpy(longname, sp + 6, vl); longname[vl] = 0; }
                        }
                        q += (size_t)rl;
                    }
                }
            }
        } else if (type != 'g') {
            count++;
            if (total && (type == '0' || type == 0)) *total += size;
            if (t && count <= FF_MAX_LIST) {
                char when[24];
                unix_time_short((long long)tar_num(h + 136, 12), when, sizeof when);
                if (type == '2') { char ln[200]; snprintf(ln, sizeof ln, "%.100s", (const char *)h + 157); text_line(t, "%10s  %s  %s -> %s", "link", when, name, ln); }
                else list_line(t, size, when, name, type == '5');
            }
            if (want && found && !*found && (!strcmp(name, want) || (!strncmp(name, "./", 2) && !strcmp(name + 2, want)))) {
                size_t l = data + size <= n ? (size_t)size : (data < n ? n - data : 0);
                *found = (char *)malloc(l + 1);
                if (*found) { memcpy(*found, b + data, l); (*found)[l] = 0; *foundn = l; }
            }
        }
        p = data + (size_t)((size + 511) / 512 * 512);
    }
    if (t && count > FF_MAX_LIST) text_line(t, "... and %d more", count - FF_MAX_LIST);
    return count;
}

/* A whole gzip file unpacked (at most cap). */
static uint8_t *gunzip_file(const FFile *f, size_t cap, size_t *outn, int *cut) {
    *outn = 0;
    size_t n;
    uint8_t *b = ff_load(f, 0, FF_MAX_LOAD, &n);
    if (!b) return NULL;
    uint32_t isize = f->size >= 4 && n == (size_t)f->size ? rd32le(b + n - 4) : 0;
    size_t want = isize && isize < cap ? isize : cap;
    uint8_t *out = (uint8_t *)malloc(want + 1);
    if (!out) { free(b); return NULL; }
    long got = fi_gunzip(b, n, out, want, cut);
    free(b);
    if (got < 0) { free(out); return NULL; }
    out[got] = 0;
    *outn = (size_t)got;
    return out;
}

static void info_gzip(const FFile *f, FInfo *in) {
    char name[256];
    fi_gzip_start(f->head, f->hn, name, sizeof name);
    if (name[0]) info_add(in, "Original name", "%s", name);
    uint32_t mt = f->hn >= 8 ? rd32le(f->head + 4) : 0;
    if (mt) { char w[40]; fmt_time((time_t)mt, w, sizeof w); info_add(in, "Packed", "%s", w); }
    static const char *os[] = {"FAT", "Amiga", "VMS", "Unix", "VM/CMS", "Atari", "HPFS", "Macintosh", "Z-System", "CP/M", "TOPS-20", "NTFS", "QDOS", "Acorn"};
    if (f->hn > 9) info_add(in, "Packed on", "%s", f->head[9] < 14 ? os[f->head[9]] : "unknown");
    uint8_t t[4];
    if (f->size >= 18 && ff_read(f, f->size - 4, t, 4) == 4) {
        char a[16];
        fmt_bytes(rd32le(t), a, sizeof a);
        info_add(in, "Unpacked", "%s%s", a, f->size > 0xFFFFFFFFLL / 4 ? " (modulo 4 GiB)" : "");
    }
    size_t n;
    int cut = 0;
    uint8_t *u = gunzip_file(f, FF_LIST_UNPACK, &n, &cut);
    if (u && n >= 512 && !memcmp(u + 257, "ustar", 5)) {
        unsigned long long total = 0;
        int c = tar_walk(u, n, NULL, NULL, NULL, NULL, &total);
        info_add(in, "Contents", "a TAR archive, %d entries%s", c, cut ? " (in the part unpacked)" : "");
    } else if (u) {
        info_add(in, "Contents", "%s", ff_looks_text(u, n < 4096 ? n : 4096) ? "text" : "binary data");
    } else {
        info_add(in, "Contents", "could not be unpacked (damaged?)");
    }
    free(u);
}

/* ---- ar and Debian packages --------------------------------------------------------------------------------------------------- */

typedef struct { char name[256]; off_t off; uint64_t size; long long mtime; } ArEnt;

static int ar_list(const FFile *f, ArEnt *out, int max) {
    off_t p = 8;
    int n = 0;
    char *gnu = NULL;
    size_t gnul = 0;
    while (p + 60 <= f->size && n < max) {
        uint8_t h[60];
        if (ff_read(f, p, h, 60) != 60 || h[58] != '`') break;
        char num[16];
        memcpy(num, h + 48, 10); num[10] = 0;
        uint64_t size = strtoull(num, NULL, 10);
        memcpy(num, h + 16, 12); num[12] = 0;
        long long mt = strtoll(num, NULL, 10);
        char name[256];
        ff_clean(name, sizeof name, h, 16);
        off_t data = p + 60;
        if (!strcmp(name, "//")) {
            free(gnu);
            gnu = (char *)ff_load(f, data, size, &gnul);
        } else if (strcmp(name, "/") && strcmp(name, "/SYM64/")) {
            if (name[0] == '/' && isdigit((unsigned char)name[1]) && gnu) {
                size_t o = strtoul(name + 1, NULL, 10);
                if (o < gnul) { size_t l = strcspn(gnu + o, "/\n"); snprintf(name, sizeof name, "%.*s", (int)l, gnu + o); }
            } else if (!strncmp(name, "#1/", 3)) {
                size_t l = strtoul(name + 3, NULL, 10);
                char ln[256];
                size_t got = ff_read(f, data, ln, l < sizeof ln - 1 ? l : sizeof ln - 1);
                ln[got] = 0;
                snprintf(name, sizeof name, "%s", ln);
                data += (off_t)l;
                size -= l < size ? l : size;
            } else {
                size_t l = strlen(name);
                if (l && name[l - 1] == '/') name[l - 1] = 0;
            }
            snprintf(out[n].name, sizeof out[n].name, "%s", name);
            out[n].off = data;
            out[n].size = size;
            out[n].mtime = mt;
            n++;
        }
        off_t next = data + (off_t)size; /* a BSD long name was taken off both */
        p = next + (next & 1);
    }
    free(gnu);
    return n;
}

/* The deb's member named prefix* unpacked, if it is gzip (or not compressed). Sets *how to its compression. */
static uint8_t *deb_member(const FFile *f, const ArEnt *ents, int n, const char *prefix, size_t *outn, const char **how) {
    *outn = 0;
    *how = "";
    for (int i = 0; i < n; i++) {
        if (strncmp(ents[i].name, prefix, strlen(prefix))) continue;
        const char *ext = ents[i].name + strlen(prefix);
        *how = !strcmp(ext, ".gz") ? "gzip" : !strcmp(ext, ".xz") ? "xz" : !strcmp(ext, ".zst") ? "zstd" : !strcmp(ext, ".bz2") ? "bzip2" : !*ext ? "none" : ext;
        size_t cn;
        uint8_t *c = ff_load(f, ents[i].off, ents[i].size, &cn);
        if (!c) return NULL;
        if (!*ext) { *outn = cn; return c; }
        if (strcmp(ext, ".gz")) { free(c); return NULL; }
        uint8_t *out = (uint8_t *)malloc(FF_LIST_UNPACK + 1);
        if (!out) { free(c); return NULL; }
        long got = fi_gunzip(c, cn, out, FF_LIST_UNPACK, NULL);
        free(c);
        if (got < 0) { free(out); return NULL; }
        *outn = (size_t)got;
        return out;
    }
    return NULL;
}

static void deb_control(const FFile *f, FInfo *in, FText *t) {
    ArEnt ents[16];
    int n = ar_list(f, ents, 16);
    size_t cn;
    const char *how;
    uint8_t *ctl = deb_member(f, ents, n, "control.tar", &cn, &how);
    if (in) info_add(in, "Control part", "%s compressed", how);
    char *control = NULL;
    size_t ctln = 0;
    if (ctl) tar_walk(ctl, cn, NULL, "control", &control, &ctln, NULL);
    free(ctl);
    if (control) {
        if (in) {
            static const char *keys[] = {"Package", "Version", "Architecture", "Maintainer", "Description", "Depends", "Installed-Size", "Section", NULL};
            for (int k = 0; keys[k]; k++) {
                char pat[40];
                snprintf(pat, sizeof pat, "\n%s:", keys[k]);
                const char *s = !strncmp(control, pat + 1, strlen(pat + 1)) ? control - 1 : strstr(control, pat);
                if (!s) continue;
                s += strlen(pat);
                while (*s == ' ') s++;
                size_t l = strcspn(s, "\n");
                info_add(in, keys[k], "%.*s%s", (int)(l < 200 ? l : 200), s, !strcmp(keys[k], "Installed-Size") ? " KiB" : "");
            }
        }
        if (t) { text_line(t, "-- control --"); text_add(t, control, ctln); text_line(t, "%s", ""); }
        free(control);
    } else if (in) {
        info_add(in, "Note", "the control part is %s compressed; only gzip is unpacked here", how);
    }
    if (t) {
        size_t dn;
        uint8_t *data = deb_member(f, ents, n, "data.tar", &dn, &how);
        text_line(t, "-- files (data.tar, %s) --", how);
        if (data) tar_walk(data, dn, t, NULL, NULL, NULL, NULL);
        else text_line(t, "not listed: only gzip is unpacked here");
        free(data);
    }
}

static void content_ar(const FFile *f, FText *t) {
    ArEnt *ents = (ArEnt *)malloc(sizeof(ArEnt) * FF_MAX_LIST);
    if (!ents) return;
    int n = ar_list(f, ents, FF_MAX_LIST);
    for (int i = 0; i < n; i++) {
        char when[24];
        unix_time_short(ents[i].mtime, when, sizeof when);
        list_line(t, ents[i].size, when, ents[i].name, 0);
    }
    free(ents);
}

/* ---- RAR ---------------------------------------------------------------------------------------------------------------------- */

static uint64_t rar_vint(const uint8_t *b, size_t n, size_t *p) {
    uint64_t v = 0;
    for (int s = 0; *p < n && s < 64; s += 7) {
        uint8_t c = b[(*p)++];
        v |= (uint64_t)(c & 127) << s;
        if (!(c & 128)) break;
    }
    return v;
}

/* RAR 4 and 5 file headers; returns the entries, or -1 when the headers are encrypted. */
static int rar_list(const FFile *f, FText *t, unsigned long long *total, int *version) {
    int count = 0;
    if (FF_AT(f, 0, "Rar!\x1a\x07\x01\x00")) {
        *version = 5;
        off_t p = 8;
        for (int guard = 0; p + 7 < f->size && guard < 100000; guard++) {
            uint8_t h[1024];
            size_t got = ff_read(f, p, h, sizeof h), q = 4;
            if (got < 7) break;
            uint64_t hsize = rar_vint(h, got, &q);
            size_t hstart = q;
            uint64_t type = rar_vint(h, got, &q), flags = rar_vint(h, got, &q), extra = 0, data = 0;
            if (flags & 1) extra = rar_vint(h, got, &q);
            if (flags & 2) data = rar_vint(h, got, &q);
            (void)extra;
            if (type == 4) return -1;
            if (type == 5) break;
            if (type == 2 && hstart + hsize <= got) {
                uint64_t ff = rar_vint(h, got, &q), usize = rar_vint(h, got, &q);
                rar_vint(h, got, &q); /* attributes */
                uint32_t mt = 0;
                if (ff & 2) { if (q + 4 <= got) mt = rd32le(h + q); q += 4; }
                if (ff & 4) q += 4;
                rar_vint(h, got, &q);
                rar_vint(h, got, &q);
                uint64_t nl = rar_vint(h, got, &q);
                char name[400];
                ff_clean(name, sizeof name, h + (q < got ? q : got), q + nl <= got ? (size_t)nl : 0);
                count++;
                *total += usize;
                if (t && count <= FF_MAX_LIST) { char when[24]; unix_time_short(mt, when, sizeof when); list_line(t, usize, when, name, (ff & 1) != 0); }
            }
            p += (off_t)(hstart + hsize + data);
        }
    } else {
        *version = 4;
        off_t p = 7;
        for (int guard = 0; p + 7 < f->size && guard < 100000; guard++) {
            uint8_t h[1024];
            size_t got = ff_read(f, p, h, sizeof h);
            if (got < 7) break;
            int type = h[2];
            unsigned flags = rd16le(h + 3), hs = rd16le(h + 5);
            uint64_t add = (flags & 0x8000) && got >= 11 ? rd32le(h + 7) : 0;
            if (hs < 7) break;
            if (type == 0x73 && (flags & 0x80)) return -1;
            if (type == 0x74 && got >= 32) {
                uint64_t pack = rd32le(h + 7), usize = rd32le(h + 11);
                unsigned nl = rd16le(h + 26);
                size_t no = 32;
                if (flags & 0x100) { if (got >= 40) { pack |= (uint64_t)rd32le(h + 32) << 32; usize |= (uint64_t)rd32le(h + 36) << 32; } no = 40; }
                char name[400];
                ff_clean(name, sizeof name, h + (no < got ? no : got), no + nl <= got ? nl : 0);
                for (char *c = name; *c; c++) if (*c == '\\') *c = '/';
                count++;
                *total += usize;
                char when[24];
                dos_time(rd16le(h + 22), rd16le(h + 20), when, sizeof when);
                if (t && count <= FF_MAX_LIST) list_line(t, usize, when, name, (flags & 0xE0) == 0xE0);
                add = pack;
            }
            if (type == 0x7B) break;
            p += hs + (off_t)add;
        }
    }
    if (t && count > FF_MAX_LIST) text_line(t, "... and %d more", count - FF_MAX_LIST);
    return count;
}

/* ---- Cabinet, cpio, LHA ------------------------------------------------------------------------------------------------------- */

static int cab_list(const FFile *f, FText *t, unsigned long long *total) {
    const uint8_t *h = f->head;
    if (f->hn < 36) return 0;
    off_t p = rd32le(h + 16);
    unsigned files = rd16le(h + 28);
    int n = 0;
    for (unsigned i = 0; i < files && i < 100000; i++) {
        uint8_t e[16 + 256];
        size_t got = ff_read(f, p, e, sizeof e);
        if (got < 17) break;
        uint32_t size = rd32le(e);
        size_t nl = strnlen((const char *)e + 16, got - 16);
        char name[260];
        ff_clean(name, sizeof name, e + 16, nl);
        for (char *c = name; *c; c++) if (*c == '\\') *c = '/';
        char when[24];
        dos_time(rd16le(e + 10), rd16le(e + 12), when, sizeof when);
        if (t && n < FF_MAX_LIST) list_line(t, size, when, name, 0);
        *total += size;
        n++;
        p += 16 + (off_t)nl + 1;
    }
    return n;
}

static int cpio_list(const FFile *f, FText *t, unsigned long long *total) {
    off_t p = 0;
    int n = 0;
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
        if (t && n < FF_MAX_LIST) { char when[24]; unix_time_short((long long)mt, when, sizeof when); list_line(t, size, when, name, (mode & 0170000) == 0040000); }
        *total += size;
        n++;
        p = data + (off_t)size;
        if (newc) p = (p + 3) & ~(off_t)3;
    }
    return n;
}

static int lzh_list(const FFile *f, FText *t, unsigned long long *total) {
    off_t p = 0;
    int n = 0;
    for (int guard = 0; p + 22 <= f->size && guard < 100000; guard++) {
        uint8_t h[512];
        size_t got = ff_read(f, p, h, sizeof h);
        if (got < 22 || h[0] == 0) break;
        if (h[2] != '-' || h[3] != 'l' || h[6] != '-') break;
        uint32_t packed = rd32le(h + 7), orig = rd32le(h + 11);
        int level = h[20];
        char name[400] = "";
        off_t next;
        if (level == 0 || level == 1) {
            int nl = h[21];
            ff_clean(name, sizeof name, h + 22, (size_t)nl < got - 22 ? (size_t)nl : got - 22);
            next = p + 2 + h[0] + (off_t)packed;
        } else if (level == 2) {
            unsigned hsz = rd16le(h);
            size_t q = 24;
            while (q + 2 <= got) {
                unsigned es = rd16le(h + q);
                if (es < 3 || q + es > got) break;
                if (h[q + 2] == 1) ff_clean(name, sizeof name, h + q + 3, es - 3);
                q += es;
            }
            next = p + hsz + (off_t)packed;
        } else break;
        for (char *c = name; *c; c++) if (*c == '\\' || (unsigned char)*c == 0xFF) *c = '/';
        if (t && n < FF_MAX_LIST) list_line(t, orig, "", name, 0);
        *total += orig;
        n++;
        if (next <= p) break;
        p = next;
    }
    return n;
}

/* ---- ISO 9660 ------------------------------------------------------------------------------------------------------------------ */

static void iso_info(const FFile *f, FInfo *in, FText *t) {
    uint8_t pvd[2048];
    if (ff_read(f, 32768, pvd, 2048) != 2048 || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5)) return;
    if (in) {
        info_field(in, "Volume", pvd + 40, 32);
        info_field(in, "System", pvd + 8, 32);
        info_field(in, "Publisher", pvd + 318, 128);
        info_field(in, "Made with", pvd + 446, 128);
        char d[24];
        ff_clean(d, sizeof d, pvd + 813, 12);
        if (strlen(d) >= 12) info_add(in, "Created", "%.4s-%.2s-%.2s %.2s:%.2s", d, d + 4, d + 6, d + 8, d + 10);
        char sz[16];
        fmt_bytes((unsigned long long)rd32le(pvd + 80) * rd16le(pvd + 128), sz, sizeof sz);
        info_add(in, "Volume size", "%s", sz);
        for (int s = 17; s < 32; s++) { /* other descriptors: El Torito, Joliet */
            uint8_t vd[64];
            if (ff_read(f, (off_t)s * 2048, vd, 64) != 64 || memcmp(vd + 1, "CD001", 5) || vd[0] == 255) break;
            if (vd[0] == 0 && !memcmp(vd + 7, "EL TORITO", 9)) info_add(in, "Bootable", "yes (El Torito)");
            if (vd[0] == 2) info_add(in, "Long names", "Joliet");
        }
    }
    if (!t) return;
    const uint8_t *root = pvd + 156;
    uint32_t ext = rd32le(root + 2), len = rd32le(root + 10);
    if (len > 4 * 1024 * 1024) len = 4 * 1024 * 1024;
    size_t n;
    uint8_t *d = ff_load(f, (off_t)ext * 2048, len, &n);
    if (!d) return;
    text_line(t, "-- the top folder --");
    for (size_t p = 0; p + 33 < n;) {
        int rl = d[p];
        if (rl == 0) { p = (p / 2048 + 1) * 2048; continue; }
        if (p + (size_t)rl > n) break;
        int nl = d[p + 32];
        if (!(nl == 1 && (d[p + 33] == 0 || d[p + 33] == 1)) && 33 + nl <= rl) {
            char name[256];
            ff_clean(name, sizeof name, d + p + 33, (size_t)nl);
            char *semi = strchr(name, ';');
            if (semi) *semi = 0;
            const uint8_t *dt = d + p + 18;
            char when[24];
            snprintf(when, sizeof when, "%04d-%02d-%02d %02d:%02d", 1900 + dt[0], dt[1], dt[2], dt[3], dt[4]);
            list_line(t, rd32le(d + p + 10), when, name, (d[p + 25] & 2) != 0);
        }
        p += (size_t)rl;
    }
    free(d);
}

/* ---- disk images: partitions and filesystems ---------------------------------------------------------------------------------- */

static const char *mbr_type(int t) {
    switch (t) {
    case 0x01: return "FAT12";
    case 0x04: case 0x06: case 0x0E: return "FAT16";
    case 0x07: return "NTFS or exFAT";
    case 0x0B: case 0x0C: return "FAT32";
    case 0x0F: case 0x05: return "extended";
    case 0x82: return "Linux swap";
    case 0x83: return "Linux";
    case 0x8E: return "Linux LVM";
    case 0xA5: return "FreeBSD";
    case 0xAF: return "Apple HFS";
    case 0xEE: return "GPT protective";
    case 0xEF: return "EFI system";
    default: return "other";
    }
}

/* What filesystem starts at off, and its label. */
static void fs_probe(const FFile *f, off_t off, char *out, size_t cap) {
    uint8_t b[2048];
    out[0] = 0;
    size_t got = ff_read(f, off, b, 2048);
    if (got < 512) return;
    if (got < 2048) memset(b + got, 0, 2048 - got);
    char label[64] = "";
    if (!memcmp(b + 3, "EXFAT   ", 8)) snprintf(out, cap, "exFAT");
    else if (!memcmp(b + 3, "NTFS    ", 8)) snprintf(out, cap, "NTFS");
    else if (!memcmp(b + 82, "FAT32   ", 8)) { ff_clean(label, sizeof label, b + 71, 11); snprintf(out, cap, "FAT32%s%s", label[0] && strcmp(label, "NO NAME") ? ", label " : "", strcmp(label, "NO NAME") ? label : ""); }
    else if (!memcmp(b + 54, "FAT1", 4)) { ff_clean(label, sizeof label, b + 43, 11); snprintf(out, cap, "%.5s%s%s", (const char *)b + 54, label[0] && strcmp(label, "NO NAME") ? ", label " : "", strcmp(label, "NO NAME") ? label : ""); }
    else if (rd16le(b + 1024 + 56) == 0xEF53) {
        uint32_t compat = rd32le(b + 1024 + 92), incompat = rd32le(b + 1024 + 96);
        ff_clean(label, sizeof label, b + 1024 + 120, 16);
        snprintf(out, cap, "%s%s%s", (incompat & 0x40) ? "ext4" : (compat & 4) ? "ext3" : "ext2", label[0] ? ", label " : "", label);
    } else if (!memcmp(b, "hsqs", 4)) snprintf(out, cap, "SquashFS");
    else if (!memcmp(b + 1024, "H+", 2) || !memcmp(b + 1024, "HX", 2)) snprintf(out, cap, "HFS+");
    else if (!memcmp(b + 32, "NXSB", 4)) snprintf(out, cap, "APFS");
    else if (!memcmp(b, "ANDROID!", 8)) snprintf(out, cap, "Android boot image");
    else if (b[510] == 0x55 && b[511] == 0xAA) snprintf(out, cap, "boot sector");
}

static void disk_info(const FFile *f, FInfo *in, FText *t) {
    char fs[96];
    uint8_t gpt[512];
    if (ff_read(f, 512, gpt, 512) == 512 && !memcmp(gpt, "EFI PART", 8)) {
        uint64_t lba = rd64le(gpt + 72);
        uint32_t count = rd32le(gpt + 80), esz = rd32le(gpt + 84);
        if (in) info_add(in, "Partitions", "GPT, %u slots", count);
        if (esz < 128 || esz > 4096 || count > 1024) return;
        int used = 0;
        for (uint32_t i = 0; i < count; i++) {
            uint8_t e[128];
            if (ff_read(f, (off_t)lba * 512 + (off_t)i * esz, e, 128) != 128) break;
            int empty = 1;
            for (int k = 0; k < 16; k++) if (e[k]) empty = 0;
            if (empty) continue;
            used++;
            uint64_t first = rd64le(e + 32), last = rd64le(e + 40);
            char name[80], sz[16];
            utf16_to_utf8(name, sizeof name, e + 56, 72, 0);
            fmt_bytes((last - first + 1) * 512, sz, sizeof sz);
            fs_probe(f, (off_t)first * 512, fs, sizeof fs);
            char k[24];
            snprintf(k, sizeof k, "Partition %d", used);
            if (in && used <= 24) info_add(in, k, "%s, %s%s%s", name[0] ? name : "(no name)", sz, fs[0] ? ", " : "", fs);
            if (t) text_line(t, "%2d  %-20s %10s  from sector %llu  %s", used, name, sz, (unsigned long long)first, fs);
        }
        return;
    }
    if (f->hn >= 512 && f->head[510] == 0x55 && f->head[511] == 0xAA && ff_is_mbr(f)) {
        if (in) {
            uint32_t sig = rd32le(f->head + 440);
            info_add(in, "Partitions", "MBR (DOS), disk ID %08X", sig);
        }
        for (int i = 0; i < 4; i++) {
            const uint8_t *e = f->head + 446 + i * 16;
            if (!e[4]) continue;
            uint32_t start = rd32le(e + 8), sectors = rd32le(e + 12);
            char sz[16];
            fmt_bytes((unsigned long long)sectors * 512, sz, sizeof sz);
            fs_probe(f, (off_t)start * 512, fs, sizeof fs);
            char k[24];
            snprintf(k, sizeof k, "Partition %d", i + 1);
            if (in) info_add(in, k, "%s (0x%02X), %s%s%s%s", mbr_type(e[4]), e[4], sz, e[0] == 0x80 ? ", boot" : "", fs[0] ? ", " : "", fs);
            if (t) text_line(t, "%d  %-16s %10s  from sector %u  %s%s", i + 1, mbr_type(e[4]), sz, start, fs, e[0] == 0x80 ? " (boot)" : "");
            if ((off_t)start * 512 + (off_t)sectors * 512 > f->size && in) info_add(in, "Note", "partition %d runs past the end of the image (cut short?)", i + 1);
        }
        return;
    }
    fs_probe(f, 0, fs, sizeof fs);
    if (in) info_add(in, "Filesystem", "%s", fs[0] ? fs : "not recognised (no partition table)");
}

/* ---- the archive dispatcher ---------------------------------------------------------------------------------------------------- */

static int info_archive(const FFile *f, int kind, FInfo *in) {
    unsigned long long total = 0;
    char a[16];
    int n, ver = 0;
    switch (kind) {
    case K_ZIP: case K_LOVE: case K_JAR: case K_OOXML: case K_ODF: case K_EPUB: case K_MSIX: info_zip(f, kind, in); return 1;
    case K_GZIP: info_gzip(f, in); return 1;
    case K_TAR: {
        size_t len;
        uint8_t *b = ff_load(f, 0, FF_MAX_LOAD, &len);
        if (b) { n = tar_walk(b, len, NULL, NULL, NULL, NULL, &total); fmt_bytes(total, a, sizeof a); info_add(in, "Contents", "%d entries, %s of files%s", n, a, (off_t)len < f->size ? " (in the first 32 MiB)" : ""); free(b); }
        return 1;
    }
    case K_DEB: deb_control(f, in, NULL); return 1;
    case K_AR: { ArEnt ents[64]; n = ar_list(f, ents, 64); info_add(in, "Members", "%d%s", n, n == 64 ? " or more" : ""); return 1; }
    case K_RAR:
        n = rar_list(f, NULL, &total, &ver);
        info_add(in, "Version", "RAR %d", ver);
        if (n < 0) info_add(in, "Contents", "names are encrypted");
        else { fmt_bytes(total, a, sizeof a); info_add(in, "Contents", "%d entries, %s unpacked", n, a); }
        return 1;
    case K_CAB: n = cab_list(f, NULL, &total); fmt_bytes(total, a, sizeof a); info_add(in, "Contents", "%d files, %s unpacked", n, a); return 1;
    case K_CPIO: n = cpio_list(f, NULL, &total); fmt_bytes(total, a, sizeof a); info_add(in, "Contents", "%d entries, %s", n, a); return 1;
    case K_LZH: n = lzh_list(f, NULL, &total); fmt_bytes(total, a, sizeof a); info_add(in, "Contents", "%d files, %s unpacked", n, a); return 1;
    case K_7Z: if (f->hn > 8) info_add(in, "Version", "%d.%d", f->head[6], f->head[7]); info_add(in, "Contents", "not listed: 7-Zip's list is itself packed"); return 1;
    case K_BZIP2: if (f->hn > 4) info_add(in, "Block size", "%d00 KB", f->head[3] - '0'); return 1;
    case K_XZ: if (f->hn > 8) { static const char *chk[] = {"none", "CRC32", "", "", "CRC64", "", "", "", "", "", "SHA-256"}; int c = f->head[7] & 15; info_add(in, "Check", "%s", c <= 10 && chk[c][0] ? chk[c] : "other"); } return 1;
    case K_ZSTD:
        if (f->hn > 6) {
            int fhd = f->head[4], fcs = fhd >> 6, single = (fhd >> 5) & 1, did = fhd & 3;
            size_t p = 5 + (single ? 0 : 1) + (did == 3 ? 4 : did);
            int len = fcs == 0 ? (single ? 1 : 0) : fcs == 1 ? 2 : fcs == 2 ? 4 : 8;
            if (len && p + (size_t)len <= f->hn) {
                unsigned long long v = len == 1 ? f->head[p] : len == 2 ? rd16le(f->head + p) + 256u : len == 4 ? rd32le(f->head + p) : rd64le(f->head + p);
                fmt_bytes(v, a, sizeof a);
                info_add(in, "Unpacked", "%s (first frame)", a);
            }
        }
        return 1;
    case K_LZIP: if (f->hn > 6) { int ds = f->head[5]; fmt_bytes((1ull << (ds & 31)) - ((1ull << (ds & 31)) / 16) * ((ds >> 5) & 7), a, sizeof a); info_add(in, "Dictionary", "%s", a); } return 1;
    case K_RPM: if (f->hn > 76) { info_field(in, "Package", f->head + 10, 66); info_add(in, "RPM version", "%d.%d", f->head[4], f->head[5]); } return 1;
    case K_ISO: iso_info(f, in, NULL); return 1;
    case K_DISK: disk_info(f, in, NULL); return 1;
    case K_DMG: {
        uint8_t k[512];
        if (ff_read(f, f->size - 512, k, 512) == 512) { fmt_bytes(rd64be(k + 0xEC) * 512, a, sizeof a); info_add(in, "Disk size", "%s", a); }
        return 1;
    }
    case K_VHD: {
        uint8_t k[512];
        if (ff_read(f, f->size - 512, k, 512) == 512) {
            fmt_bytes(rd64be(k + 48), a, sizeof a);
            unsigned dt = rd32be(k + 60);
            info_add(in, "Disk size", "%s, %s", a, dt == 2 ? "fixed" : dt == 3 ? "dynamic" : dt == 4 ? "differencing" : "?");
            if (dt == 2) disk_info(f, in, NULL); /* a fixed VHD is the raw disk followed by this footer */
        }
        return 1;
    }
    case K_OLDARC: info_add(in, "Contents", "not listed (an old format)"); return 1;
    default: return 0;
    }
}

/* The Content page for an archive; returns 1 if it is one. For a .gz that is not a TAR, *raw gets the unpacked bytes instead. */
static int content_archive(const FFile *f, int kind, FText *t, uint8_t **raw, size_t *rawn) {
    unsigned long long total = 0;
    int ver = 0, n;
    switch (kind) {
    case K_ZIP: case K_LOVE: case K_JAR: case K_OOXML: case K_ODF: case K_EPUB: case K_MSIX: content_zip(f, kind, t); return 1;
    case K_TAR: {
        size_t len;
        uint8_t *b = ff_load(f, 0, FF_MAX_LOAD, &len);
        if (b) { tar_walk(b, len, t, NULL, NULL, NULL, NULL); if ((off_t)len < f->size) text_line(t, "... (only the first 32 MiB are listed)"); free(b); }
        return 1;
    }
    case K_GZIP: {
        size_t len;
        int cut = 0;
        uint8_t *u = gunzip_file(f, FF_LIST_UNPACK, &len, &cut);
        if (!u) { text_line(t, "Could not be unpacked: damaged, or not gzip after all."); return 1; }
        if (len >= 512 && !memcmp(u + 257, "ustar", 5)) {
            tar_walk(u, len, t, NULL, NULL, NULL, NULL);
            if (cut) text_line(t, "... (only the first 48 MiB unpacked are listed)");
            free(u);
        } else {
            *raw = u;
            *rawn = len > 4u * 1024 * 1024 ? 4u * 1024 * 1024 : len;
        }
        return 1;
    }
    case K_DEB: deb_control(f, NULL, t); return 1;
    case K_AR: content_ar(f, t); return 1;
    case K_RAR: n = rar_list(f, t, &total, &ver); if (n < 0) text_line(t, "The names are encrypted: nothing can be listed without the password."); return 1;
    case K_CAB: cab_list(f, t, &total); return 1;
    case K_CPIO: cpio_list(f, t, &total); return 1;
    case K_LZH: lzh_list(f, t, &total); return 1;
    case K_ISO: iso_info(f, NULL, t); return 1;
    case K_DISK: disk_info(f, NULL, t); return 1;
    default: return 0;
    }
}

#endif
