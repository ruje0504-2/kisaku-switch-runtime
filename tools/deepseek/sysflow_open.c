/* Same driver but for open.mes standalone (module id 1 after liblary). */
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

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s mes.arc entry\n", argv[0]); return 2; }
    Ai6Archive a; if (ai6_open(&a, argv[1])) return 1;
    KVM *v = kvm_create(); if (!v) return 1;
    uint8_t *dt = NULL, *li = NULL; size_t st = 0, lt = 0;
    Ai6Entry *e;
    if ((e = find(&a, "liblary.lib"))) ai6_read(&a, (uint32_t)(e - a.entries), &li, &lt);
    if ((e = find(&a, argv[2]))) ai6_read(&a, (uint32_t)(e - a.entries), &dt, &st);
    kvm_add_module(v, "liblary.lib", li, lt);
    int mid = kvm_add_module(v, argv[2], dt, st);
    kvm_start(v, 0); kvm_run(v, 10000000); /* register lib funcs */
    kvm_start(v, mid);
    unsigned guard = 0;
    for (;;) {
        KStatus s = kvm_run(v, 2000000);
        if (s == KVM_SYSCALL) {
            printf("SYSCALL main=%d in %-10s code_off=0x%zx\n", v->syscall, v->modules[v->module].name, v->instruction_ip);
            printf("   tail:");
            unsigned start = v->sp > 14 ? v->sp - 14 : 0;
            for (unsigned i = start; i < v->sp; i++) {
                if (v->stack[i].string) printf(" \"%.24s\"", v->stack[i].string);
                else printf(" %d", v->stack[i].number);
            }
            printf("\n");
            if (++guard > 4000) { printf("guard hit\n"); break; }
            kvm_resume(v);
        } else if (s == KVM_DONE) { printf("DONE\n"); break; }
        else if (s == KVM_ERROR) { printf("ERROR: %s\n", v->error); break; }
        else if (s == KVM_YIELD) { printf("YIELD ip=0x%zx\n", v->ip); break; }
        else if (s == KVM_BUDGET) { continue; }
        else { printf("OTHER %d\n", (int)s); break; }
    }
    free(dt); free(li); ai6_close(&a); kvm_destroy(v);
    return 0;
}
