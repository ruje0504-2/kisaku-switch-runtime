#include "video.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
struct KVideo {
    const uint8_t *data;size_t size,pos;
    AVFormatContext *format;AVIOContext *io;
    AVCodecContext *video,*audio;AVFrame *frame,*sound;AVPacket *packet;
    struct SwsContext *scale;SwrContext *resample;
    int vi,ai,eof,pending,finished,range_first;int64_t range_start,range_end;int64_t origin,pts;uint64_t ticks,decoded;
    AVFrame *cache[128];int64_t cache_pts[128];
    unsigned cache_count,cache_at;size_t cache_bytes;
    int cache_first,cache_last,cache_enabled,cache_complete,cache_playing,seek_check;
    char error[160];
};
static int fail(KVideo *v,const char *s){snprintf(v->error,sizeof(v->error),"%s",s);return -1;}
static int read_data(void *ctx,uint8_t *out,int n){KVideo *v=ctx;if(v->pos==v->size)return AVERROR_EOF;size_t count=(size_t)n;if(count>v->size-v->pos)count=v->size-v->pos;memcpy(out,v->data+v->pos,count);v->pos+=count;return (int)count;}
static int64_t seek_data(void *ctx,int64_t n,int whence){
    KVideo *v=ctx;if(whence==AVSEEK_SIZE)return (int64_t)v->size;
    int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?(int64_t)v->pos:whence==SEEK_END?(int64_t)v->size:-1;
    if(base<0||n< -base||n>(int64_t)v->size-base)return AVERROR(EINVAL);
    v->pos=(size_t)(base+n);return (int64_t)v->pos;
}
static AVCodecContext *decoder(AVFormatContext *f,int index){
    if(index<0)return NULL;
    const AVCodec *codec=avcodec_find_decoder(f->streams[index]->codecpar->codec_id);
    AVCodecContext *c=codec?avcodec_alloc_context3(codec):NULL;
    if(!c)return NULL;
    c->thread_count=1;
    if(avcodec_parameters_to_context(c,f->streams[index]->codecpar)<0||avcodec_open2(c,codec,NULL)<0){avcodec_free_context(&c);return NULL;}return c;
}
KVideo *kvideo_open(const uint8_t *d,size_t n){
    if(!d||n<8||memcmp(d,"VSD1",4))return NULL;
    size_t skip=8+(size_t)((uint32_t)d[4]|((uint32_t)d[5]<<8)|((uint32_t)d[6]<<16)|((uint32_t)d[7]<<24));if(skip>=n)return NULL;
    KVideo *v=calloc(1,sizeof(*v));if(!v)return NULL;
    v->data=d+skip;v->size=n-skip;v->vi=v->ai=-1;v->origin=AV_NOPTS_VALUE;
    uint8_t *buffer=av_malloc(32768);if(!buffer)goto bad;
    v->io=avio_alloc_context(buffer,32768,0,v,read_data,NULL,seek_data);if(!v->io){av_free(buffer);goto bad;}
    v->format=avformat_alloc_context();if(!v->format)goto bad;v->format->pb=v->io;v->format->flags|=AVFMT_FLAG_CUSTOM_IO;
    if(avformat_open_input(&v->format,NULL,NULL,NULL)<0||avformat_find_stream_info(v->format,NULL)<0)goto bad;
    v->vi=av_find_best_stream(v->format,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0);
    v->ai=av_find_best_stream(v->format,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);
    v->video=decoder(v->format,v->vi);if(!v->video||v->video->width<1||v->video->height<1||v->video->width>2048||v->video->height>2048)goto bad;
    if(v->ai>=0){
        v->audio=decoder(v->format,v->ai);if(!v->audio)goto bad;
        AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
        if(swr_alloc_set_opts2(&v->resample,&stereo,AV_SAMPLE_FMT_S16,44100,&v->audio->ch_layout,v->audio->sample_fmt,v->audio->sample_rate,0,NULL)<0||swr_init(v->resample)<0)goto bad;
    }
    v->frame=av_frame_alloc();v->sound=av_frame_alloc();v->packet=av_packet_alloc();if(!v->frame||!v->sound||!v->packet)goto bad;return v;
 bad:kvideo_close(v);return NULL;
}
static void cache_clear(KVideo *v){
    for(unsigned i=0;i<v->cache_count;i++)av_frame_free(&v->cache[i]);
    v->cache_count=v->cache_at=0;v->cache_bytes=0;v->cache_complete=v->cache_playing=0;
}
static int seek_video(KVideo *v,int64_t target){
    if(av_seek_frame(v->format,v->vi,target,AVSEEK_FLAG_BACKWARD)<0)return fail(v,"MOV seek failed");
    avcodec_flush_buffers(v->video);av_frame_unref(v->frame);av_packet_unref(v->packet);
    v->eof=v->pending=v->finished=0;return 0;
}
int kvideo_range(KVideo *v,int first,int last){
    if(!v||first<0||last<=first||v->audio)return -1;
    AVStream *stream=v->format->streams[v->vi];
    int64_t origin=stream->start_time==AV_NOPTS_VALUE?0:av_rescale_q(stream->start_time,stream->time_base,AV_TIME_BASE_Q);
    int64_t start=av_rescale_q(first,(AVRational){1,30},AV_TIME_BASE_Q);
    int replay=v->cache_complete&&v->cache_first==first&&v->cache_last==last;
    if(!replay){
        cache_clear(v);v->cache_enabled=1;v->cache_first=first;v->cache_last=last;
        /* MPEG program-stream seeks can land AFTER the requested keyframe
           when its DTS precedes its PTS. Seek earlier, then decode forward. */
        int64_t preroll=origin+start-1000000;if(preroll<0)preroll=0;
        int64_t target=av_rescale_q(preroll,AV_TIME_BASE_Q,stream->time_base);
        if(seek_video(v,target))return -1;
        v->seek_check=target!=0;
    }else{av_frame_unref(v->frame);v->cache_at=0;v->cache_playing=1;}
    v->eof=v->pending=v->finished=0;v->ticks=0;v->origin=origin;v->range_first=first;v->range_start=start;v->range_end=av_rescale_q(last,(AVRational){1,30},AV_TIME_BASE_Q);
    return 0;
}
void kvideo_close(KVideo *v){if(!v)return;cache_clear(v);av_packet_free(&v->packet);av_frame_free(&v->frame);av_frame_free(&v->sound);sws_freeContext(v->scale);swr_free(&v->resample);avcodec_free_context(&v->video);avcodec_free_context(&v->audio);avformat_close_input(&v->format);if(v->io){av_freep(&v->io->buffer);avio_context_free(&v->io);}free(v);}
const char *kvideo_error(KVideo *v){return v->error;}
uint64_t kvideo_decoded_frames(KVideo *v){return v->decoded;}
static int audio_frames(KVideo *v,uint8_t **pcm,size_t *bytes){
    int rc;while((rc=avcodec_receive_frame(v->audio,v->sound))>=0){
        int samples=swr_get_out_samples(v->resample,v->sound->nb_samples);
        if(samples<0||samples>1000000||*bytes>64*1024*1024-(size_t)samples*4)return fail(v,"video PCM limit");
        uint8_t *p=realloc(*pcm,*bytes+(size_t)samples*4);if(!p)return fail(v,"video PCM allocation failed");*pcm=p;
        uint8_t *out=p+*bytes;int count=swr_convert(v->resample,&out,samples,(const uint8_t **)v->sound->extended_data,v->sound->nb_samples);
        av_frame_unref(v->sound);if(count<0)return fail(v,"audio conversion failed");*bytes+=(size_t)count*4;
    }
    return rc==AVERROR(EAGAIN)||rc==AVERROR_EOF?0:fail(v,"audio decode failed");
}
/* Shared resampler tail for VSD audio and standalone voice streams. */
static int audio_tail(KVideo *v,uint8_t **pcm,size_t *bytes){
    int samples=swr_get_out_samples(v->resample,0);
    if(samples<0||samples>1000000||*bytes>64*1024*1024-(size_t)samples*4)return fail(v,"audio tail limit");
    if(samples){
        uint8_t *p=realloc(*pcm,*bytes+(size_t)samples*4);if(!p)return fail(v,"audio tail allocation failed");*pcm=p;
        uint8_t *out=p+*bytes;int count=swr_convert(v->resample,&out,samples,NULL,0);
        if(count<0)return fail(v,"audio tail conversion failed");
        *bytes+=(size_t)count*4;
    }
    return 0;
}
int kaudio_decode(const uint8_t *data,size_t size,uint8_t **pcm,size_t *bytes){
    if(!pcm||!bytes)return -1;
    *pcm=NULL;*bytes=0;if(!data||!size||size>256*1024*1024)return -1;
    KVideo *v=calloc(1,sizeof(*v));if(!v)return -1;
    int result=-1;v->data=data;v->size=size;v->ai=v->vi=-1;
    uint8_t *buffer=av_malloc(32768);if(!buffer)goto done;
    v->io=avio_alloc_context(buffer,32768,0,v,read_data,NULL,seek_data);if(!v->io){av_free(buffer);goto done;}
    v->format=avformat_alloc_context();if(!v->format)goto done;
    v->format->pb=v->io;v->format->flags|=AVFMT_FLAG_CUSTOM_IO;
    if(avformat_open_input(&v->format,NULL,NULL,NULL)<0||avformat_find_stream_info(v->format,NULL)<0)goto done;
    v->ai=av_find_best_stream(v->format,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);v->audio=decoder(v->format,v->ai);
    if(!v->audio||v->audio->sample_rate<8000||v->audio->sample_rate>192000||v->audio->ch_layout.nb_channels<1||v->audio->ch_layout.nb_channels>2)goto done;
    AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
    if(swr_alloc_set_opts2(&v->resample,&stereo,AV_SAMPLE_FMT_S16,44100,&v->audio->ch_layout,v->audio->sample_fmt,v->audio->sample_rate,0,NULL)<0||swr_init(v->resample)<0)goto done;
    v->sound=av_frame_alloc();v->packet=av_packet_alloc();if(!v->sound||!v->packet)goto done;
    for(unsigned guard=0;guard<100000;guard++){
        int rc=av_read_frame(v->format,v->packet);
        if(rc==AVERROR_EOF){
            if(avcodec_send_packet(v->audio,NULL)<0||audio_frames(v,pcm,bytes)||audio_tail(v,pcm,bytes)||!*bytes)goto done;
            result=0;break;
        }
        if(rc<0)goto done;
        if(v->packet->stream_index==v->ai){rc=avcodec_send_packet(v->audio,v->packet);if(rc>=0)rc=audio_frames(v,pcm,bytes);}
        av_packet_unref(v->packet);if(rc<0)goto done;
    }
 done:kvideo_close(v);if(result){free(*pcm);*pcm=NULL;*bytes=0;}return result;
}
static int next(KVideo *v,uint8_t **pcm,size_t *bytes){
    for(unsigned guard=0;guard<100000;guard++){
        int rc=avcodec_receive_frame(v->video,v->frame);
        if(rc>=0){
            int64_t pts=v->frame->best_effort_timestamp;
            if(pts!=AV_NOPTS_VALUE){pts=av_rescale_q(pts,v->format->streams[v->vi]->time_base,AV_TIME_BASE_Q);if(v->origin==AV_NOPTS_VALUE)v->origin=pts;v->pts=pts-v->origin;}
            else v->pts=(int64_t)v->decoded*1000000/60;
            v->decoded++;v->pending=1;return 0;
        }
        if(rc==AVERROR_EOF){v->finished=1;return 1;}
        if(rc!=AVERROR(EAGAIN))return fail(v,"video decode failed");
        if(v->eof)return fail(v,"video drain stalled");
        rc=av_read_frame(v->format,v->packet);
        if(rc<0){
            if(rc!=AVERROR_EOF)return fail(v,"video demux failed");
            v->eof=1;if(avcodec_send_packet(v->video,NULL)<0)return fail(v,"video flush failed");
            if(v->audio&&(avcodec_send_packet(v->audio,NULL)<0||audio_frames(v,pcm,bytes)))return fail(v,"audio flush failed");
            if(v->audio&&audio_tail(v,pcm,bytes))return -1;
            continue;
        }
        if(v->packet->stream_index==v->vi)rc=avcodec_send_packet(v->video,v->packet);
        else if(v->packet->stream_index==v->ai){rc=avcodec_send_packet(v->audio,v->packet);if(rc>=0)rc=audio_frames(v,pcm,bytes);}else rc=0;
        av_packet_unref(v->packet);if(rc<0)return fail(v,"packet decode failed");
    }
    return fail(v,"video packet budget exceeded");
}
static int present(KVideo *v,AVFrame *frame,KImage *screen){
    v->scale=sws_getCachedContext(v->scale,frame->width,frame->height,frame->format,(int)screen->width,(int)screen->height,AV_PIX_FMT_BGRA,SWS_BILINEAR,NULL,NULL,NULL);
    if(!v->scale)return fail(v,"video scaler failed");
    uint8_t *dst[]={screen->pixels,NULL,NULL,NULL};int stride[]={(int)screen->stride,0,0,0};
    if(sws_scale(v->scale,(const uint8_t *const*)frame->data,frame->linesize,0,frame->height,dst,stride)<0)return fail(v,"video conversion failed");
    return 0;
}
static void cache_frame(KVideo *v){
    if(!v->range_end||!v->cache_enabled)return;
    size_t bytes=0;
    for(unsigned i=0;i<AV_NUM_DATA_POINTERS;i++)if(v->frame->buf[i])bytes+=v->frame->buf[i]->size;
    for(int i=0;i<v->frame->nb_extended_buf;i++)bytes+=v->frame->extended_buf[i]->size;
    if(v->cache_count==128||bytes>64*1024*1024-v->cache_bytes){cache_clear(v);v->cache_enabled=0;return;}
    AVFrame *frame=av_frame_clone(v->frame);
    if(!frame){cache_clear(v);v->cache_enabled=0;return;}
    v->cache[v->cache_count]=frame;v->cache_pts[v->cache_count++]=v->pts;v->cache_bytes+=bytes;
}
int kvideo_step(KVideo *v,KImage *screen,uint8_t **pcm,size_t *bytes){
    if(v->error[0])return -1;
    if(v->finished)return 1;
    /* Match timestamp rounding rather than introducing a 2/3/1 cadence. */
    int64_t clock=av_rescale_q((int64_t)v->range_first*2+(int64_t)v->ticks++,(AVRational){1,60},AV_TIME_BASE_Q);
    if(v->range_end&&clock>=v->range_end){
        v->finished=1;if(v->cache_enabled&&v->cache_count)v->cache_complete=1;return 1;
    }
    if(v->cache_playing){
        while(v->cache_at<v->cache_count&&v->cache_pts[v->cache_at]<=clock+1){
            if(present(v,v->cache[v->cache_at++],screen))return -1;
        }
        return 0;
    }
    for(unsigned guard=0;guard<1000;guard++){
        if(!v->pending){int rc=next(v,pcm,bytes);if(rc)return rc;}
        if(v->seek_check){
            v->seek_check=0;
            if(v->pts>v->range_start+1){if(seek_video(v,0))return -1;continue;}
        }
        if(v->range_end&&v->pts+1<v->range_start){av_frame_unref(v->frame);v->pending=0;continue;}
        if(v->range_end&&v->pts+1>=v->range_end)return 0;
        if(v->pts>clock+1)return 0;
        cache_frame(v);
        if(present(v,v->frame,screen))return -1;
        av_frame_unref(v->frame);v->pending=0;
    }
    return fail(v,"video frame budget exceeded");
}
