#include "font.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"usage: %s FONT\n",argv[0]);return 2;}
    KFont *font=kfont_open(argv[1],0);assert(font);
    KImage image={0,0,512,96,512*4,calloc(512*96,4)};assert(image.pixels);
    const uint32_t codepoints[]={0x9b3c,0x4f5c,0x4e2d,0x6587,0x3042,0x30a2};
    for(unsigned i=0;i<sizeof(codepoints)/sizeof(codepoints[0]);i++)
        assert(!kfont_draw(font,&image,codepoints[i],(int)i*80,0,24,24,0xffffff));
    KFontCacheStats first={0};assert(!kfont_cache_stats(font,&first));
    /* Repeated high-resolution text must reuse coverage/metrics while colour
       remains a property of the destination draw. */
    for(unsigned i=0;i<sizeof(codepoints)/sizeof(codepoints[0]);i++)
        assert(!kfont_draw(font,&image,codepoints[i],(int)i*80,24,24,24,0xffc040));
    KFontCacheStats second={0};assert(!kfont_cache_stats(font,&second));
    assert(second.rasterizations==first.rasterizations);
    assert(second.hits>=first.hits+sizeof(codepoints)/sizeof(codepoints[0]));
    assert(!kfont_draw_outline(font,&image,codepoints[0],0,48,24,24,0xffffff));
    KFontCacheStats outlined={0};assert(!kfont_cache_stats(font,&outlined));
    assert(outlined.rasterizations==second.rasterizations);
    assert(outlined.glyphs<=outlined.glyph_limit&&outlined.bitmap_bytes<=outlined.bitmap_limit);
    size_t nonzero=0;for(size_t i=0;i<512*96*4;i++)if(image.pixels[i])nonzero++;
    assert(nonzero>0);
    free(image.pixels);kfont_close(font);
    puts("Kisaku supplied OTF: Japanese/Chinese glyph render: PASS");
    return 0;
}
