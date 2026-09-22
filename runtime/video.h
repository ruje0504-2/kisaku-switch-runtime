#ifndef KISAKU_VIDEO_H
#define KISAKU_VIDEO_H
#include "rmt.h"
typedef struct KVideo KVideo;
typedef enum { KVIDEO_AUTO, KVIDEO_SOFTWARE } KVideoMode;
typedef struct {uint64_t hardware_frames,software_frames;unsigned cached_frames;int hardware_pending;char fallback[160];} KVideoStats;
/* Configure once before starting playback/workers. AUTO tries NVTEGRA on
   Switch; the host default stays software. Logs distinguish actual frames
   from successful device creation. */
void kvideo_set_default_mode(KVideoMode mode);
void kvideo_set_log_path(const char *path);
KVideo *kvideo_open_mode(const uint8_t *data,size_t size,KVideoMode mode);
int kvideo_stats(const KVideo *v,KVideoStats *stats);
/* Decode a standalone audio resource to owned 44100Hz stereo S16LE PCM. */
int kaudio_decode(const uint8_t *data,size_t size,uint8_t **pcm,size_t *pcm_size);
/* Borrow VSD bytes until close. Output PCM is owned by the caller. */
KVideo *kvideo_open(const uint8_t *data,size_t size);
/* MOV descriptors use the native 30 fps fallback time base; no embedded audio. */
int kvideo_range(KVideo *v,int first,int last);
void kvideo_close(KVideo *v);
/* One 60 Hz tick. 0 playing, 1 EOF, -1 decode error. */
int kvideo_step(KVideo *v,KImage *screen,uint8_t **pcm,size_t *pcm_size);
uint64_t kvideo_decoded_frames(KVideo *v);
const char *kvideo_error(KVideo *v);
#endif
