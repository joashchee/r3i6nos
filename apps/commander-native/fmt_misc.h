/* fmt_misc.h: documents, programs, fonts, databases, 3D models and text for Commander (included by formats.h), and the two entry
 * points the viewer calls: ff_info (the Info page) and ff_content (the Content page). */
#ifndef COMMANDER_FMT_MISC_H
#define COMMANDER_FMT_MISC_H

/* ---- PDF ---------------------------------------------------------------------------------------------------------------------- */

/* A PDF string at s ("(...)" with escapes, or "<hex>"); UTF-16 with a BOM becomes UTF-8. Returns the bytes used. */
static size_t pdf_string(const char *s, const char *end, char *out, size_t cap) {
    uint8_t raw[1024];
    size_t k = 0, i = 0;
    if (s >= end) { out[0] = 0; return 0; }
    if (*s == '(') {
        int depth = 1;
        for (i = 1; s + i < end && depth > 0; i++) {
            char c = s[i];
            if (c == '\\' && s + i + 1 < end) {
                char e = s[++i];
                int v = e == 'n' ? '\n' : e == 'r' ? '\r' : e == 't' ? '\t' : e == 'b' ? 8 : e == 'f' ? 12 : e;
                if (e >= '0' && e <= '7') {
                    v = e - '0';
                    for (int d = 0; d < 2 && s + i + 1 < end && s[i + 1] >= '0' && s[i + 1] <= '7'; d++) v = v * 8 + (s[++i] - '0');
                } else if (e == '\n' || e == '\r') continue;
                if (k < sizeof raw) raw[k++] = (uint8_t)v;
                continue;
            }
            if (c == '(') depth++;
            if (c == ')' && --depth == 0) break;
            if (k < sizeof raw) raw[k++] = (uint8_t)c;
        }
        i++;
    } else if (*s == '<') {
        int hi = -1;
        for (i = 1; s + i < end && s[i] != '>'; i++) {
            int c = s[i], v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) continue;
            if (hi < 0) hi = v;
            else { if (k < sizeof raw) raw[k++] = (uint8_t)(hi * 16 + v); hi = -1; }
        }
        if (hi >= 0 && k < sizeof raw) raw[k++] = (uint8_t)(hi * 16);
        i++;
    }
    if (k >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) utf16_to_utf8(out, cap, raw, k, 1);
    else latin1_to_utf8(out, cap, raw, k);
    return i;
}

/* Text shown by a content stream: the strings of Tj, TJ, ' and " in order, a line break at T*, ET and a Td/TD that moves down. */
static void pdf_tj_text(const char *s, size_t n, FText *t) {
    char line[2048];
    size_t k = 0;
    const char *end = s + n;
    for (const char *p = s; p < end && !t->cut; p++) {
        if (*p == '[') {
            const char *q = p + 1;
            char tmp[2048];
            size_t tk = 0;
            int ok = 1;
            while (q < end && *q != ']') {
                if (*q == '(' || *q == '<') {
                    char str[1024];
                    size_t used = pdf_string(q, end, str, sizeof str);
                    for (const char *c = str; *c; c++) if ((unsigned char)*c < 32) ok = 0;
                    size_t l = strlen(str);
                    if (tk + l < sizeof tmp) { memcpy(tmp + tk, str, l); tk += l; }
                    q += used ? used : 1;
                } else if ((*q >= '0' && *q <= '9') || *q == '-' || *q == '.') {
                    char *e;
                    double v = strtod(q, &e);
                    if (v < -200 && tk && tk + 1 < sizeof tmp && tmp[tk - 1] != ' ') tmp[tk++] = ' ';
                    q = e > q ? e : q + 1;
                } else q++;
            }
            const char *after = q + 1;
            while (after < end && (*after == ' ' || *after == '\n' || *after == '\r')) after++;
            if (after + 1 < end && after[0] == 'T' && after[1] == 'J' && ok && k + tk < sizeof line) { memcpy(line + k, tmp, tk); k += tk; }
            p = q;
        } else if (*p == '(' || (*p == '<' && p + 1 < end && p[1] != '<')) {
            char str[1024];
            size_t used = pdf_string(p, end, str, sizeof str);
            const char *after = p + used;
            while (after < end && (*after == ' ' || *after == '\n' || *after == '\r')) after++;
            int bad = 0;
            for (const char *c = str; *c; c++) if ((unsigned char)*c < 32) bad++;
            if (after + 1 < end && after[0] == 'T' && after[1] == 'j' && !bad) { size_t l = strlen(str); if (k + l < sizeof line) { memcpy(line + k, str, l); k += l; } }
            if (after < end && (*after == '\'' || *after == '"') && !bad) { line[k] = 0; if (k) text_line(t, "%s", line); k = 0; size_t l = strlen(str); if (l < sizeof line) { memcpy(line, str, l); k = l; } }
            p = after > p ? after - 1 : p;
        } else if (p + 1 < end && ((p[0] == 'T' && p[1] == '*') || (p[0] == 'E' && p[1] == 'T') || (p[0] == 'T' && (p[1] == 'd' || p[1] == 'D')))) {
            int vertical = 1;
            if (p[1] == 'd' || p[1] == 'D') { /* the number before the operator is the vertical move */
                const char *q = p - 1;
                while (q > s && (*q == ' ' || *q == '\n' || *q == '\r')) q--;
                const char *numend = q + 1;
                while (q > s && ((*q >= '0' && *q <= '9') || *q == '.' || *q == '-')) q--;
                vertical = strtod(q + 1, NULL) != 0 || numend == q + 1;
            }
            if (vertical && k) { line[k] = 0; text_line(t, "%s", line); k = 0; }
            p++;
        }
    }
    if (k) { line[k] = 0; text_line(t, "%s", line); }
}

typedef struct { int pages, streams, unpacked, encrypted, images; } PdfScan;

