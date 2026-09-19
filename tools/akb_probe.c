#include "ai6arc.h"
#include "akb.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){
    if(argc!=2)return 2;
    Ai6Archive a;if(ai6_open(&a,argv[1]))return 1;
    unsigned failed=0;
    for(unsigned i=0;i<a.count;i++){
        uint8_t *d=NULL;size_t n=0;KImage im={0};
        if(ai6_read(&a,i,&d,&n)||akb_decode(d,n,&im)){fprintf(stderr,"FAIL %s\n",a.entries[i].name);failed++;}
        free(d);rmt_free(&im);
    }
    printf("AKB: %u/%u decoded, failures=%u\n",a.count-failed,a.count,failed);
    ai6_close(&a);return failed?1:0;
}
