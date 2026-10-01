/* Enforce a single decoder/device slot while replacing real MOV streams.
   Fault injection validates ownership; it is not a Switch hardware test. */
#include "bootstrap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct KVideo {const uint8_t *bytes;size_t size;};
static unsigned live,opened,closed,peak;
static int fail_open;
KVideo *kvideo_open(const uint8_t *data,size_t size){
    assert(!live); /* A decoder may not be opened until the old device closes. */
    if(fail_open)return NULL;
    KVideo *v=malloc(sizeof(*v));assert(v);*v=(KVideo){data,size};
    opened++;live++;if(live>peak)peak=live;return v;
}
void kvideo_close(KVideo *v){if(v){assert(live==1&&v->size>=4&&!memcmp(v->bytes,"VSD1",4));live--;closed++;free(v);}}
int kvideo_range(KVideo *v,int first,int last){assert(v&&first>=0&&last>first);return 0;}
int kvideo_step(KVideo *v,KImage *target,uint8_t **pcm,size_t *size){
    assert(v&&target->pixels);(void)pcm;(void)size;memset(target->pixels,77,target->stride*target->height);return 0;
}
const char *kvideo_error(KVideo *v){(void)v;return "injected decoder error";}
static int error(KBootstrap *b,const char *message){snprintf(b->error,sizeof(b->error),"%s",message);return -1;}
static int read_named(Ai6Archive *a,const char *name,uint8_t **data,size_t *size){return ai6_read_named(a,name,data,size);}
static int option(KBootstrap *b,const char *section,const char *key,int value){(void)b;(void)section;(void)key;(void)value;return 0;}
static int effect_decode(KBootstrap *b,KEffectTrack *track,const char *name){(void)b;(void)track;(void)name;assert(0);return -1;}
#include "../runtime/movie.inc"
int main(int argc,char **argv){
    assert(argc==2);KBootstrap *b=calloc(1,sizeof(*b));assert(b);b->vm=kvm_create();assert(b->vm);
    char path[4096];snprintf(path,sizeof(path),"%s/movie.arc",argv[1]);assert(!ai6_open(&b->movies,path));
    b->layers[0]=(KImage){0,0,640,480,2560,calloc(640*480,4)};assert(b->layers[0].pixels);
    for(unsigned i=0;i<32;i++){
        assert(!movie_open(b,"ev148b_2.mov",0)&&live==1);
        movie_frame(b);assert(b->movie_frame.pixels&&b->layers[0].pixels[0]==77);
    }
    /* Decode failure after replacement must leave neither an old device nor
       partially installed command/video/frame buffers. */
    fail_open=1;assert(movie_open(b,"ev148b_2.mov",0)<0&&!live);
    assert(!b->video&&!b->video_data&&!b->mov_data&&!b->movie_frame.pixels);
    fail_open=0;b->error[0]=0;assert(!movie_open(b,"ev148b_2.mov",0));
    movie_frame(b);movie_stop(b);movie_stop(b);
    assert(!live&&opened==closed&&peak==1&&!b->movie_frame.pixels);
    rmt_free(&b->layers[0]);ai6_close(&b->movies);kvm_destroy(b->vm);free(b);
    printf("MOV replacement: %u devices closed, peak one, open failure cleanup and repeated stop: PASS\n",closed);return 0;
}