/* Walk the PDF's objects: count pages, and (if t) take the text out of every content stream that is not compressed or is Flate. */
static void pdf_scan(const uint8_t *b, size_t n, PdfScan *ps, FText *t) {
    memset(ps, 0, sizeof *ps);
    const char *s = (const char *)b, *end = s + n;
    for (const char *p = s; p + 10 < end;) {
        const char *q = memchr(p, '/', (size_t)(end - p));
        if (!q || q + 10 >= end) break;
        if (!strncmp(q, "/Type", 5)) {
            const char *v = q + 5;
            while (v < end && (*v == ' ' || *v == '\n' || *v == '\r')) v++;
            if (end - v > 6 && !strncmp(v, "/Page", 5) && !isalpha((unsigned char)v[5])) ps->pages++;
        } else if (!strncmp(q, "/Encrypt", 8)) ps->encrypted = 1;
        else if (!strncmp(q, "/Subtype", 8)) {
            const char *v = q + 8;
            while (v < end && *v == ' ') v++;
            if (end - v > 6 && !strncmp(v, "/Image", 6)) ps->images++;
        }
        p = q + 1;
    }
    for (const char *p = s; p < end;) {
        const char *st = NULL;
        for (const char *q = p; q + 6 < end; q++) {
            q = memchr(q, 's', (size_t)(end - q));
            if (!q || q + 6 >= end) break;
            if (!strncmp(q, "stream", 6) && (q[6] == '\r' || q[6] == '\n') && (q == s || q[-1] != 'd')) { st = q; break; }
        }
        if (!st) break;
        ps->streams++;
        /* the dictionary before it */
        const char *dict = st;
        for (int back = 0; dict > s && back < 2048; back++, dict--) if (dict[0] == '<' && dict[1] == '<') break;
        int flate = 0, other = 0, is_image = 0;
        for (const char *d = dict; d < st; d++) {
            if (!strncmp(d, "/FlateDecode", 12)) flate = 1;
            else if (!strncmp(d, "/DCTDecode", 10) || !strncmp(d, "/LZWDecode", 10) || !strncmp(d, "/ASCII85Decode", 14) || !strncmp(d, "/JBIG2", 6) || !strncmp(d, "/JPX", 4) || !strncmp(d, "/CCITT", 6)) other = 1;
            else if (!strncmp(d, "/Image", 6) || !strncmp(d, "/XObject", 8) || !strncmp(d, "/FontFile", 9) || !strncmp(d, "/Length1", 8)) is_image = 1;
        }
        const char *data = st + 6;
        if (*data == '\r') data++;
        if (*data == '\n') data++;
        const char *de = NULL;
        for (const char *q = data; q + 9 <= end; q++) {
            q = memchr(q, 'e', (size_t)(end - q));
            if (!q || q + 9 > end) break;
            if (!strncmp(q, "endstream", 9)) { de = q; break; }
        }
        if (!de) break;
        if (!other && !is_image && !ps->encrypted) {
            uint8_t *u = NULL;
            long un = 0;
            if (flate) {
                size_t cap = 8u * 1024 * 1024;
                u = (uint8_t *)malloc(cap + 1);
                if (u) { un = fi_zlib((const uint8_t *)data, (size_t)(de - data), u, cap, NULL); if (un < 0) { free(u); u = NULL; } else u[un] = 0; }
                if (u) ps->unpacked++;
            }
            const char *body = u ? (const char *)u : data;
            size_t bl = u ? (size_t)un : (size_t)(de - data);
            if (u) { /* object streams hold more /Type /Page entries */
                PdfScan inner;
                memset(&inner, 0, sizeof inner);
                for (const char *q = body; q + 10 < body + bl; q++)
                    if (*q == '/' && !strncmp(q, "/Type", 5)) {
                        const char *v = q + 5;
                        while (v < body + bl && (*v == ' ' || *v == '\n' || *v == '\r')) v++;
                        if (body + bl - v > 6 && !strncmp(v, "/Page", 5) && !isalpha((unsigned char)v[5])) ps->pages++;
                    }
            }
            if (t && bl && (memmem(body, bl, "Tj", 2) || memmem(body, bl, "TJ", 2)) && memmem(body, bl, "BT", 2)) pdf_tj_text(body, bl, t);
            free(u);
        }
        p = de + 9;
    }
}

static void pdf_info_strings(const uint8_t *b, size_t n, FInfo *in) {
    static const char *keys[][2] = {{"/Title", "Title"}, {"/Author", "Author"}, {"/Subject", "Subject"}, {"/Creator", "Made with"}, {"/Producer", "Producer"}, {"/CreationDate", "Created"}};
    const char *s = (const char *)b, *end = s + n;
    for (int k = 0; k < 6; k++) {
        size_t kl = strlen(keys[k][0]);
        for (const char *p = s; (p = memmem(p, (size_t)(end - p), keys[k][0], kl)) != NULL; p += kl) {
            const char *v = p + kl;
            while (v < end && *v == ' ') v++;
            if (v < end && (*v == '(' || (*v == '<' && v + 1 < end && v[1] != '<'))) {
                char out[232];
                pdf_string(v, end, out, sizeof out);
                if (k == 5 && !strncmp(out, "D:", 2) && strlen(out) >= 10) { char d[32]; snprintf(d, sizeof d, "%.4s-%.2s-%.2s", out + 2, out + 6, out + 8); info_add(in, keys[k][1], "%s", d); }
                else { char c[232]; ff_clean(c, sizeof c, (const uint8_t *)out, strlen(out)); if (c[0]) info_add(in, keys[k][1], "%s", c); }
                break;
            }
        }
    }
}

static void pdf_file(const FFile *f, FInfo *in, FText *t) {
    size_t n;
    uint8_t *b = ff_load(f, 0, FF_MAX_LOAD, &n);
    if (!b) return;
    PdfScan ps;
    if (in) {
        char v[8];
        ff_clean(v, sizeof v, b + 5, 3);
        info_add(in, "PDF version", "%s", v);
        pdf_info_strings(b, n, in);
    }
    pdf_scan(b, n, &ps, t);
    if (in) {
        if (ps.pages) info_add(in, "Pages", "%d", ps.pages);
        if (ps.images) info_add(in, "Pictures", "%d", ps.images);
        if (ps.encrypted) info_add(in, "Encrypted", "yes: its text cannot be shown");
        if (memmem(b, n < 1024 ? n : 1024, "/Linearized", 11)) info_add(in, "Web-ready", "linearized");
        if ((off_t)n < f->size) info_add(in, "Note", "only the first 32 MiB were read");
    }
    if (t && !t->n) text_line(t, "%s", ps.encrypted ? "The text is encrypted." : "No text found: the pages may be pictures, or use fonts whose letters are not stored as text.");
    free(b);
}

/* ---- RTF ---------------------------------------------------------------------------------------------------------------------- */

static unsigned cp1252(unsigned c) {
    static const unsigned short hi[32] = {0x20AC, 0, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0, 0x17D, 0,
                                          0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0, 0x17E, 0x178};
    return c >= 0x80 && c < 0xA0 && hi[c - 0x80] ? hi[c - 0x80] : c;
}

static void rtf_file(const FFile *f, FInfo *in, FText *t) {
    size_t n;
    uint8_t *b = ff_load(f, 0, FF_MAX_LOAD, &n);
    if (!b) return;
    const char *s = (const char *)b;
    if (in) {
        static const char *keys[][2] = {{"{\\title", "Title"}, {"{\\author", "Author"}, {"{\\operator", "Last saved by"}, {"{\\company", "Company"}};
        for (int k = 0; k < 4; k++) {
            const char *p = strstr(s, keys[k][0]);
            if (!p) continue;
            p += strlen(keys[k][0]);
            const char *e = strchr(p, '}');
            if (e) info_field(in, keys[k][1], (const uint8_t *)p, (size_t)(e - p));
        }
        const char *g = strstr(s, "{\\*\\generator");
        if (g) { g += 13; const char *e = strchr(g, '}'); if (e) info_field(in, "Made with", (const uint8_t *)g, (size_t)(e - g)); }
    }
    if (t) {
        char line[4096];
        size_t k = 0;
        int depth = 0, skip_depth = -1, uc_skip = 0;
        for (size_t i = 0; i < n && !t->cut;) {
            char c = s[i];
            if (c == '{') {
                depth++;
                if (skip_depth < 0 && i + 2 < n && s[i + 1] == '\\' && s[i + 2] == '*') skip_depth = depth;
                if (skip_depth < 0) {
                    static const char *dests[] = {"\\fonttbl", "\\colortbl", "\\stylesheet", "\\info", "\\pict", "\\header", "\\footer", "\\listtable", "\\listoverridetable", "\\rsidtbl", "\\themedata", "\\colorschememapping", "\\latentstyles", "\\datastore", "\\xmlnstbl", "\\object", NULL};
                    for (int d = 0; dests[d]; d++) if (!strncmp(s + i + 1, dests[d], strlen(dests[d])) && !isalpha((unsigned char)s[i + 1 + strlen(dests[d])])) skip_depth = depth;
                }
                i++;
                continue;
            }
            if (c == '}') { if (depth == skip_depth) skip_depth = -1; depth--; i++; continue; }
            if (c == '\\') {
                i++;
                if (i >= n) break;
                char e = s[i];
                if (e == '\\' || e == '{' || e == '}') { if (skip_depth < 0 && k + 1 < sizeof line) line[k++] = e; i++; continue; }
                if (e == '\'') {
                    unsigned v = (unsigned)strtoul((char[3]){i + 1 < n ? s[i + 1] : '0', i + 2 < n ? s[i + 2] : '0', 0}, NULL, 16);
                    i += 3;
                    if (uc_skip) { uc_skip--; continue; }
                    if (skip_depth < 0) k = utf8_put(line, sizeof line, k, cp1252(v));
                    continue;
                }
                if (e == '~') { if (skip_depth < 0 && k + 1 < sizeof line) line[k++] = ' '; i++; continue; }
                if (e == '\n' || e == '\r') { if (skip_depth < 0) { line[k] = 0; text_line(t, "%s", line); k = 0; } i++; continue; }
                if (!isalpha((unsigned char)e)) { i++; continue; }
                char word[32];
                size_t wl = 0;
                while (i < n && isalpha((unsigned char)s[i]) && wl + 1 < sizeof word) word[wl++] = s[i++];
                word[wl] = 0;
                long arg = 0;
                int has_arg = 0;
                if (i < n && (s[i] == '-' || isdigit((unsigned char)s[i]))) { char *ep; arg = strtol(s + i, &ep, 10); i = (size_t)(ep - s); has_arg = 1; }
                if (i < n && s[i] == ' ') i++;
                if (skip_depth >= 0) continue;
                if (!strcmp(word, "par") || !strcmp(word, "line") || !strcmp(word, "row") || !strcmp(word, "page") || !strcmp(word, "sect")) { line[k] = 0; text_line(t, "%s", line); k = 0; }
                else if (!strcmp(word, "tab") || !strcmp(word, "cell")) { if (k + 1 < sizeof line) line[k++] = ' '; }
                else if (!strcmp(word, "u") && has_arg) { k = utf8_put(line, sizeof line, k, (unsigned)(arg < 0 ? arg + 65536 : arg)); uc_skip = 1; }
                else if (!strcmp(word, "emdash")) k = utf8_put(line, sizeof line, k, 0x2014);
                else if (!strcmp(word, "endash")) k = utf8_put(line, sizeof line, k, 0x2013);
                else if (!strcmp(word, "bullet")) k = utf8_put(line, sizeof line, k, 0x2022);
                else if (!strcmp(word, "lquote")) k = utf8_put(line, sizeof line, k, 0x2018);
                else if (!strcmp(word, "rquote")) k = utf8_put(line, sizeof line, k, 0x2019);
                else if (!strcmp(word, "ldblquote")) k = utf8_put(line, sizeof line, k, 0x201C);
                else if (!strcmp(word, "rdblquote")) k = utf8_put(line, sizeof line, k, 0x201D);
                continue;
            }
            if (c == '\n' || c == '\r') { i++; continue; }
            if (uc_skip) { uc_skip--; i++; continue; }
            if (skip_depth < 0 && k + 1 < sizeof line) line[k++] = c;
            if (k + 8 >= sizeof line) { line[k] = 0; text_line(t, "%s", line); k = 0; }
            i++;
        }
        if (k) { line[k] = 0; text_line(t, "%s", line); }
    }
    free(b);
}

