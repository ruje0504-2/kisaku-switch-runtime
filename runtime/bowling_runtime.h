#ifndef KISAKU_BOWLING_RUNTIME_H
#define KISAKU_BOWLING_RUNTIME_H
#include <stdlib.h>
#include "bowling_view.h"
#include "ax.h"
typedef struct {
    KBowlingGame game;
    KImage background,tutorial,effect;
    struct ax_player ax;
    uint8_t events[AX_CELLS];
    unsigned active,clock,second_half,closing,input_held;
    unsigned fade_steps,fade_tick,fade_clock,fade_in;
    int focus,pointer_x,pointer_y;
} KBowlingRuntime;
static inline void kbowling_runtime_free(KBowlingRuntime *r){
    if(!r)return;
    rmt_free(&r->background);rmt_free(&r->tutorial);rmt_free(&r->effect);free(r);
}
#endif
