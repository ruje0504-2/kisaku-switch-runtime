#include "video.h"
#include "ai6arc.h"
#include "mov.h"
#include "media_tables.h"
#include "../build/media_tables.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t hash(const uint8_t *p,size_t n){uint64_t h=1469598103934665603ULL;while(n--)h=(h^*p++)*1099511628211ULL;return h;}
static void test_video(const uint8_t *data,size_t size,int range){
    KVideo *v=kvideo_open_mode(data,size,KVIDEO_SOFTWARE),*auto_v=kvideo_open_mode(data,size,KVIDEO_AUTO);assert(v&&auto_v);
    uint8_t *pcm=NULL,*auto_pcm=NULL;size_t bytes=0,auto_bytes=0;
    KImage a={0,0,640,480,2560,calloc(480,2560)},b={0,0,640,480,2560,calloc(480,2560)};assert(a.pixels&&b.pixels);
    uint64_t frames[24];unsigned count=0;
    if(range){assert(!kvideo_range(v,0,10));assert(!kvideo_range(auto_v,0,10));}
    for(unsigned i=0;i<(range?24:90);i++){
        int rc=kvideo_step(v,&a,&pcm,&bytes),ac=kvideo_step(auto_v,&b,&auto_pcm,&auto_bytes);
        assert(rc>=0&&rc==ac&&bytes==auto_bytes&&!memcmp(a.pixels,b.pixels,480*2560));
        if(bytes)assert(!memcmp(pcm,auto_pcm,bytes));
        if(rc==1)break;
        if(range)frames[count++]=hash(a.pixels,480*2560);
    }
    KVideoStats stats;assert(!kvideo_stats(v,&stats)&&stats.software_frames&&!stats.hardware_frames);
    assert(!kvideo_stats(auto_v,&stats)&&!stats.hardware_frames&&stats.fallback[0]);
    if(range){
        assert(count==20&&stats.cached_frames);
        uint64_t decoded=kvideo_decoded_frames(v);assert(!kvideo_range(v,0,10));
        for(unsigned i=0;i<count;i++){assert(!kvideo_step(v,&a,&pcm,&bytes));assert(hash(a.pixels,480*2560)==frames[i]);}
        assert(kvideo_step(v,&a,&pcm,&bytes)==1&&kvideo_decoded_frames(v)==decoded);
        assert(!kvideo_range(v,10,20)&&!kvideo_range(auto_v,10,20));
        for(unsigned i=0;i<20;i++){assert(!kvideo_step(v,&a,&pcm,&bytes)&&!kvideo_step(auto_v,&b,&auto_pcm,&auto_bytes));assert(!memcmp(a.pixels,b.pixels,480*2560));}
    }else assert(bytes>0);
    kvideo_close(v);kvideo_close(auto_v);free(a.pixels);free(b.pixels);free(pcm);free(auto_pcm);
}
static void test_video_slots(Ai6Archive *arc){
    KImage image={0,0,640,480,2560,calloc(480,2560)};assert(image.pixels);
    for(unsigned i=0;i<78;i++){
        uint8_t *commands=NULL,*data=NULL;size_t command_size=0,size=0;
        assert(!ai6_read_named(arc,kisaku_video_previews[i],&commands,&command_size));
        KMov mov={0};
        if(kmov_open(&mov,commands,command_size,0)){fprintf(stderr,"video slot %u invalid MOV %s: %s\n",i,kisaku_video_previews[i],mov.error);abort();}
        assert(!ai6_read_named(arc,mov.video,&data,&size));
        KVideo *v=kvideo_open_mode(data,size,KVIDEO_SOFTWARE);
        if(!v){fprintf(stderr,"video slot %u failed to open: %s\n",i,kisaku_video_previews[i]);abort();}
        uint8_t *pcm=NULL;size_t bytes=0;
        unsigned segments=0;
        for(unsigned event_count=0;event_count<20&&segments<2;event_count++){
            int event=kmov_next(&mov);assert(event>=0);
            if(!event)break;
            if(event==1){
                assert(!kvideo_range(v,mov.first,mov.last));
                int rc=kvideo_step(v,&image,&pcm,&bytes);
                if(rc<0){fprintf(stderr,"video slot %u failed to decode: %s\n",i,kisaku_video_previews[i]);abort();}
                segments++;
            }
        }
        assert(segments);
        free(pcm);kvideo_close(v);free(data);free(commands);
    }
    free(image.pixels);
}
int main(int argc,char **argv){
    assert(argc==2);char path[4096];snprintf(path,sizeof(path),"%s/movie.arc",argv[1]);
    Ai6Archive arc={0};assert(!ai6_open(&arc,path));uint8_t *data=NULL;size_t size=0;
    test_video_slots(&arc);
    assert(!ai6_read_named(&arc,"endfilm.VSD",&data,&size));test_video(data,size,0);free(data);
    int found=0;
    for(unsigned i=0;i<arc.count&&!found;i++){
        if(!strstr(arc.entries[i].name,".VSD")&&!strstr(arc.entries[i].name,".vsd"))continue;
        assert(!ai6_read(&arc,i,&data,&size));KVideo *v=kvideo_open_mode(data,size,KVIDEO_SOFTWARE);assert(v);
        int usable=!kvideo_range(v,0,10);kvideo_close(v);
        if(usable){test_video(data,size,1);found=1;printf("Loop/seek fixture: %s\n",arc.entries[i].name);}free(data);
    }
    assert(found);ai6_close(&arc);
    puts("Real VSD: software/AUTO fallback RGB+PCM equality, audio, loop cache, seek and early close: PASS");return 0;
}