/* ---- mail --------------------------------------------------------------------------------------------------------------------- */

/* A header's value (unfolded) from the block at s. */
static int mail_header(const char *s, size_t n, const char *name, char *out, size_t cap) {
    size_t nl = strlen(name);
    for (size_t i = 0; i + nl + 1 < n; ) {
        if ((i == 0 || s[i - 1] == '\n') && !strncasecmp(s + i, name, nl) && s[i + nl] == ':') {
            size_t j = i + nl + 1, k = 0;
            while (j < n && s[j] == ' ') j++;
            while (j < n && k + 1 < cap) {
                if (s[j] == '\r') { j++; continue; }
                if (s[j] == '\n') { if (j + 1 < n && (s[j + 1] == ' ' || s[j + 1] == '\t')) { j++; out[k++] = ' '; while (j < n && (s[j] == ' ' || s[j] == '\t')) j++; continue; } break; }
                out[k++] = s[j++];
            }
            out[k] = 0;
            return 1;
        }
        if (s[i] == '\n' && i + 1 < n && (s[i + 1] == '\n' || (s[i + 1] == '\r' && i + 2 < n && s[i + 2] == '\n'))) break; /* end of the headers */
        i++;
    }
    return 0;
}

static void mail_file(const FFile *f, int kind, FInfo *in, FText *t) {
    size_t n;
    uint8_t *b = ff_load(f, 0, kind == K_MBOX ? FF_MAX_LOAD : 1024 * 1024, &n);
    if (!b) return;
    const char *s = (const char *)b;
    if (kind == K_EML) {
        static const char *h[] = {"From", "To", "Cc", "Subject", "Date", "Content-Type", NULL};
        char v[232];
        for (int i = 0; h[i]; i++) if (in && mail_header(s, n, h[i], v, sizeof v)) info_add(in, h[i], "%s", v);
    } else {
        int count = 0;
        for (size_t i = 0; i + 5 < n; i++) {
            if ((i == 0 || s[i - 1] == '\n') && !strncmp(s + i, "From ", 5)) {
                count++;
                if (t && count <= FF_MAX_LIST) {
                    const char *hdr = memchr(s + i, '\n', n - i);
                    if (!hdr) break;
                    hdr++;
                    size_t rest = n - (size_t)(hdr - s);
                    char subj[160] = "", from[80] = "", date[40] = "";
                    mail_header(hdr, rest, "Subject", subj, sizeof subj);
                    mail_header(hdr, rest, "From", from, sizeof from);
                    mail_header(hdr, rest, "Date", date, sizeof date);
                    text_line(t, "%-3d %.16s  %-24.24s  %s", count, date, from, subj);
                }
            }
        }
        if (in) info_add(in, "Messages", "%d%s", count, (off_t)n < f->size ? " in the first 32 MiB" : "");
    }
    free(b);
}

/* ---- programs ----------------------------------------------------------------------------------------------------------------- */

static const char *elf_machine(unsigned m) {
    switch (m) {
    case 3: return "x86 (32-bit)";
    case 8: return "MIPS";
    case 20: return "PowerPC";
    case 21: return "PowerPC 64";
    case 40: return "ARM (32-bit)";
    case 62: return "x86-64";
    case 183: return "ARM64 (AArch64)";
    case 243: return "RISC-V";
    case 258: return "LoongArch";
    default: return "other";
    }
}

