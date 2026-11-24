/* Commander: which colour a name gets in a panel (the true-colour themes, theme/r3i6nos_themes.h). By name and mode only: nothing is opened
 * or read, so a folder of thousands of files costs nothing. A kind is an R3_FK_* from the theme header.
 *
 * Order: a folder, then a symbolic link, then a name that starts with a dot (hidden), then the extension (last one, any case), else plain.
 * The execute bit is NOT used: the games card is exFAT or FAT, which shows every file as executable. The kinds follow the viewer's own
 * groups (pictures and art, sound and tracker music, video, documents, archives, disk images, packages and ROMs, programs, source and data). */
#ifndef COMMANDER_FILEKIND_H
#define COMMANDER_FILEKIND_H

#include <string.h>
#include <strings.h>

static const struct { int kind; const char *exts; } FK_EXTS[] = {
    {R3_FK_ARCHIVE, " zip 7z rar tar gz tgz bz2 tbz tbz2 xz txz zst lz lzma lz4 lzh lha arj cab z zoo arc sit cpio ace jar war pk3 pak wad "},
    {R3_FK_IMAGE, " png jpg jpeg jpe gif bmp webp tif tiff tga pcx ico cur psd ase aseprite xpm xbm svg heic avif iff lbm ilbm sgi ppm pgm pbm pnm "
                  "dds qoi hdr ans ansi xb idf pcb ice "},
    {R3_FK_AUDIO, " wav mp3 mp2 ogg oga opus flac m4a aac wma aif aiff au voc mid midi kar rmi mod xm s3m it 669 med mtm stm ult umx nsf nsfe spc sid "
                  "vgm vgz gbs hes kss ay sap sndh ym rmt fur mds mgs gym "},
    {R3_FK_VIDEO, " mp4 m4v mkv avi mov webm wmv asf flv mpg mpeg ogv 3gp vob r3f "},
    {R3_FK_DOC, " txt md markdown rst pdf doc docx odt rtf epub html htm csv tsv log man tex ps chm djvu ods xls xlsx ppt pptx odp asc 1st nfo diz "},
    {R3_FK_CODE, " c h cc cpp cxx hpp cs java js mjs ts py rb pl pm lua sh bash zsh fish go rs php asm s inc json yaml yml toml ini cfg conf xml css "
                 "sql mk cmake make lock gd cl lisp scm hs ml swift kt gradle diff patch "},
    {R3_FK_DISK, " iso img cue nrg dmg vhd vdi vmdk qcow2 d64 d71 d81 adf dsk st woz nib crt deb rpm pkg apk ipk rom sfc smc nes gb gbc gba gen sms "
                 "gg pce z64 n64 nds 32x a26 a78 "},
    {R3_FK_EXEC, " exe com bat cmd appimage run elf dll sys scr msi "},
};

static inline int fk_of(const char *name, int is_dir, int is_link) {
    if (is_dir) return R3_FK_DIR;
    if (is_link) return R3_FK_LINK;
    if (name[0] == '.') return R3_FK_HIDDEN;
    const char *dot = strrchr(name, '.');
    if (!dot || !dot[1] || strlen(dot + 1) > 9) return R3_FK_PLAIN;
    char key[16];
    int n = 0;
    key[n++] = ' ';
    for (const char *c = dot + 1; *c; c++) key[n++] = (char)((*c >= 'A' && *c <= 'Z') ? *c + 32 : *c);
    key[n++] = ' ';
    key[n] = 0;
    for (size_t i = 0; i < sizeof FK_EXTS / sizeof FK_EXTS[0]; i++)
        if (strstr(FK_EXTS[i].exts, key)) return FK_EXTS[i].kind;
    return R3_FK_PLAIN;
}

#ifdef R3_WITH_MONO
/* The R3i6nos Mono icon (two cells, a string to print as one) of a name in a panel: the kind's picture, finer where the kinds are broad (a MIDI file, a tracker
 * module, a text file, a font). The ".." row is the open folder. NULL when the font has no such name (it always does: tests/test_r3font.py). */
static inline const char *fk_icon_name(const char *name, int is_dir, int is_link) {
    if (is_dir) return !strcmp(name, "..") ? "icon.file.folder.open" : "icon.file.folder";
    if (is_link) return "icon.file.link";
    if (name[0] == '.') return "icon.file";
    const char *dot = strrchr(name, '.');
    char key[16] = " ";
    if (dot && dot[1] && strlen(dot + 1) <= 9) {
        int n = 1;
        for (const char *c = dot + 1; *c; c++) key[n++] = (char)((*c >= 'A' && *c <= 'Z') ? *c + 32 : *c);
        key[n++] = ' ';
        key[n] = 0;
    } else key[0] = 0;
    if (key[0]) {
        if (strstr(" mid midi kar rmi ", key)) return "icon.file.midi";
        if (strstr(" mod xm s3m it 669 med mtm stm ult umx fur mds ", key)) return "icon.file.tracker";
        if (strstr(" txt md markdown rst log nfo diz asc 1st man ", key)) return "icon.file.text";
        if (strstr(" ttf otf woff woff2 bdf pcf psf fnt ", key)) return "icon.file.font";
        if (strstr(" csv tsv json yaml yml toml ini cfg conf xml sql ", key)) return "icon.file.data";
    }
    switch (fk_of(name, 0, 0)) {
    case R3_FK_ARCHIVE: return "icon.file.archive";
    case R3_FK_IMAGE: return "icon.file.image";
    case R3_FK_AUDIO: return "icon.file.music";
    case R3_FK_VIDEO: return "icon.file.video";
    case R3_FK_DOC: return "icon.file.doc";
    case R3_FK_CODE: return "icon.file.code";
    case R3_FK_DISK: return "icon.file.disk";
    case R3_FK_EXEC: return "icon.file.exec";
    default: return "icon.file";
    }
}

/* The icon string for r3_puts_mono, or NULL. */
static inline const char *file_icon(const char *name, int is_dir, int is_link) {
    return r3mono_name(fk_icon_name(name, is_dir, is_link));
}
#endif

#endif /* COMMANDER_FILEKIND_H */
