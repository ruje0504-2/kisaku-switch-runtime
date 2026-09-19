#include "read_flags.h"
#include "file_store.h"
#include <string.h>
#include <stdlib.h>
static int filename(char *path,size_t capacity,const char *root,unsigned selector){
    return !root||selector>3||snprintf(path,capacity,"%s/kisaku-read-%u.dat",root,selector)>=(int)capacity?-1:0;
}
int kread_flags_save(const char *root,unsigned selector,const uint8_t *data,size_t size){
    char path[4096],temp[4104];if(!data||!size||filename(path,sizeof(path),root,selector))return -1;
    if(snprintf(temp,sizeof(temp),"%s.tmp",path)>=(int)sizeof(temp))return -1;
    FILE *f=fopen(temp,"wb");if(!f)return -1;int bad=fwrite(data,1,size,f)!=size;if(fclose(f))bad=1;
    if(bad||kstore_replace(temp,path)){remove(temp);return -1;}return 0;
}
int kread_flags_load(const char *root,unsigned selector,uint8_t *data,size_t size){
    char path[4096];if(!data||!size||filename(path,sizeof(path),root,selector)||kstore_recover(path))return -1;
    FILE *f=fopen(path,"rb");int original=0;
    if(!f){
        if(errno!=ENOENT)return -1;
        const char *dirs[]={"save","save52","save53","save54"};
        if(snprintf(path,sizeof(path),"%s/%s/onemes.dat",root,dirs[selector])>=(int)sizeof(path))return -1;
        f=fopen(path,"rb");original=1;
        if(!f){if(errno!=ENOENT)return -1;memset(data,0,size);return 1;}
    }
    uint8_t *next=calloc(size,1);if(!next){fclose(f);return -1;}
    size_t count=fread(next,1,size,f);int extra=fgetc(f),bad=ferror(f)||extra!=EOF||(!original&&count!=size);if(fclose(f))bad=1;
    if(!bad)memcpy(data,next,size);
    free(next);return bad?-1:0;
}
