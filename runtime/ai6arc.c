/* AI6WIN archive reader. Format documented by TesterTesterov/AI6WINArcTool.
 * Names remain CP932 bytes; ASCII resource lookup does not require conversion.
 *
 * Loose-file override
 * -------------------
 * Community patches for this game (Chinese translation, uncensor pack) ship as
 * loose files instead of repacked archives, and the original engine itself opens
 * a bare file name before falling back to the archive (CArc::Open 0x40d690 ->
 * CFile::Open 0x4275d0 -> CreateFileA). To honour both without unpacking or
 * repacking, a resource name resolves in this order:
 *
 *   1. <dir>/<stem>/<name>        e.g. ELFIMAGE/rmt/top.rmt   (translation layout)
 *   2. <dir>/<name>               e.g. ELFIMAGE/ev102g.vsd    (beside the archives)
 *   3. <dir>/../<name>            e.g. kisaku/ev102g.vsd       (PC game-root layout)
 *   4. <dir>/mods/<stem>/<name>   e.g. ELFIMAGE/mods/movie/ev102g.vsd
 *   5. the archive entry itself   (normal case: no override present)
 *
 * where <dir> is the directory holding the .arc and <stem> its name without the
 * extension. Matching is case-insensitive for both the archive entry and the
 * loose file (patch files use lowercase, archives mix case, and the original
 * engine upper-cases names via strupr before its archive lookup), so a directory
 * scan is used when the exact spelling is absent. */
