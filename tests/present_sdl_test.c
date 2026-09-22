/* Actual SDL GLES renderer + native menu builders, after the custom pass. */
#include <GLES2/gl2.h>
#define KPRESENT_GLES_TEST
#define main kisaku_viewer_main
#include "../tools/runtime_viewer.c"
#undef main
#include <assert.h>
static void read_pixels(SDL_Renderer *r,uint8_t *out){
    SDL_Rect area={160,0,960,720};
    assert(!SDL_RenderReadPixels(r,&area,SDL_PIXELFORMAT_BGRA32,out,960*4));
}
int main(int argc,char **argv){
    assert(argc==4);assert(!SDL_Init(SDL_INIT_VIDEO));
    SDL_Window *w=SDL_CreateWindow("GLES panel regression",0,0,1280,720,SDL_WINDOW_HIDDEN);assert(w);
    SDL_Renderer *r=SDL_CreateRenderer(w,-1,0);assert(r);SDL_RendererInfo info;assert(!SDL_GetRendererInfo(r,&info));
    assert(strstr(info.name,"opengles2"));printf("Actual SDL renderer: %s\n",info.name);
    KBootstrap *b=bootstrap_create_split(argv[1],argv[2]);assert(b&&!b->error[0]);
    for(unsigned i=0;!b->title.active||b->title.age<64;i++){assert(i<3000&&bootstrap_run(b,100000)>=0);bootstrap_frame(b);}
    const char *shot=getenv("KISAKU_FILTER_SHOTS");
    if(shot){
        SDL_Rect area={160,0,960,720};char path[4096];
        SDL_Texture *linear=kimage_texture(r,&b->layers[0]);assert(linear);
        assert(!SDL_SetTextureScaleMode(linear,SDL_ScaleModeLinear));
        assert(!SDL_RenderCopy(r,linear,NULL,&area));
        snprintf(path,sizeof(path),"%s-linear.bmp",shot);assert(!capture(r,path));
        KPresentGles preview={0};assert(!present_gles_draw(&preview,r,&b->layers[0],&area,0));
        snprintf(path,sizeof(path),"%s-cubic.bmp",shot);assert(!capture(r,path));
        preview.edge_strength=55;assert(!present_gles_draw(&preview,r,&b->layers[0],&area,0));
        snprintf(path,sizeof(path),"%s-edge.bmp",shot);assert(!capture(r,path));
        present_gles_clear(&preview);SDL_DestroyTexture(linear);
    }
    KImage source={0,0,640,480,2560,calloc(640*480,4)};assert(source.pixels);
    for(unsigned i=0;i<640*480;i++){source.pixels[4*i]=23;source.pixels[4*i+1]=53;source.pixels[4*i+2]=201;source.pixels[4*i+3]=255;}
    uint8_t *expected=malloc(960*720*4),*actual=malloc(960*720*4);assert(expected&&actual);
    const unsigned kinds[]={22,9,23,20,5,0};const char *names[]={"CG","music","video","scene appreciation","history","load/save"};
    KPresentGles pass={.edge_strength=55};SDL_Rect dst={160,0,960,720};
    SDL_Texture *background=kimage_texture(r,&source);assert(background);
    assert(!SDL_SetTextureBlendMode(background,SDL_BLENDMODE_NONE));
    for(unsigned test=0;test<6;test++){
        MessagePanel panel={.kind=kinds[test]};SaveMenu menu={0};
        if(panel.kind==22){b->title.active=0;b->vm->status=KVM_SYSCALL;b->vm->syscall=31;b->vm->sp=0;assert(!kvm_push(b->vm,(KValue){310,NULL}));assert(!bootstrap_dispatch(b)&&b->native_cg);}
        if(!panel.kind)save_menu_open(&menu,b,1);
        assert(!SDL_RenderCopy(r,background,NULL,&dst));
        if(panel.kind)message_panel_draw(&panel,b,r);else save_menu_draw(&menu,b,r);
        assert(panel.kind?(panel.texture!=NULL||(panel.kind==22&&b->native_cg!=NULL)):menu.texture!=NULL);read_pixels(r,expected);
        for(unsigned frame=0;frame<12;frame++){
            assert(!present_gles_draw(&pass,r,&source,&dst,50));
            if(panel.kind)message_panel_draw(&panel,b,r);else save_menu_draw(&menu,b,r);
            read_pixels(r,actual);
            unsigned diff=0;for(unsigned i=0;i<960*720*4;i++)diff+=actual[i]!=expected[i];
            if(diff)fprintf(stderr,"%s frame %u: %u mismatched channels\n",names[test],frame,diff);
            assert(!diff);
        }
        if(panel.kind==22)assert(panel.cg_frame.uploads==1);
        printf("%s: 12 real SDL GLES frames after postprocess match reference exactly: PASS\n",names[test]);
        save_menu_clear(&menu);gallery_panel_clear(&panel);
        rmt_free(&panel.image);rmt_free(&panel.appendix_artwork);rmt_free(&panel.appendix_parts);
        rmt_free(&panel.direct_artwork);rmt_free(&panel.direct_parts);rmt_free(&panel.direct_thumb);
        rmt_free(&panel.history_artwork);SDL_DestroyTexture(panel.texture);texture_cache_clear(&panel.cg_frame);
    }
    KImage letters={0,0,960,720,3840,calloc(960*720,4)};assert(letters.pixels);
    KFont *font=kfont_open(argv[3],0);assert(font);
    const uint32_t glyphs[]={0x65e5,0x672c,0x8a9e,0x6587,0x5b57,0x5c65,0x6b74};
    for(unsigned i=0;i<7;i++)assert(!kfont_draw(font,&letters,glyphs[i],40+(int)i*30,610,24,24,0xffffff));
    unsigned ink=0;for(unsigned i=0;i<960*720;i++)ink+=letters.pixels[4*i+3]!=0;assert(ink>200);
    KTextureCache hq={0};assert(!texture_cache_update(&hq,r,&letters,SDL_BLENDMODE_BLEND));
    assert(!SDL_RenderCopy(r,background,NULL,&dst)&&!SDL_RenderCopy(r,hq.texture,NULL,&dst));read_pixels(r,expected);
    for(unsigned frame=0;frame<30;frame++){
        assert(!texture_cache_update(&hq,r,&letters,SDL_BLENDMODE_BLEND));
        assert(!present_gles_draw(&pass,r,&source,&dst,50));
        assert(!SDL_RenderCopy(r,hq.texture,NULL,&dst));read_pixels(r,actual);
        assert(!memcmp(actual,expected,960*720*4));
    }
    printf("960x720 HQ text: %u ink pixels, 30 compositions / 1 upload match exactly: PASS\n",ink);
    assert(hq.uploads==1&&hq.bytes==960u*720u*4u);
    letters.pixels[10*letters.stride+11*4]=15;letters.pixels[10*letters.stride+11*4+3]=255;
    assert(!texture_cache_update(&hq,r,&letters,SDL_BLENDMODE_BLEND));
    assert(hq.uploads==2&&hq.bytes==960u*721u*4u);
    SDL_Texture *full=kimage_texture(r,&letters);assert(full);
    assert(!SDL_RenderCopy(r,background,NULL,&dst)&&!SDL_RenderCopy(r,full,NULL,&dst));read_pixels(r,expected);
    assert(!SDL_RenderCopy(r,background,NULL,&dst)&&!SDL_RenderCopy(r,hq.texture,NULL,&dst));read_pixels(r,actual);
    assert(!memcmp(expected,actual,960*720*4));

    letters.pixels[10*letters.stride+11*4]=0;letters.pixels[10*letters.stride+11*4+3]=0;
    letters.pixels[719*letters.stride+5*4+3]=127;
    assert(!texture_cache_update(&hq,r,&letters,SDL_BLENDMODE_BLEND)&&hq.uploads==3);
    assert(!memcmp(hq.copy.pixels,letters.pixels,letters.height*letters.stride));
    assert(!SDL_UpdateTexture(full,NULL,letters.pixels,letters.stride));
    assert(!SDL_RenderCopy(r,background,NULL,&dst)&&!SDL_RenderCopy(r,full,NULL,&dst));read_pixels(r,expected);
    assert(!SDL_RenderCopy(r,background,NULL,&dst)&&!SDL_RenderCopy(r,hq.texture,NULL,&dst));read_pixels(r,actual);
    assert(!memcmp(expected,actual,960*720*4));SDL_DestroyTexture(full);
    puts("HQ partial upload: changed row, erased glyph and bottom row match full GPU upload: PASS");

    texture_cache_clear(&hq);SDL_DestroyTexture(background);rmt_free(&letters);kfont_close(font);
    present_gles_clear(&pass);free(source.pixels);free(expected);free(actual);bootstrap_destroy(b);
    SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();return 0;
}
