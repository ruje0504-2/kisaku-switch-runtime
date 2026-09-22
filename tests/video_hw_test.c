/* Portable fault injection for FFmpeg device negotiation and frame ownership.
   This is not an NVDEC hardware test. Real decoding is tested separately. */
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
static int device_available=1,codec_available=1,device_error,transfer_error,freed;
static enum AVHWDeviceType fake_type(const char *name){assert(!strcmp(name,"nvtegra"));return device_available?AV_HWDEVICE_TYPE_VAAPI:AV_HWDEVICE_TYPE_NONE;}
static const AVCodecHWConfig *fake_config(const AVCodec *codec,int index){
    (void)codec;static const AVCodecHWConfig config={AV_PIX_FMT_VAAPI,AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX,AV_HWDEVICE_TYPE_VAAPI};
    return !index&&codec_available?&config:NULL;
}
static int fake_device(AVBufferRef **out,enum AVHWDeviceType type,const char *device,AVDictionary *opts,int flags){
    (void)device;(void)opts;(void)flags;assert(type==AV_HWDEVICE_TYPE_VAAPI);
    if(device_error)return AVERROR(EIO);
    *out=av_buffer_alloc(1);return *out?0:AVERROR(ENOMEM);
}
static int fake_transfer(AVFrame *dst,const AVFrame *src,int flags){
    assert(src->format==AV_PIX_FMT_VAAPI&&!flags);
    if(transfer_error)return AVERROR(EIO);
    dst->format=AV_PIX_FMT_YUV420P;dst->width=src->width;dst->height=src->height;
    int rc=av_frame_get_buffer(dst,32);if(rc<0)return rc;
    memset(dst->data[0],70,(size_t)dst->linesize[0]*dst->height);
    memset(dst->data[1],128,(size_t)dst->linesize[1]*(dst->height/2));
    memset(dst->data[2],128,(size_t)dst->linesize[2]*(dst->height/2));return 0;
}
static int fail_open;
static int fake_open(AVCodecContext *c,const AVCodec *codec,AVDictionary **options){
    if(fail_open&&c->hw_device_ctx)return AVERROR(EIO);
    return avcodec_open2(c,codec,options);
}
#define avcodec_open2 fake_open
#define av_hwdevice_find_type_by_name fake_type
#define avcodec_get_hw_config fake_config
#define av_hwdevice_ctx_create fake_device
#define av_hwframe_transfer_data fake_transfer
#include "../runtime/video.c"
#undef avcodec_open2
#undef av_hwdevice_find_type_by_name
#undef avcodec_get_hw_config
#undef av_hwdevice_ctx_create
#undef av_hwframe_transfer_data
static void hardware_free(void *owner,uint8_t *data){(void)owner;freed++;av_free(data);}
int main(void){
    KVideo v={0};v.hw_format=AV_PIX_FMT_NONE;
    AVCodecContext *c=avcodec_alloc_context3(NULL);assert(c);
    const AVCodec *codec=avcodec_find_decoder(AV_CODEC_ID_MPEG1VIDEO);assert(codec);
    device_available=0;assert(video_hardware(&v,c,codec)<0&&!c->hw_device_ctx&&strstr(v.stats.fallback,"no NVTEGRA"));
    device_available=1;codec_available=0;assert(video_hardware(&v,c,codec)<0&&!c->hw_device_ctx);
    codec_available=1;device_error=1;assert(video_hardware(&v,c,codec)<0&&!c->hw_device_ctx&&strstr(v.stats.fallback,"creation"));
    device_error=0;assert(!video_hardware(&v,c,codec)&&c->hw_device_ctx&&v.stats.hardware_pending);
    enum AVPixelFormat offered[]={AV_PIX_FMT_VAAPI,AV_PIX_FMT_YUV420P,AV_PIX_FMT_NONE};
    assert(video_format(c,offered)==AV_PIX_FMT_VAAPI);
    v.frame=av_frame_alloc();v.download=av_frame_alloc();assert(v.frame&&v.download);
    v.frame->format=AV_PIX_FMT_VAAPI;v.frame->width=16;v.frame->height=16;
    v.frame->pts=v.frame->best_effort_timestamp=1234;v.frame->color_range=AVCOL_RANGE_MPEG;
    v.frame->buf[0]=av_buffer_create(av_malloc(1),1,hardware_free,NULL,0);assert(v.frame->buf[0]);
    v.frame->hw_frames_ctx=av_buffer_alloc(1);assert(v.frame->hw_frames_ctx);
    transfer_error=1;assert(video_download(&v)<0&&!freed&&!v.stats.hardware_frames&&v.frame->format==AV_PIX_FMT_VAAPI);
    transfer_error=0;v.error[0]=0;assert(!video_download(&v)&&freed==1&&v.stats.hardware_frames==1&&!v.stats.hardware_pending);
    assert(v.frame->format==AV_PIX_FMT_YUV420P&&!v.frame->hw_frames_ctx&&v.frame->data[0][0]==70);
    assert(v.frame->best_effort_timestamp==1234&&v.frame->pts==1234&&v.frame->color_range==AVCOL_RANGE_MPEG);
    /* A later format negotiation must keep hardware enabled after frame 1. */
    assert(video_format(c,offered)==AV_PIX_FMT_VAAPI);
    v.range_end=100;v.cache_enabled=1;cache_frame(&v);
    assert(v.cache_count==1&&v.cache_bytes>=16*16*3/2&&!v.cache[0]->hw_frames_ctx);
    av_frame_unref(v.frame);assert(v.cache[0]->data[0][0]==70);cache_clear(&v);
    assert(video_format(c,offered+1)==AV_PIX_FMT_YUV420P&&!v.hardware_enabled&&strstr(v.stats.fallback,"format"));
    enum AVPixelFormat no_cpu[]={AV_PIX_FMT_VAAPI,AV_PIX_FMT_NONE};assert(video_format(c,no_cpu)==AV_PIX_FMT_NONE);
    av_frame_free(&v.frame);av_frame_free(&v.download);avcodec_free_context(&c);
    AVFormatContext *format=avformat_alloc_context();assert(format);
    AVStream *stream=avformat_new_stream(format,NULL);assert(stream);
    stream->codecpar->codec_type=AVMEDIA_TYPE_VIDEO;stream->codecpar->codec_id=AV_CODEC_ID_MPEG1VIDEO;
    stream->codecpar->width=stream->codecpar->height=16;
    fail_open=1;v.stats=(KVideoStats){0};
    c=decoder(format,0,&v,KVIDEO_AUTO);
    assert(c&&!c->hw_device_ctx&&!v.hardware_enabled&&strstr(v.stats.fallback,"codec open"));
    avcodec_free_context(&c);avformat_free_context(format);
    puts("Video HW adapter: missing backend/codec/device, format fallback, download failure, timestamps, pool release and CPU-only cache: PASS");return 0;
}
