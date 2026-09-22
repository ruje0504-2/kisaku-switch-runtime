/* GPL-2.0-or-later. Kisaku AI6WIN.exe opcode dispatcher 0x46f9f5.
 * This executable uses zero operand bytes for MUL (0x507370).
 * Unknown instructions/syscalls never silently succeed. */
#include "vm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <time.h>
static uint32_t le32(const uint8_t *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static int fail(KVM *v,const char *s) { snprintf(v->error,sizeof(v->error),"%s @0x%zx op=0x%02x: %s",v->module>=0?v->modules[v->module].name:"<none>",v->instruction_ip,v->opcode,s);v->status=KVM_ERROR;return -1; }
KVM *kvm_create(void) { KVM *v=calloc(1,sizeof(*v));if(v){v->random_state=(uint32_t)time(NULL)^(uint32_t)clock();v->module=-1;v->current_list=-1;v->byte_count=8192;v->word_count=600;v->global_count[0]=100;v->global_count[1]=100;}return v; }
void kvm_destroy(KVM *v) { if(!v)return;for(unsigned i=0;i<v->module_count;i++)free(v->modules[i].boundaries);free(v->stack);free(v); }
int kvm_push(KVM *v,KValue x) {
    /* 46f390 -> 402720 -> 402c30 grows the native variant vector by 50%.
       Message return values may remain on this stack across library calls. */
    if(v->sp==v->stack_capacity){
        size_t limit=SIZE_MAX/sizeof(*v->stack);if(limit>UINT_MAX)limit=UINT_MAX;
        if(v->sp>=limit)return fail(v,"operand stack capacity overflow");
        size_t capacity=v->stack_capacity?(size_t)v->stack_capacity+v->stack_capacity/2:64;
        if(capacity<=v->sp)capacity=(size_t)v->sp+1;
        if(capacity>limit)capacity=limit;
        KValue *stack=realloc(v->stack,capacity*sizeof(*stack));
        if(!stack)return fail(v,"operand stack allocation failed");
        v->stack=stack;v->stack_capacity=(unsigned)capacity;
    }
    v->stack[v->sp++]=x;return 0;
}
int kvm_pop(KVM *v,KValue *x) { if(!v->sp)return fail(v,"stack underflow");*x=v->stack[--v->sp];return 0; }
static int number(KVM *v,int32_t *n) { KValue x;if(kvm_pop(v,&x))return -1;if(x.string)return fail(v,"integer required");*n=x.number;return 0; }
static int pushn(KVM *v,int32_t n) { return kvm_push(v,(KValue){n,NULL}); }
static int validop(unsigned op) { return op<=0x1b || op==0x1d || (op>=0x32&&op<=0x43) || op>=0xfa; }
int kvm_add_module(KVM *v,const char *name,const uint8_t *d,size_t n) {
    if(!name||!d||n<4||strlen(name)>260||v->module_count==KVM_MODULES)return fail(v,"invalid module");
    uint64_t base=4+(uint64_t)le32(d)*4;
    if(base>n)return fail(v,"invalid message table");
    const uint8_t *checkpoints=d+4;unsigned checkpoint_count=le32(d);
    d+=base;n-=(size_t)base;
    uint8_t *marks=calloc(n+1,1);if(!marks)return fail(v,"out of memory");
    size_t p=0;
    while(p<n) {
        marks[p]=1;unsigned op=d[p++];if(!validop(op))goto invalid;
        if(op==10||op==11||op==0x33) {const uint8_t *end=memchr(d+p,0,n-p);if(!end)goto invalid;p=(size_t)(end-d)+1;}
        else if(op==0x14||op==0x15||op==0x16||op==0x19||op==0x1a||op==0x32)p+=4;
        else if(op==0x1b)p++;
        if(p>n)goto invalid;
    }
    marks[n]=1;
    unsigned id=v->module_count++;KModule *m=&v->modules[id];
    m->checkpoints=checkpoints;m->checkpoint_count=checkpoint_count;
    m->code=d;m->size=n;m->boundaries=marks;snprintf(m->name,sizeof(m->name),"%s",name);return (int)id;
invalid:
    free(marks);return fail(v,"malformed bytecode");
}
int kvm_checkpoint_offset(KVM *v,int module,unsigned checkpoint,size_t *offset){
    if(module<0||(unsigned)module>=v->module_count||!offset)return -1;
    KModule *m=&v->modules[module];if(checkpoint>=m->checkpoint_count)return -1;
    uint32_t ip=le32(m->checkpoints+(size_t)checkpoint*4);
    if(ip>=m->size||!m->boundaries[ip])return -1;
    *offset=ip;return 0;
}
int kvm_switch(KVM *v,int id){
    if(id<0||(unsigned)id>=v->module_count)return fail(v,"module out of range");
    v->module=id;v->ip=0;v->sp=0;v->depth=v->script_depth?v->scripts[v->script_depth-1].depth:0;v->status=KVM_READY;v->error[0]=0;return 0;
}
int kvm_start(KVM *v,int id){v->script_depth=0;return kvm_switch(v,id);}
int kvm_call_module(KVM *v,int id){
    if(id<0||(unsigned)id>=v->module_count||v->script_depth==64)return fail(v,"script call range/depth invalid");
    unsigned slot=v->script_depth++;v->scripts[slot].module=v->module;v->scripts[slot].ip=v->ip;v->scripts[slot].depth=v->depth;
    return kvm_switch(v,id);
}
static void return_script(KVM *v){
    unsigned slot=--v->script_depth;v->module=v->scripts[slot].module;v->ip=v->scripts[slot].ip;v->depth=v->scripts[slot].depth;v->sp=0;v->status=KVM_READY;
}
int kvm_resume(KVM *v) { if(v->status!=KVM_SYSCALL&&v->status!=KVM_TEXT&&v->status!=KVM_YIELD&&v->status!=KVM_BUDGET)return -1;v->status=KVM_READY;return 0; }
static int jump(KVM *v,uint32_t to) { KModule *m=&v->modules[v->module];if(to>m->size||!m->boundaries[to])return fail(v,"jump is not an instruction boundary");v->ip=to;return 0; }
static int return_frame(KVM *v) {if(v->depth==(v->script_depth?v->scripts[v->script_depth-1].depth:0)){if(v->script_depth)return_script(v);else v->status=KVM_DONE;return 0;}KFrame *f=&v->frames[--v->depth];v->module=f->module;v->ip=f->ip;return 0;}
int kvm_list_clear(KVM *v,int32_t id){
    unsigned i=0;while(i<v->list_count&&v->lists[i].id!=id)i++;
    if(i==32)return fail(v,"list group limit");
    if(i==v->list_count)v->list_count++;
    v->lists[i].id=id;v->lists[i].count=0;v->current_list=(int)i;return 0;
}
static int binary(KVM *v,unsigned op) {
    KValue b,a;if(kvm_pop(v,&b)||kvm_pop(v,&a))return -1;
    if(op==0x42||op==0x43) {int eq=a.string&&b.string?!strcmp(a.string,b.string):!a.string&&!b.string&&a.number==b.number;return pushn(v,op==0x42?eq:!eq);}
    if(a.string||b.string)return fail(v,"unsupported string arithmetic");
    int32_t x=a.number,y=b.number,r=0;
    switch(op) {
    case 0x34:r=(int32_t)((uint32_t)x+(uint32_t)y);break;
    case 0x35:r=(int32_t)((uint32_t)x-(uint32_t)y);break;
    case 0x36:r=(int32_t)((uint32_t)x*(uint32_t)y);break;
    case 0x37:case 0x38:
        if(!y)return fail(v,"division by zero");
        if(x==INT32_MIN&&y==-1)return fail(v,"signed division overflow");
        r=op==0x37?x/y:x%y;break;
    case 0x3a:r=x&&y;break;case 0x3b:r=x||y;break;case 0x3c:r=x&y;break;case 0x3d:r=x|y;break;
    case 0x3e:r=x<y;break;case 0x3f:r=x>y;break;case 0x40:r=x<=y;break;case 0x41:r=x>=y;break;
    default:return fail(v,"unsupported arithmetic");
    }return pushn(v,r);
}
KStatus kvm_run(KVM *v,unsigned budget) {
    if(v->status!=KVM_READY)return v->status;
    if(v->module<0){fail(v,"no active module");return v->status;}
    for(unsigned tick=0;tick<budget&&v->status==KVM_READY;tick++) {
        KModule *m=&v->modules[v->module];
        if(v->ip==m->size){return_frame(v);continue;}
        if(v->ip>m->size||!m->boundaries[v->ip]){fail(v,"invalid instruction pointer");break;}
        v->instruction_ip=v->ip;unsigned op=v->opcode=m->code[v->ip++];v->instructions++;
        /* 46f3f0/46f5e0/507580: capture bytes as read, before effects or
           jumps. Text/newline have an additional native recording path. */
        if(op!=0x0a&&op!=0x0b&&op!=0x1b&&v->record_bytes&&
           (v->globals[0][50].number&0x280)==0x280){
            size_t count=1;
            if(op==0x14||op==0x15||op==0x16||op==0x19||op==0x1a||op==0x32)count=5;
            else if(op==0x33)count+=strlen((const char *)m->code+v->ip)+1;
            if(v->record_bytes(v->record_owner,m->code+v->instruction_ip,count)){
                v->ip=v->instruction_ip;fail(v,"command recording failed");break;
            }
        }
        int32_t index=0;KValue value;uint32_t target=0;
        if(op==0x14||op==0x15||op==0x16||op==0x19||op==0x1a||op==0x32){target=be32(m->code+v->ip);v->ip+=4;}
        switch(op) {
        case 0:if(v->script_depth)return_script(v);else v->status=KVM_YIELD;break;
        case 1:return_frame(v);break;
        case 0x32:pushn(v,(int32_t)target);break;
        case 0x0a:case 0x0b:
            v->text=(const char *)m->code+v->ip;v->text_size=strlen(v->text);
            v->ip+=v->text_size+1;v->status=KVM_TEXT;break;
        case 0x1b:
            if(m->code[v->ip++]){fail(v,"unsupported newline operand");break;}
            if(v->record_newline&&v->record_newline(v->record_owner,0)){fail(v,"newline recording failed");break;}
            /* 505890: measurement records the command but never moves the
               cursor, nor requires numeric drawing coordinates. */
            if((uint32_t)v->globals[0][50].number&UINT32_C(0x80000000))break;
            if(v->globals[0][42].string||v->globals[0][47].string||v->globals[0][31].string){fail(v,"newline requires numeric coordinates");break;}
            /* 505890: reset horizontal cursor and advance one line. */
            v->globals[0][46]=v->globals[0][42];
            v->globals[0][47].number=(int32_t)((uint32_t)v->globals[0][47].number+(uint32_t)v->globals[0][31].number);
            break;
        case 0x33:value=(KValue){0,(const char *)m->code+v->ip};v->ip+=strlen(value.string)+1;kvm_push(v,value);break;
        case 2:case 3:case 4:case 5:
            if(number(v,&index))break;
            if(index<0||(unsigned)index>=(op==2?v->byte_count:op==3?v->word_count:v->global_count[op-4])){fail(v,"global index out of range");break;}
            if(op==2)pushn(v,v->bytes[index]);else if(op==3)pushn(v,v->words[index]);else kvm_push(v,v->globals[op-4][index]);break;
        case 6:
            if(number(v,&index))break;
            if(v->depth==(v->script_depth?v->scripts[v->script_depth-1].depth:0)||index<0||(unsigned)index>=v->frames[v->depth-1].argc){fail(v,"local index out of range");break;}
            kvm_push(v,v->frames[v->depth-1].args[index]);break;
        case 0x10:
            /* 504dd0: assignment to the current function's argument bank. */
            if(number(v,&index)||kvm_pop(v,&value))break;
            if(v->depth==(v->script_depth?v->scripts[v->script_depth-1].depth:0)||index<0||(unsigned)index>=v->frames[v->depth-1].argc){fail(v,"local index out of range");break;}
            v->frames[v->depth-1].args[index]=value;break;
        case 7:case 8:case 9:case 0x11:case 0x12:case 0x13:{
            /* 505350/505270/505190 and 504d20/504c60/504ba0.
               Word/dword indices are elements, not byte offsets. */
            unsigned width=1u<<((op<=9?op-7:op-0x11));
            if(number(v,&index))break;
            if(index<0||!v->raw||(size_t)index>=v->raw_size/width){fail(v,"raw variable index out of range");break;}
            uint8_t *p=v->raw+(size_t)index*width;
            if(op<=9){uint32_t n=0;for(unsigned j=0;j<width;j++)n|=(uint32_t)p[j]<<(8*j);pushn(v,(int32_t)n);}
            else {int32_t n;if(number(v,&n))break;for(unsigned j=0;j<width;j++)p[j]=(uint8_t)((uint32_t)n>>(8*j));}
            break;
        }
        case 0x0c:case 0x0d:case 0x0e:case 0x0f:
            if(number(v,&index)||kvm_pop(v,&value))break;
            if(index<0||(unsigned)index>=(op==0x0c?v->byte_count:op==0x0d?v->word_count:v->global_count[op-0x0e])){fail(v,"global index out of range");break;}
            if(op<=0x0d&&value.string){fail(v,"numeric store requires integer");break;}
            if(op==0x0c)v->bytes[index]=(uint8_t)value.number;else if(op==0x0d)v->words[index]=(uint16_t)(value.number<0?0:value.number);else v->globals[op-0x0e][index]=value;break;
        case 0x14:if(!number(v,&index)&&!index)jump(v,target);break;
        case 0x15:jump(v,target);break;
        case 0x16:{
            int32_t argc;if(number(v,&index)||number(v,&argc))break;
            if(index<0||index>=1024||argc<0||argc>64){fail(v,"invalid library registration");break;}
            size_t body=v->ip;if(jump(v,target))break;
            v->functions[index]=(KFunction){v->module,body,(unsigned)argc,1};break;
        }
        case 0x17:{
            if(number(v,&index))break;
            if(index<0||index>=1024||!v->functions[index].valid){fail(v,"unregistered library function");break;}
            KFunction *fn=&v->functions[index];
            if(v->depth==64||v->sp<fn->argc){fail(v,"library call depth/argument error");break;}
            KFrame *f=&v->frames[v->depth++];f->module=v->module;f->ip=v->ip;f->argc=fn->argc;
            for(unsigned i=0;i<fn->argc;i++)kvm_pop(v,&f->args[i]);
            v->module=fn->module;v->ip=fn->ip;break;
        }
        case 0x1a:{
            /* 41d9e0 appends (body IP, popped value), then skips the body. */
            if(number(v,&index))break;
            if(v->current_list<0||(unsigned)v->current_list>=v->list_count){fail(v,"list group missing");break;}
            KList *list=&v->lists[v->current_list];if(list->count==64){fail(v,"list item limit");break;}
            size_t body=v->ip;if(jump(v,target))break;
            list->items[list->count++]=(KListItem){v->module,body,index};break;
        }
        case 0x19:
            /* 41d920 saves the immediate checkpoint ID and snapshots controls
               via the same native method as syscall28/11 (435410). */
            if(v->global_count[0]<=48){fail(v,"checkpoint slot missing");break;}
            v->globals[0][48]=(KValue){(int32_t)target,NULL};
            if(!pushn(v,11)){v->syscall=28;v->status=KVM_SYSCALL;}break;
        case 0x18:if(!number(v,&v->syscall))v->status=KVM_SYSCALL;break;
        case 0x39:{
            /* 415710: unary MSVC rand() modulo bound; never a binary op. */
            int32_t bound;if(number(v,&bound))break;
            if(!bound){fail(v,"random bound is zero");break;}
            v->random_state=v->random_state*214013u+2531011u;
            pushn(v,(int32_t)((v->random_state>>16)&32767u)%bound);break;
        }
        case 0x34:case 0x35:case 0x36:case 0x37:case 0x38:case 0x3a:case 0x3b:case 0x3c:case 0x3d:case 0x3e:case 0x3f:case 0x40:case 0x41:case 0x42:case 0x43:binary(v,op);break;
        default:fail(v,"instruction not implemented");break;
        }
    }
    if(v->status==KVM_READY)v->status=KVM_BUDGET;
    return v->status;
}