#include "ai6arc.h"
#include "lzss.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
/* ASCII case folding; resource names are CP932 but lookups only need ASCII. */
static int fold(int c) { return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c; }
static int name_equal(const char *a, const char *b) {
    while (*a && *b) { if (fold((unsigned char)*a++) != fold((unsigned char)*b++)) return 0; }
    return *a == *b;
}
void ai6_close(Ai6Archive *a) {
    if (a->file) fclose(a->file);
    free(a->entries);
    memset(a, 0, sizeof(*a));
}
/* Split the archive path into its directory and extension-less stem. */
static void split_path(Ai6Archive *a, const char *path) {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    const char *base = slash ? slash + 1 : path;
    size_t dl = slash ? (size_t)(slash - path) : 0;
    if (dl >= sizeof(a->dir)) dl = sizeof(a->dir) - 1;
    memcpy(a->dir, path, dl);
    a->dir[dl] = 0;
    size_t sl = strcspn(base, "."), bl = strlen(base);
    if (sl == bl) sl = bl;  /* no extension */
    if (sl >= sizeof(a->stem)) sl = sizeof(a->stem) - 1;
    memcpy(a->stem, base, sl);
    a->stem[sl] = 0;
}
int ai6_open(Ai6Archive *a, const char *path) {
    uint8_t b[272];
    memset(a, 0, sizeof(*a));
    split_path(a, path);
    a->file = fopen(path, "rb");
    if (!a->file) return -1;
    if (fseek(a->file, 0, SEEK_END)) goto fail;
    long length = ftell(a->file);
    if (length < 4 || fseek(a->file, 0, SEEK_SET) || fread(b,1,4,a->file)!=4) goto fail;
    a->count = (uint32_t)b[0] | (uint32_t)b[1]<<8 | (uint32_t)b[2]<<16 | (uint32_t)b[3]<<24;
    uint64_t end = 4 + (uint64_t)a->count * 272;
    if (end > (uint64_t)length || a->count > 1000000) goto fail;
    a->entries = calloc(a->count ? a->count : 1, sizeof(*a->entries));
    if (!a->entries) goto fail;
    for (uint32_t i=0; i<a->count; i++) {
        if (fread(b,1,272,a->file)!=272) goto fail;
        Ai6Entry *e = &a->entries[i];
        size_t n=260;
        while (n && !b[n-1]) n--;
        if (!n) goto fail;
        for (size_t k=0;k<n;k++) {
            e->name[k]=(char)(uint8_t)(b[k]-(n-k+1));
            if (!e->name[k]) goto fail;
        }
        e->packed=be32(b+260); e->size=be32(b+264); e->offset=be32(b+268);
        if (e->offset < end || (uint64_t)e->offset+e->packed > (uint64_t)length) goto fail;
    }
    return 0;
fail:
    ai6_close(a);
    return -1;
}
int ai6_read(Ai6Archive *a, uint32_t index, uint8_t **data, size_t *size) {
    *data=NULL; *size=0;
    if (!a->file || index>=a->count) return -1;
    const Ai6Entry *e=&a->entries[index];
    if (e->size>256u*1024*1024 || e->packed>256u*1024*1024) return -1;
    uint8_t *in=malloc(e->packed ? e->packed : 1), *out=NULL;
    if (!in) return -1;
    if (fseek(a->file,e->offset,SEEK_SET) || fread(in,1,e->packed,a->file)!=e->packed) goto fail;
    if (e->packed==e->size) { *data=in; *size=e->size; return 0; }
    out=malloc(e->size ? e->size : 1);
    if (!out) goto fail;
    if (kawa_lzss(in,e->packed,out,e->size)) goto fail;
    free(in); *data=out; *size=e->size; return 0;
fail:
    free(in); free(out); return -1;
}
/* Case-insensitive lookup of <dir>/<name>, preferring the exact spelling. */
static int find_loose(const char *dir, const char *name, char *out, size_t outn) {
    if (snprintf(out,outn,"%s/%s",dir,name) >= (int)outn) return -1;
    FILE *f=fopen(out,"rb");
    if (f) { fclose(f); return 0; }
    DIR *d=opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    int found=-1;
    while ((e=readdir(d))) {
        if (!name_equal(e->d_name,name)) continue;
        if (snprintf(out,outn,"%s/%s",dir,e->d_name) < (int)outn) found=0;
        break;
    }
    closedir(d);
    return found;
}
int ai6_override_path(const Ai6Archive *a, const char *name, char *out, size_t outn) {
    if (!a->dir[0] || !name[0] || strchr(name,'/') || strchr(name,'\\')) return -1;
    char dir[2100];
    /* 1. Translation layout: loose assets in a per-archive subdirectory, which is
     *    where the Chinese patch keeps its images (rmt/ next to rmt.arc). */
    if (a->stem[0] && snprintf(dir,sizeof(dir),"%s/%s",a->dir,a->stem) < (int)sizeof(dir) &&
        find_loose(dir,name,out,outn)==0) return 0;
    /* 2. Beside the archives; equivalent to the original engine's bare-name probe. */
    if (find_loose(a->dir,name,out,outn)==0) return 0;
    /* 3. PC layout: the game root that contains ELFIMAGE/, so a patch copied from
     *    the Windows release works unchanged. */
    if (snprintf(dir,sizeof(dir),"%s/..",a->dir) < (int)sizeof(dir) && find_loose(dir,name,out,outn)==0) return 0;
    /* 4. Generic mod directory. */
    if (a->stem[0] && snprintf(dir,sizeof(dir),"%s/mods/%s",a->dir,a->stem) < (int)sizeof(dir) &&
        find_loose(dir,name,out,outn)==0) return 0;
    return -1;
}
static int read_file(const char *path, uint8_t **data, size_t *size) {
    *data=NULL; *size=0;
    FILE *f=fopen(path,"rb");
    if (!f) return -1;
    if (fseek(f,0,SEEK_END)) { fclose(f); return -1; }
    long length=ftell(f);
    if (length<0 || (unsigned long)length>256u*1024*1024 || fseek(f,0,SEEK_SET)) { fclose(f); return -1; }
    uint8_t *p=malloc(length ? (size_t)length : 1);
    if (!p) { fclose(f); return -1; }
    if (length && fread(p,1,(size_t)length,f)!=(size_t)length) { free(p); fclose(f); return -1; }
    fclose(f); *data=p; *size=(size_t)length; return 0;
}
int ai6_read_named(Ai6Archive *a, const char *name, uint8_t **data, size_t *size) {
    char path[4096];
    if (ai6_override_path(a,name,path,sizeof(path))==0) return read_file(path,data,size);
    for (uint32_t i=0;i<a->count;i++) if (name_equal(name,a->entries[i].name)) return ai6_read(a,i,data,size);
    return -1;
}
