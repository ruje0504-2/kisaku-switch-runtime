/* Package 1a regression: the two runtime VM-capacity gates.
 *
 * `runtime/scene_replay.inc` and `runtime/history_reset.inc` used to require
 * `byte_count == 8192`. 8192 is only `kvm_create`'s initial value; the living
 * story VM declares 9192 bytes through `14/0` (word 600 / raw 15000 /
 * bank1 100), so both gates were constantly false against the real runtime.
 * The fixture checks that 8192 is refused while the declared 9192 passes, so
 * it fails against the pre-fix code in both directions.
 *
 * Reachability, recorded honestly because it is not what the first draft of
 * the package record claimed:
 *  - scene replay is the live path (`tools/scene_replay_menu.inc:30`);
 *  - `bootstrap_history_reset` has two callers and neither can run here. The
 *    native `31/29` dialog wants `topsub.rmt`, `topsub_p.rmt` and
 *    `FlagDlg.area` - reference-project names that appear in none of 鬼作's
 *    seven archives - and the frontend panel (kind 15, tools/message_panel.inc
 *    :104) is never armed by any `p->kind=15` assignment. The reset therefore
 *    reconstructs its own precondition instead of pretending to click a
 *    checkbox, and the missing native resources are pinned below.
 *
 * Static data only: this does not launch the PC program and is not a Switch
 * hardware test.
 */
#include "bootstrap.h"
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

/* The native New Game reset dialog. It must fail here, because 鬼作 ships none
 * of its artwork; this is a characterisation of the current build, not an
 * endorsement of the reference-project implementation. */
static void test_native_reset_dialog_unavailable(KBootstrap *b)
{
    b->error[0] = 0;
    b->vm->status = KVM_SYSCALL;
    b->vm->syscall = 31;
    b->vm->sp = 0;
    assert(!kvm_push(b->vm, (KValue){29, NULL}));
    assert(bootstrap_dispatch(b) < 0);
    assert(strstr(b->error, "history dialog resource decode failed"));
    assert(!b->flag_dialog.active);
    printf("31/29 native dialog: unavailable (reference artwork absent), as recorded\n");
}

/* Precondition the reachable callers would set: a checked box in the open flag
 * dialog plus the armed reset request (bootstrap_confirm sets kind 15). */
static void test_history_reset_gate(KBootstrap *b)
{
    assert(b->vm->byte_count == 9192);
    assert(b->raw_size >= 6000 && b->read_size && b->vm->global_count[1] >= 100);

    memset(&b->flag_dialog.checked, 0, sizeof(b->flag_dialog.checked));
    b->flag_dialog.active = 1;
    b->flag_dialog.checked[0] = 1;   /* read history */
    b->extra_active = 1;
    b->extra_kind = b->extra_request = 15;
    b->vm->bytes[8100] = 0;          /* selector used for the written file names */

    /* kvm_create's default must not be mistaken for the runtime capacity: with
       8192 accepted the staging copy below silently truncates the byte area. */
    b->vm->byte_count = 8192;
    b->error[0] = 0;
    int was_loaded = (int)b->read_loaded;
    int rc = bootstrap_history_reset(b, 1);
    if (rc >= 0 || !strstr(b->error, "history reset state invalid")) {
        fprintf(stderr, "reset@8192: rc=%d err=%s\n", rc, b->error);
        abort();
    }
    assert((int)b->read_loaded == was_loaded && b->extra_active && b->extra_kind == 15);

    /* The capacity the startup script actually declares passes. */
    b->vm->byte_count = 9192;
    b->error[0] = 0;
    assert(!bootstrap_history_reset(b, 1));
    assert(b->read_loaded && !b->read_dirty && b->read_selector == 0);
    assert(b->flag_dialog.checked[0] == 2 && !b->extra_active && !b->extra_kind && !b->reset_pending);
    char path[4096];
    snprintf(path, sizeof(path), "%s/kisaku-read-0.dat", bootstrap_save_dir(b));
    FILE *written = fopen(path, "rb");
    assert(written);
    assert(!fclose(written));
    remove(path);
    b->flag_dialog.active = 0;
    printf("history reset gate : 8192 rejected, 9192 accepted (%u-byte VM)\n", b->vm->byte_count);
}

/* Scene replay copies the frozen story runtime into a second instance.
 *
 * Two things had to be separated here. `runtime/scene_transitions.inc` and
 * `runtime/scene_catalog.inc` are generated from the reference project's EXE
 * (tools/generate_scene_transitions.py / native_scene_routes.py default to
 * KAWA2_EXE), and the 416 alias names they carry have *zero* overlap with the
 * 682 modules in 鬼作's mes.arc. So `kscene_module(target.module) !=
 * target.scene` rejects every history entry that names a real 鬼作 module
 * before the capacity gate is ever reached - the 8192 check was a second,
 * currently unreachable defect. Both facts are pinned below: the foreign-table
 * rejection as characterisation, and the capacity gate by registering the
 * reference name the table knows and cloning a real module under that name.
 */
