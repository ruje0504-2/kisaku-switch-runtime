/* CDiaryWnd 4beab0: reset records (4bdc80), resize scroll range
   (4bdd40), redraw the complete backing bitmap (4be480). */
static int diary_reset(KBootstrap *b){
    KImage atlas={0},surface={0};uint8_t *data=NULL;size_t size=0;
    if(read_named(&b->images,"diary.akb",&data,&size)||rmt_decode(data,size,&atlas)){
        free(data);return error(b,"CDiaryWnd atlas decode failed (arguments preserved)");
    }
    free(data);
    if(atlas.width<608||atlas.height<796){rmt_free(&atlas);return error(b,"CDiaryWnd atlas bounds (arguments preserved)");}
    surface=(KImage){0,0,608,1936,608*4,calloc(1936,608*4)};
    if(!surface.pixels){rmt_free(&atlas);return error(b,"CDiaryWnd allocation failed (arguments preserved)");}
    /* Source coordinates read from 542828. The second day sequence starts
       at entry 13 with the second day, as in the original table. */
    static const unsigned days[24][2]={{0,16},{120,16},{240,16},{360,16},{480,16},
        {0,96},{120,96},{240,96},{360,96},{480,96},{0,176},{120,176},{240,176},
        {120,16},{240,16},{360,16},{480,16},{0,96},{120,96},{240,96},{360,96},{480,96},{0,176},{120,176}};
    for(unsigned y=0;y<16;y++)memcpy(surface.pixels+y*surface.stride,atlas.pixels+y*atlas.stride,608*4);
    for(unsigned day=0;day<24;day++){
        for(unsigned y=0;y<80;y++)memcpy(surface.pixels+(16+day*80+y)*surface.stride,
            atlas.pixels+(days[day][1]+y)*atlas.stride+days[day][0]*4,120*4);
        for(unsigned row=0;row<4;row++)for(unsigned y=0;y<20;y++){
            uint8_t *dst=surface.pixels+(16+(day*4+row)*20+y)*surface.stride;
            memcpy(dst+120*4,atlas.pixels+(376+y)*atlas.stride,152*4);
            memcpy(dst+272*4,atlas.pixels+(776+y)*atlas.stride+152*4,336*4);
        }
    }
    rmt_free(&atlas);rmt_free(&b->diary_surface);b->diary_surface=surface;
    memset(b->diary_people,0,sizeof(b->diary_people));memset(b->diary_events,0,sizeof(b->diary_events));
    b->diary_days=1;b->diary_content_height=96;
    if(b->diary_viewport_height>96)b->diary_viewport_height=96;
    if(b->diary_scroll+b->diary_page_height>96)b->diary_scroll=96-b->diary_page_height;
    return 0;
}
