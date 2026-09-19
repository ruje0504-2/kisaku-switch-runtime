#ifndef KISAKU_IMAGE_WORKER_H
#define KISAKU_IMAGE_WORKER_H
#include "rmt.h"
typedef struct KImageWorker KImageWorker;
KImageWorker *kimage_worker_create(const char *archive);
void kimage_worker_destroy(KImageWorker *worker);
/* One outstanding request. Names are copied; the worker owns its archive. */
int kimage_worker_submit(KImageWorker *worker,const char *name);
/* 0 pending, 1 transfers image ownership, -1 read/decode failure. */
int kimage_worker_poll(KImageWorker *worker,KImage *image);
#endif
