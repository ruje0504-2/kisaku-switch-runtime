#ifndef KISAKU_VOICE_WORKER_H
#define KISAKU_VOICE_WORKER_H
#include <stddef.h>
#include <stdint.h>
typedef struct KVoiceWorker KVoiceWorker;
KVoiceWorker *kvoice_worker_create(const char *archive);
void kvoice_worker_destroy(KVoiceWorker *worker);
int kvoice_worker_submit(KVoiceWorker *worker,const char *name,double gain);
void kvoice_worker_cancel(KVoiceWorker *worker);
/* 0 pending, 1 PCM ownership transferred, -1 decode/read failure. */
int kvoice_worker_poll(KVoiceWorker *worker,uint8_t **pcm,size_t *size);
#endif
