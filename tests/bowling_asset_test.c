#include "ai6arc.h"
#include "akb.h"
#include "ax.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

static int has_entry(const Ai6Archive *a,const char *name){
    for(uint32_t i=0;i<a->count;i++)if(!strcasecmp(a->entries[i].name,name))return 1;
    return 0;
}

static void decode_layer_asset(Ai6Archive *layer,const char *name){
    uint8_t *data=NULL;size_t size=0;KImage image={0};
    assert(!ai6_read_named(layer,name,&data,&size)&&data&&size);
    assert(!akb_decode(data,size,&image));
    assert(image.pixels&&image.width&&image.height&&image.stride>=image.width*4);
    rmt_free(&image);free(data);
}

int main(int argc,char **argv){
    assert(argc==2);
    char layer_path[4096],data_path[4096],effect_path[4096];
    assert(snprintf(layer_path,sizeof(layer_path),"%s/layer.arc",argv[1])<(int)sizeof(layer_path));
    assert(snprintf(data_path,sizeof(data_path),"%s/data.arc",argv[1])<(int)sizeof(data_path));
    assert(snprintf(effect_path,sizeof(effect_path),"%s/effect.arc",argv[1])<(int)sizeof(effect_path));
    Ai6Archive layer={0},data={0},effect={0};
    assert(!ai6_open(&layer,layer_path)&&!ai6_open(&data,data_path)&&!ai6_open(&effect,effect_path));
    const char *images[]={"bow_ball.akb","bow_bg.akb","bow_pin.akb","bow_pt.akb","bow_pt02.akb","bow_sr.akb","tutbow.akb"};
    for(size_t i=0;i<sizeof(images)/sizeof(*images);i++){
        assert(has_entry(&layer,images[i]));
        decode_layer_asset(&layer,images[i]);
    }
    assert(has_entry(&data,"bow.ax"));
    uint8_t *ax_data=NULL;size_t ax_size=0;assert(!ai6_read_named(&data,"bow.ax",&ax_data,&ax_size));
    struct ax_player player;assert(ax_load(&player,"bow.ax",ax_data,ax_size)&&ax_valid(&player));
    int32_t boundaries=0;assert(ax_count_boundaries(&player,0,&boundaries)&&boundaries>=0);
    free(ax_data);
    const char *sounds[]={"bowthrow.wav","bowgut.wav","claps.wav","claps2.wav","buuuuu.wav","bowall.wav","bowh.wav","bowl.wav"};
    for(size_t i=0;i<sizeof(sounds)/sizeof(*sounds);i++)assert(has_entry(&effect,sounds[i]));
    ai6_close(&effect);ai6_close(&data);ai6_close(&layer);
    puts("CBowling asset gate: real AKB resources decode, bow.ax validates and track 0 is finite, sound table present: PASS");
    return 0;
}