static void elf_file(const FFile *f, FInfo *in, FText *t) {
    const uint8_t *h = f->head;
    if (f->hn < 52) return;
    int is64 = h[4] == 2, le = h[5] == 1;
#define E16(p) (le ? rd16le(p) : rd16be(p))
#define E32(p) (le ? rd32le(p) : rd32be(p))
#define E64(p) (le ? rd64le(p) : rd64be(p))
#define EADDR(p) (is64 ? E64(p) : (uint64_t)E32(p))
    unsigned type = E16(h + 16), mach = E16(h + 18);
    uint64_t phoff = EADDR(h + (is64 ? 32 : 28)), shoff = EADDR(h + (is64 ? 40 : 32));
    unsigned phes = E16(h + (is64 ? 54 : 42)), phn = E16(h + (is64 ? 56 : 44)), shes = E16(h + (is64 ? 58 : 46)), shn = E16(h + (is64 ? 60 : 48)), shstr = E16(h + (is64 ? 62 : 50));
    char interp[128] = "";
    uint64_t dyn_off = 0, dyn_size = 0;
    struct { uint64_t vaddr, off, filesz; } loads[16];
    int nl = 0;
    for (unsigned i = 0; i < phn && i < 64; i++) {
        uint8_t ph[56];
        if (ff_read(f, (off_t)(phoff + (uint64_t)i * phes), ph, is64 ? 56 : 32) != (is64 ? 56u : 32u)) break;
        uint32_t pt = E32(ph);
        uint64_t off = is64 ? E64(ph + 8) : E32(ph + 4), vaddr = is64 ? E64(ph + 16) : E32(ph + 8), filesz = is64 ? E64(ph + 32) : E32(ph + 16);
        if (pt == 3) { size_t g = ff_read(f, (off_t)off, interp, filesz < sizeof interp - 1 ? (size_t)filesz : sizeof interp - 1); interp[g] = 0; }
        if (pt == 2) { dyn_off = off; dyn_size = filesz; }
        if (pt == 1 && nl < 16) { loads[nl].vaddr = vaddr; loads[nl].off = off; loads[nl].filesz = filesz; nl++; }
    }
    const char *kind = type == 1 ? "object file" : type == 2 ? "program" : type == 3 ? (interp[0] ? "program (position-independent)" : "shared library") : type == 4 ? "core dump" : "other";
    info_add(in, "Kind", "%s", kind);
    info_add(in, "For", "%s, %d-bit, %s-endian", elf_machine(mach), is64 ? 64 : 32, le ? "little" : "big");
    if (h[7]) info_add(in, "OS ABI", "%d", h[7]);
    if (interp[0]) info_add(in, "Loader", "%s", interp);
    if (mach == 183 && is64) info_add(in, "On the R36S", "%s", interp[0] && !strstr(interp, "aarch64") ? "ARM64, but its loader is not the usual one" : "the right kind (ARM64 Linux)");
    else if (mach == 40) info_add(in, "On the R36S", "no: 32-bit ARM, and r3i6nos has only 64-bit libraries");
    else if (type == 2 || type == 3) info_add(in, "On the R36S", "no: built for %s", elf_machine(mach));
    /* the libraries it needs, from the dynamic section */
    if (dyn_off && dyn_size && dyn_size < 1024 * 1024) {
        size_t dn;
        uint8_t *d = ff_load(f, (off_t)dyn_off, dyn_size, &dn);
        uint64_t strtab = 0, strsz = 0;
        size_t es = is64 ? 16 : 8;
        for (size_t i = 0; d && i + es <= dn; i += es) {
            uint64_t tag = is64 ? E64(d + i) : E32(d + i), val = is64 ? E64(d + i + 8) : E32(d + i + 4);
            if (tag == 5) strtab = val;
            if (tag == 10) strsz = val;
            if (tag == 0) break;
        }
        uint64_t stroff = 0;
        for (int i = 0; i < nl; i++) if (strtab >= loads[i].vaddr && strtab < loads[i].vaddr + loads[i].filesz) stroff = strtab - loads[i].vaddr + loads[i].off;
        if (d && stroff && strsz && strsz < 4 * 1024 * 1024) {
            size_t sn;
            char *str = (char *)ff_load(f, (off_t)stroff, strsz, &sn);
            int needed = 0;
            for (size_t i = 0; str && i + es <= dn; i += es) {
                uint64_t tag = is64 ? E64(d + i) : E32(d + i), val = is64 ? E64(d + i + 8) : E32(d + i + 4);
                if (tag == 0) break;
                if ((tag == 1 || tag == 14 || tag == 29 || tag == 15) && val < sn) {
                    if (tag == 1) { needed++; if (t) text_line(t, "needs  %s", str + val); }
                    else info_add(in, tag == 14 ? "Library name" : "Search path", "%s", str + val);
                }
            }
            if (needed) info_add(in, "Libraries", "%d needed (listed on the next page)", needed);
            free(str);
        }
        free(d);
    }
    /* stripped? */
    if (shoff && shn && shn < 4096 && shstr < shn) {
        uint8_t sh[64];
        int symtab = 0, debug = 0;
        for (unsigned i = 0; i < shn; i++) {
            if (ff_read(f, (off_t)(shoff + (uint64_t)i * shes), sh, is64 ? 64 : 40) != (is64 ? 64u : 40u)) break;
            uint32_t st = E32(sh + 4);
            if (st == 2) symtab = 1;
            (void)debug;
        }
        info_add(in, "Symbols", "%s", symtab ? "present (not stripped)" : "stripped");
    }
#undef E16
#undef E32
#undef E64
#undef EADDR
}

static void pe_file(const FFile *f, FInfo *in) {
    const uint8_t *h = f->head;
    uint32_t pe = f->hn >= 64 ? rd32le(h + 60) : 0;
    if (!pe || pe + 24 > f->hn || memcmp(h + pe, "PE\0\0", 4)) { info_add(in, "Kind", "DOS program"); return; }
    unsigned mach = rd16le(h + pe + 4), nsec = rd16le(h + pe + 6), ch = rd16le(h + pe + 22);
    uint32_t ts = rd32le(h + pe + 8);
    const char *m = mach == 0x14c ? "x86 (32-bit)" : mach == 0x8664 ? "x86-64" : mach == 0xAA64 ? "ARM64" : mach == 0x1C0 || mach == 0x1C4 ? "ARM" : "other";
    info_add(in, "Kind", "%s", (ch & 0x2000) ? "Windows library (DLL)" : "Windows program");
    info_add(in, "For", "%s", m);
    if (pe + 24 + 70 <= f->hn) {
        unsigned magic = rd16le(h + pe + 24), sub = rd16le(h + pe + 24 + 68);
        info_add(in, "Format", "%s", magic == 0x20B ? "PE32+ (64-bit)" : "PE32");
        static const char *subs[] = {"?", "native", "graphical (GUI)", "console", "?", "OS/2", "?", "POSIX", "?", "Windows CE", "EFI application", "EFI boot driver", "EFI runtime driver"};
        info_add(in, "Subsystem", "%s", sub < 13 ? subs[sub] : "other");
        size_t dd = pe + 24 + (magic == 0x20B ? 112 : 96);
        if (dd + 15 * 8 <= f->hn && rd32le(h + dd + 14 * 8)) info_add(in, ".NET", "yes (managed code)");
    }
    info_add(in, "Sections", "%u", nsec);
    if (ts > 315532800 && ts < 4102444800u) { char w[40]; fmt_time((time_t)ts, w, sizeof w); info_add(in, "Built", "%s", w); }
    info_add(in, "On the R36S", "no: a Windows program");
}

static void macho_file(const FFile *f, int kind, FInfo *in) {
    const uint8_t *h = f->head;
    if (kind == K_FAT) {
        uint32_t n = rd32be(h + 4);
        info_add(in, "Kind", "universal (%u architectures)", n);
        for (uint32_t i = 0; i < n && i < 8 && 8 + i * 20 + 4 <= f->hn; i++) {
            uint32_t cpu = rd32be(h + 8 + i * 20);
            char k[24];
            snprintf(k, sizeof k, "Architecture %u", i + 1);
            info_add(in, k, "%s", cpu == 7 ? "x86" : cpu == 0x01000007 ? "x86-64" : cpu == 12 ? "ARM" : cpu == 0x0100000C ? "ARM64" : cpu == 18 ? "PowerPC" : "other");
        }
        return;
    }
    int le = h[0] == 0xCE || h[0] == 0xCF;
    uint32_t cpu = le ? rd32le(h + 4) : rd32be(h + 4), ft = le ? rd32le(h + 12) : rd32be(h + 12);
    info_add(in, "For", "%s", cpu == 7 ? "x86" : cpu == 0x01000007 ? "x86-64" : cpu == 12 ? "ARM" : cpu == 0x0100000C ? "ARM64 (Apple)" : "other");
    info_add(in, "Kind", "%s", ft == 2 ? "program" : ft == 6 ? "library (dylib)" : ft == 8 ? "bundle" : ft == 1 ? "object file" : "other");
    info_add(in, "On the R36S", "no: a macOS program");
}

/* ---- fonts -------------------------------------------------------------------------------------------------------------------- */

