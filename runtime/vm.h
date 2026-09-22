#ifndef KISAKU_VM_H
#define KISAKU_VM_H
#include <stdint.h>
#include <stddef.h>
#define KVM_MODULES 1024
typedef struct { int32_t number; const char *string; } KValue;
typedef struct { const uint8_t *code,*checkpoints; unsigned checkpoint_count; size_t size; uint8_t *boundaries; char name[261]; } KModule;
typedef struct { int module; size_t ip; unsigned argc; int valid; } KFunction;
typedef struct { int module; size_t ip; KValue args[64]; unsigned argc; } KFrame;
typedef struct {int module;size_t ip;int32_t value;} KListItem;
typedef struct {int32_t id;unsigned count;KListItem items[64];} KList;
typedef enum { KVM_READY, KVM_SYSCALL, KVM_TEXT, KVM_YIELD, KVM_DONE, KVM_ERROR, KVM_BUDGET } KStatus;
typedef struct {
    KModule modules[KVM_MODULES]; unsigned module_count;
    KFunction functions[1024];
    KList lists[32];unsigned list_count;int current_list;
    KFrame frames[64]; unsigned depth;
    struct {int module;size_t ip;unsigned depth;} scripts[64];unsigned script_depth;
    KValue *stack; unsigned sp,stack_capacity; /* Native growable variant vector. */
    const char *text;size_t text_size;
    /* Optional native recorders; failure stops before instruction effects or
       newline cursor changes. The owner and callbacks outlive the VM. */
    int (*record_bytes)(void *owner,const uint8_t *data,size_t count);
    int (*record_newline)(void *owner,unsigned operand);void *record_owner;
    KValue globals[2][8192]; uint8_t bytes[16384]; uint16_t words[8192];
    unsigned byte_count, word_count, global_count[2];
    /* Borrowed byte storage; 07..09 and 11..13 share little-endian views. */
    uint8_t *raw; size_t raw_size;
    int module; size_t ip, instruction_ip; unsigned opcode; int32_t syscall;
    uint32_t random_state;uint64_t instructions; KStatus status; char error[256];
} KVM;
KVM *kvm_create(void);
void kvm_destroy(KVM *vm);
/* Input bytes must remain alive until VM destruction; strings point into modules. */
int kvm_add_module(KVM *vm, const char *name, const uint8_t *data, size_t size);
int kvm_start(KVM *vm, int module);
int kvm_checkpoint_offset(KVM *vm,int module,unsigned checkpoint,size_t *offset);
int kvm_switch(KVM *vm,int module);
int kvm_call_module(KVM *vm,int module);
KStatus kvm_run(KVM *vm, unsigned budget);
int kvm_push(KVM *vm, KValue value);
int kvm_pop(KVM *vm, KValue *value);
int kvm_resume(KVM *vm);
int kvm_list_clear(KVM *vm,int32_t id);
#endif
