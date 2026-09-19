/* GPL-3.0-or-later. Research helper: log every SYSCALL the interpreter would
 * pause on while executing start.mes, then the module it finally switches to.
 * A host syscall handler is *synthetically acknowledged* so flow advances, but
 * no handler is actually implemented (correct report stays "static/bootstrap
 * boundary reached", never gameplay). Uses copies under local builds; does NOT
 * touch ../.. runtime sources.
 *
 * Convention logged below matches AI6WIN.exe: before opcode 0x18 the VM has the
 * arg list (bottom..top) then the MAIN selector then the sub selector on top.
 * The 0x18 op already popped MAIN into v->syscall, so at pause the printed
 * stack is <arg0..argN, sub>.  EXE main dispatch then pops MAIN (done), and the
 * per-main SubDispatcher pops the trailing entry printed last == sub.
*/
#include "vm.h"
#include "ai6arc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static Ai6Entry *find(Ai6Archive *a, const char *n) {
    for (uint32_t i = 0; i < a->count; i++)
        if (!strcmp(a->entries[i].name, n)) return &a->entries[i];
    return NULL;
}

static void dump(KVM *v) {
    printf("   remaining VM stack (== ABI args, then trailing sub selector):");
    for (unsigned i = 0; i < v->sp; i++) {
        if (v->stack[i].string) printf(" \"%.40s\"", v->stack[i].string);
        else printf(" %d", v->stack[i].number);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s mes.arc\n", argv[0]); return 2; }
    Ai6Archive a;
    if (ai6_open(&a, argv[1])) return 1;
    KVM *v = kvm_create();
    if (!v) { ai6_close(&a); return 1; }
    uint8_t *dt = NULL, *li = NULL;
    size_t st = 0, lt = 0;
    Ai6Entry *e;
    if ((e = find(&a, "start.mes"))) ai6_read(&a, (uint32_t)(e - a.entries), &dt, &st);
    if ((e = find(&a, "liblary.lib"))) ai6_read(&a, (uint32_t)(e - a.entries), &li, &lt);
    int mid_li = kvm_add_module(v, "liblary.lib", li, lt);
    (void)mid_li;
    kvm_add_module(v, "start.mes", dt, st);
    /* First reproduce LIB registration for the module that start.mes will rely on,
       exactly like the shipped probe: register liblary functions by running liblary. */
    int m = 0;
    /* Actually module 0 is liblary.lib, run it to register its functions. */
    KStatus r = (KStatus)0;
    kvm_start(v, 0);
    r = kvm_run(v, 10000000);
    if (r == KVM_DONE || r == KVM_YIELD) {
        unsigned n = 0;
        for (unsigned i = 0; i < 1024; i++) n += v->functions[i].valid;
        printf("[liblary.lib] registered %u functions\n", n);
    } else if (r == KVM_ERROR) {
        printf("[liblary.lib] error: %s\n", v->error); return 1;
    }
    /* Now start.mes */
    m = (v->module_count > 0) ? 1 : -1; /* module id assigned to start.mes */
    kvm_start(v, m);
    unsigned guard = 0;
    for (;;) {
        KStatus s = kvm_run(v, 1000000);
        if (s == KVM_SYSCALL) {
            printf("SYSCALL main=%d  in %-10s code_off=0x%zx ip_next=0x%zx\n",
                   v->syscall, v->modules[v->module].name, v->instruction_ip, v->ip);
            dump(v);
            /* Optionally pop a pseudo "sub" so a synthetic return can be pushed
               for calls that the bytecode expects to return a value.  Not needed
               to discover the bootstrap arg lists of unassigned system calls. */
            if (++guard > 20000) { printf("guard exceeded\n"); break; }
            kvm_resume(v);
        } else if (s == KVM_DONE) {
            printf("DONE (start.mes ended) final sp=%u\n", v->sp); break;
        } else if (s == KVM_ERROR) {
            printf("ERROR: %s\n", v->error); break;
        } else if (s == KVM_YIELD) {
            printf("YIELD at 0x%zx\n", v->ip); break;
        } else if (s == KVM_BUDGET) { continue; }
        else { printf("OTHER status=%d\n", (int)s); break; }
    }
    free(dt); free(li);
    ai6_close(&a);
    kvm_destroy(v);
    return 0;
}