static void sfnt_name(const uint8_t *nm, size_t n, FInfo *in) {
    if (n < 6) return;
    unsigned count = rd16be(nm + 2), so = rd16be(nm + 4);
    static const struct { int id; const char *k; } ids[] = {{1, "Family"}, {2, "Style"}, {4, "Full name"}, {5, "Version"}, {0, "Copyright"}, {8, "Maker"}, {9, "Designer"}, {13, "Licence"}};
    for (size_t w = 0; w < sizeof ids / sizeof ids[0]; w++) {
        int best = -1, best_score = -1;
        for (unsigned i = 0; i < count && 6 + i * 12 + 12 <= n; i++) {
            const uint8_t *r = nm + 6 + i * 12;
            unsigned plat = rd16be(r), lang = rd16be(r + 4), id = rd16be(r + 6);
            if ((int)id != ids[w].id) continue;
            int score = plat == 3 ? (lang == 0x409 ? 3 : 2) : plat == 1 ? 1 : plat == 0 ? 2 : 0;
            if (score > best_score) { best_score = score; best = (int)i; }
        }
        if (best < 0) continue;
        const uint8_t *r = nm + 6 + best * 12;
        unsigned plat = rd16be(r), len = rd16be(r + 8), off = rd16be(r + 10);
        if ((size_t)so + off + len > n) continue;
        char v[232];
        if (plat == 3 || plat == 0) utf16_to_utf8(v, sizeof v, nm + so + off, len, 1);
        else latin1_to_utf8(v, sizeof v, nm + so + off, len);
        char c[232];
        ff_clean(c, sizeof c, (const uint8_t *)v, strlen(v));
        if (c[0]) info_add(in, ids[w].k, "%s", c);
    }
}

static void sfnt_file(const FFile *f, off_t base, FInfo *in) {
    uint8_t h[12];
    if (ff_read(f, base, h, 12) != 12) return;
    unsigned nt = rd16be(h + 4);
    info_add(in, "Outlines", "%s", !memcmp(h, "OTTO", 4) ? "PostScript (CFF)" : "TrueType");
    info_add(in, "Tables", "%u", nt);
    for (unsigned i = 0; i < nt && i < 100; i++) {
        uint8_t e[16];
        if (ff_read(f, base + 12 + (off_t)i * 16, e, 16) != 16) break;
        uint32_t off = rd32be(e + 8), len = rd32be(e + 12);
        if (!memcmp(e, "name", 4) && len < 1024 * 1024) {
            size_t n;
            uint8_t *nm = ff_load(f, off, len, &n);
            if (nm) sfnt_name(nm, n, in);
            free(nm);
        } else if (!memcmp(e, "maxp", 4)) {
            uint8_t m[6];
            if (ff_read(f, off, m, 6) == 6) info_add(in, "Glyphs", "%u", rd16be(m + 4));
        } else if (!memcmp(e, "OS/2", 4)) {
            uint8_t o[6];
            if (ff_read(f, off, o, 6) == 6) info_add(in, "Weight", "%u", rd16be(o + 4));
        }
    }
}

static void woff_file(const FFile *f, FInfo *in) {
    const uint8_t *h = f->head;
    if (f->hn < 44) return;
    unsigned nt = rd16be(h + 12);
    info_add(in, "Outlines", "%s", !memcmp(h + 4, "OTTO", 4) ? "PostScript (CFF)" : "TrueType");
    info_add(in, "Version", "%u.%u", rd16be(h + 20), rd16be(h + 22));
    char a[16];
    fmt_bytes(rd32be(h + 16), a, sizeof a);
    info_add(in, "Unpacked", "%s", a);
    for (unsigned i = 0; i < nt && i < 100; i++) {
        uint8_t e[20];
        if (ff_read(f, 44 + (off_t)i * 20, e, 20) != 20) break;
        uint32_t off = rd32be(e + 4), clen = rd32be(e + 8), olen = rd32be(e + 12);
        if (memcmp(e, "name", 4) || olen > 1024 * 1024) continue;
        size_t n;
        uint8_t *c = ff_load(f, off, clen, &n);
        if (!c) continue;
        if (clen < olen) {
            uint8_t *u = (uint8_t *)malloc(olen + 1);
            long got = u ? fi_zlib(c, n, u, olen, NULL) : -1;
            if (got > 0) sfnt_name(u, (size_t)got, in);
            free(u);
        } else sfnt_name(c, n, in);
        free(c);
    }
}

static void psf_file(const FFile *f, FInfo *in) {
    const uint8_t *h = f->head;
    if (h[0] == 0x36 && f->hn >= 4) info_add(in, "Glyphs", "%d, 8 x %d pixels%s", (h[2] & 1) ? 512 : 256, h[3], (h[2] & 2) ? ", with a Unicode table" : "");
    else if (f->hn >= 32) info_add(in, "Glyphs", "%u, %u x %u pixels%s", rd32le(h + 16), rd32le(h + 28), rd32le(h + 24), (rd32le(h + 12) & 1) ? ", with a Unicode table" : "");
}

/* ---- databases ---------------------------------------------------------------------------------------------------------------- */

static uint64_t sq_varint(const uint8_t *b, size_t n, size_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 9 && *p < n; i++) {
        uint8_t c = b[(*p)++];
        if (i == 8) return v << 8 | c;
        v = v << 7 | (c & 127);
        if (!(c & 128)) break;
    }
    return v;
}

/* The schema table's rows (type, name) from the b-tree page `page`. */
static void sqlite_schema(const FFile *f, unsigned psize, uint32_t page, int depth, int *counts, FText *t, char *tables, size_t tcap) {
    if (depth > 6 || page == 0) return;
    uint8_t *b = (uint8_t *)malloc(psize);
    if (!b) return;
    if (ff_read(f, (off_t)(page - 1) * psize, b, psize) != psize) { free(b); return; }
    size_t h = page == 1 ? 100 : 0;
    int type = b[h];
    unsigned cells = rd16be(b + h + 3);
    size_t ptrs = h + (type == 5 ? 12 : 8);
    for (unsigned i = 0; i < cells && ptrs + i * 2 + 2 <= psize; i++) {
        size_t c = rd16be(b + ptrs + i * 2);
        if (c >= psize) continue;
        if (type == 5) { sqlite_schema(f, psize, rd32be(b + c), depth + 1, counts, t, tables, tcap); continue; }
        if (type != 13) continue;
        size_t p = c;
        uint64_t plen = sq_varint(b, psize, &p);
        sq_varint(b, psize, &p);
        size_t rec = p, hp = p;
        uint64_t hlen = sq_varint(b, psize, &hp);
        uint64_t st[3] = {0, 0, 0};
        for (int k = 0; k < 3 && hp < rec + hlen && hp < psize; k++) st[k] = sq_varint(b, psize, &hp);
        size_t vp = rec + (size_t)hlen;
        (void)plen;
        char vals[2][128] = {"", ""};
        for (int k = 0; k < 2; k++) {
            if (st[k] >= 13 && (st[k] & 1)) {
                size_t l = (size_t)(st[k] - 13) / 2;
                if (vp + l <= psize) ff_clean(vals[k], sizeof vals[k], b + vp, l);
                vp += l;
            } else break;
        }
        int which = !strcmp(vals[0], "table") ? 0 : !strcmp(vals[0], "index") ? 1 : !strcmp(vals[0], "view") ? 2 : !strcmp(vals[0], "trigger") ? 3 : -1;
        if (which < 0) continue;
        counts[which]++;
        if (t) text_line(t, "%-8s %s", vals[0], vals[1]);
        if (which == 0 && strncmp(vals[1], "sqlite_", 7)) {
            size_t l = strlen(tables);
            if (l + strlen(vals[1]) + 3 < tcap) snprintf(tables + l, tcap - l, "%s%s", l ? ", " : "", vals[1]);
        }
    }
    if (type == 5) sqlite_schema(f, psize, rd32be(b + h + 8), depth + 1, counts, t, tables, tcap);
    free(b);
}

