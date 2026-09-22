#ifndef KISAKU_SCENE_MODE_H
#define KISAKU_SCENE_MODE_H
#include "scene_mode_catalog.h"
/* 470270, 4705d0, 472700. Indices are native categories, not resource IDs. */
static inline unsigned kscene_mode_variants(unsigned cat,unsigned item){
    if(cat==3&&((item>=11&&item<=13)||(item>=18&&item<=20)))return 3;
    if(cat==7&&((item>=8&&item<=10)||(item>=14&&item<=16)))return 3;
    if(cat==8){static const uint8_t n[15]={0,0,0,0,0,0,4,0,7,8,5,3,0,9,6};return item<15?n[item]:0;}
    return cat==9&&item==8?3:0;
}
static inline unsigned kscene_mode_flag_value(const uint8_t *bytes,unsigned size,unsigned flag){return flag&&flag<size?bytes[flag]:0;}
static inline int kscene_mode_unlocked(const uint8_t *bytes,unsigned size,unsigned cat,unsigned item,int variant){
    if(cat>=11||item>=kscene_mode_counts[cat])return 0;
    unsigned flag=kscene_mode_flags[cat][item],value=kscene_mode_flag_value(bytes,size,flag);
    unsigned extra=cat==3&&item>=11&&item<=13?3541+(item-11)*2:cat==3&&item>=18&&item<=20?3547+(item-18)*2:0;
    if(variant<0){
        if(cat==8&&item==14){for(unsigned i=3496;i<=3501;i++)if(kscene_mode_flag_value(bytes,size,i))return 1;return 0;}
        return value||(extra&&(kscene_mode_flag_value(bytes,size,extra)||kscene_mode_flag_value(bytes,size,extra+1)));
    }
    if((unsigned)variant>=kscene_mode_variants(cat,item))return 0;
    if(cat==7)return (value&(1u<<variant))!=0;
    if(cat==3)flag=variant?extra+(unsigned)variant-1:flag;
    else if(cat==8&&item==11)flag=3481+(unsigned)variant;
    else flag+=1+(unsigned)variant;
    return kscene_mode_flag_value(bytes,size,flag)!=0;
}
#endif
