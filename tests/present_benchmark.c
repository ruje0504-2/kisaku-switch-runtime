/* Reproducible host presentation benchmark; timings are not Switch FPS.
   Hashes allow comparing the same scripted state against a prior build. */
#include "bootstrap.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static unsigned long long digest(const KImage *im){
    unsigned long long h=1469598103934665603ULL;
    for(unsigned y=0;y<im->height;y++)for(unsigned x=0;x<im->width*4;x++)h=(h^im->pixels[y*im->stride+x])*1099511628211ULL;
    return h;
}
int main(int argc,char **argv){
    assert(argc==3);KBootstrap *b=bootstrap_create_split(argv[1],argv[2]);assert(b&&!b->error[0]);b->present_hires=1;
    for(unsigned i=0;!b->title.active||b->title.age<64;i++){assert(i<3000&&bootstrap_run(b,100000)>=0);bootstrap_frame(b);}
    b->title.selected=0;bootstrap_confirm(b);
    for(unsigned i=0;;i++){
        assert(i<10000&&bootstrap_run(b,100000)>=0);
        if(b->flag_dialog.active)bootstrap_pointer(b,300,350,1);
        bootstrap_frame(b);
        if(b->message_active&&!b->message_slide&&!b->message_revealing)break;
    }
    const KImage *overlay=NULL,*clean=bootstrap_present_layers(b,&overlay);assert(clean&&overlay);
    printf("raw=%016llx clean=%016llx overlay=%016llx\n",digest(&b->layers[0]),digest(clean),digest(overlay));
    clock_t start=clock();
    for(unsigned i=0;i<300;i++){clean=bootstrap_present_layers(b,&overlay);assert(clean&&overlay);}
    printf("300 stable HQ preparations CPU milliseconds: %.3f\n",1000.0*(clock()-start)/CLOCKS_PER_SEC);
    /* Native overlays after the message compose must mask HQ pixels exactly. */
    for(unsigned y=409;y<440;y++)for(unsigned x=39;x<280;x++){
        uint8_t *p=b->layers[0].pixels+y*b->layers[0].stride+x*4;p[0]=41;p[1]=193;p[2]=7;p[3]=255;
    }
    clean=bootstrap_present_layers(b,&overlay);
    printf("occluded clean=%016llx overlay=%016llx\n",digest(clean),digest(overlay));
    bootstrap_destroy(b);return 0;
}