static void sqlite_file(const FFile *f, FInfo *in, FText *t) {
    const uint8_t *h = f->head;
    if (f->hn < 100) return;
    unsigned psize = rd16be(h + 16);
    if (psize == 1) psize = 65536;
    if (psize < 512 || (psize & (psize - 1))) return;
    if (in) {
        uint32_t pages = rd32be(h + 28), ver = rd32be(h + 96);
        info_add(in, "Pages", "%u of %u bytes", pages, psize);
        info_add(in, "Text", "%s", h[59] == 1 ? "UTF-8" : h[59] == 2 ? "UTF-16 little-endian" : h[59] == 3 ? "UTF-16 big-endian" : "?");
        if (ver) info_add(in, "Written by", "SQLite %u.%u.%u", ver / 1000000, ver / 1000 % 1000, ver % 1000);
        if (rd32be(h + 60)) info_add(in, "User version", "%u", rd32be(h + 60));
        if (rd32be(h + 68)) info_add(in, "Application ID", "0x%08X", rd32be(h + 68));
        info_add(in, "Journal", "%s", h[18] == 2 ? "write-ahead log (WAL)" : "rollback");
    }
    int counts[4] = {0, 0, 0, 0};
    char tables[232] = "";
    sqlite_schema(f, psize, 1, 0, counts, t, tables, sizeof tables);
    if (in) {
        info_add(in, "Schema", "%d tables, %d indexes, %d views, %d triggers", counts[0], counts[1], counts[2], counts[3]);
        if (tables[0]) info_add(in, "Tables", "%s", tables);
    }
}

static void dbf_file(const FFile *f, FInfo *in, FText *t) {
    const uint8_t *h = f->head;
    if (f->hn < 32) return;
    if (in) {
        info_add(in, "Version", "0x%02X%s", h[0], (h[0] & 0x80) ? " (with memo file)" : "");
        info_add(in, "Last changed", "%04d-%02d-%02d", 1900 + h[1], h[2], h[3]);
        info_add(in, "Records", "%u of %u bytes", rd32le(h + 4), rd16le(h + 10));
    }
    int fields = 0;
    for (size_t p = 32; p + 32 <= f->hn && h[p] != 0x0D; p += 32) {
        fields++;
        char name[16];
        ff_clean(name, sizeof name, h + p, 11);
        if (t) text_line(t, "%-12s %c(%u%s%.0u)", name, h[p + 11], h[p + 16], h[p + 17] ? "." : "", h[p + 17]);
    }
    if (in) info_add(in, "Fields", "%d", fields);
}

/* ---- 3D, torrents ------------------------------------------------------------------------------------------------------------- */

static void json_string_after(const char *s, const char *key, char *out, size_t cap) {
    out[0] = 0;
    const char *p = strstr(s, key);
    if (!p) return;
    p = strchr(p + strlen(key), ':');
    if (!p) return;
    p++;
    while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') p++;
    if (*p != '"') return;
    p++;
    const char *e = strchr(p, '"');
    if (e) snprintf(out, cap, "%.*s", (int)(e - p), p);
}

static void model_file(const FFile *f, int kind, FInfo *in) {
    if (kind == K_STL) {
        if (FF_AT(f, 0, "solid") && memmem(f->head, f->hn, "facet", 5)) {
            size_t n;
            uint8_t *b = ff_load(f, 0, FF_MAX_LOAD, &n);
            long c = 0;
            for (const char *p = (const char *)b; b && (p = strstr(p, "facet normal")) != NULL; p += 12) c++;
            info_add(in, "Triangles", "%ld", c);
            free(b);
        } else if (f->hn >= 84) {
            uint32_t c = rd32le(f->head + 80);
            info_add(in, "Triangles", "%u%s", c, (off_t)84 + (off_t)c * 50 == f->size ? "" : " (the size does not match: damaged?)");
            info_field(in, "Header", f->head, 80);
        }
    } else if (kind == K_OBJ) {
        size_t n;
        uint8_t *b = ff_load(f, 0, FF_MAX_LOAD, &n);
        if (!b) return;
        long v = 0, vt = 0, vn = 0, fc = 0, o = 0;
        for (size_t i = 0; i + 2 < n; i++) {
            if (i && b[i - 1] != '\n') continue;
            if (b[i] == 'v' && b[i + 1] == ' ') v++;
            else if (b[i] == 'v' && b[i + 1] == 't') vt++;
            else if (b[i] == 'v' && b[i + 1] == 'n') vn++;
            else if (b[i] == 'f' && b[i + 1] == ' ') fc++;
            else if ((b[i] == 'o' || b[i] == 'g') && b[i + 1] == ' ') o++;
        }
        info_add(in, "Vertices", "%ld (%ld texture, %ld normals)", v, vt, vn);
        info_add(in, "Faces", "%ld", fc);
        if (o) info_add(in, "Objects/groups", "%ld", o);
        const char *m = strstr((const char *)b, "mtllib ");
        if (m) { size_t l = strcspn(m + 7, "\r\n"); info_add(in, "Materials", "%.*s", (int)(l < 200 ? l : 200), m + 7); }
        free(b);
    } else {
        size_t n;
        uint8_t *b = ff_load(f, 0, 4 * 1024 * 1024, &n);
        if (!b) return;
        const char *json = (const char *)b;
        if (kind == K_GLB && n >= 20) {
            info_add(in, "glTF version", "%u", rd32le(b + 4));
            json = (const char *)b + 20;
        }
        char v[200];
        json_string_after(json, "\"generator\"", v, sizeof v);
        if (v[0]) info_add(in, "Made with", "%s", v);
        json_string_after(json, "\"version\"", v, sizeof v);
        if (v[0]) info_add(in, "Asset version", "%s", v);
        free(b);
    }
}

static void torrent_file(const FFile *f, FInfo *in) {
    size_t n;
    uint8_t *b = ff_load(f, 0, 4 * 1024 * 1024, &n);
    if (!b) return;
    const char *s = (const char *)b;
    static const char *keys[][2] = {{"4:name", "Name"}, {"8:announce", "Tracker"}, {"10:created by", "Made with"}, {"7:comment", "Comment"}};
    for (int k = 0; k < 4; k++) {
        const char *p = memmem(s, n, keys[k][0], strlen(keys[k][0]));
        if (!p) continue;
        p += strlen(keys[k][0]);
        char *colon;
        long l = strtol(p, &colon, 10);
        if (*colon == ':' && l > 0 && colon + 1 + l <= s + n) info_field(in, keys[k][1], (const uint8_t *)colon + 1, (size_t)l);
    }
    int files = 0;
    for (const char *p = s; (p = memmem(p, n - (size_t)(p - s), "6:lengthi", 9)) != NULL; p += 9) files++;
    if (files) info_add(in, "Files", "%d", files);
    const char *pl = memmem(s, n, "12:piece lengthi", 16);
    if (pl) { char a[16]; fmt_bytes(strtoull(pl + 16, NULL, 10), a, sizeof a); info_add(in, "Piece size", "%s", a); }
    free(b);
}

/* ---- text --------------------------------------------------------------------------------------------------------------------- */

static int utf8_valid(const uint8_t *b, size_t n, int *non_ascii) {
    *non_ascii = 0;
    for (size_t i = 0; i < n;) {
        uint8_t c = b[i];
        if (c < 0x80) { i++; continue; }
        *non_ascii = 1;
        int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (!len || c == 0xC0 || c == 0xC1) return 0;
        if (i + (size_t)len > n) return 1; /* cut at the read limit */
        for (int k = 1; k < len; k++) if ((b[i + k] & 0xC0) != 0x80) return 0;
        i += (size_t)len;
    }
    return 1;
}

