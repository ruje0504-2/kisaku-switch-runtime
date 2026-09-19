#ifndef KISAKU_MOV_H
#define KISAKU_MOV_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    const uint8_t *data;size_t size,ip,begin;
    unsigned count,entry,requested,changing,depth;
    struct {size_t ip;int32_t remaining;} loops[64];
    const char *video,*audio;
    int32_t first,last;
    char error[128];
} KMov;
/* Borrow bytes. Events: 0 stop, 1 segment, 2 play audio, 3 stop audio, -1 error. */
int kmov_open(KMov *m,const uint8_t *data,size_t size,unsigned entry);
int kmov_request(KMov *m,unsigned entry);
int kmov_next(KMov *m);
#endif