static unsigned module_id(const KBootstrap *b, const char *name)
{
    for (unsigned i = 0; i < b->vm->module_count; i++)
        if (!strcmp(b->vm->modules[i].name, name)) return i;
    return b->vm->module_count;
}

/* First catalog slot for scene 1, registering `module` at checkpoint 0. */
static unsigned register_scene_slot(KSceneHistory *history, const char *dir,
                                    const char *module, unsigned *part_out)
{
    for (unsigned part = 1; part <= 30; part++) {
        if (khistory_register(history, dir, 1, (int)part, module, 0)) continue;
        if (khistory_completion(history, dir, 1, (int)part, 1, NULL)) continue;
        for (unsigned s = 0; s < 600; s++) {
            KSceneCheckpoint target;
            if (!khistory_checkpoint(history, s, &target) && !strcmp(target.module, module)) {
                *part_out = part;
                return s;
            }
        }
    }
    return 600;
}

static void test_scene_replay_gate(KBootstrap *replay, KBootstrap *owner)
{
    assert(replay != owner && replay->title.active && owner->title.active);
    assert(owner->vm->byte_count == 9192);
    assert(owner->vm->word_count >= 381 && owner->vm->global_count[0] > 50 &&
           owner->vm->global_count[1] > 69);
    assert(!owner->scene_replay && !strcmp(replay->root, owner->root));

    KSceneHistory history = {0};
    const char *dir = bootstrap_save_dir(owner);
    assert(!khistory_load(&history, dir));

    /* Characterisation: the alias table belongs to the reference game. */
    assert(kscene_module("s01.mes") == 1);        /* in the table, not in 鬼作 */
    assert(kscene_module("lev00_1.mes") == 0);    /* in 鬼作's mes.arc, not in the table */
    unsigned part = 0;
    unsigned slot = register_scene_slot(&history, dir, "lev00_1.mes", &part);
    assert(slot < 600);
    owner->error[0] = 0;
    replay->error[0] = 0;
    owner->vm->byte_count = 9192;
    assert(bootstrap_scene_replay_begin(replay, owner, slot) < 0);
    assert(!replay->scene_replay);
    printf("scene replay       : real 鬼作 history entry rejected by the reference alias table\n");

    /* Give the VM a module carrying a name the table accepts, cloned from a
       real one, so the capacity gate itself becomes reachable. */
    unsigned source = module_id(replay, "liblary.lib");
    assert(source < replay->vm->module_count);
    KModule *origin = &replay->vm->modules[source];
    uint8_t *copy = malloc(origin->size);
    assert(copy);
    memcpy(copy, origin->code, origin->size);
    int added = kvm_add_module(replay->vm, "s01.mes", copy, origin->size);
    assert(added >= 0);
    unsigned checkpoint = replay->vm->modules[added].checkpoint_count;
    for (unsigned c = 0; c < replay->vm->modules[added].checkpoint_count; c++) {
        size_t offset;
        if (!kvm_checkpoint_offset(replay->vm, added, c, &offset)) {
            checkpoint = c;
            break;
        }
    }
    assert(checkpoint < replay->vm->modules[added].checkpoint_count);
    assert(!khistory_register(&history, dir, 1, (int)part, "s01.mes", (int32_t)checkpoint));
    assert(!khistory_completion(&history, dir, 1, (int)part, 1, NULL));
    replay->error[0] = 0;
    owner->error[0] = 0;

    /* 8192 must be refused: before the fix it was accepted and the replay
       started from a runtime that is not the live one. */
    owner->vm->byte_count = 8192;
    assert(bootstrap_scene_replay_begin(replay, owner, slot) < 0);
    assert(!replay->scene_replay && !replay->error[0]);

    owner->vm->byte_count = 9192;
    assert(!bootstrap_scene_replay_begin(replay, owner, slot));
    assert(replay->scene_replay && !replay->title.active);
    assert(replay->vm->byte_count == owner->vm->byte_count);
    assert(replay->vm->word_count == owner->vm->word_count);
    assert(replay->vm->globals[0][48].number == (int32_t)checkpoint);
    assert(replay->vm->globals[1][69].number == 1);
    printf("scene replay gate  : 8192 rejected, 9192 accepted (slot %u, cp%u)\n", slot, checkpoint);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    KBootstrap *b = bootstrap_create_split(argv[1], argv[2]);
    assert(b && !b->error[0]);
    title(b);
    test_native_reset_dialog_unavailable(b);
    test_history_reset_gate(b);
    b->error[0] = 0;

    KBootstrap *replay = bootstrap_create_split(argv[1], argv[2]);
    assert(replay && !replay->error[0]);
    title(replay);
    test_scene_replay_gate(replay, b);

    bootstrap_destroy(replay);
    bootstrap_destroy(b);
    puts("VM capacity gates accept the declared 9192-byte runtime and reject 8192");
    return 0;
}