/* A JSON value: 1 if well-formed (just the structure, not every escape). */
static int json_check(const char *s, size_t n, char *top, size_t cap) {
    char stack[256];
    int depth = 0, instr = 0, seen = 0;
    top[0] = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (instr) { if (c == '\\') i++; else if (c == '"') instr = 0; continue; }
        if (c == '"') { instr = 1; if (!seen) { snprintf(top, cap, "a string"); seen = 1; } continue; }
        if (c == '{' || c == '[') {
            if (!seen) { snprintf(top, cap, c == '{' ? "an object" : "a list"); seen = 1; }
            if (depth >= 255) return 0;
            stack[depth++] = c;
        } else if (c == '}' || c == ']') {
            if (!depth || stack[depth - 1] != (c == '}' ? '{' : '[')) return 0;
            depth--;
        } else if (!seen && !isspace((unsigned char)c)) { snprintf(top, cap, "a single value"); seen = 1; }
    }
    return !instr && depth == 0 && seen;
}

static void text_file(const FFile *f, int kind, FInfo *in) {
    size_t n;
    uint8_t *b = ff_load(f, 0, FF_MAX_LOAD, &n);
    if (!b) return;
    const char *enc;
    int non_ascii = 0;
    if (n >= 2 && ((b[0] == 0xFF && b[1] == 0xFE) || (b[0] == 0xFE && b[1] == 0xFF))) enc = b[0] == 0xFF ? "UTF-16 little-endian" : "UTF-16 big-endian";
    else if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) enc = "UTF-8 with a byte-order mark";
    else enc = utf8_valid(b, n, &non_ascii) ? (non_ascii ? "UTF-8" : "ASCII") : "8-bit (not UTF-8: Latin-1 or a DOS code page?)";
    info_add(in, "Text", "%s", enc);
    long lines = 0, longest = 0, cur = 0, crlf = 0, lf = 0, cr = 0, words = 0;
    int inword = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = b[i];
        if (c == '\n') { lines++; lf++; if (cur > longest) longest = cur; cur = 0; if (i && b[i - 1] == '\r') { crlf++; lf--; } }
        else if (c == '\r') { if (i + 1 >= n || b[i + 1] != '\n') { cr++; lines++; if (cur > longest) longest = cur; cur = 0; } }
        else if ((c & 0xC0) != 0x80) cur++;
        int w = !isspace(c);
        if (w && !inword) words++;
        inword = w;
    }
    if (cur) { lines++; if (cur > longest) longest = cur; }
    info_add(in, "Lines", "%ld, the longest %ld characters%s", lines, longest, (off_t)n < f->size ? " (in the first 32 MiB)" : "");
    info_add(in, "Words", "%ld", words);
    info_add(in, "Line ends", "%s", (crlf && (lf || cr)) || (lf && cr) ? "mixed" : crlf ? "CR LF (DOS, Windows)" : cr ? "CR (old Mac)" : lf ? "LF (Unix)" : "none");
    const char *s = (const char *)b;
    if (kind == K_JSON) {
        char top[32];
        int ok = json_check(s, n, top, sizeof top);
        info_add(in, "JSON", ok ? "well-formed, %s" : "not well-formed%s", ok ? top : "");
        char v[200];
        json_string_after(s, "\"format\"", v, sizeof v);
        if (v[0]) info_add(in, "Format", "%s", v);
    } else if (kind == K_CSV && n) {
        char sep = !strcmp(f->ext, "tsv") ? '\t' : ',';
        int cols = 1, q = 0;
        for (size_t i = 0; i < n && b[i] != '\n'; i++) { if (b[i] == '"') q = !q; else if (b[i] == sep && !q) cols++; }
        if (sep == ',' && cols == 1) { cols = 1; for (size_t i = 0; i < n && b[i] != '\n'; i++) if (b[i] == ';') cols++; }
        info_add(in, "Columns", "%d (from the first row)", cols);
    } else if ((kind == K_XML || kind == K_MUSICTEXT || kind == K_SVG) && n && s[strspn(s, " \t\r\n\xEF\xBB\xBF")] == '<') {
        const char *r = s;
        while ((r = strchr(r, '<')) != NULL && (r[1] == '?' || r[1] == '!')) r++;
        if (r) { size_t l = strcspn(r + 1, " >/\n\r\t"); if (l < 60) info_add(in, "Root element", "%.*s", (int)l, r + 1); }
        char v[232];
        if (xml_tag_text(s, "work-title", v, sizeof v)) info_add(in, "Title", "%s", v);
        if (xml_tag_text(s, "creator", v, sizeof v)) info_add(in, "Creator", "%s", v);
    } else if (kind == K_HTML) {
        char v[232];
        if (xml_tag_text(s, "title", v, sizeof v) || xml_tag_text(s, "TITLE", v, sizeof v)) info_add(in, "Title", "%s", v);
    } else if (kind == K_SCRIPT && n > 2 && b[0] == '#' && b[1] == '!') {
        size_t l = strcspn(s + 2, "\r\n");
        info_add(in, "Runs with", "%.*s", (int)(l < 200 ? l : 200), s + 2);
    }
    /* counts for a few formats with records */
    static const struct { const char *ext, *pat, *what; } counts[] = {
        {"abc", "\nX:", "Tunes"}, {"srt", " --> ", "Subtitles"}, {"vtt", " --> ", "Subtitles"}, {"ics", "BEGIN:VEVENT", "Events"},
        {"vcf", "BEGIN:VCARD", "Contacts"}, {"m3u", "\n", "Lines"}, {"cue", "TRACK ", "Tracks"}, {"md", "\n#", "Headings"},
    };
    for (size_t k = 0; k < sizeof counts / sizeof counts[0]; k++) {
        if (strcmp(f->ext, counts[k].ext)) continue;
        long c = 0;
        size_t pl = strlen(counts[k].pat);
        for (const char *p = s; (p = memmem(p, n - (size_t)(p - s), counts[k].pat, pl)) != NULL; p += pl) c++;
        if (!strcmp(f->ext, "abc") && !strncmp(s, "X:", 2)) c++;
        if (!strcmp(f->ext, "m3u")) { c = 0; for (const char *p = s; p < s + n; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : s + n) if (*p && *p != '#' && *p != '\n' && *p != '\r') c++; info_add(in, "Entries", "%ld", c); continue; }
        info_add(in, counts[k].what, "%ld", c);
    }
    if (!strcmp(f->ext, "abc")) { const char *ti = strstr(s, "\nT:"); if (ti || !strncmp(s, "T:", 2)) { ti = ti ? ti + 3 : s + 2; size_t l = strcspn(ti, "\r\n"); info_add(in, "Title", "%.*s", (int)(l < 200 ? l : 200), ti); } }
    free(b);
}

/* ---- SAUCE and KlamANSI ------------------------------------------------------------------------------------------------------- */

static void sauce_info(const FFile *f, FInfo *in) {
    uint8_t r[128];
    if (!ff_sauce(f, r)) return;
    info_field(in, "Title", r + 7, 35);
    info_field(in, "Artist", r + 42, 20);
    info_field(in, "Group", r + 62, 20);
    char d[12];
    ff_clean(d, sizeof d, r + 82, 8);
    if (strlen(d) == 8 && strcmp(d, "00000000")) info_add(in, "Date", "%.4s-%.2s-%.2s", d, d + 4, d + 6);
    static const char *types[] = {"none", "character", "bitmap", "vector", "audio", "binary text", "XBin", "archive", "executable"};
    int dt = r[94];
    if (dt < 9) info_add(in, "SAUCE type", "%s", types[dt]);
    if (dt == 1 || dt == 5 || dt == 6) {
        unsigned w = dt == 5 ? r[95] * 2u : rd16le(r + 96), h = rd16le(r + 98);
        if (w || h) info_add(in, "Characters", "%u x %u", w, h);
        if (r[105] & 1) info_add(in, "Colours", "iCE (16 backgrounds, no blinking)");
        info_field(in, "Font", r + 106, 22);
    }
    if (r[104]) info_add(in, "Comments", "%d lines", r[104]);
}

