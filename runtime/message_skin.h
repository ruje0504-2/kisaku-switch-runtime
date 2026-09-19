#ifndef KISAKU_MESSAGE_SKIN_H
#define KISAKU_MESSAGE_SKIN_H
#include "rmt.h"
typedef struct {KImage atlas,buttons[6],background;} KMessageSkin;
static inline void kmessage_skin_free(KMessageSkin *s){
    rmt_free(&s->atlas);for(unsigned i=0;i<6;i++)rmt_free(&s->buttons[i]);rmt_free(&s->background);
}
#endif
