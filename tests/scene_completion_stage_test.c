/* Package 1b regression: the 31/23 scene-completion merge staged a fixed
 * `uint8_t bytes[8192]` local, then handed `v->byte_count` to
 * `kflags_progress`, which reads exactly that many bytes while merging
 * (`runtime/flags.c:96`).  The live Kisaku runtime declares 9192 bytes through
 * `14/0` (a=9192, c=600, d=15000, e=100), so the last 1000 bytes of that read
 * came from the stack and their
 * maxima were written into the saved FLAG slot - progress could gain bits that
 * no part of the runtime ever held.
 *
 * The capacity is only ever set through the `14/0` resize (`bootstrap.c`, which
 * accepts any `a <= sizeof(v->bytes)`), so the stage has to be sized from the
 * VM's own byte area: a stage pinned to the currently declared 9192 would newly
 * reject every capacity above it that `14/0` admits.
 *
 * This fixture drives the real 31/23 completion path twice - once on the live
 * 9192-byte runtime, comparing the merged slot byte-for-byte with max(saved,
 * live) in the 8192..9191 tail the old stage could not carry, and once after
 * growing the runtime to 10000 bytes through the real `14/0` entry point, where
 * it also fails if the merge rejects that capacity or drops the staged tail.
 * The deterministic detector for the over-read itself is the ASan/UBSan build,
 * because the functional result of folding uninitialised stack bytes is not
 * guaranteed to differ from the live values.
 *
 * Static data only: this does not launch the PC program and is not a Switch
 * hardware test.
 */
#include "bootstrap.h"
#include "flags.h"
#include "scene_history.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void title(KBootstrap *b)
{
    for (unsigned i = 0; i < 3000; i++) {
        assert(bootstrap_run(b, 100000) >= 0);
        if (b->title.active && b->title.age >= 64) return;
        bootstrap_frame(b);
    }
    assert(!"title timeout");
}

/* Push one syscall's argument vector in pop order and dispatch it. */
static int call_sys(KBootstrap *b, int sys, int sub, int a, int c, int d, int e)
{
    KVM *v = b->vm;
    v->status = KVM_SYSCALL;
    v->syscall = sys;
    v->sp = 0;
    if (kvm_push(v, (KValue){e, NULL})) return -1;
    if (kvm_push(v, (KValue){d, NULL})) return -1;
    if (kvm_push(v, (KValue){c, NULL})) return -1;
    if (kvm_push(v, (KValue){a, NULL})) return -1;
    if (kvm_push(v, (KValue){sub, NULL})) return -1;
    return bootstrap_dispatch(b);
}

