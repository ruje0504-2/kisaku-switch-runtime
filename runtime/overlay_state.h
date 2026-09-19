#ifndef KISAKU_OVERLAY_STATE_H
#define KISAKU_OVERLAY_STATE_H
/* Visibility of the four native overlay sprites and the independent badge.
   Image construction/show calls must update these fields when implemented. */
typedef struct {
    unsigned visible[4],saved[4];
    unsigned active,saved_active,badge_visible,badge_saved;
} KOverlayState;
static inline void koverlay_suspend(KOverlayState *s){
    /* 46c170 stores indices 3,2,1,0 and disables the group. 45c800
       separately stores the badge. Repeated suspend replaces the snapshot. */
    s->saved_active=s->active;s->active=0;
    for(unsigned i=0;i<4;i++){s->saved[i]=s->visible[i];if(s->visible[i]==1)s->visible[i]=0;}
    s->badge_saved=s->badge_visible;if(s->badge_visible==1)s->badge_visible=0;
}
static inline void koverlay_restore(KOverlayState *s){
    /* 46c080 only shows previously visible sprites; it does not hide a
       sprite shown meanwhile. 45c7d0 retains the badge snapshot. */
    for(unsigned i=0;i<4;i++){if(s->saved[i]==1)s->visible[i]=1;s->saved[i]=0;}
    s->active=s->saved_active;if(s->badge_saved==1)s->badge_visible=1;
}
#endif
