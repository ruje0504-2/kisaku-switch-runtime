#ifndef KISAKU_SOUND_VOLUME_H
#define KISAKU_SOUND_VOLUME_H
/* 464610 / 464680 / 4646f0: Voice, Effect and Music use the same
   0..104 slider, converted to DirectSound hundredths of a decibel. */
static inline int kisaku_sound_volume_db(int volume,int enabled){
    if(!enabled)return -10000;
    if(volume<0)volume=0;
    if(volume>104)volume=104;
    int delta=104-volume;
    return -((delta+100)*delta)/10;
}
#endif
