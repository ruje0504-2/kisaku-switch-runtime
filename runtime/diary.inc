/* CDiaryWnd 4be480 / 4bdd40. Build before publishing so allocation failures
   cannot leave a partially updated diary.  The values stored in the diary
   arrays are script record ids, while the atlas only contains a finite set
   of visible rows.  The original keeps records that have no visible atlas
   row; those records must render as blank instead of stopping the VM. */
static int diary_draw(KBootstrap *b,const int32_t people[96],const int32_t events[96],unsigned days_count){
    if(days_count<1||days_count>24)return error(b,"CDiaryWnd day range (arguments preserved)");
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
            unsigned index=day*4+row;
            /* 7/27 are the last atlas coordinates accepted by the native
               layout; zero and all future/unknown record ids use its blank
               row.  Keep the original ids in the VM and save data. */
            unsigned person=(people[index]>0&&people[index]<=7)?(unsigned)people[index]:0;
            unsigned event=(events[index]>0&&events[index]<=27)?(unsigned)events[index]:0;
            unsigned py=person?236+person*20:376;
            unsigned ey=event?236+event*20:776;
            uint8_t *dst=surface.pixels+(16+index*20+y)*surface.stride;
            memcpy(dst+120*4,atlas.pixels+(py+y)*atlas.stride,152*4);
            memcpy(dst+272*4,atlas.pixels+(ey+y)*atlas.stride+152*4,336*4);
        }
    }
    rmt_free(&atlas);rmt_free(&b->diary_surface);b->diary_surface=surface;
    memcpy(b->diary_people,people,sizeof(b->diary_people));memcpy(b->diary_events,events,sizeof(b->diary_events));
    b->diary_days=(int)days_count;b->diary_content_height=16+(int)days_count*80;
    if(b->diary_viewport_height>b->diary_content_height)b->diary_viewport_height=b->diary_content_height;
    if(b->diary_scroll+b->diary_page_height>b->diary_content_height)b->diary_scroll=b->diary_content_height-b->diary_page_height;
    return 0;
}
/* 4beab0 clears private records, not the VM words. */
static int diary_reset(KBootstrap *b){
    int32_t empty[96]={0};return diary_draw(b,empty,empty,1);
}
/* 4be9e0 counts nonzero people then divides by four (not round-up). */
static int diary_restore(KBootstrap *b,const uint16_t *words){
    int32_t people[96],events[96];unsigned count=0;
    for(unsigned i=0;i<96;i++){people[i]=words[200+i];events[i]=words[300+i];count+=people[i]!=0;}
    unsigned days=count/4;return diary_draw(b,people,events,days?days:1);
}
