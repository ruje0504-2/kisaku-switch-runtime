#include "save_slot.h"
#include "file_store.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <time.h>
static int paths(const char *root,unsigned selector,unsigned slot,char *base,size_t size){
    return !root||selector>3||slot<1||slot>100||snprintf(base,size,"%s/kisaku-slot-%u-%03u",root,selector,slot)>=(int)size?-1:0;
}
/* 1 absent, 0 valid, -1 malformed. Never fall back over a broken port save. */
static int generation(const char *base,uint32_t *value,unsigned *version){
    char path[4096],line[80],extra;snprintf(path,sizeof(path),"%s.index",base);
    if(kstore_recover(path))return -1;
    FILE *f=fopen(path,"rb");if(!f)return errno==ENOENT?1:-1;
    unsigned format=0;
    int bad=!fgets(line,sizeof(line),f)||sscanf(line,"KISAKU-SLOT-%u %" SCNu32 " %c",&format,value,&extra)!=2||(format<1||format>5)||fgetc(f)!=EOF||ferror(f);
    fclose(f);if(!bad&&version)*version=format;return bad?-1:0;
}
int kslot_read(const char *root,unsigned selector,unsigned slot,KFlags **flags,KControlStore **controls){
    *flags=NULL;*controls=NULL;char base[3800],path[4096];uint32_t gen=0;
    if(paths(root,selector,slot,base,sizeof(base)))return -1;
    unsigned version=0;int status=generation(base,&gen,&version);if(status<0||(!status&&!gen))return -1;
    if(status==1){*flags=kflags_read_slot(root,selector,slot);*controls=kcontrol_read_slot(root,selector,slot);}
    else{
        snprintf(path,sizeof(path),"%s-%" PRIu32 ".flag",base,gen);*flags=kflags_read(path);
        snprintf(path,sizeof(path),"%s-%" PRIu32 ".control",base,gen);*controls=kcontrol_read(path);
    }
    unsigned frames=0;if(*controls)for(unsigned i=0;i<(*controls)->count;i++)frames+=(*controls)->records[i].type==0xfffc;
    if(!*flags||!*controls||(version==4&&frames!=1)){kflags_free(*flags);kcontrol_free(*controls);*flags=NULL;*controls=NULL;return -1;}return 0;
}
int kslot_write_state(const char *root,unsigned selector,unsigned slot,const KFlags *flags,const KControlRecord *records,unsigned count,const KImage *image,const KImage *scene){
    char base[3800],flag[4096],control[4096],index[4096],temp[4100];uint32_t gen=0;
    if(paths(root,selector,slot,base,sizeof(base))||generation(base,&gen,NULL)<0||gen==UINT32_MAX)return -1;
    uint32_t next=gen+1;
    snprintf(flag,sizeof(flag),"%s-%" PRIu32 ".flag",base,next);
    snprintf(control,sizeof(control),"%s-%" PRIu32 ".control",base,next);
    snprintf(index,sizeof(index),"%s.index",base);snprintf(temp,sizeof(temp),"%s.tmp",index);
    if(kflags_write(flags,flag)||kcontrol_write(control,records,count))return -1;
    if(!image){char preview[4096];snprintf(preview,sizeof(preview),"%s-%" PRIu32 ".preview",base,next);if(remove(preview)&&errno!=ENOENT)return -1;}
    if(image){
        if(!image->pixels||image->width!=640||image->height!=480||image->stride<2560)return -1;
        char preview[4096];snprintf(preview,sizeof(preview),"%s-%" PRIu32 ".preview",base,next);
        FILE *pf=fopen(preview,"wb");if(!pf)return -1;
        uint8_t header[16]={'K','A','T','H','M','0','1',0};uint64_t now=(uint64_t)time(NULL);
        for(unsigned i=0;i<8;i++)header[8+i]=(uint8_t)(now>>(i*8));
        int failed=fwrite(header,1,16,pf)!=16;
        for(unsigned y=0;y<252&&!failed;y++)for(unsigned x=0;x<336;x++){
            const uint8_t *pixel=image->pixels+(y*480/252)*image->stride+(x*640/336)*4;
            if(fwrite(pixel,1,4,pf)!=4){failed=1;break;}
        }
        if(fclose(pf))failed=1;
        if(failed){remove(preview);return -1;}
    }
    char scene_path[4096];snprintf(scene_path,sizeof(scene_path),"%s-%" PRIu32 ".scene",base,next);
    if(scene){
        if(!scene->pixels||scene->width!=640||scene->height<480||scene->stride<2560)return -1;
        FILE *sf=fopen(scene_path,"wb");if(!sf)return -1;
        int failed=fwrite("KASCENE1",1,8,sf)!=8;
        for(unsigned y=0;y<480&&!failed;y++)failed=fwrite(scene->pixels+y*scene->stride,1,2560,sf)!=2560;
        if(fclose(sf))failed=1;
        if(failed){remove(scene_path);return -1;}
    }else if(remove(scene_path)&&errno!=ENOENT)return -1;
    FILE *f=fopen(temp,"wb");if(!f)return -1;
    unsigned format=1;for(unsigned i=0;i<count;i++){if(records[i].type==0xfffc)format=4;else if(records[i].type==0xfffd&&format<3)format=3;else if(records[i].type==0xfffe&&format<2)format=2;}
    if(scene)format=5;
    int bad=fprintf(f,"KISAKU-SLOT-%u %" PRIu32 "\n",format,next)<0;if(fclose(f))bad=1;
    if(bad||kstore_replace(temp,index)){remove(temp);return -1;}
    if(gen){snprintf(flag,sizeof(flag),"%s-%" PRIu32 ".flag",base,gen);snprintf(control,sizeof(control),"%s-%" PRIu32 ".control",base,gen);remove(flag);remove(control);snprintf(flag,sizeof(flag),"%s-%" PRIu32 ".preview",base,gen);remove(flag);snprintf(flag,sizeof(flag),"%s-%" PRIu32 ".scene",base,gen);remove(flag);}
    return 0;
}

