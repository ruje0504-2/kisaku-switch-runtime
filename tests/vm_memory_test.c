/* Native Kisaku opcode contracts, independent of game resources. */
#include "vm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char code[1024];static size_t size;
static void emit(unsigned n){code[size++]=(unsigned char)n;}
static void integer(int32_t n){emit(0x32);for(int i=3;i>=0;i--)emit((uint32_t)n>>(i*8));}
static void store(int32_t n,int32_t i,unsigned op){integer(n);integer(i);emit(op);}
static void load(int32_t i,unsigned op){integer(i);emit(op);}
static KVM *run(uint8_t *raw,size_t raw_size){
    KVM *v=kvm_create();assert(v);v->raw=raw;v->raw_size=raw_size;
    int id=kvm_add_module(v,"memory",code,size);assert(id==0);assert(!kvm_start(v,id));kvm_run(v,10000);return v;
}
int main(void){
    uint8_t raw[13]={0};size=4;memset(code,0,sizeof(code));
    store((int32_t)0x89abcdef,1,0x13);load(4,7);load(5,7);load(2,8);load(1,9);
    store(0x1234,3,0x12);load(1,9);store(-1,12,0x11);load(12,7);
    store(-1,4,0x0d);load(4,3);store(65537,4,0x0d);load(4,3);emit(1);
    KVM *v=run(raw,sizeof(raw));assert(v->status==KVM_DONE&&v->sp==8);
    const int32_t expected[]={0xef,0xcd,0xcdef,(int32_t)0x89abcdef,0x1234cdef,255,0,1};
    for(unsigned i=0;i<8;i++)assert(v->stack[i].number==expected[i]&&!v->stack[i].string);
    assert(raw[12]==255);kvm_destroy(v);
    /* Reject a partial dword at the end, without changing any bytes. */
    size=4;store(123,3,0x13);emit(1);uint8_t before[13];memcpy(before,raw,13);
    v=run(raw,sizeof(raw));assert(v->status==KVM_ERROR&&!memcmp(raw,before,13));kvm_destroy(v);
    size=4;load(-1,7);emit(1);v=run(raw,sizeof(raw));assert(v->status==KVM_ERROR);kvm_destroy(v);
    /* Locals can be assigned; values belong to the active call frame. */
    size=4;store(91,0,0x10);load(0,6);emit(1);
    v=kvm_create();assert(v);int id=kvm_add_module(v,"local",code,size);assert(id==0);kvm_start(v,id);
    v->depth=1;v->frames[0].argc=1;v->frames[0].module=0;v->frames[0].ip=size-4;
    kvm_run(v,100);assert(v->status==KVM_DONE&&v->sp==1&&v->stack[0].number==91);kvm_destroy(v);
    puts("Kisaku VM raw memory views, bounds, word clamp and local assignment: PASS");
}
