#include "ai6arc.h"
#include "rmt.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
static int same(const char *a,const char *b) {while(*a&&*b){if(tolower((unsigned char)*a++)!=tolower((unsigned char)*b++))return 0;}return *a==*b;}
int main(int argc,char **argv) {
    if(argc!=2&&argc!=4){fprintf(stderr,"Usage: %s rmt.arc [name output.bgra]\n",argv[0]);return 2;}
    Ai6Archive a;if(ai6_open(&a,argv[1])){fputs("Invalid archive\n",stderr);return 1;}
    unsigned count=0,failures=0;uint64_t pixels=0;size_t max_bytes=0;
    for(uint32_t i=0;i<a.count;i++) {
        if(argc==4&&!same(a.entries[i].name,argv[2]))continue;
        uint8_t *data=NULL;size_t size=0;KImage im={0};
        if(ai6_read(&a,i,&data,&size)||rmt_decode(data,size,&im)) {
            fprintf(stderr,"FAIL %s\n",a.entries[i].name);failures++;free(data);continue;
        }
        free(data);count++;pixels+=(uint64_t)im.width*im.height;
        size_t bytes=im.stride*im.height;if(bytes>max_bytes)max_bytes=bytes;
        if(argc==4) {
            FILE *f=fopen(argv[3],"wb");
            if(!f)failures++;
            else {if(fwrite(im.pixels,1,bytes,f)!=bytes)failures++;if(fclose(f))failures++;}
        }
        rmt_free(&im);
    }
    ai6_close(&a);
    printf("{\"decoded\":%u,\"failures\":%u,\"pixels\":%llu,\"largest_image_bytes\":%zu}\n",count,failures,(unsigned long long)pixels,max_bytes);
    return failures||!count ? 1:0;
}