int kslot_write_image(const char *root,unsigned selector,unsigned slot,const KFlags *flags,const KControlRecord *records,unsigned count,const KImage *image){
    return kslot_write_state(root,selector,slot,flags,records,count,image,NULL);
}
/* 1 means a legacy save has no resident scene; malformed new saves fail. */
int kslot_scene(const char *root,unsigned selector,unsigned slot,KImage *scene){
    char base[3800],path[4096];uint32_t gen=0;unsigned version=0;
    if(paths(root,selector,slot,base,sizeof(base)))return -1;
    int status=generation(base,&gen,&version);
    if(status<0)return -1;
    if(status||version<5)return 1;
    snprintf(path,sizeof(path),"%s-%" PRIu32 ".scene",base,gen);
    FILE *f=fopen(path,"rb");if(!f)return -1;
    uint8_t header[8],*pixels=malloc(480*2560);if(!pixels){fclose(f);return -1;}
    int bad=fread(header,1,8,f)!=8||memcmp(header,"KASCENE1",8)||
        fread(pixels,1,480*2560,f)!=480*2560||fgetc(f)!=EOF||ferror(f);
    fclose(f);if(bad){free(pixels);return -1;}
    rmt_free(scene);*scene=(KImage){0,0,640,480,2560,pixels};return 0;
}

int kslot_write(const char *root,unsigned selector,unsigned slot,const KFlags *flags,const KControlRecord *records,unsigned count){
    return kslot_write_image(root,selector,slot,flags,records,count,NULL);
}
int kslot_preview(const char *root,unsigned selector,unsigned slot,KImage *image,int64_t *saved_time){
    char base[3800],path[4096];uint32_t gen=0;*saved_time=0;
    if(paths(root,selector,slot,base,sizeof(base)))return -1;
    int status=generation(base,&gen,NULL);if(status<0||(!status&&!gen))return -1;
    const char *dirs[]={"save","save52","save53","save54"};
    if(status)snprintf(path,sizeof(path),"%s/%s/Thm%03u",root,dirs[selector],slot);
    else snprintf(path,sizeof(path),"%s-%" PRIu32 ".preview",base,gen);
    FILE *f=fopen(path,"rb");if(!f)return -1;
    if(!status){uint8_t h[16];if(fread(h,1,16,f)!=16||memcmp(h,"KATHM01",8)){fclose(f);return -1;}
        uint64_t t=0;for(unsigned i=0;i<8;i++)t|=(uint64_t)h[8+i]<<(i*8);if(t>INT64_MAX){fclose(f);return -1;}*saved_time=(int64_t)t;
    }else{struct stat st;if(!stat(path,&st))*saved_time=(int64_t)st.st_mtime;}
    uint8_t *pixels=malloc(336*252*4);if(!pixels){fclose(f);return -1;}
    int bad=fread(pixels,1,336*252*4,f)!=336*252*4||fgetc(f)!=EOF||ferror(f);fclose(f);
    if(bad){free(pixels);return -1;}
    if(status){uint8_t row[1344];for(unsigned y=0;y<126;y++){memcpy(row,pixels+y*1344,1344);memcpy(pixels+y*1344,pixels+(251-y)*1344,1344);memcpy(pixels+(251-y)*1344,row,1344);}}
    free(image->pixels);*image=(KImage){0,0,336,252,336*4,pixels};return 0;
}

/* Kisaku 478240: raw+1002, 100 records of 45 bytes. Date fields are deliberately
   unaligned; copying a C struct here would corrupt the original layout. */
int kslot_info(const KFlags *f,unsigned slot,KSlotInfo *info){
    if(!info)return -1;
    memset(info,0,sizeof(*info));
    if(!f||!f->raw||slot<1||slot>100||f->raw_count<1002+slot*45)return -1;
    const uint8_t *p=f->raw+1002+(slot-1)*45;
    if(p[0]!=1)return -1;
    info->stage=(unsigned)p[1]|(unsigned)p[2]<<8;
    info->scene=(unsigned)p[3]|(unsigned)p[4]<<8;
    const uint8_t *end=memchr(p+17,0,28);if(!end)return -1;
    memcpy(info->comment,p+17,(size_t)(end-p-17));
    unsigned year=(unsigned)p[5]|(unsigned)p[6]<<8,month=(unsigned)p[7]|(unsigned)p[8]<<8,day=(unsigned)p[9]|(unsigned)p[10]<<8;
    unsigned hour=(unsigned)p[11]|(unsigned)p[12]<<8,minute=(unsigned)p[13]|(unsigned)p[14]<<8,second=(unsigned)p[15]|(unsigned)p[16]<<8;
    if(year>=1970&&year<=9999&&month>=1&&month<=12&&day>=1&&day<=31&&hour<24&&minute<60&&second<60){
        struct tm tm={0};tm.tm_year=(int)year-1900;tm.tm_mon=(int)month-1;tm.tm_mday=(int)day;tm.tm_hour=(int)hour;tm.tm_min=(int)minute;tm.tm_sec=(int)second;tm.tm_isdst=-1;
        time_t stamp=mktime(&tm);if(stamp!=(time_t)-1)info->saved_time=(int64_t)stamp;
    }
    return 0;
}
