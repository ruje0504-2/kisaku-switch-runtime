/* Inventory native calls in every original MES entry.
 * This is a static/dynamic VM walk: it executes each module with the real
 * bytecode and library registrations, but never acknowledges a syscall.
 * The first call and its preserved VM stack are enough to identify the
 * original 31/<sub> boundary without launching the Windows executable.
 */
#include "vm.h"
#include "ai6arc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int is_mes(const char *name){
    size_t n=strlen(name);
    return n>=4&&!strcasecmp(name+n-4,".mes");
}

static void print_stack(const KVM *v){
    printf(" stack=");
    unsigned start=v->sp>16?v->sp-16:0;
    for(unsigned i=start;i<v->sp;i++){
        if(v->stack[i].string)printf("[%s]",v->stack[i].string);
        else printf("%d",v->stack[i].number);
        if(i+1<v->sp)putchar(',');
    }
}

typedef struct { size_t ip; int main,sub; } Seen;

static int seen_call(Seen *seen,unsigned *count,size_t ip,int main,int sub){
    for(unsigned i=0;i<*count;i++)
        if(seen[i].ip==ip&&seen[i].main==main&&seen[i].sub==sub)return 1;
    if(*count<4096)seen[(*count)++]=(Seen){ip,main,sub};
    return 0;
}

static void walk(Ai6Archive *archive,const Ai6Entry *lib,const Ai6Entry *entry){
    uint8_t *ld=NULL,*ed=NULL;size_t ls=0,es=0;
    if(ai6_read(archive,(uint32_t)(lib-archive->entries),&ld,&ls)||
       ai6_read(archive,(uint32_t)(entry-archive->entries),&ed,&es)){free(ld);free(ed);return;}
    KVM *v=kvm_create();
    if(!v){free(ld);free(ed);return;}
    int lm=kvm_add_module(v,"liblary.lib",ld,ls);
    int em=kvm_add_module(v,entry->name,ed,es);
    if(lm<0||em<0){kvm_destroy(v);free(ld);free(ed);return;}
    if(kvm_start(v,lm)<0){kvm_destroy(v);free(ld);free(ed);return;}
    KStatus s=kvm_run(v,10000000);
    if(s==KVM_SYSCALL||s==KVM_DONE||s==KVM_YIELD||s==KVM_BUDGET){
        if(kvm_start(v,em)<0){kvm_destroy(v);free(ld);free(ed);return;}
        unsigned calls=0,unique=0;Seen seen[4096]={0};
        for(;calls<10000;calls++){
            s=kvm_run(v,1000000);
            if(s==KVM_SYSCALL){
                int sub=v->sp&& !v->stack[v->sp-1].string?v->stack[v->sp-1].number:-1;
                if(!seen_call(seen,&unique,v->instruction_ip,v->syscall,sub)){
                    printf("%s\t%zu\t%d\t%d\t%u\t",entry->name,v->instruction_ip,v->syscall,sub,v->sp);
                    print_stack(v);putchar('\n');
                }
                if(unique>=4096)break;
                /* Resume without changing the stack to expose the next
                   call site. This intentionally is not gameplay emulation. */
                if(kvm_resume(v)<0)break;
            }else if(s==KVM_BUDGET)continue;
            else break;
        }
    }
    kvm_destroy(v);free(ld);free(ed);
}

int main(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"usage: %s mes.arc\n",argv[0]);return 2;}
    Ai6Archive a;if(ai6_open(&a,argv[1]))return 1;
    const Ai6Entry *lib=NULL;
    for(uint32_t i=0;i<a.count;i++)if(!strcasecmp(a.entries[i].name,"liblary.lib")){lib=&a.entries[i];break;}
    if(!lib){fprintf(stderr,"liblary.lib missing\n");ai6_close(&a);return 1;}
    puts("module\tip\tmain\tsub\tsp\tstack");
    for(uint32_t i=0;i<a.count;i++)if(&a.entries[i]!=lib&&is_mes(a.entries[i].name))walk(&a,lib,&a.entries[i]);
    ai6_close(&a);return 0;
}
