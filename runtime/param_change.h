/* CKisakuParamWnd 49d680: numeric part of 4a00f0, independent of AX/UI. */
#ifndef KISAKU_PARAM_CHANGE_H
#define KISAKU_PARAM_CHANGE_H
#include <stdint.h>
typedef struct {
    uint16_t start[4],target[4],steps,total_target;
    float increment[4];
} KParamChange;
/* Arguments are low unsigned words in the original assembly (movzx).
   A zero duration still has final targets; it has no interpolation iterations. */
static inline int kparam_change_plan(KParamChange *out,const int16_t values[4],
                                    uint16_t total,int32_t duration,const int32_t encoded[4]){
    KParamChange next={0};next.steps=(uint16_t)duration;
    for(unsigned i=0;i<4;i++){
        unsigned maximum=i==3?80:999;
        if(values[i]<0||(unsigned)values[i]>maximum)return -1;
        unsigned old=(unsigned)values[i],code=(uint16_t)encoded[i],target=old;
        if(code>1000){target=old+code-1000;if(target>maximum)target=maximum;}
        else if(code<1000){unsigned amount=1000-code;target=amount>old?0:old-amount;}
        next.start[i]=(uint16_t)old;next.target[i]=(uint16_t)target;
        unsigned distance=target>old?target-old:old-target;
        next.increment[i]=next.steps?(float)distance/next.steps:0;
    }
    /* 4a00f0 updates the total by the fourth item's signed difference. */
    next.total_target=(uint16_t)(total+(int)next.target[3]-(int)next.start[3]);
    *out=next;return 0;
}
#endif
