#include "ai6arc.h"
#include "vm.h"
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv) {
    if(argc!=2){fprintf(stderr,"Usage: %s mes.arc\n",argv[0]);return 2;}
    Ai6Archive a;if(ai6_open(&a,argv[1]))return 1;
    KVM *v=kvm_create();uint8_t *data[2]={0};int result=1;
    if(!v){ai6_close(&a);return 1;}
    const char *names[]={"liblary.lib","startup.mes"};
    for(unsigned k=0;k<2;k++) {
        size_t size=0;int found=0;
        for(uint32_t i=0;i<a.count;i++)if(!strcmp(a.entries[i].name,names[k])){found=1;if(ai6_read(&a,i,&data[k],&size))goto done;break;}
        if(!found)goto done;
        int m=kvm_add_module(v,names[k],data[k],size);if(m<0||kvm_start(v,m))goto done;
        KStatus status=kvm_run(v,100000);
        if(k==0) {
            if(status!=KVM_DONE&&status!=KVM_YIELD)goto done;
            unsigned n=0;for(unsigned i=0;i<1024;i++)n+=v->functions[i].valid;
            printf("Library registration verified: %u functions\n",n);
        } else if(status==KVM_SYSCALL) {
            printf("PAUSED %s @0x%zx: SYSCALL %d\n",names[k],v->instruction_ip,v->syscall);
            printf("Arguments (stack bottom to top):");
            for(unsigned i=0;i<v->sp;i++)if(v->stack[i].string)printf(" <string>");else printf(" %d",v->stack[i].number);
            puts("\nPlatform handler required; no syscall was skipped. This is a VM probe, not gameplay.");result=0;
        }
    }
done:
    if(v->status==KVM_ERROR)fprintf(stderr,"%s\n",v->error);
    kvm_destroy(v);free(data[0]);free(data[1]);ai6_close(&a);return result;
}