/* Report why a dispatch failed before aborting, so a red run names the gate. */
static void expect_ok(KBootstrap *b, int rc)
{
    if (rc < 0 || b->error[0] || b->vm->sp)
        fprintf(stderr, "dispatch failed: rc=%d error='%s' sp=%u\n", rc, b->error, b->vm->sp);
    assert(rc >= 0 && !b->error[0] && !b->vm->sp);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    KBootstrap *b = bootstrap_create_split(argv[1], argv[2]);
    assert(b && !b->error[0]);
    title(b);

    KVM *v = b->vm;
    assert(v->byte_count == 9192 && v->word_count >= 381 && v->global_count[1] > 69);

    /* 44b820 (scene restore) leaves the frontend holding a SceneData copy; the
       completion merge only consumes that copy.  The title screen is reached
       before navigation exists, so build the same object here. */
    assert(!b->scene);
    b->scene = calloc(1, sizeof(*b->scene));
    assert(b->scene);
    /* Markers that must travel through the staged copy into the saved slot, so
       the fixture fails if the completion merge is never reached. */
    b->scene->visited[999] = 1;
    b->scene->status[5] = 1;
    b->scene->flags[7] = 1;

    unsigned selector = v->bytes[8100];
    assert(selector <= 3);

    KFlags *before = kflags_read_slot(bootstrap_save_dir(b), selector, 0);
    assert(before && before->byte_count == 9192);

    /* Put a live value in every tail byte the old stage dropped, so each one
       has to be carried by the staging copy and must arrive unchanged. */
    for (unsigned i = 8192; i < 9192; i++) v->bytes[i] = (uint8_t)(1 + (i & 0x7F));

    /* 31/23 a=1 c=scene d=part e=completion, pushed in pop order. */
    expect_ok(b, call_sys(b, 31, 23, 1, 1, 1, 1));

    KFlags *after = kflags_read_slot(bootstrap_save_dir(b), selector, 0);
    assert(after && after->byte_count == 9192);

    /* Proof the staged merge ran at all: the three overlays land in the slot. */
    assert(before->bytes[3999] == 0 && before->bytes[4505] == 0 && before->bytes[5007] == 0);
    assert(after->bytes[3999] == 1 && after->bytes[4505] == 1 && after->bytes[5007] == 1);

    unsigned mismatches = 0, raised = 0;
    for (unsigned i = 8192; i < 9192; i++) {
        uint8_t expect = before->bytes[i] > v->bytes[i] ? before->bytes[i] : v->bytes[i];
        if (after->bytes[i] != expect) mismatches++;
        if (after->bytes[i] != before->bytes[i]) raised++;
    }
    unsigned tail_before = 0, expected_raised = 0;
    for (unsigned i = 8192; i < 9192; i++) {
        if (before->bytes[i]) tail_before++;
        if (v->bytes[i] > before->bytes[i]) expected_raised++;
    }
    printf("31/23 completion merge: staged %u bytes, tail writes=%u/%u mismatches=%u, "
           "slot byte_count=%u, tail non-zero before=%u\n",
           v->byte_count, raised, expected_raised, mismatches, before->byte_count, tail_before);
    assert(!mismatches && raised == expected_raised && expected_raised);

    /* `14/0` admits capacities above the 9192 the story currently declares, so
       the stage must not turn those into a new rejection.  Grow through the real
       resize entry and merge again with a 10000-byte runtime. */
    expect_ok(b, call_sys(b, 14, 0, 10000, (int)v->word_count, (int)b->raw_size, (int)v->global_count[1]));
    assert(v->byte_count == 10000);

    b->scene->visited[500] = 1;          /* must reach the slot as bytes[3500] */
    for (unsigned i = 9192; i < 10000; i++) v->bytes[i] = (uint8_t)(1 + (i & 0x7F));
    assert(v->bytes[3500] == 0);

    KFlags *before2 = kflags_read_slot(bootstrap_save_dir(b), selector, 0);
    assert(before2 && before2->byte_count == 9192);
    expect_ok(b, call_sys(b, 31, 23, 1, 2, 1, 1));

    KFlags *after2 = kflags_read_slot(bootstrap_save_dir(b), selector, 0);
    assert(after2 && after2->byte_count == 9192);
    /* The overlay reached the slot again ... */
    assert(before2->bytes[3500] == 0 && after2->bytes[3500] == 1);
    /* ... and every staged byte the old 9192-sized stage could not carry came
       back untouched instead of being dropped or zeroed. */
    unsigned widened_tail = 0;
    for (unsigned i = 9192; i < 10000; i++) {
        if (v->bytes[i] != (uint8_t)(1 + (i & 0x7F))) widened_tail++;
    }
    printf("31/23 completion merge at widened capacity: staged %u bytes, "
           "tail 9192..9999 mismatches=%u, slot byte_count=%u\n",
           v->byte_count, widened_tail, after2->byte_count);
    kflags_free(after2);
    kflags_free(before2);
    kflags_free(after);
    kflags_free(before);
    bootstrap_destroy(b);
    assert(!widened_tail);
    puts("Kisaku 31/23 scene completion stages the whole runtime, folds only live bytes, and keeps every "
         "14/0 capacity from 9192 up to sizeof(v->bytes): PASS");
    return 0;
}
