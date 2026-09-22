#include "font.h"
#include <stdlib.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#ifdef __SWITCH__
#include <switch.h>
#endif
struct KFont {FT_Library library;FT_Face faces[3];unsigned count,shared;};
/* Nintendo's shared fonts are returned as raw SFNT/TTC memory.  FreeType
 * normally selects Unicode automatically, but some Horizon revisions expose
 * a platform-default charmap first; that leaves FT_Get_Char_Index returning
 * zero for every CP932 code point even though the glyphs are present.  Keep
 * the face usable for both the HOS font service and host TTC fallbacks by
 * explicitly selecting Unicode whenever it is available. */
static void select_unicode(FT_Face face){
    if(!face)return;
    /* Do not trust FreeType's first Unicode alias on a raw HOS SFNT/TTC:
       firmware revisions may expose both a BMP and a UCS-4 Microsoft cmap,
       and the alias is not stable.  Prefer the concrete Microsoft maps,
       then Apple's Unicode map, then the first advertised map. */
    FT_CharMap candidate=NULL;
    for(int pass=0;pass<3&&!candidate;pass++)for(int i=0;i<face->num_charmaps;i++){
        FT_CharMap map=face->charmaps[i];
        if(pass==0&&map->platform_id==3&&(map->encoding_id==10||map->encoding_id==1))candidate=map;
        else if(pass==1&&map->platform_id==0)candidate=map;
        else if(pass==2&&!candidate)candidate=map;
    }
    if(candidate)(void)FT_Set_Charmap(face,candidate);
    else (void)FT_Select_Charmap(face,FT_ENCODING_UNICODE);
}
/* A shared font can contain a valid Unicode cmap with only part of the
   repertoire.  Probe the remaining subtables before declaring a glyph
   missing; this is needed for HOS Japanese/Chinese fallback faces and keeps
   history/name panels visible when the preferred cmap is sparse. */
static FT_UInt glyph_index(FT_Face face,uint32_t codepoint){
    FT_UInt index=FT_Get_Char_Index(face,codepoint);if(index)return index;
    FT_CharMap saved=face->charmap;
    for(int i=0;i<face->num_charmaps;i++){
        FT_CharMap map=face->charmaps[i];if(map==saved)continue;
        if(FT_Set_Charmap(face,map))continue;
        index=FT_Get_Char_Index(face,codepoint);if(index)return index;
    }
    if(saved)(void)FT_Set_Charmap(face,saved);
    return 0;
}
void kfont_close(KFont *f){
    if(!f)return;
    for(unsigned i=0;i<f->count;i++)FT_Done_Face(f->faces[i]);
    if(f->library)FT_Done_FreeType(f->library);
#ifdef __SWITCH__
    if(f->shared)plExit();
#endif
    free(f);
}
KFont *kfont_open(const char *path,int simplified){
    KFont *f=calloc(1,sizeof(*f));if(!f)return NULL;
    if(FT_Init_FreeType(&f->library)){kfont_close(f);return NULL;}
    if(path&&*path){
        if(FT_New_Face(f->library,path,0,&f->faces[0])){kfont_close(f);return NULL;}
        select_unicode(f->faces[0]);
        f->count=1;
    }else {
#ifdef __SWITCH__
        if(R_FAILED(plInitialize(PlServiceType_User))){kfont_close(f);return NULL;}f->shared=1;
        PlSharedFontType types[]={simplified?PlSharedFontType_ChineseSimplified:PlSharedFontType_Standard,simplified?PlSharedFontType_Standard:PlSharedFontType_ChineseSimplified,PlSharedFontType_ExtChineseSimplified};
        for(unsigned i=0;i<3;i++){
            PlFontData data;
            if(R_SUCCEEDED(plGetSharedFontByType(&data,types[i]))&&!FT_New_Memory_Face(f->library,data.address,data.size,0,&f->faces[f->count])){
                select_unicode(f->faces[f->count]);
                f->count++;
            }
        }
#elif defined(__APPLE__)
        (void)simplified;
        if(!FT_New_Face(f->library,"/System/Library/Fonts/Hiragino Sans GB.ttc",0,&f->faces[0])){select_unicode(f->faces[0]);f->count=1;}
#else
        (void)simplified;
#endif
    }
    if(!f->count){kfont_close(f);return NULL;}return f;
}
int kfont_draw(KFont *f,KImage *dst,uint32_t cp,int x,int y,unsigned width,unsigned height,uint32_t rgb){
    if(!f||!dst||!dst->pixels||dst->stride<(size_t)dst->width*4||width<1||height<1||width>256||height>256)return -1;
    FT_Face face=NULL;FT_UInt index=0;
    for(unsigned i=0;i<f->count;i++)if((index=glyph_index(f->faces[i],cp))){face=f->faces[i];break;}
    if(!face||FT_Set_Pixel_Sizes(face,width,height)||FT_Load_Glyph(face,index,FT_LOAD_RENDER|FT_LOAD_TARGET_NORMAL))return -1;
    FT_GlyphSlot g=face->glyph;FT_Bitmap *bitmap=&g->bitmap;
    if(bitmap->pixel_mode!=FT_PIXEL_MODE_GRAY&&bitmap->pixel_mode!=FT_PIXEL_MODE_MONO)return -1;
    int64_t left=(int64_t)x+g->bitmap_left,top=(int64_t)y+(face->size->metrics.ascender>>6)-g->bitmap_top;
    for(unsigned row=0;row<bitmap->rows;row++){
        int64_t yy=top+row;if(yy<0||yy>=dst->height)continue;
        const uint8_t *source=bitmap->buffer+(bitmap->pitch>=0?row:bitmap->rows-row-1)*(size_t)abs(bitmap->pitch);
        for(unsigned col=0;col<bitmap->width;col++){
            int64_t xx=left+col;if(xx<0||xx>=dst->width)continue;
            unsigned alpha=bitmap->pixel_mode==FT_PIXEL_MODE_MONO?((source[col/8]&(0x80>>(col%8)))?255:0):source[col];
            if(!alpha)continue;
            uint8_t *p=dst->pixels+(size_t)yy*dst->stride+(size_t)xx*4;
            /* Straight alpha source-over, preserving transparent text layers. */
            unsigned out_alpha=alpha+p[3]*(255-alpha)/255;
            for(unsigned c=0;c<3;c++)p[c]=(uint8_t)((((rgb>>(8*c))&255)*alpha+p[c]*p[3]*(255-alpha)/255)/out_alpha);
            p[3]=(uint8_t)out_alpha;
        }
    }
    return 0;
}