static void kan_info(const FFile *f, FInfo *in) {
    char buf[FF_HEAD + 1];
    memcpy(buf, f->head, f->hn);
    buf[f->hn] = 0;
    const char *sz = strstr(buf, "\nsize ");
    int w, h;
    if (sz && sscanf(sz + 6, "%d %d", &w, &h) == 2) info_add(in, "Characters", "%d x %d", w, h);
    info_add(in, "Format", "KlamANSI drawing script (%.*s)", (int)strcspn(buf, "\n"), buf);
}

/* ---- the entry points --------------------------------------------------------------------------------------------------------- */

static int ff_text_kind(int kind) {
    return kind == K_TEXT || kind == K_JSON || kind == K_XML || kind == K_CSV || kind == K_HTML || kind == K_EML || kind == K_MBOX ||
           kind == K_MUSICTEXT || kind == K_SVG || kind == K_SCRIPT || kind == K_RTF || kind == K_ANSI || kind == K_KAN || kind == K_OBJ || kind == K_GLTF;
}

/* The Info page. */
static void ff_info(const FFile *f, FType ty, FInfo *in) {
    char a[16];
    info_add(in, "Type", "%s", ty.label);
    info_add(in, "Category", "%s", ty.cat);
    fmt_bytes((unsigned long long)f->size, a, sizeof a);
    info_add(in, "Size", "%lld bytes (%s)", (long long)f->size, a);
    int k = ty.kind;
    if (info_media(f, k, in) || info_archive(f, k, in)) {
    } else if (k == K_PDF) pdf_file(f, in, NULL);
    else if (k == K_RTF) rtf_file(f, in, NULL);
    else if (k == K_EML || k == K_MBOX) mail_file(f, k, in, NULL);
    else if (k == K_ELF || k == K_APPIMAGE) elf_file(f, in, NULL);
    else if (k == K_PE) pe_file(f, in);
    else if (k == K_MACHO || k == K_FAT) macho_file(f, k, in);
    else if (k == K_CLASS && f->hn >= 8) { unsigned mj = rd16be(f->head + 6); info_add(in, "Needs", "Java %u or later (class version %u)", mj >= 49 ? mj - 44 : 1, mj); }
    else if (k == K_WASM && f->hn >= 8) info_add(in, "Version", "%u", rd32le(f->head + 4));
    else if (k == K_LUAC && f->hn >= 5) info_add(in, "Version", f->head[1] == 'L' ? "LuaJIT bytecode %d" : "Lua %d.%d", f->head[1] == 'L' ? f->head[3] : f->head[4] >> 4, f->head[4] & 15);
    else if (k == K_TTF) sfnt_file(f, 0, in);
    else if (k == K_TTC && f->hn >= 16) { info_add(in, "Fonts", "%u", rd32be(f->head + 8)); sfnt_file(f, rd32be(f->head + 12), in); }
    else if (k == K_WOFF) woff_file(f, in);
    else if (k == K_WOFF2 && f->hn >= 16) { info_add(in, "Tables", "%u", rd16be(f->head + 12)); info_add(in, "Note", "WOFF2 is Brotli-packed: its names are not read here"); }
    else if (k == K_PSF) psf_file(f, in);
    else if (k == K_SQLITE) sqlite_file(f, in, NULL);
    else if (k == K_DBF) dbf_file(f, in, NULL);
    else if (k == K_PARQUET && f->size > 12) { uint8_t t[8]; if (ff_read(f, f->size - 8, t, 8) == 8) { fmt_bytes(rd32le(t), a, sizeof a); info_add(in, "Metadata", "%s at the end", a); } }
    else if (k == K_STL || k == K_OBJ || k == K_GLB || k == K_GLTF) model_file(f, k, in);
    else if (k == K_TORRENT) torrent_file(f, in);
    else if (k == K_KAN) kan_info(f, in);
    else if (k == K_OLE) {
        size_t n;
        uint8_t *b = ff_load(f, 0, FF_MAX_LOAD, &n);
        static const struct { const char *u16; size_t l; const char *what; } subs[] = {{"W\0o\0r\0d\0D\0o\0c\0u\0m\0e\0n\0t\0", 24, "Word document (doc)"},
            {"W\0o\0r\0k\0b\0o\0o\0k\0", 16, "Excel workbook (xls)"}, {"P\0o\0w\0e\0r\0P\0o\0i\0n\0t\0 \0D\0", 24, "PowerPoint presentation (ppt)"}};
        for (size_t i = 0; b && i < 3; i++) if (memmem(b, n, subs[i].u16, subs[i].l)) { info_add(in, "Holds", "%s", subs[i].what); break; }
        free(b);
    }
    if (k == K_ANSI || k == K_BINART || k == K_XBIN || k == K_ADF || k == K_IDF || k == K_TND || ff_text_kind(k)) {
        uint8_t r[128];
        if (ff_sauce(f, r)) sauce_info(f, in);
    }
    if (ff_text_kind(k) && k != K_RTF && k != K_KAN && k != K_ANSI) text_file(f, k, in);
    if (k == K_XBIN && f->hn >= 11) {
        info_add(in, "Characters", "%u x %u", rd16le(f->head + 5), rd16le(f->head + 7));
        int fl = f->head[10];
        info_add(in, "Holds", "%s%s%s", (fl & 1) ? "a palette " : "", (fl & 2) ? "a font " : "", (fl & 4) ? "(packed)" : "");
    }
}

enum { FC_NONE, FC_LINES, FC_BYTES };

/* The Content page: lines of text (listings, document text, names), or raw bytes to show as text (a .gz). The page's name goes in
 * title ("Contents", "Text", ...). Pictures and art are not made here. */
static int ff_content(const FFile *f, FType ty, FText *t, uint8_t **raw, size_t *rawn, char *title, size_t tcap) {
    int k = ty.kind;
    *raw = NULL;
    *rawn = 0;
    snprintf(title, tcap, "Contents");
    if (content_archive(f, k, t, raw, rawn)) {
        if (*raw) { snprintf(title, tcap, "Unpacked"); return FC_BYTES; }
        if (k == K_OOXML || k == K_ODF || k == K_EPUB) snprintf(title, tcap, "Text");
        if (k == K_DISK) snprintf(title, tcap, "Partitions");
        if (k == K_DEB) snprintf(title, tcap, "Package");
        return FC_LINES;
    }
    switch (k) {
    case K_PDF: pdf_file(f, NULL, t); snprintf(title, tcap, "Text"); return FC_LINES;
    case K_RTF: rtf_file(f, NULL, t); snprintf(title, tcap, "Text"); return FC_LINES;
    case K_HTML: {
        size_t n;
        uint8_t *b = ff_load(f, 0, FF_MAX_LOAD, &n);
        if (b) { xml_to_text((const char *)b, n, t); free(b); }
        snprintf(title, tcap, "Text");
        return FC_LINES;
    }
    case K_MBOX: mail_file(f, k, NULL, t); snprintf(title, tcap, "Messages"); return FC_LINES;
    case K_MIDI: case K_RMID: midi_file(f, k, NULL, t); snprintf(title, tcap, "Names and words"); if (!t->n) text_line(t, "No names, lyrics or markers in this file."); return FC_LINES;
    case K_MOD: case K_S3M: case K_XM: case K_IT: case K_UMF: module_file(f, k, NULL, t); snprintf(title, tcap, k == K_UMF ? "Chunks" : "Instruments"); if (!t->n) text_line(t, "No names in this module."); return FC_LINES;
    case K_ELF: case K_APPIMAGE: { FInfo tmp = {0, 0, 0}; elf_file(f, &tmp, t); info_free(&tmp); snprintf(title, tcap, "Libraries"); if (!t->n) return FC_NONE; return FC_LINES; }
    case K_SQLITE: sqlite_file(f, NULL, t); snprintf(title, tcap, "Schema"); return FC_LINES;
    case K_DBF: dbf_file(f, NULL, t); snprintf(title, tcap, "Fields"); return FC_LINES;
    default: return FC_NONE;
    }
}

#endif
