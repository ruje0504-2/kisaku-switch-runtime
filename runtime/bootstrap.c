#include "bootstrap.h"
#include "file_store.h"
#include "reset_store.h"
#include "read_flags.h"
#include "save_slot.h"
#include "restore_name.h"
#include "text_layout.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
static int error(KBootstrap *b,const char *s){
    const char *name=b->vm->module>=0?b->vm->modules[b->vm->module].name:"<none>";
    if(b->vm->status==KVM_TEXT)snprintf(b->error,sizeof(b->error),"%s @0x%zx text: %s",name,b->vm->instruction_ip,s);
    else snprintf(b->error,sizeof(b->error),"%s @0x%zx syscall=%d: %s",name,b->vm->instruction_ip,b->vm->syscall,s);
    return -1;
}
/* VM values borrow strings; intern distinct contents instead of retaining
   every decoded FLAG file or repeated settings read until shutdown. */
static const char *owned_string(KBootstrap *b,const char *value){
    for(unsigned i=0;i<b->setting_value_count;i++)if(!strcmp(b->setting_values[i],value))return b->setting_values[i];
    if(b->setting_value_count>=4096){error(b,"owned string limit");return NULL;}
    char *copy=malloc(strlen(value)+1);if(!copy){error(b,"owned string allocation failed");return NULL;}
    char **owned=realloc(b->setting_values,((size_t)b->setting_value_count+1)*sizeof(*owned));
    if(!owned){free(copy);error(b,"owned string table allocation failed");return NULL;}
    strcpy(copy,value);b->setting_values=owned;owned[b->setting_value_count++]=copy;return copy;
}
static int owned_value(KBootstrap *b,KValue *dst,KValue src){
    if(src.string){src.string=owned_string(b,src.string);if(!src.string)return -1;}
    *dst=src;return 0;
}
static int equal(const char *a,const char *c){while(*a&&*c){if(tolower((unsigned char)*a++)!=tolower((unsigned char)*c++))return 0;}return *a==*c;}
/* Japanese original only. Do not reinterpret valid CP932 private-use characters
 * as a translated encoding. Translation patches are outside this port stage. */
static KTextEncoding text_encoding(KBootstrap *b){(void)b;return KTEXT_CP932;}
static int decode_text(KBootstrap *b,const uint8_t *data,size_t size,KTextChar *out,size_t capacity,size_t *count,KTextEncoding *used){
    (void)b;
    if(used)*used=KTEXT_CP932;
    return ktext_decode(KTEXT_CP932,data,size,out,capacity,count);
}
/* The shared font must carry the glyph set of the active script language, so
 * the cached face is reopened when the effective encoding changes. */
static KFont *text_font(KBootstrap *b,const char *path,const char *mincho,KTextEncoding encoding){
    int simplified=encoding==KTEXT_GBK;
    if(b->font&&b->font_simplified!=simplified){kfont_close(b->font);b->font=NULL;}
    if(!b->font){b->font=kfont_open(path,simplified);b->font_simplified=simplified;}
    if(mincho&&*mincho)b->novel_font=kfont_open(mincho,simplified);
    return b->font;
}

/* Shared font path settings, used by both the story text and the panels. */
static void ui_font_settings(KBootstrap *b,const char **path,const char **mincho){
    *path=NULL;*mincho=NULL;
    for(unsigned i=0;i<b->setting_count;i++){
        if(!equal(b->settings[i].section,"Runtime"))continue;
        if(equal(b->settings[i].key,"FontFile"))*path=b->settings[i].value;
        else if(equal(b->settings[i].key,"MinchoFontFile"))*mincho=b->settings[i].value;
    }
}
KTextEncoding bootstrap_text_encoding(const KBootstrap *b){
    return text_encoding((KBootstrap *)b);
}
KFont *bootstrap_ui_font(KBootstrap *b){
    const char *path=NULL,*mincho=NULL;
    if(!b)return NULL;
    ui_font_settings(b,&path,&mincho);
    return text_font(b,path,mincho,text_encoding(b));
}
/* Panels that render raw script bytes (backlog, history page) must decode them
   with the same strict CP932 decoder and Japanese glyph set as the story. */
int bootstrap_decode_ui_text(KBootstrap *b,const char *text,size_t size,KTextChar *out,size_t capacity,size_t *count){
    const char *path=NULL,*mincho=NULL;
    KTextEncoding encoding;
    if(!b||!text||!out||!count)return -1;
    encoding=text_encoding(b);
    if(decode_text(b,(const uint8_t *)text,size,out,capacity,count,&encoding))return -1;
    ui_font_settings(b,&path,&mincho);
    return text_font(b,path,mincho,encoding)?0:-1;
}
/* Loose override files (translation / uncensor patches) win over archive
 * entries; see ai6_read_named in ai6arc.c for the search order. */
static int read_named(Ai6Archive *a,const char *name,uint8_t **d,size_t *n){return ai6_read_named(a,name,d,n);}
static int integer(KBootstrap *b,int32_t *n){KValue x;if(kvm_pop(b->vm,&x)||x.string)return error(b,"integer argument required");*n=x.number;return 0;}
static int string(KBootstrap *b,const char **s){KValue x;if(kvm_pop(b->vm,&x)||!x.string)return error(b,"string argument required");*s=x.string;return 0;}
static int resize_bytes(uint8_t **p,size_t *old,size_t n){uint8_t *q=realloc(*p,n?n:1);if(!q)return -1;if(n>*old)memset(q+*old,0,n-*old);*p=q;*old=n;return 0;}
static int module(KBootstrap *b,const char *name){
    for(unsigned i=0;i<b->vm->module_count;i++)if(equal(name,b->vm->modules[i].name))return (int)i;
    uint8_t *d=NULL;size_t n=0;if(read_named(&b->scripts,name,&d,&n))return error(b,"module resource missing");
    int id=kvm_add_module(b->vm,name,d,n);if(id<0){free(d);return error(b,"invalid module");}b->module_data[id]=d;return id;
}
#include "save_frames.inc"
static int scene_transition(KBootstrap *b,const char *from,const char *to){
    if(!b->scene||b->vm->globals[1][69].number)return 0;
    if(kscene_transition(b->scene,from,to,b->vm->bytes[291])<0)return error(b,"scene transition invalid");
    /* 44bd63..44bdf6 finalizes epilogue 3 and exports permanent progress. */
    if(kscene_module(from)==370&&equal(to,"open.mes")){
        KVM *v=b->vm;if(v->byte_count<5370||v->word_count<381)return error(b,"scene export capacity");
        memcpy(v->bytes+3000,b->scene->visited,1000);memcpy(v->bytes+4500,b->scene->status,370);memcpy(v->bytes+5000,b->scene->flags,370);memcpy(v->words+11,b->scene->counters,sizeof(b->scene->counters));
        unsigned selector=v->bytes[8100];if(selector>3)selector=3;
        if(kflags_progress(bootstrap_save_dir(b),selector,v->bytes,v->byte_count))return error(b,"ending scene progress save failed");
    }
    return 0;
}
static char *trim(char *s){while(isspace((unsigned char)*s))s++;size_t n=strlen(s);while(n&&isspace((unsigned char)s[n-1]))s[--n]=0;return s;}
static int setting_put(KBootstrap *b,const char *section,const char *key,const char *value){
    if(strlen(section)>=128||strlen(key)>=128||strlen(value)>=512||strpbrk(section,"\r\n[]")||strpbrk(key,"\r\n=")||strpbrk(value,"\r\n"))return error(b,"invalid setting text");
    unsigned i=0;for(;i<b->setting_count;i++)if(equal(section,b->settings[i].section)&&equal(key,b->settings[i].key))break;
    if(i==128)return error(b,"setting count limit");
    if(i==b->setting_count)b->setting_count++;
    strcpy(b->settings[i].section,section);strcpy(b->settings[i].key,key);strcpy(b->settings[i].value,value);return 0;
}
static int settings_load(KBootstrap *b,const char *name){
    char path[4096],line[1024],section[128]="";snprintf(path,sizeof(path),"%s/%s",!strcmp(name,"AI6WIN.ini")?b->root:bootstrap_save_dir(b),name);
    if(!strcmp(name,"kisaku-runtime.ini")&&kstore_recover(path))return error(b,"settings recovery failed");
    FILE *f=fopen(path,"rb");if(!f)return errno==ENOENT?0:error(b,"settings read failed");
    int bad=0;while(fgets(line,sizeof(line),f)){
        if(!strchr(line,'\n')&&!feof(f)){bad=1;break;}
        char *p=trim(line);if(!*p||*p==';'||*p=='#')continue;
        if(*p=='['){char *end=strchr(p,']');if(!end){bad=1;break;}*end=0;p=trim(p+1);if(strlen(p)>=sizeof(section)){bad=1;break;}strcpy(section,p);}
        else {char *eq=strchr(p,'=');if(!eq)continue;*eq=0;if(setting_put(b,section,trim(p),trim(eq+1))){bad=1;break;}}
    }
    if(ferror(f))bad=1;
    if(fclose(f))bad=1;
    return bad?error(b,"invalid settings file"):0;
}
static int settings_save(KBootstrap *b){
    char path[4096],tmp[4096];snprintf(path,sizeof(path),"%s/kisaku-runtime.ini",bootstrap_save_dir(b));snprintf(tmp,sizeof(tmp),"%s/kisaku-runtime.ini.tmp",bootstrap_save_dir(b));
    FILE *f=fopen(tmp,"wb");if(!f)return error(b,"cannot save runtime settings");
    int bad=0;for(unsigned i=0;i<b->setting_count;i++){
        unsigned j=0;for(;j<i;j++)if(equal(b->settings[j].section,b->settings[i].section))break;
        if(j<i)continue;
        if(fprintf(f,"[%s]\n",b->settings[i].section)<0)bad=1;
        for(j=i;j<b->setting_count;j++)if(equal(b->settings[j].section,b->settings[i].section)&&fprintf(f,"%s=%s\n",b->settings[j].key,b->settings[j].value)<0)bad=1;
    }
    if(fclose(f))bad=1;
    if(bad||kstore_replace(tmp,path)){remove(tmp);return error(b,"runtime settings write failed");}
    return 0;
}
static void mam_cache(KBootstrap *b);
KBootstrap *bootstrap_create(const char *root){
    return bootstrap_create_split(root,root);
}
KBootstrap *bootstrap_create_split(const char *root,const char *save_root){
    if(!root||!save_root||strlen(root)>1800||strlen(save_root)>1800)return NULL;
    KBootstrap *b=calloc(1,sizeof(*b));if(!b)return NULL;b->vm=kvm_create();if(!b->vm){free(b);return NULL;}
    ax_reset(&b->ax);
    ax_reset(&b->ax_extra);
    for(unsigned i=0;i<320;i++)b->animation_status[i]=255;
    KImage *initial[]={&b->layers[0],&b->canvas,&b->auxiliary};
    for(unsigned i=0;i<3;i++){
        uint8_t *p=calloc(640*480,4);if(!p){error(b,"screen allocation failed");return b;}
        *initial[i]=(KImage){0,0,640,480,640*4,p};
    }
    strcpy(b->root,root);strcpy(b->save_root,save_root);b->last_loaded_layer=-1;b->animation_id=-1;b->message_hover=-1;b->exec522_current=-1;
    b->reset_pending=1;
    if(kreset_recover(save_root)){error(b,"history reset recovery failed");return b;}
    b->reset_pending=0;
    char path[2048];snprintf(path,sizeof(path),"%s/mes.arc",root);
    if(ai6_open(&b->scripts,path)){error(b,"cannot open mes.arc");return b;}
    snprintf(path,sizeof(path),"%s/layer.arc",root);
    if(ai6_open(&b->images,path)){error(b,"cannot open layer.arc");return b;}
    snprintf(path,sizeof(path),"%s/data.arc",root);
    if(ai6_open(&b->data,path)){error(b,"cannot open data.arc");return b;}
    mam_cache(b);
    snprintf(path,sizeof(path),"%s/effect.arc",root);
    if(ai6_open(&b->effects,path)){error(b,"cannot open effect.arc");return b;}
    snprintf(path,sizeof(path),"%s/movie.arc",root);
    if(ai6_open(&b->movies,path)){error(b,"cannot open movie.arc");return b;}
    snprintf(path,sizeof(path),"%s/music.arc",root);
    if(ai6_open(&b->music,path)){error(b,"cannot open music.arc");return b;}
    snprintf(path,sizeof(path),"%s/voice.arc",root);
    if(ai6_open(&b->voice,path)){error(b,"cannot open voice.arc");return b;}
    if(settings_load(b,"AI6WIN.ini")||settings_load(b,"kisaku-runtime.ini"))return b;
    /* Native defaults: 0x406450 ff. P+0x4c is independent of 14/0. */
    b->vm->globals[0][50].number=0x57;b->vm->globals[0][29].number=0x101;
    int m=module(b,"startup.mes");if(m>=0)kvm_start(b->vm,m);return b;
}
int bootstrap_flush_progress(KBootstrap *b){
    if(!b||b->reset_pending)return -1;
    if(kgallery_flush(&b->gallery,bootstrap_save_dir(b)))return error(b,"CG history save failed");
    if(b->read_loaded&&b->read_dirty){
        if(kread_flags_save(bootstrap_save_dir(b),b->read_selector,b->read_flags,b->read_size))return error(b,"read history save failed");
        b->read_dirty=0;
    }
    return 0;
}
static void mark_read(KBootstrap *b,unsigned id){
    uint8_t mask=(uint8_t)(0x80u>>(id&7));
    if(!(b->read_flags[id>>3]&mask)){b->read_flags[id>>3]|=mask;b->read_dirty=1;}
}
int bootstrap_enable_async_voice(KBootstrap *b){
    if(!b)return -1;
    if(b->voice_worker)return 0;
    char path[2060];snprintf(path,sizeof(path),"%s/voice.arc",b->root);
    b->voice_worker=kvoice_worker_create(path);return b->voice_worker?0:-1;
}
int bootstrap_enable_async_images(KBootstrap *b){
    if(!b)return -1;
    if(b->image_worker)return 0;
    char path[2060];snprintf(path,sizeof(path),"%s/layer.arc",b->root);
    b->image_worker=kimage_worker_create(path);return b->image_worker?0:-1;
}
/* Native message/choice windows are separate from the script's page 0.
   Every scene write, including asynchronous image installs, must use its
   backing surface while an overlay is visible. */
static KImage *scene_surface(KBootstrap *b){
    if(b->choice_active&&b->choice_base.pixels)return &b->choice_base;
    if(b->message_visible&&b->message_base.pixels)return &b->message_base;
    return &b->layers[0];
}
static int install_image(KBootstrap *b,int layer,KImage *im,const char *name){
    KImage *dst=layer==0?scene_surface(b):&b->layers[layer];
    if(!dst->pixels||im->x<0||im->y<0||(uint64_t)im->x+im->width>dst->width||(uint64_t)im->y+im->height>dst->height)return error(b,"RMT outside destination surface");
    for(unsigned row=0;row<im->height;row++)memcpy(dst->pixels+(row+(size_t)im->y)*dst->stride+(size_t)im->x*4,im->pixels+row*im->stride,im->stride);
    KVM *v=b->vm;
    v->globals[0][38].number=im->x;v->globals[0][39].number=im->y;v->globals[0][40].number=im->x+(int32_t)im->width;v->globals[0][41].number=im->y+(int32_t)im->height;
    b->last_loaded_layer=layer;
    /* Kisaku has no Kawa2 bmptbl.dat/cglist.dat. Image installation must
       not invoke the other title's gallery catalog. */
    (void)name;return 0;
}
void bootstrap_destroy(KBootstrap *b){if(!b)return;rmt_free(&b->param_surface);rmt_free(&b->param_atlas);rmt_free(&b->diary_surface);kmessage_skin_free(&b->message_skin);(void)kbowling_release(&b->bowling,&b->current_bowling);for(unsigned i=0;i<3;i++)rmt_free(&b->letter_surfaces[i]);for(unsigned bank=0;bank<2;bank++)for(unsigned i=0;i<6;i++)rmt_free(&b->choice_rows[bank][i]);rmt_free(&b->gallery_movie_base);rmt_free(&b->scene_tiles);rmt_free(&b->scene_parts);free(b->novel_mask);free(b->mam_data);free(b->mam_archive);rmt_free(&b->status_image);rmt_free(&b->status_parts);for(unsigned i=0;i<3;i++)rmt_free(&b->bonus52_ui[i]);kimage_worker_destroy(b->image_worker);kvoice_worker_destroy(b->voice_worker);while(b->control_files){KControlStore *s=b->control_files;b->control_files=s->next;kcontrol_free(s);}free(b->mov_data);free(b->movie_effect.pcm);rmt_free(&b->novel_original);rmt_free(&b->novel_background);rmt_free(&b->novel_from);rmt_free(&b->novel_target);kfont_close(b->novel_font);rmt_free(&b->choice_parts);rmt_free(&b->choice_text);rmt_free(&b->choice_base);for(unsigned i=0;i<64;i++)free(b->effect_tracks[i].pcm);kfont_close(b->font);for(unsigned i=0;i<3;i++)rmt_free(&b->helper_surfaces[i]);for(unsigned i=0;i<2;i++)rmt_free(&b->exec526_surfaces[i]);for(unsigned i=0;i<4;i++){rmt_free(&b->exec522_sprites[i]);rmt_free(&b->exec522_backing[i]);}ktitle_free(&b->title);kflag_dialog_free(&b->flag_dialog);free(b->scene);for(unsigned i=0;i<b->setting_value_count;i++)free(b->setting_values[i]);free(b->setting_values);while(b->flag_files){KFlags *f=b->flag_files;b->flag_files=f->next;kflags_free(f);}for(unsigned i=0;i<b->saved_control_count;i++)free(b->saved_controls[i].values);for(unsigned i=0;i<b->control_count;i++)free(b->controls[i].values);for(unsigned i=0;i<b->message_count;i++){free(b->messages[i].data);free(b->messages[i].text);}free(b->messages);rmt_free(&b->canvas);rmt_free(&b->auxiliary);rmt_free(&b->fade_surface);rmt_free(&b->message_text);rmt_free(&b->message_base);rmt_free(&b->message_parts);rmt_free(&b->overlay524_base);for(unsigned i=0;i<64;i++)rmt_free(&b->layers[i]);for(unsigned i=0;i<KVM_MODULES;i++)free(b->module_data[i]);for(unsigned i=0;i<3;i++)free(b->audio_objects[i]);free(b->records);free(b->raw_variables);free(b->read_flags);kvideo_close(b->video);free(b->video_data);free(b->movie_pcm);ai6_close(&b->movies);ai6_close(&b->music);ai6_close(&b->voice);free(b->audio_pcm);free(b->voice_pcm);ai6_close(&b->effects);free(b->animation_data);ai6_close(&b->data);ai6_close(&b->scripts);ai6_close(&b->images);kvm_destroy(b->vm);free(b);}
static int message_init(KBootstrap *b){
    /* Kisaku 481be0: reset metrics/cursor and clear the private text region
       (32,8,560,54) on bank0[49]=1's layer. */
    KVM *v=b->vm;KImage *dst=&b->layers[1];
    if(v->global_count[0]<=49||!b->layer_count||!dst->pixels||dst->width<592||dst->height<62)
        return error(b,"message initialization requires system variables and layer 1 >= 592x62");
    if(!b->message_text.pixels){
        uint8_t *p=calloc(560*54,4);if(!p)return error(b,"message surface allocation failed");
        /* Text coordinates are local to the native message sprite. */
        b->message_text=(KImage){32,8,560,54,560*4,p};
    }
    memset(b->message_text.pixels,0,b->message_text.stride*b->message_text.height);
    for(unsigned y=8;y<62;y++)memset(dst->pixels+y*dst->stride+32*4,0,560*4);
    const unsigned slots[]={42,43,44,45,46,47,30,31,49};
    const int values[]={32,8,592,62,32,8,16,18,1};
    for(unsigned i=0;i<sizeof(slots)/sizeof(*slots);i++)v->globals[0][slots[i]]=(KValue){values[i],NULL};
    b->font_width=b->font_height=16;
    b->message_pending_size=0;b->message_pending[0]=0;b->message_voice_name[0]=0;
    b->message_cursor_x=32;b->message_cursor_y=8;b->message_initialized=1;
    return 0;
}
static int record_append(KBootstrap *b,const uint8_t *data,size_t count){
    if(b->message_index<0||(unsigned)b->message_index>=b->message_count)return error(b,"message recorder has no current slot");
    KMessageRecord *r=&b->messages[b->message_index];size_t prefix=r->size?r->size-1:0;
    if(count>262144||prefix>262144-count)return error(b,"message recording limit");
    size_t next=prefix+count+1;
    if(next>r->capacity){uint8_t *p=realloc(r->data,next);if(!p)return error(b,"message recording allocation failed");r->data=p;r->capacity=next;}
    memcpy(r->data+prefix,data,count);r->data[next-1]=0;r->size=next;return 0;
}
static unsigned le16(const uint8_t *p){return (unsigned)p[0]|((unsigned)p[1]<<8);}
static int mam_prepare(KBootstrap *b,const char *voice);
static void mam_stop(KBootstrap *b);
static void message_skip_voice(KBootstrap *b){
    mam_stop(b);kvoice_worker_cancel(b->voice_worker);b->voice_loading=b->voice_active=0;
    free(b->voice_pcm);b->voice_pcm=NULL;b->voice_size=b->voice_read_cursor=b->voice_clock_cursor=0;
    if(b->audio_counts[2])b->audio_objects[2][0].state=0;
    if(!b->music_active){b->audio_size=b->audio_cursor=0;b->audio_serial++;}
}
static int voice_play(KBootstrap *b){
    if(b->audio_counts[2]!=1)return error(b,"voice channel missing");
    if(b->message_active&&(b->force_skip||(b->message_was_read&&bootstrap_message_setting(b,16,0)))){message_skip_voice(b);return 0;}
        if(b->audio_objects[2][0].state){
            if((b->video_active&&!b->video_background)||b->logo_phase)return error(b,"voice over movie/logo not implemented");
            if(b->music_active&&(b->audio_rate!=44100||b->audio_channels!=2))return error(b,"music mixing format unsupported");
            int volume=255,enabled=1;
            for(unsigned i=0;i<b->setting_count;i++)if(equal(b->settings[i].section,"Voice")){
                if(equal(b->settings[i].key,"Volume"))volume=atoi(b->settings[i].value);
                if(equal(b->settings[i].key,"IsVoice"))enabled=atoi(b->settings[i].value)!=0;
            }
            if(volume<0)volume=0;
            if(volume>255)volume=255;
            double gain=enabled?pow(10.0,(volume-255)*18.0/2000.0):0;
            if(mam_prepare(b,b->audio_objects[2][0].name))return -1;
            if(b->voice_worker){
                if(kvoice_worker_submit(b->voice_worker,b->audio_objects[2][0].name,gain))return error(b,"voice worker submission failed");
                free(b->voice_pcm);b->voice_pcm=NULL;b->voice_size=b->voice_read_cursor=b->voice_clock_cursor=0;b->voice_clock=0;
                if(!b->music_active){b->audio_size=b->audio_cursor=0;}
                if(b->audio_rate!=44100||b->audio_channels!=2){b->audio_rate=44100;b->audio_channels=2;b->audio_serial++;}
                b->voice_loading=b->voice_active=1;b->audio_objects[2][0].state=0;return 0;
            }
            uint8_t *data=NULL,*pcm=NULL;size_t size=0,bytes=0;
            if(read_named(&b->voice,b->audio_objects[2][0].name,&data,&size)||kaudio_decode(data,size,&pcm,&bytes)){free(data);free(pcm);return error(b,"voice decode failed");}free(data);
            for(size_t i=0;i<bytes;i+=2){int16_t sample=(int16_t)le16(pcm+i),out=(int16_t)(sample*gain);pcm[i]=(uint8_t)out;pcm[i+1]=(uint8_t)((uint16_t)out>>8);}
            free(b->voice_pcm);b->voice_pcm=NULL;b->voice_size=b->voice_read_cursor=b->voice_clock_cursor=0;b->voice_clock=0;
            if(b->music_active){
                b->voice_pcm=pcm;b->voice_size=bytes;b->voice_active=1;
                b->audio_objects[2][0].state=0;return 0;
            }
            free(b->audio_pcm);b->audio_pcm=pcm;b->audio_size=bytes;b->audio_cursor=b->audio_loop_start=b->audio_loop_end=0;
            b->audio_rate=44100;b->audio_channels=2;b->audio_clock=0;b->audio_serial++;b->voice_active=1;
            strcpy(b->audio_name,b->audio_objects[2][0].name);b->audio_objects[2][0].state=0;
        }
    return 0;
}
static int option(KBootstrap *b,const char *section,const char *key,int fallback){
    if(b->effect_fast&&equal(section,"Display")&&equal(key,"EffectSpeed"))return 2;
    for(unsigned i=0;i<b->setting_count;i++)if(equal(b->settings[i].section,section)&&equal(b->settings[i].key,key))return atoi(b->settings[i].value);
    return fallback;
}
/* Music volume is stored by the original engine as an integer slider in the
   range 0..104.  48de20/4646f0 convert that slider to hundredths of a dB;
   keeping the integer division here also matches x86 idiv's truncation. */
static int music_volume_db(const KBootstrap *b,int *enabled){
    int volume=option((KBootstrap *)b,"Music","Volume",72);
    if(volume<0)volume=0;
    if(volume>104)volume=104;
    if(enabled)*enabled=option((KBootstrap *)b,"Music","IsMusic",1)!=0;
    int delta=104-volume;
    return enabled&&!*enabled?-10000:-((delta+100)*delta)/10;
}
static double db_gain(int db){return pow(10.0,db/2000.0);}
/* 004fd1d0 (CFuncExec case 0x28 / syscall 31/40) slides the page-0
   display window through the 640x960 layer-2 atlas.  The native loop yields
   once per 8/32/64-pixel step, so the portable runtime keeps the same wait
   boundary and lets bootstrap_frame perform one copy per tick. */
static int exec_wipe_begin(KBootstrap *b,int reverse){
    KImage *src=&b->layers[2],*dst=&b->layers[0];
    if(!src->pixels||!dst->pixels||src->width<640||src->height<960||dst->width<640||dst->height<480)
        return error(b,"31/40 wipe requires layer 2 640x960 and layer 0 640x480");
    int speed=option(b,"Display","EffectSpeed",0);
    /* GetPrivateProfileIntA leaves the native default step in place for
       out-of-range values; do the same instead of rejecting the call. */
    if(speed<0||speed>2)speed=0;
    b->exec_wipe_active=1;b->exec_wipe_reverse=reverse!=0;
    b->exec_wipe_step=speed==0?8u:speed==1?32u:64u;
    b->exec_wipe_offset=reverse?480u:0u;
    return 0;
}
static void exec_wipe_frame(KBootstrap *b){
    if(!b->exec_wipe_active)return;
    KImage *src=&b->layers[2],*dst=&b->layers[0];
    unsigned offset=b->exec_wipe_offset;
    if(b->exec_wipe_reverse){
        /* Reverse direction starts on the lower page and moves upward. */
        for(unsigned y=0;y<480;y++)memcpy(dst->pixels+y*dst->stride,src->pixels+(offset+y)*src->stride,640*4);
        if(offset<=b->exec_wipe_step){
            for(unsigned y=0;y<480;y++)memcpy(dst->pixels+y*dst->stride,src->pixels+y*src->stride,640*4);
            b->exec_wipe_offset=0;b->exec_wipe_active=0;
        }
        else b->exec_wipe_offset=offset-b->exec_wipe_step;
    }else{
        unsigned top=offset,height=480-offset;
        for(unsigned y=0;y<height;y++)memcpy(dst->pixels+y*dst->stride,src->pixels+(top+y)*src->stride,640*4);
        for(unsigned y=0;y<offset;y++)memcpy(dst->pixels+(480-offset+y)*dst->stride,src->pixels+(480+y)*src->stride,640*4);
        if(offset+b->exec_wipe_step>=480){
            for(unsigned y=0;y<480;y++)memcpy(dst->pixels+y*dst->stride,src->pixels+(480+y)*src->stride,640*4);
            b->exec_wipe_offset=480;b->exec_wipe_active=0;
        }
        else b->exec_wipe_offset=offset+b->exec_wipe_step;
    }
}
static void message_copy_text(KBootstrap *b){
    KImage *src=&b->layers[b->vm->globals[0][49].number];
    int top=b->vm->globals[0][43].number;
    if(top<0||top>(int)src->height-54)return;
    for(unsigned y=0;y<54;y++)memcpy(b->message_text.pixels+y*b->message_text.stride,src->pixels+(top+y)*src->stride+32*4,576*4);
    b->message_revealing=0;
}
static void message_compose(KBootstrap *b){
    unsigned offset=0;
    if(b->message_slide){unsigned f=b->message_slide_frame,d=b->message_slide-1;unsigned move=d&&f<d?86*f/d:86;offset=b->message_hiding?move:86-move;}
    memcpy(b->layers[0].pixels,b->message_base.pixels,640*480*4);
    if(b->message_user_hidden)return;
    unsigned alpha=b->message_color>>24;
    for(unsigned y=394+offset;y<480;y++)for(unsigned x=0;x<640;x++){
        uint8_t *out=b->layers[0].pixels+y*b->layers[0].stride+x*4;
        for(unsigned c=0;c<3;c++){unsigned color=(b->message_color>>(c*8))&255;out[c]=(uint8_t)((color*alpha+out[c]*(255-alpha))/255);}
    }
    for(unsigned y=0;y<54&&408+offset+y<480;y++)for(unsigned x=0;x<576;x++){
        uint8_t *src=b->message_text.pixels+y*b->message_text.stride+x*4,*out=b->layers[0].pixels+(408+offset+y)*b->layers[0].stride+(32+x)*4;
        for(unsigned c=0;c<3;c++)out[c]=(uint8_t)((src[c]*src[3]+out[c]*(255-src[3]))/255);
    }
    /* Native mes_open.area positions; controls use the original alpha sheet. */
    for(unsigned item=b->message_open?0:7;item<8;item++){
        unsigned dx=item?320+(item-1)*44:278;
        unsigned state=b->message_hover==(int)item?1:0;
        if((item==0&&option(b,"Msg","IsAutoMes",0))||(item==1&&option(b,"Msg","IsOneMes",0)))state=2;
        unsigned sy=state*16;
        if(sy+16>b->message_parts.height)sy=0;
        for(unsigned y=0;y<16&&464+offset+y<480;y++)for(unsigned x=0;x<54;x++){
            uint8_t *src=b->message_parts.pixels+(sy+y)*b->message_parts.stride+(item*56+x)*4,*out=b->layers[0].pixels+(464+offset+y)*b->layers[0].stride+(dx+2+x)*4;
            for(unsigned c=0;c<3;c++)out[c]=(uint8_t)(src[c]*src[3]/255+out[c]*(255-src[3])/255);
        }
    }
}
#include "message_history.inc"
static int message_begin(KBootstrap *b,int id){
    KVM *v=b->vm;

    if(id< -1||(id>=0&&(size_t)id>=b->read_size*8)||v->globals[0][49].number!=1||v->globals[0][42].number!=32||v->globals[0][43].number!=8||v->globals[0][44].number!=592||v->globals[0][45].number!=62)return error(b,"message region/read id unsupported");
    if(!b->message_parts.pixels){
        uint8_t *data=NULL;size_t n=0;
        if(read_named(&b->images,"kisaku_DL_mes_p.akb",&data,&n)||rmt_decode(data,n,&b->message_parts)){free(data);return error(b,"message controls decode failed");}free(data);
        if(b->message_parts.width<448||b->message_parts.height<16)return error(b,"message controls dimensions invalid");
    }
    if(!b->message_base.pixels){b->message_base=(KImage){0,0,640,480,2560,malloc(640*480*4)};if(!b->message_base.pixels)return error(b,"message backdrop allocation failed");}
    if(!b->message_visible)memcpy(b->message_base.pixels,b->layers[0].pixels,640*480*4);
    int a=option(b,"Msg","Alpha",128),r=option(b,"Msg","Red",0),g=option(b,"Msg","Green",0),blue=option(b,"Msg","Blue",0),speed=option(b,"Msg","ShowSpeed",128);
    if(a<0||a>255||r<0||r>255||g<0||g>255||blue<0||blue>255||speed<0||speed>255)return error(b,"message settings range");
    b->message_color=((uint32_t)(255-a)<<24)|((uint32_t)r<<16)|((uint32_t)g<<8)|(unsigned)blue;
    b->message_delay=(255-speed)*148/255;b->message_clock=0;b->message_revealing=1;
    int top=v->globals[0][43].number,end_x=v->globals[0][46].number,end_y=v->globals[0][47].number;
    b->message_reveal_x=32;b->message_reveal_y=408;b->message_end_x=end_x;b->message_end_y=408+end_y-top;
    if(end_y<top||end_y>v->globals[0][45].number||end_x<32||end_x>608)return error(b,"message cursor range");
    memset(b->message_text.pixels,0,576*54*4);
    if(!b->message_delay)message_copy_text(b);
    unsigned duration=12;int effect=option(b,"Display","EffectSpeed",0);if(effect==1)duration>>=1;else if(effect==2||(v->globals[0][50].number&0x4000))duration=0;
    b->message_slide=b->message_visible?0:duration+1;b->message_slide_frame=0;b->message_hiding=0;b->message_active=b->message_visible=1;b->message_read_id=id;
    message_history_record(b);
    b->message_auto_clock=0;
    int auto_speed=option(b,"Msg","AutoMesSpeed",128);if(auto_speed<0)auto_speed=0;if(auto_speed>255)auto_speed=255;
    b->message_auto_delay=500+(unsigned)((int)((255-auto_speed)*1.7))*(unsigned)(v->text_size/2);
    b->message_had_voice=b->audio_counts[2]&&b->audio_objects[2][0].state;
    b->message_was_read=id>=0&&(b->read_flags[(unsigned)id>>3]&(0x80u>>(id&7)))!=0;
    if(!b->message_was_read&&option(b,"Msg","IsOneMes",0))setting_put(b,"Msg","IsOneMes","0");
    /* 448fc0 -> 47fd80: MSB-first read bit, in memory only. */
    if(id>=0)mark_read(b,(unsigned)id);
    b->message_timed=0;b->message_user_hidden=0;
    b->message_voice_pending=b->message_slide!=0;
    if(!b->message_slide&&voice_play(b))return -1;
    message_compose(b);return 0;
}
static KImage *surface(KBootstrap *b,int id){
    if(id==-1)return b->fade_visible?scene_surface(b):&b->canvas;
    if(id==0)return scene_surface(b);
    if(id==-2)return &b->auxiliary;
    if(id<0||(unsigned)id>b->layer_count)return NULL;
    return &b->layers[id];
}
static int blit_args(KBootstrap *b,int keyed,const int32_t q[9]){
    /* 431b90/431dd0: dx,dy,w,h,dst,sx,sy,src,alpha-or-key. */
    KImage *dst=surface(b,q[4]),*src=surface(b,q[7]);
    if(!dst||!src||!dst->pixels||!src->pixels)return error(b,"blit surface missing");
    int64_t dx=q[0],dy=q[1],w=q[2],h=q[3],sx=q[5],sy=q[6];
    /* CDIB copy methods return immediately for nonpositive dimensions. */
    if(w<=0||h<=0)return 0;
    if(dx<0){sx-=dx;w+=dx;dx=0;}if(sx<0){dx-=sx;w+=sx;sx=0;}
    if(dy<0){sy-=dy;h+=dy;dy=0;}if(sy<0){dy-=sy;h+=sy;sy=0;}
    if(w>(int64_t)dst->width-dx)w=(int64_t)dst->width-dx;
    if(w>(int64_t)src->width-sx)w=(int64_t)src->width-sx;
    if(h>(int64_t)dst->height-dy)h=(int64_t)dst->height-dy;
    if(h>(int64_t)src->height-sy)h=(int64_t)src->height-sy;
    if(w<=0||h<=0)return 0;
    /* Snapshot permits overlapping source/destination rectangles. */
    uint8_t *copy=malloc((size_t)w*h*4);if(!copy)return error(b,"blit allocation failed");
    for(int64_t y=0;y<h;y++)memcpy(copy+y*w*4,src->pixels+(sy+y)*src->stride+sx*4,(size_t)w*4);
    uint32_t key=(uint32_t)q[8]&0xffffffu;
    for(int64_t y=0;y<h;y++)for(int64_t x=0;x<w;x++){
        uint8_t *s=copy+(y*w+x)*4,*d=dst->pixels+(dy+y)*dst->stride+(dx+x)*4;
        uint32_t color=(uint32_t)s[0]|((uint32_t)s[1]<<8)|((uint32_t)s[2]<<16);
        if(keyed==2){unsigned alpha=s[3];for(unsigned k=0;k<3;k++)d[k]=(uint8_t)(s[k]*alpha/255+d[k]*(255-alpha)/255);continue;}
        if(keyed&&color==key)continue;
        memcpy(d,s,((!keyed&&q[8]==1)||keyed==3)?4:3);
    }
    free(copy);return 0;
}
static int blit(KBootstrap *b,int keyed){
    int32_t q[9];for(unsigned i=0;i<9;i++)if(integer(b,&q[i]))return -1;
    return blit_args(b,keyed,q);
}
/* CFuncLayer action 4 (4f68e0 -> CDIB+0x60) uses a color key.  Its ninth
 * and tenth operands select whether the source alpha is copied and the RGB
 * key respectively; unlike action 3 it always reads ten stack values. */
static int blit_color_key(KBootstrap *b){
    int32_t q[10];for(unsigned i=0;i<10;i++)if(integer(b,&q[i]))return -1;
    KImage *dst=surface(b,q[4]),*src=surface(b,q[7]);
    if(!dst||!src||!dst->pixels||!src->pixels)return error(b,"color-key surface missing");
    int64_t dx=q[0],dy=q[1],w=q[2],h=q[3],sx=q[5],sy=q[6];
    if(w<=0||h<=0)return 0;
    if(dx<0){sx-=dx;w+=dx;dx=0;}if(sx<0){dx-=sx;w+=sx;sx=0;}
    if(dy<0){sy-=dy;h+=dy;dy=0;}if(sy<0){dy-=sy;h+=sy;sy=0;}
    if(w>(int64_t)dst->width-dx)w=(int64_t)dst->width-dx;
    if(w>(int64_t)src->width-sx)w=(int64_t)src->width-sx;
    if(h>(int64_t)dst->height-dy)h=(int64_t)dst->height-dy;
    if(h>(int64_t)src->height-sy)h=(int64_t)src->height-sy;
    if(w<=0||h<=0)return 0;
    uint8_t *copy=malloc((size_t)w*h*4);if(!copy)return error(b,"color-key allocation failed");
    for(int64_t y=0;y<h;y++)memcpy(copy+y*w*4,src->pixels+(sy+y)*src->stride+sx*4,(size_t)w*4);
    uint32_t key=(uint32_t)q[9]&0xffffffu;
    for(int64_t y=0;y<h;y++)for(int64_t x=0;x<w;x++){
        uint8_t *s=copy+(y*w+x)*4,*d=dst->pixels+(dy+y)*dst->stride+(dx+x)*4;
        if((((uint32_t)s[0]|((uint32_t)s[1]<<8)|((uint32_t)s[2]<<16))&0xffffffu)==key)continue;
        if(q[8]==1)memcpy(d,s,4);
        else memcpy(d,s,3);
    }
    free(copy);return 0;
}
#include "animation523.inc"
#include "animation522.inc"
#include "animation521.inc"
/* CFuncLayer action 8 (4f6280 -> CDIB+0x58) applies a constant Alpha to
 * source RGB.  The native lookup table is floor(channel*alpha/255); the
 * destination Alpha byte is never changed.  Alpha 255 delegates to the
 * regular four-byte copy, while nonpositive Alpha is a no-op. */
static int blit_global_alpha(KBootstrap *b){
    int32_t q[9];for(unsigned i=0;i<9;i++)if(integer(b,&q[i]))return -1;
    KImage *dst=surface(b,q[4]),*src=surface(b,q[7]);
    if(!dst||!src||!dst->pixels||!src->pixels)return error(b,"global-alpha surface missing");
    int alpha=q[8];
    if(q[2]<=0||q[3]<=0||alpha<=0)return 0;
    int64_t dx=q[0],dy=q[1],w=q[2],h=q[3],sx=q[5],sy=q[6];
    if(dx<0){sx-=dx;w+=dx;dx=0;}if(sx<0){dx-=sx;w+=sx;sx=0;}
    if(dy<0){sy-=dy;h+=dy;dy=0;}if(sy<0){dy-=sy;h+=sy;sy=0;}
    if(w>(int64_t)dst->width-dx)w=(int64_t)dst->width-dx;
    if(w>(int64_t)src->width-sx)w=(int64_t)src->width-sx;
    if(h>(int64_t)dst->height-dy)h=(int64_t)dst->height-dy;
    if(h>(int64_t)src->height-sy)h=(int64_t)src->height-sy;
    if(w<=0||h<=0)return 0;
    uint8_t *copy=malloc((size_t)w*h*4);if(!copy)return error(b,"global-alpha allocation failed");
    for(int64_t y=0;y<h;y++)memcpy(copy+y*w*4,src->pixels+(sy+y)*src->stride+sx*4,(size_t)w*4);
    if(alpha>=255){
        for(int64_t y=0;y<h;y++)for(int64_t x=0;x<w;x++)
            memcpy(dst->pixels+(dy+y)*dst->stride+(dx+x)*4,copy+(y*w+x)*4,4);
    }else{
        unsigned a=(unsigned)alpha,inv=255u-a;
        for(int64_t y=0;y<h;y++)for(int64_t x=0;x<w;x++){
            uint8_t *s=copy+(y*w+x)*4,*d=dst->pixels+(dy+y)*dst->stride+(dx+x)*4;
            for(unsigned c=0;c<3;c++)d[c]=(uint8_t)(((unsigned)d[c]*inv)/255u+((unsigned)s[c]*a)/255u);
        }
    }
    free(copy);return 0;
}
/* CFuncLayer action 9 (4f6120 -> CDIB+0xa4) writes only the destination
 * Alpha byte in a rectangle, preserving RGB. */
static int layer_alpha(KBootstrap *b){
    int32_t q[6];for(unsigned i=0;i<6;i++)if(integer(b,&q[i]))return -1;
    KImage *dst=surface(b,q[4]);if(!dst||!dst->pixels)return error(b,"alpha surface missing");
    if(q[2]<=0||q[3]<=0)return 0;
    int64_t left=q[0],top=q[1],right=left+q[2],bottom=top+q[3];
    if(left<0)left=0;
    if(top<0)top=0;
    if(right>dst->width)right=dst->width;
    if(bottom>dst->height)bottom=dst->height;
    if(left>=right||top>=bottom)return 0;
    uint8_t alpha=(uint8_t)q[5];
    for(int64_t y=top;y<bottom;y++)for(int64_t x=left;x<right;x++)
        dst->pixels[(size_t)y*dst->stride+(size_t)x*4+3]=alpha;
    return 0;
}
/* 4f9eb0 -> 45c940 draws the Japanese three-part status sprite from the
   private layer-7 sheet. The native routine uses fixed coordinate tables;
   keep those tables here instead of treating the arguments as arbitrary
   surface copies. */
static int overlay524_draw(KBootstrap *b,int first,int second,int third){
    static const unsigned first_xy[][2]={
        {0,0},{0,32},{0,64},{0,96},{0,128},
        {76,0},{80,32},{80,64},{80,96},{80,128},{160,0},{160,32}
    };
    static const unsigned second_xy[][2]={{240,0},{240,28},{240,56},{240,84}};
    static const unsigned third_xy[][2]={{160,120},{0,0},{0,0},{0,0},{160,64},{0,0},{160,92}};
    KImage *src=&b->layers[7],*dst=scene_surface(b);
    if(first<0||first>=(int)(sizeof(first_xy)/sizeof(*first_xy))||
       second<0||second>=(int)(sizeof(second_xy)/sizeof(*second_xy))||
       third<0||third>=(int)(sizeof(third_xy)/sizeof(*third_xy)))
        return error(b,"31/524 sprite index out of range");
    if(!src->pixels||src->width<640||src->height<400||!dst->pixels||dst->width<640||dst->height<480)
        return error(b,"31/524 overlay layer 7 or destination surface missing");
    if(!b->overlay524_visible){
        if(!b->overlay524_base.pixels){
            uint8_t *p=malloc(640*480*4);if(!p)return error(b,"31/524 overlay backing allocation failed");
            b->overlay524_base=(KImage){0,0,640,480,640*4,p};
        }
        memcpy(b->overlay524_base.pixels,dst->pixels,640*480*4);
    }
    int32_t q[9]={0,0,120,128,0,520,272,7,0xff00};
    if(blit_args(b,3,q))return -1;
    q[0]=25;q[1]=24;q[2]=80;q[3]=32;q[5]=(int32_t)first_xy[first][0];q[6]=(int32_t)first_xy[first][1];q[8]=1;
    if(blit_args(b,0,q))return -1;
    q[0]=29;q[1]=56;q[2]=72;q[3]=28;q[5]=(int32_t)second_xy[second][0];q[6]=(int32_t)second_xy[second][1];
    if(blit_args(b,0,q))return -1;
    q[0]=29;q[1]=84;q[2]=72;q[3]=28;q[5]=(int32_t)third_xy[third][0];q[6]=(int32_t)third_xy[third][1];
    if(blit_args(b,0,q))return -1;
    b->overlay524_visible=1;return 0;
}
static int overlay524_clear(KBootstrap *b){
    if(b->overlay524_visible&&b->overlay524_base.pixels){
        KImage *dst=scene_surface(b);
        if(!dst->pixels||dst->width<640||dst->height<480)return error(b,"31/524 overlay destination surface missing");
        memcpy(dst->pixels,b->overlay524_base.pixels,640*480*4);
    }
    b->overlay524_visible=0;return 0;
}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}

static int play_pcm(KBootstrap *b,Ai6Archive *archive,const char *name){
    uint8_t *d=NULL;size_t n=0;
    if(read_named(archive,name,&d,&n)||n<12||memcmp(d,"RIFF",4)||memcmp(d+8,"WAVE",4)){free(d);return error(b,"WAV missing/invalid");}
    unsigned rate=0,channels=0;size_t offset=0,bytes=0,loop_start=0,loop_end=0;
    for(size_t at=12;at+8<=n;){
        size_t size=le32(d+at+4),body=at+8;if(size>n-body){free(d);return error(b,"truncated WAV chunk");}
        if(!memcmp(d+at,"fmt ",4)){
            if(size<16||le16(d+body)!=1||le16(d+body+14)!=16){free(d);return error(b,"unsupported WAV encoding");}
            channels=le16(d+body+2);rate=le32(d+body+4);
        }else if(!memcmp(d+at,"data",4)){offset=body;bytes=size;}
        else if(!memcmp(d+at,"smpl",4)&&size>=36&&le32(d+body+28)){
            if(size<60){free(d);return error(b,"truncated WAV loop");}
            loop_start=le32(d+body+44);loop_end=le32(d+body+48);
        }
        at=body+size+(size&1);
    }
    if(!offset||!bytes||channels<1||channels>2||rate<8000||rate>96000||bytes%(channels*2)){free(d);return error(b,"invalid WAV format");}
    if(loop_end&&(loop_start>=loop_end||loop_end>bytes/(channels*2))){free(d);return error(b,"invalid WAV loop range");}
    uint8_t *pcm=malloc(bytes);if(!pcm){free(d);return error(b,"PCM allocation failed");}
    memcpy(pcm,d+offset,bytes);free(d);free(b->audio_pcm);b->audio_pcm=pcm;
    b->audio_loop_start=loop_start*channels*2;b->audio_loop_end=loop_end*channels*2;
    b->audio_size=bytes;b->audio_cursor=0;b->audio_clock=0;b->audio_rate=rate;b->audio_channels=channels;b->audio_serial++;
    snprintf(b->audio_name,sizeof(b->audio_name),"%s",name);return 0;
}
static int effect_decode(KBootstrap *b,KEffectTrack *track,const char *name){
    if(!option(b,"Effect","IsEffect",1)){free(track->pcm);memset(track,0,sizeof(*track));return 0;}
    if((b->audio_size||b->voice_pcm)&&(b->audio_rate!=44100||b->audio_channels!=2))return error(b,"effect mixing format unsupported");
    uint8_t *data=NULL,*pcm=NULL;size_t size=0,pcm_size=0;
    if(read_named(&b->effects,name,&data,&size))return error(b,"effect resource missing");
    unsigned rate=0;size_t first=0,last=0;
    if(size<12||memcmp(data,"RIFF",4)||memcmp(data+8,"WAVE",4)){free(data);return error(b,"effect WAV header invalid");}
    for(size_t at=12;at+8<=size;){
        size_t n=le32(data+at+4),body=at+8;if(n>size-body){free(data);return error(b,"effect WAV chunk invalid");}
        if(!memcmp(data+at,"fmt ",4)&&n>=16)rate=le32(data+body+4);
        if(!memcmp(data+at,"smpl",4)&&n>=36&&le32(data+body+28)){
            if(n<60){free(data);return error(b,"effect loop truncated");}
            first=le32(data+body+44);last=le32(data+body+48);
        }
        at=body+n+(n&1);
    }
    if(!rate||kaudio_decode(data,size,&pcm,&pcm_size)){free(data);return error(b,"effect decode failed");}free(data);
    first=(size_t)((uint64_t)first*44100/rate)*4;last=(size_t)((uint64_t)last*44100/rate)*4;
    if(last&&(first>=last||last>pcm_size)){free(pcm);return error(b,"effect loop range invalid");}
    int volume=option(b,"Effect","Volume",230);if(volume<0)volume=0;if(volume>255)volume=255;
    double gain=pow(10.0,(volume-255)*18.0/2000.0);
    for(size_t i=0;i+1<pcm_size;i+=2){int16_t sample=(int16_t)((int16_t)le16(pcm+i)*gain);pcm[i]=(uint8_t)sample;pcm[i+1]=(uint8_t)((uint16_t)sample>>8);}
    free(track->pcm);*track=(KEffectTrack){.pcm=pcm,.size=pcm_size,.loop_start=first,.loop_end=last,.fade_limit=5000+(volume-255)*18};
    if(b->audio_rate!=44100||b->audio_channels!=2){b->audio_rate=44100;b->audio_channels=2;b->audio_serial++;}
    return 0;
}
static int effect_play(KBootstrap *b,unsigned slot,const char *name){
    if(slot>=b->audio_counts[1]||slot>=64)return error(b,"effect channel range");
    if(effect_decode(b,&b->effect_tracks[slot],name))return -1;
    snprintf(b->audio_objects[1][slot].name,sizeof(b->audio_objects[1][slot].name),"%s",name);b->audio_objects[1][slot].state=0;
    return 0;
}
/* Kisaku CNormalSelect::initialize 4f1a40 and reset 4effe0.
   The atlas lives in layer 5. Initialization adjusts alpha only: it must
   preserve RGB, including an atlas that scripts have already loaded. */
static int choice_initialize(KBootstrap *b){
    KImage *atlas=&b->layers[5];
    if(!atlas->pixels||atlas->width<496||atlas->height<408)return error(b,"choice atlas layer too small");
    for(unsigned bank=0;bank<2;bank++)for(unsigned i=0;i<6;i++){
        KImage *row=&b->choice_rows[bank][i];unsigned h=i<2?34:52;
        if(!row->pixels){row->pixels=calloc(496*h,4);if(!row->pixels)return error(b,"choice row allocation failed");row->width=496;row->height=h;row->stride=496*4;}
    }
    for(unsigned y=0;y<68;y++)for(unsigned x=0;x<320;x++)atlas->pixels[y*atlas->stride+x*4+3]=255;
    unsigned theme=b->vm->bytes[1000]!=0;
    if(b->vm->globals[1][61].number==1)theme=0;
    for(unsigned state=0;state<5;state++)for(unsigned y=0;y<68;y++)for(unsigned x=0;x<496;x++){
        unsigned sx=(state+theme*5)*32+(x<8?x:x>=488?24+x-488:8+(x-8)%16);
        memcpy(atlas->pixels+(68+state*68+y)*atlas->stride+x*4,atlas->pixels+y*atlas->stride+sx*4,4);
    }
    /* Keep the native row surfaces usable by 31/30. The original creates
       four retained 496-pixel rows from the generated state atlas; the
       remaining entries are scratch rows used while rebuilding a page. */
    for(unsigned bank=0;bank<2;bank++)for(unsigned row=0;row<6;row++){
        KImage *dst=&b->choice_rows[bank][row];unsigned state=row<5?row:4;
        unsigned h=dst->height;if(68+state*68+h>atlas->height)h=atlas->height>68+state*68?atlas->height-(68+state*68):0;
        for(unsigned y=0;y<h;y++)memcpy(dst->pixels+y*dst->stride,atlas->pixels+(68+state*68+y)*atlas->stride,496*4);
    }
    b->choice_selected=-1;b->choice_prepared=1;return 0;
}
static int choice_page_text(KBootstrap *b,unsigned page,unsigned count,int top){
    memset(b->choice_text.pixels,0,640*480*4);
    KTextEncoding encoding=text_encoding(b);
    const char *path=NULL;
    for(unsigned i=0;i<b->setting_count;i++)if(equal(b->settings[i].section,"Runtime")&&equal(b->settings[i].key,"FontFile"))path=b->settings[i].value;
    for(unsigned i=0;i<count;i++){
        unsigned item=page*4+i;KTextChar chars[128];size_t n=0;
        if(decode_text(b,(const uint8_t *)b->choice_labels[item],b->choice_lengths[item],chars,128,&n,&encoding))return error(b,"choice text encoding invalid");
        if(!text_font(b,path,NULL,encoding))return error(b,"choice font missing");
        int x=(640-(int)(b->choice_lengths[item]/2)*16)/2;
        unsigned color=b->vm->bytes[8100]==0&&b->vm->bytes[2000+b->choice_values[item]]?0xffb400:0xffffff;
        for(size_t j=0;j<n;j++){
            if(kfont_draw(b->font,&b->choice_text,chars[j].codepoint,x,top+(int)i*52+18,16,16,color))return error(b,"choice glyph unavailable");
            x+=chars[j].columns*8;
        }
    }
    if(b->choice_count>4){
        char label[48];snprintf(label,sizeof(label),"<    %u / %u    >",page+1,(b->choice_count+3)/4);
        int x=(640-(int)strlen(label)*8)/2;
        for(unsigned i=0;label[i];i++)if(kfont_draw(b->font,&b->choice_text,(unsigned char)label[i],x+(int)i*8,448,16,16,0xffffff))return error(b,"choice page glyph unavailable");
    }
    b->choice_rendered_page=page;return 0;
}
static void choice_draw(KBootstrap *b){
    if(!b->choice_active)return;
    if(b->choice_selected< -1||(b->choice_selected>=0&&(unsigned)b->choice_selected>=b->choice_count)){error(b,"choice selection invalid");return;}
    unsigned page=b->choice_selected<0?0:(unsigned)b->choice_selected/4,count=b->choice_count-page*4;if(count>4)count=4;
    int top=(480-(int)count*52)/2;
    if(b->choice_rendered_page!=page&&choice_page_text(b,page,count,top))return;
    memcpy(b->layers[0].pixels,b->choice_base.pixels,640*480*4);
    for(unsigned i=0;i<count;i++){
        unsigned item=page*4+i;int seen=b->vm->bytes[2000+b->choice_values[item]]!=0;
        /* Main-story seen rows in selparts2 are cyan. Keep the requested
           black background for both states; seen labels carry the distinction.
           Appendix games retain every original atlas state. */
        int sy=(seen&&b->vm->bytes[8100]!=0?156:0)+((int)item==b->choice_selected?52:0);
        const KImage *row=b->choice_normal?&b->choice_rows[0][((int)item==b->choice_selected)?3:2]:NULL;
        for(unsigned y=0;y<52;y++)for(unsigned x=0;x<496;x++){
            const uint8_t *src=b->choice_normal?row->pixels+y*row->stride+x*4:b->choice_parts.pixels+(sy+y)*b->choice_parts.stride+x*4;
            uint8_t *dst=b->layers[0].pixels+(top+i*52+y)*b->layers[0].stride+(72+x)*4;
            unsigned a=src[3];
            for(unsigned c=0;c<3;c++)dst[c]=(uint8_t)(src[c]*a/255+dst[c]*(255-a)/255);
            dst[3]=(uint8_t)(a+dst[3]*(255-a)/255);
        }
    }
    for(size_t i=0;i<640*480;i++){
        uint8_t *s=b->choice_text.pixels+i*4,*d=b->layers[0].pixels+i*4;unsigned a=s[3];
        for(unsigned k=0;k<3;k++)d[k]=(uint8_t)(s[k]*a/255+d[k]*(255-a)/255);
    }
}
static int choice_begin(KBootstrap *b){
    KVM *v=b->vm;
    if(v->current_list<0)return error(b,"choice list missing");
    KList *list=&v->lists[v->current_list];
    if(!list->count||list->count>64)return error(b,"choice list count invalid");
    /* 47dc36 changes the native choice-window mode using bank1[69], but
       still opens the choices. Appendix animation routes use it as well. */
    KVM *eval=malloc(sizeof(*eval));if(!eval)return error(b,"choice evaluator allocation failed");
    const char *labels[64]={0};size_t lengths[64]={0};
    for(unsigned i=0;i<list->count;i++){
        memcpy(eval,v,sizeof(*eval));eval->raw=NULL;eval->raw_size=0;eval->module=list->items[i].module;eval->ip=list->items[i].ip;eval->sp=eval->depth=eval->script_depth=0;eval->status=KVM_READY;
        unsigned texts=0;KStatus state=KVM_READY;
        for(unsigned n=0;n<1000;n++){
            state=kvm_run(eval,1000);
            if(state==KVM_TEXT){labels[i]=eval->text;lengths[i]=eval->text_size;texts++;kvm_resume(eval);}
            else break;
        }
        if(state!=KVM_YIELD||texts!=1||eval->globals[0][16].string||lengths[i]>60){free(eval);return error(b,"choice body unsupported");}
        b->choice_values[i]=eval->globals[0][16].number;
        b->choice_returns[i]=(int32_t)((uint32_t)list->items[i].value+1);
        if(b->choice_values[i]<0||2000u+(unsigned)b->choice_values[i]>=v->byte_count){free(eval);return error(b,"choice value out of range");}
        /* Actual bodies assign only return slot 16 and emit one text literal. */
        eval->globals[0][16]=v->globals[0][16];
        if(memcmp(eval->globals,v->globals,sizeof(v->globals))||memcmp(eval->bytes,v->bytes,sizeof(v->bytes))||memcmp(eval->words,v->words,sizeof(v->words))){free(eval);return error(b,"choice body side effects unsupported");}
    }
    free(eval);
    if(!b->choice_normal){
        rmt_free(&b->choice_parts);uint8_t *data=NULL;size_t size=0;
        const char *parts=(v->bytes[8100]==1||v->bytes[8100]==3)?"selparts.rmt":"selparts2.rmt";
        if(read_named(&b->images,parts,&data,&size)||rmt_decode(data,size,&b->choice_parts)){free(data);return error(b,"choice parts load failed");}free(data);
        if(b->choice_parts.width<496||b->choice_parts.height<312)return error(b,"choice parts dimensions invalid");
    }
    KImage *images[]={&b->choice_text,&b->choice_base};
    for(unsigned i=0;i<2;i++){if(!images[i]->pixels){uint8_t *p=calloc(640*480,4);if(!p)return error(b,"choice surface allocation failed");*images[i]=(KImage){0,0,640,480,640*4,p};}}
    memcpy(b->choice_base.pixels,b->layers[0].pixels,640*480*4);memset(b->choice_text.pixels,0,640*480*4);
    b->choice_count=list->count;b->choice_selected=-1;b->choice_rendered_page=~0u;
    for(unsigned i=0;i<list->count;i++){memcpy(b->choice_labels[i],labels[i],lengths[i]);b->choice_labels[i][lengths[i]]=0;b->choice_lengths[i]=(unsigned)lengths[i];}
    b->choice_active=1;choice_draw(b);return b->error[0]?-1:0;
}
static void draw_ax(const uint32_t d[7],unsigned cell,void *context){
    (void)cell;KBootstrap *b=context;
    /* AX vtable 4d5674 + 0x38 -> 40c370 is an empty ret 0xc. */
    if(d[0]==2)return;
    if(d[0]>2){error(b,"AX descriptor requires private menu canvas");return;}
    /* 40c040: kind0 copies BGRA from page5 to page0, no alpha blending. */
    int32_t q[9]={(int32_t)d[5],(int32_t)d[6],(int32_t)d[3],(int32_t)d[4],b->ax_destination,(int32_t)d[1],(int32_t)d[2],5,1};
    if(!d[0])blit_args(b,0,q);
    else if(b->ax_fast){q[8]=0xff00;blit_args(b,3,q);}
    else {
        /* 40c0c0: update sprite plane 4, restore background plane 2,
           then composite the sprite plane over the destination rectangle. */
        q[4]=4;q[8]=0xff00;if(blit_args(b,3,q))return;
        q[4]=b->ax_destination;q[5]=(int32_t)d[5];q[6]=(int32_t)d[6];q[7]=2;q[8]=1;
        if(blit_args(b,0,q))return;
        q[7]=4;blit_args(b,2,q);
    }
}
/* CAnimeManagerEX vtable 4dd7a0/4dd690.  The constructor leaves the
 * destination selector at (0,0) and the source selector at (0,8), so the
 * fallback source is layer 8.  Syscall 31/520/12 temporarily supplies the
 * destination surface selector.  Descriptor kinds 0 and 1 are direct and
 * color-key copies; kinds 2 and 3 are empty native methods. */
static void draw_ax_extra(const uint32_t d[7],unsigned cell,void *context){
    (void)cell;KBootstrap *b=context;
    if(d[0]==2||d[0]==3)return;
    if(d[0]>3){error(b,"extended AX descriptor kind unsupported");return;}
    int32_t y=(int32_t)d[6];
    if(d[0]==1&&y>0x1df)y-=0x1e0;
    /* 4dd7a0 passes a zero copy-alpha flag; 4dd690 passes key 0xff00 and
       the same zero alpha flag.  The portable layer helper has the same
       byte-preserving behavior for q[8]==0. */
    int32_t q[9]={(int32_t)d[5],y,(int32_t)d[3],(int32_t)d[4],b->animation_target_layer,
                 (int32_t)d[1],(int32_t)d[2],8,d[0]==1?0xff00:0};
    if(blit_args(b,d[0]==1?1:0,q))return;
}
#include "ax_runtime.inc"
#include "mam_runtime.inc"
#include "credits.inc"
#include "montage.inc"
#include "novel.inc"
#include "letter.inc"
#include "message_skin.inc"
#include "diary.inc"
#include "param_window.inc"
#include "title_initial.inc"
#include "../build/media_tables.h"
#include "distort.inc"
#include "movie.inc"
static int start_logo_track(KBootstrap *b,unsigned cell){
    if(!ax_control(&b->ax,3,0,cell))return error(b,"invalid logo AX track");
    return 0;
}
static int draw_text(KBootstrap *b);
#include "bonus52.inc"
#include "area_runtime.inc"
#include "status_runtime.inc"

static void title_extra(KBootstrap *b){
    KTitle *t=&b->title;t->extra=1;t->main_count=t->count;t->count=0;t->selected=0;
    unsigned selector=b->vm->bytes[8100];if(selector>3)selector=3;
    if(kgallery_load(&b->gallery,&b->data,bootstrap_save_dir(b),selector)){error(b,"CG history load failed");return;}
    if(b->gallery.flags[KGALLERY_COUNT])t->extra_ids[t->count++]=1;
    if(b->vm->bytes[4005]==1)t->extra_ids[t->count++]=2;
    if(b->vm->bytes[4004]==1)t->extra_ids[t->count++]=3;
    t->extra_ids[t->count++]=4;t->extra_ids[t->count++]=5;
}
static void exec526_release(KBootstrap *b){
    /* 0x47ae90 releases the two native working surfaces and clears the
       companion state field.  Keep this pair separate from CLetter and the
       31/13 helper surfaces; an idle native pair is a valid no-op. */
    for(unsigned i=0;i<2;i++)rmt_free(&b->exec526_surfaces[i]);
    b->exec526_active=0;
}
int bootstrap_dispatch(KBootstrap *b){
    KVM *v=b->vm;int32_t main=v->syscall,sub,a,c,d,e;
    if(v->status!=KVM_SYSCALL)return error(b,"VM is not at a syscall");
    /* Peek first: unsupported handlers preserve their arguments for diagnostics. */
    if(!v->sp||v->stack[v->sp-1].string)return error(b,"missing integer subcall");
    sub=v->stack[v->sp-1].number;
    if(main==31&&sub==110&&v->sp>=3&&!v->stack[v->sp-2].string&&!v->stack[v->sp-3].string&&
       (v->stack[v->sp-2].number==0||v->stack[v->sp-2].number==1)&&v->stack[v->sp-3].number==0){
        if(v->stack[v->sp-2].number==0?title_initial(b):title_open_native(b))return -1;
        v->sp-=3;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==10&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==3){
        if(message_skin_reload(b))return -1;
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==528&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==0){
        if(v->sp<6)return error(b,"CKisakuParamWnd four values required (arguments preserved)");
        int32_t values[4];for(unsigned i=0;i<4;i++){
            if(v->stack[v->sp-3-i].string)return error(b,"CKisakuParamWnd numeric value required (arguments preserved)");
            values[i]=v->stack[v->sp-3-i].number;
        }
        if(param_window_values(b,values))return -1;
        v->sp-=6;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==528&&v->sp>=5&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==10&&
       !v->stack[v->sp-3].string&&v->stack[v->sp-3].number==1&&!v->stack[v->sp-4].string&&!v->stack[v->sp-5].string){
        if(param_window_counts(b,v->stack[v->sp-4].number,v->stack[v->sp-5].number))return -1;
        v->sp-=5;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==528&&v->sp>=3&&!v->stack[v->sp-2].string&&
       (v->stack[v->sp-2].number==25||v->stack[v->sp-2].number==26)&&!v->stack[v->sp-3].string){
        if(param_window_marker(b,v->stack[v->sp-2].number-25,v->stack[v->sp-3].number))return -1;
        v->sp-=3;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==528&&v->sp>=4&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==10&&
       !v->stack[v->sp-3].string&&v->stack[v->sp-3].number==0&&!v->stack[v->sp-4].string){
        if(param_window_rows(b,v->stack[v->sp-4].number,0))return -1;
        v->sp-=4;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==528&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==24){
        if(diary_reset(b))return -1;
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==528&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==9){
        /* 4fa9e0's four-case switch has no case for action 9 and returns
           without touching the preceding library arguments.  The Japanese
           liblary path builds subcall 528 as (9 + 10 + 518); consume only
           action/subcall and leave its eight preceding values intact. */
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==612&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==2){
        /* 4fb5a0 -> CBowling secondary vtable 54519c +10 -> 4cfd20. */
        if(kbowling_release(&b->bowling,&b->current_bowling))return error(b,"CBowling resource ownership invalid (arguments preserved)");
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==522){
        /* 4fcef0 consumes the mode followed by one selector for modes 0..3
           and two selectors for modes 4/5.  The four CImage windows are kept
           independently from script layers; unknown modes and malformed
           selectors preserve the complete argument stack for diagnostics. */
        if(v->sp<2)return error(b,"31/522 mode value required (arguments preserved)");
        if(v->stack[v->sp-2].string)return error(b,"31/522 mode must be numeric (arguments preserved)");
        int mode=v->stack[v->sp-2].number,count=(mode==4||mode==5)?2:((mode>=0&&mode<=3)?1:0);
        if(mode<0||mode>5)return error(b,"31/522 mode unsupported (arguments preserved)");
        if(v->sp<2u+(unsigned)count)return error(b,"31/522 mode arguments missing (arguments preserved)");
        int args[2]={0,0};
        for(unsigned i=0;i<(unsigned)count;i++){
            unsigned pos=v->sp-3-i;
            if(v->stack[pos].string)return error(b,"31/522 selector must be numeric (arguments preserved)");
        }
        /* Stack order is the script order: [item,group,mode,522]. */
        if(count==1)args[0]=v->stack[v->sp-3].number;
        else if(count==2){args[0]=v->stack[v->sp-4].number;args[1]=v->stack[v->sp-3].number;}
        if(animation522_draw(b,mode,args,(unsigned)count))return -1;
        v->sp-=2+(unsigned)count;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==523){
        /* 4fce50 dispatches the two Sungeki board renderers and then
           releases its temporary actor.  The renderer performs the same
           fixed six-step layer copies before the syscall is consumed. */
        if(v->sp<2)return error(b,"31/523 mode value required (arguments preserved)");
        if(v->stack[v->sp-2].string)return error(b,"31/523 mode must be numeric (arguments preserved)");
        if(animation523_draw(b,v->stack[v->sp-2].number))return -1;
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==521){
        /* 4fa060 parses one layer selector and calls CDIB::virtual_180
           (48cc20), which transfers the lower page's blue mask into the
           upper page Alpha bytes.  Keep the selector and subcall on the VM
           stack if the target surface cannot be represented safely. */
        if(v->sp<2)return error(b,"31/521 layer selector required (arguments preserved)");
        if(v->stack[v->sp-2].string)return error(b,"31/521 layer selector must be numeric (arguments preserved)");
        if(animation521_draw(b,v->stack[v->sp-2].number))return -1;
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==525&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==2){
        if(letter_exit(b))return -1;
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==524&&v->sp>=2&&!v->stack[v->sp-2].string){
        int action=v->stack[v->sp-2].number;
        if(action==29||action==30){
            /* 4f9eb0 -> 46c170/45c800 and 46c080/45c7d0.
               Initial sprites are hidden. Resource creation and badge drawing
               actions remain gated until their renderers are implemented. */
            if(action==29){animation522_restore(b);koverlay_suspend(&b->overlays);}else koverlay_restore(&b->overlays);
            v->sp-=2;b->handled++;return kvm_resume(v);
        }
    }
    if(main==31&&sub==524&&v->sp>=2&&!v->stack[v->sp-2].string){
        int action=v->stack[v->sp-2].number;
        if(action==0){
            /* 4f9eb0 consumes [var_8h, var_ch, action, sub]. The packed
               var_ch value selects the first and second fixed sprite rows. */
            if(v->sp<4||v->stack[v->sp-3].string||v->stack[v->sp-4].string)
                return error(b,"31/524 draw arguments required (arguments preserved)");
            int packed=v->stack[v->sp-3].number,third=v->stack[v->sp-4].number;
            int first=packed/100-1,second=packed%100-1;
            if(overlay524_draw(b,first,second,third))return -1;
            v->sp-=4;b->handled++;return kvm_resume(v);
        }
        if(action==1){
            if(overlay524_clear(b))return -1;
            v->sp-=2;b->handled++;return kvm_resume(v);
        }
        return error(b,"31/524 action unsupported (arguments preserved)");
    }
    if(main==14&&sub>=3&&sub<=6){
        /* 4f8ad0 / 4f88a0 / 4f8670 / 4f8440: copy the same indexed
           range from a snapshot into byte/word/raw/bank1 storage. */
        if(v->sp<4)return error(b,"FLAG restore arguments required (arguments preserved)");
        for(unsigned i=2;i<=4;i++)if(v->stack[v->sp-i].string)return error(b,"FLAG restore numeric arguments required (arguments preserved)");
        int slot=v->stack[v->sp-2].number,start=v->stack[v->sp-3].number,count=v->stack[v->sp-4].number;
        size_t capacity=sub==3?v->byte_count:sub==4?v->word_count:sub==5?b->raw_size:v->global_count[1];
        if(slot<0||slot>999||start<0||count<0||(uint64_t)(unsigned)start+(unsigned)count>capacity)return error(b,"FLAG restore destination range (arguments preserved)");
        KFlags *f=kflags_read_slot(bootstrap_save_dir(b),0,(unsigned)slot);
        if(!f)return error(b,"FLAG restore snapshot missing or malformed (arguments preserved)");
        size_t source=sub==3?f->byte_count:sub==4?f->word_count:sub==5?f->raw_count:f->counts[1];
        if((uint64_t)(unsigned)start+(unsigned)count>source){kflags_free(f);return error(b,"FLAG restore source range (arguments preserved)");}
        if(count){
            if(sub==3)memcpy(v->bytes+start,f->bytes+start,(size_t)count);
            else if(sub==4)memcpy(v->words+start,f->words+start,(size_t)count*sizeof(uint16_t));
            else if(sub==5)memcpy(b->raw_variables+start,f->raw+start,(size_t)count);
            else for(int i=0;i<count;i++)if(owned_value(b,&v->globals[1][start+i],f->globals[1][start+i])){kflags_free(f);return -1;}
        }
        kflags_free(f);v->sp-=4;b->handled++;return kvm_resume(v);
    }
    if(main==14&&sub==11){
        /* 4f7ab0 -> CFuncFlag vtable +0c -> 5080a0. The Japanese
           game uses FLAG100/FLAG201, without Kawa2's byte8100 selector. */
        if(v->sp<2||v->stack[v->sp-2].string)return error(b,"FLAG merge slot required (arguments preserved)");
        int slot=v->stack[v->sp-2].number;
        if(slot<0||slot>999)return error(b,"FLAG merge slot range (arguments preserved)");
        KFlags *saved=kflags_read_slot(bootstrap_save_dir(b),0,(unsigned)slot);
        if(!saved)return error(b,"FLAG merge snapshot missing or malformed (arguments preserved)");
        KFlags current={0};current.bytes=v->bytes;current.byte_count=v->byte_count;
        current.words=v->words;current.word_count=v->word_count;
        current.globals[1]=v->globals[1];current.counts[1]=v->global_count[1];
        int failed=kflags_merge(saved,&current);
        if(!failed)failed=kflags_write_slot(saved,bootstrap_save_dir(b),0,(unsigned)slot);
        kflags_free(saved);
        if(failed)return error(b,"FLAG merge failed (arguments preserved)");
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    if(getenv("KISAKU_TRACE_CALLS")){fprintf(stderr,"%s @%zx %d/%d stack=",v->modules[v->module].name,v->instruction_ip,main,sub);for(unsigned i=0;i<v->sp;i++)if(v->stack[i].string)fprintf(stderr," [string]");else fprintf(stderr," %d",v->stack[i].number);fputc('\n',stderr);}
    /* 486160 returns immediately with no selected native media stream.
       Preserve the error boundary for the still-unmapped active-stream case. */
    if(main==31&&sub==1010&&v->sp>=2&&!v->stack[v->sp-2].string&&!b->video&&!b->mov_data){
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    /* 4b3850(0) enables auxiliary-window access without restoring any
       window; foreground activation is owned by the platform frontend. */
    if(main==31&&sub==528&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==22){
        b->auxiliary_windows_enabled=1;
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    /* 4fc811 -> 4b2fa0(0): clear auxiliary-window visibility and hide
       all three windows. Only the already-hidden startup state is mapped;
       their interactive show/restore interfaces remain unsupported. */
    if(main==31&&sub==528&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==23&&
       !b->status_visible&&!b->scene_ui_pending&&!b->scene_modal&&!b->scene_focus){
        b->auxiliary_windows_enabled=0;
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    /* 4f9960 actions 2/10 -> 503cd0/503d30: clear/set the background
       resource name, with native ASCII uppercase conversion (457c7d). */
    if(main==31&&sub==1011&&v->sp>=2&&!v->stack[v->sp-2].string){
        int action=v->stack[v->sp-2].number;
        if(action==11&&!b->video&&!b->mov_data){
            /* 5042d0 consults CAnimeManager's registered (bank,cell) list,
               not the direct AX playback state. Constructor 4de390 creates
               an empty list; native manager start interfaces remain gated.
               In this state only records with no conditions can match. */
            size_t cursor=0;const KMediaRecord *record;
            while((record=kmedia_find(&b->media_tables,b->media_background_name,&cursor))){
                if(record->conditions[0]!=-1)continue;
                if(record->flag<0||(unsigned)record->flag>=v->byte_count)return error(b,"media flag bounds (arguments preserved)");
                v->bytes[record->flag]=1;
                int mode=v->globals[1][61].number;
                if(mode==1){v->bytes[3600]=1;v->bytes[3601]=1;}
                else if(mode==0){v->bytes[4001]=1;v->bytes[4005]=1;}
                break;
            }
            v->sp-=2;b->handled++;return kvm_resume(v);
        }
        if(action==0||action==1){
            /* 504420 replaces the primary map; only action 0 also replaces
               the auxiliary map through 503db0. Background name is retained. */
            b->media_tables.records=action==0?kisaku_media_main:kisaku_media_alternate;
            b->media_tables.count=action==0?sizeof(kisaku_media_main)/sizeof(*kisaku_media_main):sizeof(kisaku_media_alternate)/sizeof(*kisaku_media_alternate);
            if(action==0){b->media_tables.links=kisaku_media_links;b->media_tables.link_count=sizeof(kisaku_media_links)/sizeof(*kisaku_media_links);}
            v->sp-=2;b->handled++;return kvm_resume(v);
        }
        if(action==2){b->media_background_name[0]=0;v->sp-=2;b->handled++;return kvm_resume(v);}
        if(action==10&&v->sp>=3&&v->stack[v->sp-3].string){
            const char *name=v->stack[v->sp-3].string;size_t n=strlen(name);
            if(n>=sizeof(b->media_background_name))return error(b,"media background name too long");
            for(size_t i=0;i<=n;i++){unsigned char ch=(unsigned char)name[i];b->media_background_name[i]=(char)(ch>='a'&&ch<='z'?ch-'a'+'A':ch);}
            v->sp-=3;b->handled++;return kvm_resume(v);
        }
    }
    if(main==31&&sub==30&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==0){
        if(choice_initialize(b))return -1;
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    /* 4fb980 constructs CFuncAnimeEx. The native dispatcher reads one
       variant command after subcall 520; commands 0, 1 and 12 then consume
       one, two and three additional variants respectively. Opening.mes uses
       these routes through liblary function 14. A string command falls
       through the native switch default and returns without touching the
       stack further. */
    if(main==31&&sub==520&&v->sp>=2){
        KValue command=v->stack[v->sp-2];
        if(command.string){
            v->sp-=2;b->handled++;return kvm_resume(v);
        }
        if(command.number==7){
            /* 5032a0 -> CAnimeManagerEX +28 -> 4dec50: set all 320 tracks
               to 0xff. This is independent of syscall 13's animation bank. */
            for(unsigned i=0;i<AX_CELLS;i++)b->ax_extra.cells[i].state=AX_STOPPED;
            b->ax_extra_clock=0;
            b->animation_track_selected=0;
            v->sp-=2;b->handled++;return kvm_resume(v);
        }
        if(command.number==10){
            /* 503480 -> 4de9a0 synchronizes the extended manager and drains
               its native event queue.  The portable manager has no event
               queue yet, so mirror the statically verified idle transition
               (state 0 -> pending state 3); an active track stays on the
               explicit unsupported boundary. */
            for(unsigned i=0;i<AX_CELLS;i++){
                struct ax_cell *c=&b->ax_extra.cells[i];
                if(c->state==0)c->state=3;
                else if(c->state!=AX_STOPPED&&c->state!=4)
                    return error(b,"31/520 action 10 active manager unsupported (arguments preserved)");
            }
            b->animation_track_selected=0;
            v->sp-=2;b->handled++;return kvm_resume(v);
        }
        if(command.number==0){
            /* 4fede0 -> 403bb0 -> manager +0x0c. Retain the selected
               animation value for the portable manager. The native manager
               also loads the AX stream here; keep that resource boundary
               explicit even though its private drawing path is still gated. */
            if(v->sp<3)return error(b,"31/520 action 0 value required (arguments preserved)");
            KValue value=v->stack[v->sp-3];
            if(value.string){
                if(strlen(value.string)>=sizeof(b->animation_name))return error(b,"31/520 animation name too long (arguments preserved)");
                uint8_t *data=NULL;size_t size=0;
                if(read_named(&b->data,value.string,&data,&size)||!ax_load(&b->ax_extra,value.string,data,size)){
                    free(data);return error(b,"31/520 animation resource invalid (arguments preserved)");
                }
                free(data);
                strcpy(b->animation_name,value.string);
                b->ax_extra_clock=0;
            }else {b->animation_id=value.number;ax_reset(&b->ax_extra);b->ax_extra_clock=0;}
            b->animation_track_selected=0;
            v->sp-=3;b->handled++;return kvm_resume(v);
        }
        if(command.number==1){
            /* 4fee60 case 1 -> 4fed70 reads two variants before routing
               through manager +0x14 (4df540), which only selects the
               stream pointer and descriptor table for the next manager
               operation. Opening's function 14 passes [0,0]. */
            if(v->sp<4)return error(b,"31/520 action 1 track values required (arguments preserved)");
            if(v->stack[v->sp-3].string||v->stack[v->sp-4].string)
                return error(b,"31/520 action 1 numeric track values required (arguments preserved)");
            int bank=v->stack[v->sp-4].number,cell=v->stack[v->sp-3].number;
            if(bank<0||bank>=10||cell<0||cell>=32)
                return error(b,"31/520 action 1 track range (arguments preserved)");
            unsigned index=(unsigned)bank*32u+(unsigned)cell;
            uint32_t start=b->ax_extra.cells[index].start;
            memset(&b->ax_extra.cells[index],0,sizeof(b->ax_extra.cells[index]));
            b->ax_extra.cells[index].start=start;b->ax_extra.cells[index].state=AX_STOPPED;
            b->animation_track_bank=(unsigned)bank;b->animation_track_cell=(unsigned)cell;b->animation_track_selected=1;
            v->sp-=4;b->handled++;return kvm_resume(v);
        }
        if(command.number==2){
            /* 4fee60 case 2 -> 4fed00 reads two variants and dispatches to
               CAnimeManagerEX +0x1c (4df1f0 -> 4052d0).  The native helper
               marks the selected extended track as running; it does not
               advance or draw a frame until the manager tick runs. */
            if(v->sp<4)return error(b,"31/520 action 2 track values required (arguments preserved)");
            if(v->stack[v->sp-3].string||v->stack[v->sp-4].string)
                return error(b,"31/520 action 2 numeric track values required (arguments preserved)");
            int bank=v->stack[v->sp-4].number,cell=v->stack[v->sp-3].number;
            if(bank<0||bank>=10||cell<0||cell>=32)
                return error(b,"31/520 action 2 track range (arguments preserved)");
            if(!ax_control(&b->ax_extra,2,(unsigned)bank,(unsigned)cell))
                return error(b,"31/520 action 2 track unavailable (arguments preserved)");
            b->animation_track_bank=(unsigned)bank;b->animation_track_cell=(unsigned)cell;b->animation_track_selected=1;
            v->sp-=4;b->handled++;return kvm_resume(v);
        }
        if(command.number==3||command.number==4){
            /* 4fec90/4fec20 consume the same (bank,cell) pair and call
               CAnimeManagerEX +0x1c/+0x20.  Those native setters only write
               the track state; they do not require an AX stream to be loaded. */
            if(v->sp<4)return error(b,"31/520 action track values required (arguments preserved)");
            if(v->stack[v->sp-3].string||v->stack[v->sp-4].string)
                return error(b,"31/520 action track values must be numeric (arguments preserved)");
            int bank=v->stack[v->sp-4].number,cell=v->stack[v->sp-3].number;
            if(bank<0||bank>=10||cell<0||cell>=32)
                return error(b,"31/520 action track range (arguments preserved)");
            struct ax_cell *track=&b->ax_extra.cells[(unsigned)bank*32u+(unsigned)cell];
            track->state=command.number==3?1:AX_STOPPED;
            b->animation_track_bank=(unsigned)bank;b->animation_track_cell=(unsigned)cell;b->animation_track_selected=1;
            v->sp-=4;b->handled++;return kvm_resume(v);
        }
        if(command.number==6){
            /* 5032e0 -> CAnimeManagerEX +0x24 (4df140): every track except
               the stopped sentinel is put into the running state. */
            for(unsigned i=0;i<AX_CELLS;i++)
                if(b->ax_extra.cells[i].state!=AX_STOPPED)b->ax_extra.cells[i].state=1;
            b->animation_track_selected=0;
            v->sp-=2;b->handled++;return kvm_resume(v);
        }
        if(command.number==12){
            /* 4fea10 reads three numeric variants before calling 005031b0.
               That wrapper selects the stream and immediately runs
               004ddcc0, whose first descriptor is rendered synchronously by
               CAnimeManagerEX::virtual_48/52. */
            if(v->sp<5)return error(b,"31/520 action 12 values required (arguments preserved)");
            for(unsigned i=3;i<=5;i++)if(v->stack[v->sp-i].string)return error(b,"31/520 action 12 numeric values required (arguments preserved)");
            int bank=v->stack[v->sp-5].number,cell=v->stack[v->sp-4].number;
            if(bank<0||bank>=10||cell<0||cell>=32)
                return error(b,"31/520 action 12 track range (arguments preserved)");
            if(v->stack[v->sp-3].string)return error(b,"31/520 action 12 target layer must be numeric (arguments preserved)");
            b->animation_target_layer=v->stack[v->sp-3].number;
            b->animation_track_bank=(unsigned)bank;b->animation_track_cell=(unsigned)cell;b->animation_track_selected=1;
            if(!ax_first_frame(&b->ax_extra,(unsigned)bank*32u+(unsigned)cell,draw_ax_extra,b))
                return error(b,"31/520 action 12 extended AX first frame invalid (arguments preserved)");
            if(b->error[0])return -1;
            v->sp-=5;b->handled++;return kvm_resume(v);
        }
        return error(b,"31/520 animation action unsupported (arguments preserved)");
    }
    if(main==31&&sub==526&&v->sp>=3&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==1){
        /* 4fd790 case 1 parses one variant, then 47ae90 releases its two
           private working surfaces and resets the native pair state. */
        if(v->stack[v->sp-3].string)return error(b,"31/526 action 1 value must be numeric (arguments preserved)");
        exec526_release(b);
        v->sp-=3;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==526&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==2){
        /* 4fd790 case 2 calls 48adf0, the same saved-surface crossfade
           helper used by CLetter.  Keep the transition asynchronous on the
           portable frame clock, then consume only the action and subcall. */
        if(letter_exit(b))return -1;
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    /* 481b00 -> 4837a0 checks CMesWnd visibility before animating.
       A visible window still needs the Kisaku sprite transition implementation. */
    if(main==31&&sub==10&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==2&&!b->message_visible&&!b->message_slide){
        b->message_active=0;v->sp-=2;b->handled++;return kvm_resume(v);
    }
    /* Remaining main/31 actions are validated by the dispatch table below.
       An earlier startup-only guard admitted only 31/10 action 0 here and
       made the already implemented message, choice, scene, and title paths
       unreachable.  Keep unknown calls on the explicit unsupported boundary
       instead of rejecting every recognized action before it is decoded. */
    int message_timed=main==31&&sub==10&&v->sp>=4&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==4;
    /* Native table 42b7f4 aliases actions 0 and 3 to 42b6d5. */
    int message_reset=main==31&&sub==10&&v->sp>=2&&!v->stack[v->sp-2].string&&(v->stack[v->sp-2].number==0||v->stack[v->sp-2].number==3);
    int animation_reset=main==31&&sub==11&&v->sp>=2&&!v->stack[v->sp-2].string&&(v->stack[v->sp-2].number>=0&&v->stack[v->sp-2].number<=5);
    int scene_reset=main==31&&sub==21&&v->sp>=2&&!v->stack[v->sp-2].string&&(v->stack[v->sp-2].number==3||v->stack[v->sp-2].number==4);
    int message_hidden=main==31&&sub==10&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==2;
    int effects_idle=main==31&&sub==11&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==6;
    int helper_reset=main==31&&sub==13&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number>=0&&v->stack[v->sp-2].number<=2;
    int scene_restore=main==31&&sub==21&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==2;
    int scene_flags=main==31&&sub==21&&v->sp>=2&&!v->stack[v->sp-2].string&&(v->stack[v->sp-2].number==5||v->stack[v->sp-2].number==6||v->stack[v->sp-2].number==9||v->stack[v->sp-2].number==10);
    int helper_present=main==31&&sub==13&&v->sp>=5&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==3&&!v->stack[v->sp-5].string&&v->stack[v->sp-5].number==1;
    int helper_hide=main==31&&sub==13&&v->sp>=3&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==4;
    int voice_register=main==17&&sub==5&&!(v->globals[0][50].number&0x400);
    int record_idle=main==23&&(sub==2||sub==3);
    int scene_register=main==31&&sub==23&&v->sp>=4&&!v->stack[v->sp-2].string&&(v->stack[v->sp-2].number==0||v->stack[v->sp-2].number==1);
    int bonus54_title=main==31&&sub==64&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==0;
    int bonus54_menu=main==31&&sub==64&&v->sp>=2&&!v->stack[v->sp-2].string&&(v->stack[v->sp-2].number==1||v->stack[v->sp-2].number==2);
    int area_open=main==31&&sub==60&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==1;
    int name_attach=main==31&&sub==63&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==0;
    int bonus53_title=main==31&&sub==63&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==1;
    int bonus_credits=main==31&&sub==62&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==2;
    int bonus_meter=main==31&&sub==62&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==1;
    int offset_image=main==31&&sub==60&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==0;
    int bonus_title=main==31&&sub==62&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==0;
    int title_open=(main==31&&sub==12)||bonus_title||bonus53_title||bonus54_title;
    int scene_ui=main==31&&sub==21&&v->sp>=2&&!v->stack[v->sp-2].string&&(v->stack[v->sp-2].number==1||v->stack[v->sp-2].number==8||v->stack[v->sp-2].number==11);
    int scene_export=main==31&&sub==21&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==0;
    int scene_hide=main==31&&sub==21&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==7;
    /* Calls handled by the general table must still be explicit.  The
       startup-only guard that used to sit here rejected 31/0 before the
       message dispatcher, while removing it entirely would silently accept
       unsupported 31/612 and 31/1011 variants through the default branch. */
    int main31_known=main!=31||
        sub==0||sub==1||sub==12||sub==14||sub==15||sub==16||sub==17||sub==18||
        sub==19||sub==20||sub==22||sub==24||sub==25||sub==26||sub==27||sub==28||
        sub==29||sub==30||sub==40||sub==65||sub==66||sub==67||sub==522||sub==523||
        (sub==10&&(message_timed||message_reset||message_hidden))||
        (sub==11&&(animation_reset||effects_idle))||
        scene_ui||scene_export||scene_hide||scene_restore||scene_flags||scene_reset||
        scene_register||helper_reset||helper_present||helper_hide||
        bonus54_title||bonus54_menu||area_open||name_attach||bonus53_title||
        bonus_credits||bonus_meter||offset_image||bonus_title;
    if(!main31_known)return error(b,"Kisaku game-specific interface not yet mapped (arguments preserved)");
    if(!((main==31&&(sub<0||sub>67||(sub>=2&&sub<=9)||(sub>=31&&sub<=39)||(sub>=41&&sub<=59)||sub==61))||(main==31&&(sub==20||sub==22||sub==40))||(main==31&&sub==15)||(main==31&&sub==17)||(main==31&&sub==16)||(main==31&&sub==18)||(main==31&&sub==14)||(main==1&&sub==0)||(main==16&&(sub==1||sub==3||sub==5||sub==6||sub==7))||(main==31&&(sub==0||sub==1))||(main==30&&sub==0)||(main==17&&(sub==1||sub==6||sub==7))||voice_register||record_idle||scene_register||(main==31&&sub==29)||(main==31&&(sub==65||sub==66||sub==67))||bonus54_menu||area_open||name_attach||bonus_credits||bonus_meter||offset_image||title_open||(main==31&&sub==19)||scene_ui||scene_export||scene_hide||(main==31&&(sub==25||sub==26||sub==27||sub==28||sub==30))||(main==31&&sub==523)||scene_flags||scene_restore||helper_reset||helper_present||helper_hide||effects_idle||message_hidden||scene_reset||(main==31&&sub==24)||animation_reset||message_timed||message_reset||(main==21&&(sub==0||sub==1||sub==4))||(main==13)||(main==24&&sub>=0&&sub<=6)||(main==28&&sub>=0&&sub<=11)||(main==23&&(sub==0||sub==1))||(main==27&&(sub>=0&&sub<=3))||(main==26&&(sub==0||sub==1))||(main==14&&(sub==0||sub==2||sub==3||sub==6||sub==11||sub==13))||((main>=15&&main<=18)&&sub==0)||((main>=15&&main<=17)&&sub==2)||(main==15&&(sub==1||sub==3||sub==5||sub==6))||((main==10||main==11)&&sub==0)||(main==19&&(sub>=0&&sub<=9))||(main==25&&sub>=0&&sub<=3)||(main==22&&sub>=0&&sub<=2))){
        char msg[128];snprintf(msg,sizeof(msg),"unsupported subcall %d (arguments preserved)",sub);return error(b,msg);
    }
    if(integer(b,&sub))return -1;
    if(message_timed){
        if(integer(b,&a)||integer(b,&c)||integer(b,&d))return -1;
        b->message_keep_on_confirm=1;if(message_begin(b,d))return -1;
        b->message_timed=1;b->message_timed_delay=c>0?(unsigned)c:0;b->message_timed_clock=0;
    }else if(main==31&&(sub<0||sub>67||(sub>=2&&sub<=9)||(sub>=31&&sub<=39)||(sub>=41&&sub<=59)||sub==61)){
        /* 42b230 byte-index table: only these ranges select the native default. */
    }else if(main==31&&sub==19){b->title_load_requested=b->load_modal=1;}else if(scene_ui){
        if(integer(b,&a)||!b->scene)return error(b,"scene interface state missing");
        if(a==1){
            KScene *next=malloc(sizeof(*next));if(!next)return error(b,"scene import allocation failed");*next=*b->scene;
            if(kscene_restore(next,v->bytes,v->byte_count,b->raw_variables,b->raw_size)||kscene_path_restore(next,v->words,v->word_count,NULL)){free(next);return error(b,"scene import invalid");}
            *b->scene=*next;free(next);b->scene_ui_pending=1;
        }else{
            b->scene_focus=b->scene->path_count?b->scene->counters[b->scene->path_count-1]:0;
            b->scene_panel_request=a==8?1:2;if(a==8)b->scene_modal=1;
        }
    }else if(scene_export){
        if(integer(b,&a))return -1;
        /* 44b9c0 exports map visited/status/flags and the chronological path. */
        if(b->scene){memcpy(v->bytes+3000,b->scene->visited,1000);memcpy(v->bytes+4500,b->scene->status,370);memcpy(v->bytes+5000,b->scene->flags,370);memcpy(v->words+11,b->scene->counters,370*sizeof(uint16_t));}
    }else if(scene_hide){if(integer(b,&a))return -1;b->scene_ui_pending=b->scene_modal=0;b->scene_panel_request=-1;}
    else if(main==31&&sub==22){if(montage_begin(b))return -1;}
    else if(main==31&&sub==20){if(credits_begin(b))return -1;}
    else if(main==31&&sub==28){b->extra_active=1;b->extra_kind=b->extra_request=10;}
    else if(main==31&&sub==26){b->extra_active=1;b->extra_kind=b->extra_request=13;}
    else if(main==31&&sub==27){b->extra_active=1;b->extra_kind=b->extra_request=11;}
    else if(main==31&&sub==30){
        if(integer(b,&a))return -1;
        if(a!=1)return error(b,"CNormalSelect action unsupported (arguments preserved)");
        b->choice_normal=1;
        if(choice_begin(b))return -1;
    }
    else if(main==31&&sub==25){b->extra_active=1;b->extra_kind=b->extra_request=9;}
    else if(main==31&&sub==15){if(scroll_begin(b))return -1;}
    else if(main==31&&sub==17){if(blink_begin(b))return -1;}
    else if(main==31&&sub==16){if(distort_begin(b))return -1;}
    else if(main==31&&sub==18){if(novel_dispatch(b))return -1;}
    else if(main==31&&sub==14){if(choice_begin(b))return -1;}
    else if(main==1&&sub==0){if(integer(b,&a)||kvm_list_clear(v,a))return error(b,"list initialization failed");}
    else if(main==31&&(sub==0||sub==1)){
        b->message_keep_on_confirm=sub==1;
        if(integer(b,&a)||message_begin(b,a))return -1;
    }else if(main==31&&sub==40){
        /* CFuncExec case 0x28 consumes one direction flag. */
        if(integer(b,&a)||exec_wipe_begin(b,a))return -1;
    }else if(record_idle){
        if(v->globals[0][50].number&0x80){
            if(sub==2){
                if(!(v->globals[0][50].number&0x100)){
                    if(!b->message_count)return error(b,"message recorder has no slots");
                    b->message_index++;
                    if((unsigned)b->message_index>=b->message_count){
                        free(b->messages[0].data);free(b->messages[0].text);
                        memmove(b->messages,b->messages+1,(b->message_count-1)*sizeof(*b->messages));
                        memset(&b->messages[b->message_count-1],0,sizeof(*b->messages));b->message_index=(int)b->message_count-1;
                    }
                }
                v->globals[0][50].number|=0x100;
            }else {
                if(v->globals[0][50].number&0x100){uint8_t zero=0;if(record_append(b,&zero,1))return -1;}
                v->globals[0][50].number&=~0x100;
            }
        }
    }else if(scene_register){
        if(integer(b,&a)||integer(b,&c)||integer(b,&d))return -1;
        if(!a){if(!b->scene_replay&&khistory_register(&b->scene_history,bootstrap_save_dir(b),c,d,v->modules[v->module].name,v->globals[0][48].number))return error(b,"scene checkpoint registration failed");}
        else {
            if(integer(b,&e))return -1;
            if(e<0||e>255)return error(b,"scene completion value out of range");
            /* 475a65 tests SceneData + 0x56b8, not script bank1[69].
               Appendix animation scripts also use that script variable. */
            if(b->scene_replay){
                b->scene_replay_finished=1;b->handled++;kvm_resume(v);return 0;
            }
            int previous=0;
            if(khistory_completion(&b->scene_history,bootstrap_save_dir(b),c,d,-1,&previous))return error(b,"scene completion lookup failed");
            if(!previous){
                if(!b->scene||v->byte_count<8101||v->word_count<381)return error(b,"scene completion state missing");
                KScene *next=malloc(sizeof(*next));if(!next)return error(b,"scene completion allocation failed");*next=*b->scene;
                if(kscene_complete(next,c,d)){free(next);return error(b,"scene completion range invalid");}
                uint8_t bytes[8192];memcpy(bytes,v->bytes,sizeof(bytes));
                memcpy(bytes+3000,next->visited,1000);memcpy(bytes+4500,next->status,370);memcpy(bytes+5000,next->flags,370);
                unsigned selector=v->bytes[8100];if(selector>3)selector=3;
                if(kflags_progress(bootstrap_save_dir(b),selector,bytes,v->byte_count)){free(next);return error(b,"scene progress save failed");}
                memcpy(v->bytes,bytes,sizeof(bytes));memcpy(v->words+11,next->counters,sizeof(next->counters));*b->scene=*next;free(next);
            }
            if(khistory_completion(&b->scene_history,bootstrap_save_dir(b),c,d,e,NULL))return error(b,"scene completion update failed");
        }
    }else if(main==31&&sub==29){
        /* 42e720 -> 4285b0/428660: optional history reset before New Game. */
        KFlagDialog dialog={0};const char *names[]={"topsub.rmt","topsub_p.rmt"};
        KImage *images[]={&dialog.background,&dialog.parts};
        for(unsigned i=0;i<2;i++){
            uint8_t *data=NULL;size_t size=0;
            if(read_named(&b->images,names[i],&data,&size)||rmt_decode(data,size,images[i])){free(data);kflag_dialog_free(&dialog);return error(b,"history dialog resource decode failed");}free(data);
        }
        uint8_t *data=NULL;size_t size=0;
        if(read_named(&b->data,"FlagDlg.area",&data,&size)||kflag_dialog_areas(&dialog,data,size)){free(data);kflag_dialog_free(&dialog);return error(b,"history dialog areas invalid");}free(data);
        dialog.active=1;dialog.selected=-1;
        kflag_dialog_free(&b->flag_dialog);b->flag_dialog=dialog;
        if(kflag_dialog_draw(&b->flag_dialog,&b->layers[0]))return error(b,"history dialog composition failed");
    }else if(main==31&&sub==67){
        if(integer(b,&a)||a<0||a>2)return error(b,"status meter action");
        if(a==1)b->status_visible=0;
        else {if(status_refresh(b))return -1;if(a==0)b->status_visible=1;}
    }else if(main==31&&sub==65){
        if(integer(b,&a)||a<0||a>255||(unsigned)a>v->sp||v->byte_count<1952+(unsigned)a)return error(b,"cursor tool count");
        v->bytes[1950]=0;v->bytes[1951]=(uint8_t)a;v->bytes[1952]=0;
        for(int i=0;i<a;i++){if(integer(b,&c))return -1;v->bytes[1952+i]=(uint8_t)c;}
    }else if(main==31&&sub==66){
        const char *name;if(string(b,&name)||area_begin(b,name))return -1;
        b->area_active=2;
    }else if(bonus54_menu){
        /* 43b230 / 43b2b0 change the Windows menu resource and EnableMenuItem. */
        if(integer(b,&a))return -1;
    }else if(area_open){
        const char *name;if(integer(b,&a)||string(b,&name)||area_begin(b,name))return -1;
    }else if(name_attach){
        if(integer(b,&a))return -1;
        const uint8_t *name=v->bytes+1950,*end=memchr(name,0,33);
        if(!end)return error(b,"unterminated player name");
        if(end!=name){v->text=(const char *)name;v->text_size=(size_t)(end-name);v->status=KVM_TEXT;if(draw_text(b))return -1;}
    }else if(bonus_credits){
        if(integer(b,&a)||bonus52_credits_begin(b))return -1;
    }else if(bonus_meter){
        if(integer(b,&a)||integer(b,&a)||integer(b,&c)||integer(b,&d))return -1;
        if(bonus52_begin(b,a,c,d))return -1;
    }else if(title_open){
        if(bootstrap_flush_progress(b))return -1;
        /* 490c60/490de0: bank1[60]==1 reopens the extras submenu. */
        KTitle title={0};const char *names[]={"top_bg.rmt","top.rmt"};
        if(bonus_title){if(integer(b,&a))return -1;title.variant=1;names[0]="52_top_bg.rmt";names[1]="52_top.rmt";}
        if(bonus53_title){if(integer(b,&a))return -1;title.variant=2;names[0]="53_title_bg.rmt";names[1]="53_title_pt.rmt";}
        if(bonus54_title){if(integer(b,&a))return -1;title.variant=3;names[0]="54_title.rmt";names[1]="54_title_p.rmt";}
        KImage *images[]={&title.background,&title.parts};
        for(unsigned i=0;i<2;i++){
            uint8_t *data=NULL;size_t size=0;
            if(read_named(&b->images,names[i],&data,&size)||rmt_decode(data,size,images[i])){free(data);ktitle_free(&title);return error(b,"title resource decode failed");}free(data);
        }
        title.unlocked=v->globals[1][53].number!=0;title.count=(title.unlocked?4:3)-(title.variant==2?1:0);title.selected=-1;title.active=1;
        if(title.variant==3){title.count=0;title.extra_ids[title.count++]=0;if(title.unlocked)title.extra_ids[title.count++]=1;if(v->bytes[4030])title.extra_ids[title.count++]=2;title.extra_ids[title.count++]=3;}
        ktitle_free(&b->title);b->title=title;
        if(!title.variant&&v->globals[1][60].number==1)title_extra(b);
        /* Two native 32-tick crossfades; the first leads into a blank
           private page, the second presents the composed title. */
        memcpy(b->auxiliary.pixels,b->layers[0].pixels,640*480*4);
    }else if(scene_flags){
        if(integer(b,&a))return -1;
        /* 42cc33/42cc4a change +fac; 42ccaa/42ccbf change +fb4.
           48f080 refresh is inactive before navigation UI creation. */
        if(a==5||a==6)b->scene_flag_fac=a==5;else b->scene_flag_fb4=a==10;
    }else if(scene_restore){
        /* 44b820/44bb70 import map state and the saved chronological path. */
        if(integer(b,&a))return -1;
        if(b->raw_size<5402||v->global_count[1]<=53)return error(b,"scene variable capacity");
        unsigned sel=v->bytes[8100];if(sel>3)sel=3;
        KFlags *f=kflags_read_slot(bootstrap_save_dir(b),sel,0);if(!f)return error(b,"scene FLAG read failed");
        uint8_t *data=NULL;size_t size=0;KScene *scene=calloc(1,sizeof(*scene));
        if(!scene||f->counts[1]<=53||read_named(&b->data,"map.map",&data,&size)||kscene_map(scene,data,size)||kscene_restore(scene,v->bytes,v->byte_count,f->raw,f->raw_count)||kscene_path_restore(scene,v->words,v->word_count,NULL)){
            free(data);free(scene);kflags_free(f);return error(b,"scene state preparation failed");
        }
        free(data);free(b->scene);b->scene=scene;b->scene_ui_pending=1;
        memcpy(b->raw_variables,f->raw,5402);
        int failed=owned_value(b,&v->globals[1][53],f->globals[1][53]);kflags_free(f);if(failed)return -1;
    }else if(helper_hide){
        /* 4455b0: fade accumulated text from 255 to zero, then hide. */
        if(integer(b,&a)||integer(b,&c))return -1;
        if(!b->helper_surfaces[1].pixels||!b->helper_visible||c<0||c>10000)return error(b,"helper hide state invalid");
        int speed=0;
        for(unsigned i=0;i<b->setting_count;i++)if(equal(b->settings[i].section,"Display")&&equal(b->settings[i].key,"EffectSpeed"))speed=atoi(b->settings[i].value);
        if(speed<0||speed>2)return error(b,"helper effect speed invalid");
        if(speed==1)c>>=1;
        if(speed==2||(v->globals[0][50].number&0x4000))c=0;
        b->helper_steps=(unsigned)c+1;b->helper_frame=0;b->helper_hide=1;
    }else if(helper_present){
        /* 4451c0 mode 1: center the current line, cross-fade the accumulated
           text and capture, then retain the capture for the next line. */
        if(integer(b,&a)||integer(b,&c)||integer(b,&d)||integer(b,&e))return -1;
        KImage *src=surface(b,v->globals[0][49].number);
        int x=v->globals[0][46].number,y=v->globals[0][47].number,h=v->globals[0][31].number;
        if(!src||!src->pixels||!b->helper_surfaces[0].pixels||!b->helper_surfaces[1].pixels||x<0||x>638||y<0||h<0||(int64_t)y+h>480||src->width<(unsigned)x+2||src->height<(unsigned)(y+h)||c<0||c>10000)return error(b,"helper presentation bounds invalid");
        int speed=0;
        for(unsigned i=0;i<b->setting_count;i++)if(equal(b->settings[i].section,"Display")&&equal(b->settings[i].key,"EffectSpeed"))speed=atoi(b->settings[i].value);
        if(speed<0||speed>2)return error(b,"helper effect speed invalid");
        if(speed==1)c>>=1;
        if(speed==2||(v->globals[0][50].number&0x4000))c=0;
        if(!b->helper_visible)memcpy(b->auxiliary.pixels,b->layers[0].pixels,640*480*4);
        unsigned left=(640-x)/2;
        for(int row=0;row<h;row++)memcpy(b->helper_surfaces[0].pixels+(y+row)*b->helper_surfaces[0].stride+left*4,src->pixels+(y+row)*src->stride,(x+2)*4);
        b->helper_steps=(unsigned)c+1;b->helper_frame=0;b->helper_visible=1;b->helper_hide=0;
    }else if(helper_reset){
        if(integer(b,&a))return -1;
        if(a==1){if(b->helper_visible)memcpy(b->layers[0].pixels,b->auxiliary.pixels,640*480*4);b->helper_visible=b->helper_steps=0;for(unsigned i=0;i<3;i++)rmt_free(&b->helper_surfaces[i]);}
        else if(a==0){
            /* 444e50 creates a window-sized capture and two 640x480 helper
               surfaces; the last starts at BGRA 0x64000000. */
            int w=800,h=600;
            for(unsigned i=0;i<b->setting_count;i++)if(equal(b->settings[i].section,"Display")){
                if(equal(b->settings[i].key,"Width"))w=atoi(b->settings[i].value);
                if(equal(b->settings[i].key,"Height"))h=atoi(b->settings[i].value);
            }
            if(w<640||h<480||w>4096||h>4096)return error(b,"helper display dimensions invalid");
            KImage next[3]={{0}};
            for(unsigned i=0;i<3;i++){
                unsigned width=i?640:(unsigned)w,height=i?480:(unsigned)h;
                next[i]=(KImage){0,0,width,height,width*4,calloc((size_t)width*height,4)};
                if(!next[i].pixels){for(unsigned j=0;j<3;j++)rmt_free(&next[j]);return error(b,"helper surface allocation failed");}
            }
            for(size_t i=0;i<640*480;i++)next[2].pixels[i*4+3]=100;
            for(unsigned i=0;i<3;i++){rmt_free(&b->helper_surfaces[i]);b->helper_surfaces[i]=next[i];}
        }else {
            /* 445030 clears helper pixels and the current message layer;
               sets the full-screen text rectangle in system variables. */
            KImage *target=surface(b,v->globals[0][49].number);
            if(!b->helper_surfaces[0].pixels||!b->helper_surfaces[1].pixels||!target||!target->pixels||target->width<640||target->height<480)return error(b,"helper clear surface missing");
            for(unsigned i=0;i<2;i++)for(unsigned y=0;y<480;y++)memset(b->helper_surfaces[i].pixels+y*b->helper_surfaces[i].stride,0,640*4);
            for(unsigned y=0;y<480;y++)memset(target->pixels+y*target->stride,0,640*4);
            for(unsigned i=0;i<4;i++)v->globals[0][42+i]=(KValue){i<2?0:i==2?640:480,NULL};
        }
    }else if(effects_idle){
        /* 443010 visits two native transient-effect slots. They are still
           uncreated here; portable panels are opened by their respective calls. */
        if(integer(b,&a))return -1;
    }else if(message_hidden){
        if(integer(b,&a))return -1;
        if(b->message_visible){
            unsigned duration=12;int speed=option(b,"Display","EffectSpeed",0);if(speed==1)duration>>=1;else if(speed==2||(v->globals[0][50].number&0x4000))duration=0;
            b->message_slide=duration+1;b->message_slide_frame=0;b->message_hiding=1;
        }
    }else if(scene_reset){
        if(integer(b,&a))return -1;
        /* 42cbe9/42cc0e set scene mode zero/one; 48f080 has no UI effects before
           navigation initialization (constructor 48cffd sets active=0). */
        b->scene_mode=a==4;
    }else if(main==31&&sub==24){
        if(!b->animation_data||!ax_load(&b->ax,b->loaded_animation,b->animation_data,b->animation_size))return error(b,"invalid AX data");
        if(play_pcm(b,&b->effects,"logo02.wav"))return -1;
        b->logo_phase=1;b->ax_clock=0;
    }else if(animation_reset){
        if(integer(b,&a))return -1;
        if(a==0){mam_stop(b);b->mouth_sheet_ready=0;const char *name;if(string(b,&name))return -1;if(strlen(name)>=sizeof(b->animation_name))return error(b,"animation context name too long");strcpy(b->animation_name,name);}
        else if(a==1||a==2){
            if(integer(b,&c))return -1;
            if(c<0||c>=32||!ax_control(&b->ax,1,0,(unsigned)c))return error(b,"invalid story AX track");
            if(a==1){
                if(integer(b,&d))return -1;
                if(!surface(b,d))return error(b,"invalid AX destination");
                b->ax_destination=d;b->ax_fast=1;
                int ok=ax_first_frame(&b->ax,(unsigned)c,draw_ax,b);
                b->ax_destination=0;b->ax_fast=0;
                if(!ok)return error(b,"invalid AX first frame");
                if(b->error[0])return -1;
            }else b->animation_id=c;
        }
        else if(a==5){
            mam_stop(b);b->mouth_sheet_ready=0;
            if(integer(b,&c))return -1;
            /* 42ba1f/41ad80: optional mouth sheet, native save selector 3 skips it. */
            if(v->byte_count<=8100)return error(b,"animation save selector missing");
            if(v->bytes[8100]!=3){
                char name[288],stem[261];memcpy(stem,b->animation_name,sizeof(stem));
                char *dot=strrchr(stem,'.');if(dot)*dot=0;
                snprintf(name,sizeof(name),"%s_%02d_k.rmt",stem,c);
                uint8_t *data=NULL;size_t size=0;KImage im={0};
                if(!read_named(&b->images,name,&data,&size)){
                    if(rmt_decode(data,size,&im)){free(data);return error(b,"mouth RMT decode failed");}free(data);
                    KImage *dst=&b->layers[7];
                    if(!dst->pixels||im.x<0||im.y<0||(uint64_t)im.x+im.width>dst->width||(uint64_t)im.y+im.height>dst->height){rmt_free(&im);return error(b,"mouth RMT outside layer 7");}
                    for(unsigned y=0;y<im.height;y++)memcpy(dst->pixels+(y+(size_t)im.y)*dst->stride+(size_t)im.x*4,im.pixels+y*im.stride,im.stride);
                    rmt_free(&im);b->mouth_sheet_ready=1;
                }
            }
        }
        /* 42ba09 clears the animation context name and resets its ID. */
        else {mam_stop(b);if(a==3){
            if(b->animation_id>=32)return error(b,"animation ID out of range");
            if(b->animation_id>=0){b->animation_status[b->animation_id]=1;b->ax.cells[b->animation_id].state=1;}
        }else b->animation_name[0]=0;
        b->animation_id=-1;}
    }else if(message_reset){
        if(integer(b,&a)||message_init(b))return -1;
    }else if(main==21&&sub==4){
        /* 436e40 returns the fade overlay visibility (native slot cc). */
        if(kvm_push(v,(KValue){(int32_t)b->fade_visible,NULL}))return error(b,"fade result stack overflow");
    }else if(main==21){
        if(integer(b,&a))return -1;
        if(sub==0&&integer(b,&c))return -1;
        int duration=sub==0?c:a;
        unsigned steps=(unsigned)(duration<0?0:duration>255?255:duration);
        if(!b->fade_surface.pixels){
            uint8_t *p=calloc(640*480,4);if(!p)return error(b,"fade surface allocation failed");
            b->fade_surface=(KImage){0,0,640,480,640*4,p};
        }
        if(sub==0){
            if(a!=-1){uint32_t color=(uint32_t)a|0xff000000u;for(size_t i=0;i<640*480;i++){uint8_t *p=b->fade_surface.pixels+i*4;p[0]=(uint8_t)color;p[1]=(uint8_t)(color>>8);p[2]=(uint8_t)(color>>16);p[3]=255;}}
            b->fade_visible=1;b->fade_from=0;b->fade_to=255;b->fade_hide=0;
        }else {b->fade_from=255;b->fade_to=0;b->fade_hide=1;}
        if(sub==0||b->fade_visible){
            b->fade_alpha=b->fade_from;b->fade_frame=0;b->fade_steps=steps;
            if(!steps){b->fade_alpha=b->fade_to;if(b->fade_hide)b->fade_visible=0;}
        }
    }else if(main==13&&sub==0){
        /* 40af40 only loads AX bytes/name; task execution is a later call. */
        const char *name;uint8_t *p=NULL;size_t n=0;
        if(string(b,&name))return -1;
        if(strlen(name)>=sizeof(b->loaded_animation)||read_named(&b->data,name,&p,&n))return error(b,"AX load failed");
        if(!ax_load(&b->ax,name,p,n)){free(p);return error(b,"invalid AX data");}
        free(b->animation_data);b->animation_data=p;b->animation_size=n;strcpy(b->loaded_animation,name);
    }else if(main==13){if(ax_script_command(b,sub))return -1;
    }else if(main==30&&sub==0){
        /* 437660 -> 4376a0 -> 426b00: fade the private canvas into view,
           commit it to page 0, stop the movie, and hide the transition UI. */
        if(integer(b,&a))return -1;
        if(a<0||a>10000||b->fade_visible||b->helper_visible||(b->video_active&&!b->video_background))return error(b,"unsupported canvas transition state");
        int speed=0;
        for(unsigned i=0;i<b->setting_count;i++)if(equal(b->settings[i].section,"Display")&&equal(b->settings[i].key,"EffectSpeed"))speed=atoi(b->settings[i].value);
        if(speed<0||speed>2)return error(b,"canvas effect speed invalid");
        if(speed==1)a>>=1;
        if(speed==2||(v->globals[0][50].number&0x4000))a=0;
        memcpy(b->auxiliary.pixels,b->message_visible?b->message_base.pixels:b->layers[0].pixels,640*480*4);
        b->transition_steps=(unsigned)a+1;b->transition_frame=0;
    }else if(main==24&&sub==1){
        const char *name;if(string(b,&name)||integer(b,&a)||a<0)return error(b,"MOV arguments invalid");
        if(movie_open(b,name,(unsigned)a))return -1;
    }else if(main==24&&sub==6){
        if(integer(b,&a)||a<0||kmov_request(&b->mov,(unsigned)a))return error(b,"MOV entry request invalid");
        b->video_change_wait=b->mov.changing;
    }else if(main==24&&(sub==4||sub==5)){
        b->video_paused=sub==4;
        if(b->video_paused)v->globals[0][50].number&=~0x2000;
        else if(b->video_active)v->globals[0][50].number|=0x2000;
    }else if(main==24&&sub==3&&b->video_background){
        b->video_wait=b->video_active;
    }else if(main==24&&sub==0){
        if(b->logo_phase)return error(b,"movie during logo sequence");
        if((b->music_active||b->voice_active)&&(b->audio_rate!=44100||b->audio_channels!=2))return error(b,"movie mixing format unsupported");
        const char *name;uint8_t *data=NULL;size_t size=0;if(string(b,&name))return -1;
        if(read_named(&b->movies,name,&data,&size))return error(b,"VSD resource missing");
        KVideo *video=kvideo_open(data,size);if(!video){free(data);return error(b,"VSD open failed");}
        movie_stop(b);b->video=video;b->video_data=data;b->video_active=1;b->video_eof=0;
        free(b->movie_pcm);b->movie_pcm=NULL;b->movie_pcm_size=b->movie_read=b->movie_clock=0;
        if(!b->music_active&&!b->voice_active){free(b->audio_pcm);b->audio_pcm=NULL;b->audio_size=b->audio_cursor=b->audio_loop_start=b->audio_loop_end=0;}
        if(b->audio_rate!=44100||b->audio_channels!=2){b->audio_rate=44100;b->audio_channels=2;b->audio_serial++;}
        v->globals[0][50].number|=0x2000;
    }else if(main==24&&(sub==2||sub==3)){
        movie_stop(b);
    }else if(main==28&&(sub==0||sub==1)){
        /* 435730 saves the checkpoint copy; 435650 loads and snapshots it. */
        if(integer(b,&a)||a<0||a>999)return error(b,"control slot out of range");
        unsigned selector=v->bytes[8100];if(selector>3)selector=3;
        if(sub==0){
            if(kcontrol_write_slot(bootstrap_save_dir(b),selector,(unsigned)a,b->saved_controls,b->saved_control_count))return error(b,"control save failed");
        }else{
            if(b->control_file_count>=128)return error(b,"control file lifetime limit");
            KControlStore *loaded=kcontrol_read_slot(bootstrap_save_dir(b),selector,(unsigned)a);
            if(!loaded)return error(b,"control save missing or malformed");
            KControlRecord current[29]={0},snapshot[29]={0};int failed=0;
            for(unsigned i=0;i<loaded->count;i++){
                current[i]=snapshot[i]=loaded->records[i];
                size_t bytes=loaded->records[i].count*sizeof(KValue);
                current[i].values=malloc(bytes?bytes:1);snapshot[i].values=malloc(bytes?bytes:1);
                if(!current[i].values||!snapshot[i].values){failed=1;break;}
                if(bytes){memcpy(current[i].values,loaded->records[i].values,bytes);memcpy(snapshot[i].values,loaded->records[i].values,bytes);}
            }
            if(failed){for(unsigned i=0;i<29;i++){free(current[i].values);free(snapshot[i].values);}kcontrol_free(loaded);return error(b,"control restore allocation failed");}
            for(unsigned i=0;i<b->control_count;i++)free(b->controls[i].values);
            for(unsigned i=0;i<b->saved_control_count;i++)free(b->saved_controls[i].values);
            memcpy(b->controls,current,sizeof(current));memcpy(b->saved_controls,snapshot,sizeof(snapshot));
            b->control_count=b->saved_control_count=loaded->count;
            /* VM and subsequent snapshots borrow these immutable strings. */
            loaded->next=b->control_files;b->control_files=loaded;b->control_file_count++;
        }
    }else if(main==28&&sub==2){
        /* 435800: destination bank1 offset, type, id; return found boolean. */
        if(integer(b,&a)||integer(b,&c)||integer(b,&d))return -1;
        if(a<0||(unsigned)a>v->global_count[1])return error(b,"control restore destination");
        unsigned i=0;for(;i<b->control_count;i++)if(b->controls[i].type==(uint16_t)c&&b->controls[i].id==(uint16_t)d)break;
        if(i<b->control_count){
            KControlRecord *r=&b->controls[i];
            if(r->count>v->global_count[1]-(unsigned)a)return error(b,"control restore range");
            if(r->count)memcpy(v->globals[1]+a,r->values,r->count*sizeof(KValue));
        }
        if(kvm_push(v,(KValue){i<b->control_count,NULL}))return error(b,"control result stack overflow");
    }else if(main==28&&sub==3){
        /* 435aa0: destination bank1 offset, type; write IDs, return count. */
        if(integer(b,&a)||integer(b,&c))return -1;
        unsigned count=0;for(unsigned i=0;i<b->control_count;i++)if(b->controls[i].type==(uint16_t)c)count++;
        if(a<0||(uint64_t)(unsigned)a+count>v->global_count[1])return error(b,"control list destination");
        unsigned at=0;for(unsigned i=0;i<b->control_count;i++)if(b->controls[i].type==(uint16_t)c)v->globals[1][a+at++]=(KValue){b->controls[i].id,NULL};
        if(kvm_push(v,(KValue){(int32_t)count,NULL}))return error(b,"control list stack overflow");
    }else if(main==28&&sub==7){
        /* 435f50 clears the live control vector, leaving checkpoint copy. */
        for(unsigned i=0;i<b->control_count;i++)free(b->controls[i].values);
        b->control_count=0;
    }else if(main==28&&sub==8){
        /* 4077c0 stores outer raw return IPs and the active checkpoint ID. */
        unsigned keep=0;for(unsigned i=0;i<b->saved_control_count;i++)if(b->saved_controls[i].type!=0xffff)keep++;
        if(keep+v->script_depth+1>29)return error(b,"saved script chain limit");
        KValue *values[29]={0};
        for(unsigned i=0;i<=v->script_depth;i++){
            int id=i<v->script_depth?v->scripts[i].module:v->module;
            /* A LIB executes in the active script context in the native VM. */
            unsigned base=v->script_depth?v->scripts[v->script_depth-1].depth:0;
            if(i==v->script_depth&&v->depth>base)id=v->frames[base].module;
            values[i]=calloc(3,sizeof(KValue));
            if(!values[i]){for(unsigned j=0;j<i;j++)free(values[j]);return error(b,"script snapshot allocation failed");}
            values[i][0]=(KValue){0,v->modules[id].name};
            values[i][1]=(KValue){i<v->script_depth?(int32_t)v->scripts[i].ip:v->globals[0][48].number,NULL};
            values[i][2]=(KValue){i==v->script_depth,NULL};
        }
        for(unsigned i=0;i<b->saved_control_count;){
            if(b->saved_controls[i].type!=0xffff){i++;continue;}
            free(b->saved_controls[i].values);b->saved_control_count--;memmove(b->saved_controls+i,b->saved_controls+i+1,(b->saved_control_count-i)*sizeof(KControlRecord));
        }
        for(unsigned i=0;i<=v->script_depth;i++)b->saved_controls[b->saved_control_count++]=(KControlRecord){(uint16_t)i,0xffff,values[i],3};
    }else if(main==28&&sub==9){
        /* 407a70 reconstructs the outer script chain; active script starts
           at its restore preamble, then sub10 selects the checkpoint. */
        int ids[29];size_t offsets[29];unsigned count=0;
        for(unsigned i=0;i<b->control_count;i++){
            KControlRecord *r=&b->controls[i];if(r->type!=0xffff)continue;
            if(r->id!=count||r->count!=3||!r->values[0].string||r->values[1].string||r->values[2].string)return error(b,"saved script record invalid");
            char target[261];
            if(krestore_name(target,r->values[0].string,r->values[2].number,v->bytes[8191],v->bytes[8190]))return error(b,"saved script name invalid");
            int id=module(b,target);if(id<0)return -1;
            if(r->values[2].number){
                if(r->values[2].number!=1||i+1!=b->control_count||r->values[1].number<0||kvm_checkpoint_offset(v,id,(unsigned)r->values[1].number,&offsets[count]))return error(b,"saved checkpoint invalid");
                offsets[count]=0;
            }else{
                size_t ip=(uint32_t)r->values[1].number;
                if(ip>v->modules[id].size||!v->modules[id].boundaries[ip])return error(b,"saved return address invalid");
                offsets[count]=ip;
            }
            ids[count++]=id;
        }
        if(!count||!b->controls[b->control_count-1].values[2].number)return error(b,"saved active script missing");
        KSavedFrames *outer=NULL;
        if(saved_frame_load(b,b->controls,b->control_count,count-1,&outer))return error(b,"saved outer frames invalid");
        if(kvm_start(v,ids[count-1])){free(outer);return -1;}
        for(unsigned i=0;i+1<count;i++){v->scripts[i].module=ids[i];v->scripts[i].ip=offsets[i];v->scripts[i].depth=outer?outer->depths[i]:0;}
        v->script_depth=count-1;
        if(outer){memcpy(v->frames,outer->frames,outer->count*sizeof(KFrame));v->depth=outer->count;free(outer);}
    }else if(main==28&&sub==10){
        /* 407db0 -> 487700 indexes the MES little-endian message table. */
        for(unsigned i=0;i<b->control_count;i++){
            KControlRecord *r=&b->controls[i];if(r->type!=0xffff)continue;
            if(r->count!=3)return error(b,"checkpoint control shape invalid");
            if(!r->values[2].number)continue;
            size_t ip;if(r->values[1].number<0||kvm_checkpoint_offset(v,v->module,(unsigned)r->values[1].number,&ip))return error(b,"checkpoint lookup failed");
            v->ip=ip;
        }
    }else if(main==28&&sub==11){
        for(unsigned i=0;i<b->saved_control_count;i++)free(b->saved_controls[i].values);
        b->saved_control_count=0;
        for(unsigned i=0;i<b->control_count;i++){
            KControlRecord copy=b->controls[i];copy.values=NULL;
            if(copy.count){copy.values=malloc(copy.count*sizeof(KValue));if(!copy.values)return error(b,"control snapshot allocation failed");memcpy(copy.values,b->controls[i].values,copy.count*sizeof(KValue));}
            b->saved_controls[b->saved_control_count++]=copy;
        }
    }else if(main==28&&sub==4){
        /* 435cc0 pops bank1 start,count,type,id, then 484fb0 upserts
           the copied values and sorts by unsigned (type<<16)|id. */
        if(integer(b,&a)||integer(b,&c)||integer(b,&d)||integer(b,&e))return -1;
        if(a<0||c<0||(uint64_t)(unsigned)a+(unsigned)c>v->global_count[1])return error(b,"control source range");
        unsigned i=0;for(;i<b->control_count;i++)if(b->controls[i].id==(uint16_t)e&&b->controls[i].type==(uint16_t)d)break;
        if(i==29)return error(b,"control record limit");
        KValue *p=c?malloc((size_t)c*sizeof(*p)):NULL;if(c&&!p)return error(b,"control allocation failed");
        if(c)memcpy(p,v->globals[1]+a,(size_t)c*sizeof(*p));
        if(i<b->control_count)free(b->controls[i].values);else b->control_count++;
        b->controls[i]=(KControlRecord){(uint16_t)e,(uint16_t)d,p,(unsigned)c};
        for(unsigned j=i;j>0;j--){
            KControlRecord *x=&b->controls[j],*y=x-1;
            if((((uint32_t)x->type<<16)|x->id)>=(((uint32_t)y->type<<16)|y->id))break;
            KControlRecord tmp=*x;*x=*y;*y=tmp;
        }
    }else if(main==28&&(sub==5||sub==6)){
        /* 485160 erases every record whose high key word matches; no wait. */
        if(integer(b,&a)||(sub==5&&integer(b,&c)))return -1;
        for(unsigned i=0;i<b->control_count;){
            if(b->controls[i].type!=(uint16_t)a||(sub==5&&b->controls[i].id!=(uint16_t)c)){i++;continue;}
            free(b->controls[i].values);b->control_count--;
            memmove(b->controls+i,b->controls+i+1,(b->control_count-i)*sizeof(*b->controls));
        }
    }else if(main==23&&sub==0){
        /* 414950 appends, rather than resizing/replacing, message records. */
        if(integer(b,&a))return -1;
        if(a<0||(unsigned)a>4096-b->message_count)return error(b,"message record limit");
        if(a){
            KMessageRecord *p=realloc(b->messages,(b->message_count+(unsigned)a)*sizeof(*p));
            if(!p)return error(b,"message allocation failed");
            b->messages=p;memset(p+b->message_count,0,(size_t)a*sizeof(*p));b->message_count+=(unsigned)a;
        }
    }else if(main==23&&sub==1){
        /* 414a70 clears vector/string lengths, retains allocations and flag. */
        b->message_index=-1;
        for(unsigned i=0;i<b->message_count;i++){b->messages[i].size=0;if(b->messages[i].text)b->messages[i].text[0]=0;}
    }else if(main==27&&sub==1){
        /* 437de0 -> 4217b0: mouse/keyboard state query, used by clip loops. */
        if(integer(b,&a))return -1;
        unsigned bit=(a==256||a==28)?1:(a==257||a==1)?2:0;
        int pressed=(b->input_events&bit)!=0;b->input_events&=~bit;
        if(kvm_push(v,(KValue){pressed,NULL}))return error(b,"input query overflow");
        b->wait_clock=1;
    }else if(main==27&&sub==3){
        /* Kisaku 4f3470 -> 4f31c0 -> 4041f0: ShowWindow(SW_SHOW).
           SDL owns window visibility; no script argument or return value. */
    }else if(main==27&&sub==2){
        /* 437f20 invokes Win32 DrawMenuBar; the Switch frontend has no
           native window menu. No VM values or game UI state are changed. */
    }else if(main==27){
        /* 437ba0: positive milliseconds, otherwise wait for confirmation. */
        if(integer(b,&a))return -1;
        b->wait_input=a<=0;b->wait_clock=a>0?(uint64_t)a*60:0;
    }else if(main==26){
        const char *section,*key;KValue value;
        if(string(b,&section)||string(b,&key)||kvm_pop(v,&value))return error(b,"setting arguments required");
        if(sub==0){
            unsigned i=0;for(;i<b->setting_count;i++)if(equal(section,b->settings[i].section)&&equal(key,b->settings[i].key))break;
            if(i<b->setting_count){
                if(value.string){
                    const char *copy=owned_string(b,b->settings[i].value);if(!copy)return -1;
                    value=(KValue){0,copy};
                }
                else {char *end;long long n=strtoll(b->settings[i].value,&end,0);value=(KValue){(int32_t)(uint32_t)(end==b->settings[i].value?0:n),NULL};}
            }
            if(kvm_push(v,value))return error(b,"settings result overflow");
        }else {
            char number[32];snprintf(number,sizeof(number),"%d",value.number);
            if(setting_put(b,section,key,value.string?value.string:number)||settings_save(b))return -1;
        }
    }else if(main==14&&sub==2){
        /* Kisaku 4f8d00 -> 4f7720 -> 5036c0: full FLAG write. */
        if(integer(b,&a)||a<0||a>999)return error(b,"FLAG save slot range");
        unsigned base;if(saved_frame_depth(v,&base))return error(b,"FLAG script context invalid");
        int active=v->depth>base?v->frames[base].module:v->module;
        if(active<0||(unsigned)active>=v->module_count)return error(b,"FLAG module missing");
        KFlags f={0};snprintf((char *)f.module,sizeof(f.module),"%.259s",v->modules[active].name);
        for(unsigned i=0;i<2;i++){f.globals[i]=v->globals[i];f.counts[i]=v->global_count[i];}
        f.bytes=v->bytes;f.byte_count=v->byte_count;f.words=v->words;f.word_count=v->word_count;f.raw=b->raw_variables;f.raw_count=(unsigned)b->raw_size;
        if(kflags_write_slot(&f,bootstrap_save_dir(b),0,(unsigned)a))return error(b,"FLAG save failed");
    }else if(main==14&&sub==13){
        /* Kisaku 4f7610 clears bytes, words, raw and bank1; preserve bank0. */
        memset(v->bytes,0,v->byte_count);memset(v->words,0,v->word_count*sizeof(uint16_t));
        if(b->raw_size)memset(b->raw_variables,0,b->raw_size);
        memset(v->globals[1],0,v->global_count[1]*sizeof(KValue));
    }else if(main==14){
        if(integer(b,&a)||integer(b,&c)||integer(b,&d)||integer(b,&e))return -1;
        if(a<0||(unsigned)a>sizeof(v->bytes)||c<0||c>8192||d<0||d>16777216||e<0||e>8192)return error(b,"variable capacity exceeds supported bounds");
        /* Native resize preserves prefix and zeros newly grown records. */
        if((unsigned)a>v->byte_count)memset(v->bytes+v->byte_count,0,(unsigned)a-v->byte_count);
        if((unsigned)c>v->word_count)memset(v->words+v->word_count,0,((unsigned)c-v->word_count)*sizeof(uint16_t));
        if((unsigned)e>v->global_count[1])memset(v->globals[1]+v->global_count[1],0,((unsigned)e-v->global_count[1])*sizeof(KValue));
        if(resize_bytes(&b->raw_variables,&b->raw_size,(size_t)d))return error(b,"variable allocation failed");
        v->raw=b->raw_variables;v->raw_size=b->raw_size;
        v->byte_count=(unsigned)a;v->word_count=(unsigned)c;v->global_count[1]=(unsigned)e;
    }else if(main==16&&sub==1){
        /* 4331f0/4332c0: immediate named playback using channel Effect gain. */
        const char *name;if(string(b,&name)||integer(b,&a))return -1;
        if(a<0||effect_play(b,(unsigned)a,name))return -1;
    }else if(main==16&&sub==6){
        if(integer(b,&a)||a<0||(unsigned)a>=b->audio_counts[1])return error(b,"effect playback channel range");
        if(b->audio_objects[1][a].state){char name[261];strcpy(name,b->audio_objects[1][a].name);if(effect_play(b,(unsigned)a,name))return -1;}
    }else if(main==16&&sub==7){
        /* 433750 -> 433840 -> 4939f0: playback status, excluding registration. */
        if(integer(b,&a)||a<0||(unsigned)a>=b->audio_counts[1]||a>=64)return error(b,"effect query channel range");
        KEffectTrack *t=&b->effect_tracks[a];int active=t->pcm&&t->size&&(t->loop_end||t->clock_position<t->size);
        if(kvm_push(v,(KValue){active,NULL}))return error(b,"effect query overflow");
    }else if(main==17&&sub==7){
        if(integer(b,&a)||a!=0||!b->audio_counts[2])return error(b,"voice query channel range");
        if(kvm_push(v,(KValue){b->voice_loading||b->voice_active,NULL}))return error(b,"voice query overflow");
    }else if(main==17&&sub==6){
        /* 433710 -> 432fb0 -> 493cb0: play the pending voice, then clear
           its pending flag. Mono Vorbis is converted to the stereo output bus. */
        if(integer(b,&a))return -1;
        if(a!=0||b->audio_counts[2]!=1)return error(b,"voice channel range");
        if(voice_play(b))return -1;
    }else if(main==15&&sub==1){
        /* CFuncMusic virtual_16 (4b6ac0) consumes a name and a channel,
           then calls the music object's start method. The opening script
           uses (0,"bgm13.wav"); its lower temporary value remains on the
           VM stack and is intentionally not consumed here. */
        const char *name;
        if(string(b,&name)||integer(b,&a))return -1;
        if(a!=0||b->audio_counts[0]!=1)return error(b,"music channel range");
        if(b->video_active||b->logo_phase)return error(b,"concurrent music/movie not implemented");
        if(play_pcm(b,&b->music,name))return -1;
        int enabled=1,volume_db=music_volume_db(b,&enabled);
        double gain=enabled?db_gain(volume_db):0;
        for(size_t i=0;i+1<b->audio_size;i+=2){
            int16_t sample=(int16_t)le16(b->audio_pcm+i);
            int16_t out=(int16_t)(sample*gain);
            b->audio_pcm[i]=(uint8_t)out;
            b->audio_pcm[i+1]=(uint8_t)((uint16_t)out>>8);
        }
        b->music_active=1;b->music_enabled=enabled;
        b->music_db=volume_db;
        b->fade_db=b->music_db;b->music_gain=1;b->music_fading=0;
    }else if(main==15&&sub==6){
        /* 433710 applies Music volume then opens/starts the named WAV. */
        if(integer(b,&a))return -1;
        if(a!=0||b->audio_counts[0]!=1)return error(b,"music playback slot range");
        if(b->audio_objects[0][0].state){
        if((b->video_active&&!b->video_background)||b->logo_phase)return error(b,"concurrent music/movie not implemented");
        if(b->voice_active&&!b->voice_pcm){
            b->voice_pcm=b->audio_pcm;b->voice_size=b->audio_size;b->voice_clock_cursor=b->voice_read_cursor=b->audio_cursor;b->voice_clock=0;b->audio_pcm=NULL;
        }
        if(play_pcm(b,&b->music,b->audio_objects[0][0].name))return -1;
        int enabled=1,volume_db=music_volume_db(b,&enabled);
        double gain=enabled?db_gain(volume_db):0;
        for(size_t i=0;i<b->audio_size;i+=2){int16_t sample=(int16_t)le16(b->audio_pcm+i);int16_t out=(int16_t)(sample*gain);b->audio_pcm[i]=(uint8_t)out;b->audio_pcm[i+1]=(uint8_t)((uint16_t)out>>8);}
        b->music_active=1;b->music_enabled=enabled;b->music_db=volume_db;b->fade_db=b->music_db;b->music_gain=1;b->music_fading=0;b->audio_objects[0][0].state=0;
        }
    }else if(((main==15||main==16)&&sub==5)||voice_register||(main==17&&sub==1)){
        /* 4335d0 -> 4336a0 -> 432fa0 -> 432ff0: retain name and set
           pending-load state 1. Actual I/O/playback happens in sub6. */
        const char *name;if(string(b,&name)||integer(b,&a))return -1;
        unsigned bank=(unsigned)(main-15);
        if(a<0||(unsigned)a>=b->audio_counts[bank]||!name[0]||strlen(name)>=261)return error(b,"audio slot/name range");
        int enabled=1;
        if(main==17){
            if(sub==5&&(v->globals[0][50].number&0x280)==0x280){if(b->message_index<0||(unsigned)b->message_index>=b->message_count)return error(b,"voice record slot missing");b->messages[b->message_index].flag=1;}
            /* 4904c0: A/E/F/G/I/K/N female, all other prefixes male. */
            const char *key=strchr("AEFGIKN",toupper((unsigned char)name[0]))?"IsWomanVoice":"IsManVoice";
            for(unsigned i=0;i<b->setting_count;i++)if(equal(b->settings[i].section,"Voice")&&equal(b->settings[i].key,key))enabled=atoi(b->settings[i].value)!=0;
        }
        if(main==17)snprintf(b->message_voice_name,sizeof(b->message_voice_name),"%s",enabled?name:"");
        if(enabled){strcpy(b->audio_objects[bank][a].name,name);b->audio_objects[bank][a].state=1;if(main==17&&sub==1&&voice_play(b))return -1;}
        else {b->audio_objects[bank][a].state=0;b->audio_objects[bank][a].name[0]=0;}
    }else if((main>=15&&main<=17)&&(sub==2||sub==3)){
        /* 433380 stops a channel; 4333f0 requests music fade-stop. */
        if(integer(b,&a))return -1;
        if(sub==3){d=a;if(integer(b,&c))return -1;a=c;}
        unsigned bank=(unsigned)(main-15);
        if(a<0||(unsigned)a>=b->audio_counts[bank])return error(b,"audio channel range");
        memset(&b->audio_objects[bank][a],0,sizeof(KAudioSlot));
        if(main==16){
            KEffectTrack *track=&b->effect_tracks[a];
            if(sub==3&&track->pcm){if(d<=0)return error(b,"invalid effect fade duration");track->fade_step=(track->fade_limit-track->fade_attenuation)/d;if(track->fade_step<=0)track->fade_step=1;track->fade_clock=0;}
            else {free(track->pcm);memset(track,0,sizeof(*track));}
        }
        if(main==17){
            b->message_voice_name[0]=0;mam_stop(b);kvoice_worker_cancel(b->voice_worker);b->voice_loading=0;
            b->voice_active=0;
            if(b->voice_pcm){free(b->voice_pcm);b->voice_pcm=NULL;b->voice_size=b->voice_read_cursor=b->voice_clock_cursor=0;}
            else if(!b->music_active){b->audio_size=b->audio_cursor=0;b->audio_serial++;}
        }
        if(main==15&&b->music_active){
            if(sub==3&&b->music_enabled&&b->fade_db>-5000){
                if(d<=0)return error(b,"invalid music fade duration");
                /* 4047c0/4051c0 use truncated dB steps, updated by the
                   streaming backend's 15 ms timer (405811). */
                b->fade_db_step=(b->fade_db+5000)/d;
                /* A repeated fade near silence can truncate to zero; retain
                   progress instead of turning this valid request into a stall. */
                if(!b->fade_db_step)b->fade_db_step=1;
                b->music_fading=1;b->music_fade_clock=0;
            }else {b->music_active=b->music_fading=0;b->audio_size=b->audio_cursor=b->audio_loop_start=b->audio_loop_end=0;b->audio_serial++;}
        }
    }else if(main>=15&&main<=17){
        if(integer(b,&a))return -1;
        if(a<0||a>64)return error(b,"invalid audio object count");
        KAudioSlot *p=calloc(a?a:1,sizeof(KAudioSlot));if(!p)return error(b,"audio state allocation failed");
        free(b->audio_objects[main-15]);b->audio_objects[main-15]=p;b->audio_counts[main-15]=(unsigned)a;
    }else if(main==11){
        /* 437f40 / 4385f0: unsigned decimal, zero padded by bank0[37],
           bank0[36] == 1 selects full-width CP932 digits. */
        if(integer(b,&a))return -1;
        int width=v->globals[0][37].number;
        if(width<0||width>128)return error(b,"numeric text width invalid");
        char digits[129];snprintf(digits,sizeof(digits),"%0*u",width,(unsigned)(uint32_t)a);
        size_t at=0;
        for(const char *p=digits;*p;p++){
            if(v->globals[0][36].number==1){b->number_text[at++]=(char)0x82;b->number_text[at++]=(char)(0x4f+*p-'0');}
            else b->number_text[at++]=*p;
        }
        b->number_text[at]=0;v->text=b->number_text;v->text_size=at;v->status=KVM_TEXT;
        if(draw_text(b))return -1;
    }else if(main==10){
        if(integer(b,&a)||integer(b,&c))return -1;
        if(a<1||c<1||a>256||c>256)return error(b,"invalid font dimensions");
        b->font_width=a;b->font_height=c;
    }else if(main==18){
        if(integer(b,&a))return -1;
        if(a<0||(unsigned)a>1024-b->record_count)return error(b,"record count limit");
        void *p=realloc(b->records,(b->record_count+(unsigned)a+1)*16);if(!p)return error(b,"record allocation failed");
        b->records=p;memset((uint8_t *)p+b->record_count*16,0,(size_t)a*16);b->record_count+=(unsigned)a;
    }else if(main==19&&sub==7){
        /* CFuncLayer action 7 (0x4f6470 -> 0x4f5da0 -> CDIB+0x78)
           halves RGB in a rectangle and preserves the destination alpha.
           The VM presents these five operands in reverse order, so q is
           [x,y,width,height,layer], matching the other layer operations. */
        int32_t q[5];for(unsigned i=0;i<5;i++)if(integer(b,q+i))return -1;
        KImage *dst=surface(b,q[4]);
        if(!dst||!dst->pixels)return error(b,"invalid darken surface");
        if(q[2]>0&&q[3]>0){
            if(q[0]<0||q[1]<0||(uint64_t)q[0]+(uint32_t)q[2]>dst->width||
               (uint64_t)q[1]+(uint32_t)q[3]>dst->height)
                return error(b,"darken rectangle outside surface");
            for(int32_t y=0;y<q[3];y++)for(int32_t x=0;x<q[2];x++){
                uint8_t *p=dst->pixels+(size_t)(q[1]+y)*dst->stride+(size_t)(q[0]+x)*4;
                p[0]=(uint8_t)((p[0]&0xfeu)>>1);p[1]=(uint8_t)((p[1]&0xfeu)>>1);p[2]=(uint8_t)((p[2]&0xfeu)>>1);
            }
        }
    }else if(main==19&&sub==6){
        /* 4f6590 -> 4f5e00 -> CDIB+0x9c/48d240:
           x,y,w,h,layer,color,alpha-mode. */
        int32_t q[7];for(unsigned i=0;i<7;i++)if(integer(b,q+i))return -1;
        KImage *dst=surface(b,q[4]);if(!dst||!dst->pixels)return error(b,"invalid fill surface");
        if(q[2]<=0||q[3]<=0)goto layer_fill_done;
        int64_t left=q[0],top=q[1],right=left+q[2],bottom=top+q[3];
        if(left<0)left=0;
        if(top<0)top=0;
        if(right>dst->width)right=dst->width;
        if(bottom>dst->height)bottom=dst->height;
        uint32_t color=(uint32_t)q[5];
        if(left<right&&top<bottom)for(int64_t y=top;y<bottom;y++)for(int64_t x=left;x<right;x++){
            uint8_t *p=dst->pixels+(size_t)y*dst->stride+(size_t)x*4;
            if(!q[6]&&!color){p[0]=p[1]=p[2]=p[3]=0;continue;}
            p[0]=(uint8_t)color;p[1]=(uint8_t)(color>>8);p[2]=(uint8_t)(color>>16);if(q[6])p[3]=(uint8_t)(color>>24);
        }
layer_fill_done:;
    }else if(main==19&&sub==5){
        /* 4f6720 -> 4f5e70 -> CDIB+0x4c: eight operands, per-pixel
           source Alpha, preserve destination Alpha. */
        int32_t q[9]={0};for(unsigned i=0;i<8;i++)if(integer(b,q+i))return -1;
        if(blit_args(b,2,q))return -1;
    }else if(main==19&&sub==4){
        if(blit_color_key(b))return -1;
    }else if(main==19&&sub==3){
        if(blit(b,0))return -1;
    }else if(main==19&&sub==8){
        if(blit_global_alpha(b))return -1;
    }else if(main==19&&sub==9){
        if(layer_alpha(b))return -1;
    }else if(main==19&&sub==0){
        if(integer(b,&a))return -1;
        if(a<0||(unsigned)a>63-b->layer_count)return error(b,"layer count limit");
        b->layer_count+=(unsigned)a; /* Slot zero reserved for native display surface. */
    }else if(main==19&&sub==2){
        if(integer(b,&a)||integer(b,&c)||integer(b,&d))return -1;
        if(d<1||(unsigned)d>b->layer_count||a<1||c<1||(uint64_t)a*c>16777216)return error(b,"invalid layer/dimensions");
        uint8_t *p=calloc((size_t)a*c,4);if(!p)return error(b,"surface allocation failed");
        rmt_free(&b->layers[d]);b->layers[d]=(KImage){0,0,(unsigned)a,(unsigned)c,(size_t)a*4,p};
    }else if((main==19&&sub==1)||offset_image){
        /* 42d69f: RMT load with additional x/y offsets; -1 keeps the
           corresponding coordinate from the resource header. */
        if(offset_image&&integer(b,&a))return -1;
        const char *name;if(string(b,&name)||integer(b,&a))return -1;
        int32_t ox=0,oy=0;if(offset_image){if(integer(b,&ox)||integer(b,&oy))return -1;if(ox==-1)ox=0;if(oy==-1)oy=0;}
        if(ox < -16384||ox > 16384||oy < -16384||oy > 16384)return error(b,"RMT offset range");
        if(a<1||(unsigned)a>b->layer_count||!b->layers[a].pixels)return error(b,"unallocated layer");
        if(b->image_worker){
            if(kimage_worker_submit(b->image_worker,name))return error(b,"image worker submission failed");
            b->image_layer=a;b->image_offset_x=ox;b->image_offset_y=oy;b->image_loading=1;snprintf(b->image_name,sizeof(b->image_name),"%s",name);
        }else{
            uint8_t *data=NULL;size_t size=0;KImage im={0};
            if(read_named(&b->images,name,&data,&size)||rmt_decode(data,size,&im)){free(data);return error(b,"RMT load failed");}free(data);
            im.x+=ox;im.y+=oy;int rc=install_image(b,a,&im,name);rmt_free(&im);if(rc)return -1;
        }
    }else if(main==25&&sub==1){
        if(b->read_flags&&b->read_size){memset(b->read_flags,0,b->read_size);b->read_dirty=1;}
    }else if(main==25&&sub==3){
        if(bootstrap_flush_progress(b))return -1;
    }else if(main==25&&sub==0){
        if(bootstrap_flush_progress(b))return -1;
        if(integer(b,&a))return -1;
        if(a<0||a>134217728)return error(b,"read flags capacity limit");
        if(resize_bytes(&b->read_flags,&b->read_size,((size_t)a+7)/8))return error(b,"read flags allocation failed");
    }else if(main==25&&sub==2){
        /* Original onemes.dat remains read-only; each selector owns a port file. */
        if(!b->read_size||v->byte_count<=8100)return error(b,"read flag buffer/selector uninitialized");
        if(bootstrap_flush_progress(b))return -1;
        unsigned sel=v->bytes[8100];if(sel>3)sel=3;
        int loaded=kread_flags_load(bootstrap_save_dir(b),sel,b->read_flags,b->read_size);
        if(loaded<0)return error(b,"read history load failed");
        if(loaded)b->missing_read_flags++;
        b->read_selector=sel;b->read_loaded=1;b->read_dirty=0;
    }else if(main==22){
        const char *name;if(string(b,&name))return -1;int m=module(b,name);if(m<0)return -1;
        int previous_module=v->module;
        if(sub==0){if(kvm_switch(v,m))return error(b,"module switch failed");}
        else if(sub==1){if(kvm_call_module(v,m))return error(b,"module call failed");}
        else {
            int old=v->module;size_t ip=v->ip;unsigned sp=v->sp,depth=v->depth,script_depth=v->script_depth;
            /* Registration-only LIB: verified real file contains LIBREG records, no host calls. */
            v->module=m;v->ip=0;v->script_depth=0;v->status=KVM_READY;KStatus s=kvm_run(v,100000);
            if((s!=KVM_DONE&&s!=KVM_YIELD)||v->sp!=sp||v->depth!=depth)return error(b,"library initialization did not finish cleanly");
            v->module=old;v->ip=ip;v->script_depth=script_depth;v->status=KVM_SYSCALL;
        }
        if(sub<2&&scene_transition(b,v->modules[previous_module].name,name))return -1;
    }
    b->handled++;if(v->status==KVM_SYSCALL)kvm_resume(v);return 0;
}
static int draw_text(KBootstrap *b){
    KVM *v=b->vm;const char *path=NULL;
    for(unsigned i=0;i<b->setting_count;i++)if(equal(b->settings[i].section,"Runtime")){
        if(equal(b->settings[i].key,"TextEncoding")&&!equal(b->settings[i].value,"CP932"))
            return error(b,"unsupported text encoding");
        if(equal(b->settings[i].key,"FontFile"))path=b->settings[i].value;
    }
    KTextEncoding encoding=text_encoding(b);
    /* 41dc80 records bytes only when BOTH system flags 0x80 and 0x100
       are set. 437390 uses 4886e0 for CP932 punctuation layout. */
    /* 41cc16..41cc33: measurement counts encoded bytes, skips drawing and
       cursor writes. 41dc80 still records the input, including its NUL. */
    if(v->globals[0][50].number&0x80000000u){
        if((v->globals[0][50].number&0x180)==0x180&&record_append(b,(const uint8_t *)v->text,v->text_size+1))return -1;
        b->text_measure_bytes+=(uint32_t)v->text_size;return kvm_resume(v);
    }
    KImage *dst=surface(b,v->globals[0][49].number);
    if(!dst||!dst->pixels||v->text_size>4096)return error(b,"text surface or length invalid");
    KTextChar chars[4096];size_t count=0;
    if(decode_text(b,(const uint8_t *)v->text,v->text_size,chars,4096,&count,&encoding))return error(b,"invalid encoded text");
    int x=v->globals[0][46].number,y=v->globals[0][47].number;
    int advance=v->globals[0][30].number/2,line=v->globals[0][31].number;
    int left=v->globals[0][42].number,top=v->globals[0][43].number,right=v->globals[0][44].number,bottom=v->globals[0][45].number;
    if(advance<1||advance>128||line<1||line>256||x<left||(int64_t)x>right+2*advance||y<top||left<0||top<0||right>(int)dst->width||bottom>(int)dst->height)return error(b,"text rectangle invalid");
    KTextPosition positions[4096];
    int layout=ktext_layout_native(chars,count,left,top,right,bottom,advance,line,&x,&y,positions);
    if(layout)return error(b,"text layout or inline control invalid");
    if(!text_font(b,path,NULL,encoding))return error(b,"cannot load font; set Runtime FontFile to a supported font");
    KFont *active_font=b->font;
    if(b->novel_mode){
        if(!b->novel_font){
            const char *mincho=NULL;
            for(unsigned i=0;i<b->setting_count;i++)if(equal(b->settings[i].section,"Runtime")&&equal(b->settings[i].key,"MinchoFontFile"))mincho=b->settings[i].value;
#ifdef __APPLE__
            if(!mincho)mincho="/System/Library/Fonts/ヒラギノ明朝 ProN.ttc";
#endif
            if(mincho&&*mincho){b->novel_font=kfont_open(mincho,encoding==KTEXT_GBK);if(!b->novel_font)return error(b,"cannot load MinchoFontFile");}
        }
        if(b->novel_font)active_font=b->novel_font;
    }
    for(size_t i=0;i<count;i++){
        if(kfont_draw(active_font,dst,chars[i].codepoint,positions[i].x,positions[i].y,(unsigned)b->font_width,(unsigned)b->font_height,(uint32_t)v->globals[0][33].number))return error(b,"glyph unavailable");
    }
    if((v->globals[0][50].number&0x180)==0x180&&record_append(b,(const uint8_t *)v->text,v->text_size+1))return -1;
    /* A displayed line may contain several TEXT opcodes, a substituted name,
       or a numeric syscall. Retain the whole message for portable history. */
    if(b->message_initialized&&v->globals[0][49].number==1&&(top==v->globals[0][43].number||b->novel_mode)){
        if(b->novel_mode&&!b->message_pending_size){b->novel_start_x=v->globals[0][46].number;b->novel_start_y=v->globals[0][47].number;}
        if(v->text_size>4096-b->message_pending_size)return error(b,"message history length limit");
        memcpy(b->message_pending+b->message_pending_size,v->text,v->text_size);
        b->message_pending_size+=v->text_size;b->message_pending[b->message_pending_size]=0;
    }
    v->globals[0][46]=(KValue){x,NULL};v->globals[0][47]=(KValue){y,NULL};b->text_count++;
    return kvm_resume(v);
}
/* A novel checkpoint can begin a page containing several prompts. Rebuild
   preceding paragraphs through the real script before exposing the saved one. */
static int restore_message(KBootstrap *b){
    if(!b->restore_pending)return 0;
    if(b->choice_active)return error(b,"novel restore encountered an unexpected choice");
    if(!b->message_active)return 0;
    if(!b->novel_mode)return error(b,"novel restore reached a different message mode");
    if(b->message_read_id==b->restore_read_id){b->restore_pending=0;return 0;}
    if(!b->restore_remaining)return error(b,"novel restore target not reached");
    b->restore_remaining--;
    if(b->novel_transition){b->novel_step=255;novel_fade_frame(b);}
    bootstrap_confirm(b);return b->error[0]?-1:0;
}
int bootstrap_run(KBootstrap *b,unsigned budget){
    if(!b||b->error[0])return -1;
    b->vm->raw=b->raw_variables;b->vm->raw_size=b->raw_size;
    if(restore_message(b))return -1;
    history_restore_apply(b);
    if(b->scene_replay_finished)return 1;
    if(b->letter_transition||b->load_modal||b->scene_modal||ax_modal_wait(b)||b->montage_active||b->credits_active||b->area_active||b->bonus52_active||b->extra_active||b->image_loading||b->scroll_active||b->blink_active||b->distort_count||b->novel_transition||b->choice_active||b->message_active||b->message_slide||b->flag_dialog.active||b->title.active||b->transition_steps||b->exec_wipe_active||b->helper_steps||b->fade_steps||b->logo_phase||b->wait_clock||b->wait_input||b->video_wait||b->video_change_wait||(b->video_active&&!b->video_background))return 1;
    while(budget--){b->vm->raw=b->raw_variables;b->vm->raw_size=b->raw_size;int old_module=b->vm->module;unsigned old_scripts=b->vm->script_depth;KStatus s=kvm_run(b->vm,1);
        /* Native 408060 notifies navigation on a script return, but library
           function calls only change the VM instruction source. */
        if(b->vm->script_depth<old_scripts&&scene_transition(b,b->vm->modules[old_module].name,b->vm->modules[b->vm->module].name))return -1;
        if(s==KVM_SYSCALL){if(bootstrap_dispatch(b))return -1;b->vm->raw=b->raw_variables;b->vm->raw_size=b->raw_size;if(restore_message(b))return -1;history_restore_apply(b);if(b->scene_replay_finished)return 1;if(b->letter_transition||b->load_modal||b->scene_modal||ax_modal_wait(b)||b->montage_active||b->credits_active||b->area_active||b->bonus52_active||b->extra_active||b->image_loading||b->scroll_active||b->blink_active||b->distort_count||b->novel_transition||b->choice_active||b->message_active||b->message_slide||b->flag_dialog.active||b->title.active||b->transition_steps||b->exec_wipe_active||b->helper_steps||b->fade_steps||b->logo_phase||b->wait_clock||b->wait_input||b->video_wait||b->video_change_wait||(b->video_active&&!b->video_background))return 1;}
        else if(s==KVM_TEXT){if(draw_text(b))return -1;}
        else if(s==KVM_BUDGET)kvm_resume(b->vm);
        else if(s==KVM_ERROR)return error(b,b->vm->error);
        else if(s==KVM_DONE||s==KVM_YIELD)return 0;
    }return error(b,"instruction budget exhausted");
}

void bootstrap_frame(KBootstrap *b){
    b->frames++;if(b->frames>b->input_event_until)b->input_events=0;
    animation522_begin_frame(b);
    if(b->image_loading){
        KImage im={0};int ready=kimage_worker_poll(b->image_worker,&im);
        if(ready){
            b->image_loading=0;
            if(ready<0){rmt_free(&im);error(b,"background RMT load failed");return;}
            im.x+=b->image_offset_x;im.y+=b->image_offset_y;int rc=install_image(b,b->image_layer,&im,b->image_name);rmt_free(&im);if(rc)return;
        }
    }
    if(b->letter_transition)letter_frame(b);
    if(b->novel_transition)novel_frame(b);
    if(b->voice_loading){
        uint8_t *pcm=NULL;size_t size=0;int ready=kvoice_worker_poll(b->voice_worker,&pcm,&size);
        if(ready){b->voice_loading=0;if(ready<0){free(pcm);error(b,"background voice decode failed");return;}
            free(b->voice_pcm);b->voice_pcm=pcm;b->voice_size=size;b->voice_read_cursor=b->voice_clock_cursor=0;b->voice_clock=0;b->voice_active=1;
        }
    }
    if(b->wait_clock)b->wait_clock=b->wait_clock>1000?b->wait_clock-1000:0;
    /* 60 Hz host clock, 20 ms AX ticks. Headless diagnostics advance this
       virtual clock; SDL queues the same PCM to the actual device. */
    if(b->audio_cursor<b->audio_size){
        b->audio_clock+=b->audio_rate;
        size_t samples=b->audio_clock/60;b->audio_clock%=60;
        size_t bytes=samples*b->audio_channels*2;
        if(b->audio_loop_end){
            b->audio_cursor+=bytes;
            if(b->audio_cursor>=b->audio_loop_end)b->audio_cursor=b->audio_loop_start+(b->audio_cursor-b->audio_loop_end)%(b->audio_loop_end-b->audio_loop_start);
        }else b->audio_cursor+=bytes<b->audio_size-b->audio_cursor?bytes:b->audio_size-b->audio_cursor;
    }
    if(b->voice_pcm&&b->voice_clock_cursor<b->voice_size){
        b->voice_clock+=44100;size_t advance=(b->voice_clock/60)*4;b->voice_clock%=60;
        b->voice_clock_cursor+=advance<b->voice_size-b->voice_clock_cursor?advance:b->voice_size-b->voice_clock_cursor;
        if(b->voice_clock_cursor==b->voice_size)b->voice_active=0;
    }else if(!b->voice_loading&&!b->voice_pcm&&b->voice_active&&b->audio_cursor==b->audio_size)b->voice_active=0;
    for(unsigned i=0;i<65;i++){
        if(i==64&&b->video_paused)continue;
        KEffectTrack *t=i==64?&b->movie_effect:&b->effect_tracks[i];if(!t->pcm)continue;
        if(t->fade_step){t->fade_clock+=1000;while(t->fade_clock>=900){t->fade_clock-=900;t->fade_attenuation+=t->fade_step;}
            if(t->fade_attenuation>=t->fade_limit){free(t->pcm);memset(t,0,sizeof(*t));continue;}}
        if(t->clock_position>=t->size)continue;
        t->clock_position+=2940;
        if(t->loop_end&&t->clock_position>=t->loop_end)t->clock_position=t->loop_start+(t->clock_position-t->loop_end)%(t->loop_end-t->loop_start);
        else if(t->clock_position>t->size)t->clock_position=t->size;
    }
    if(b->music_fading){
        b->music_fade_clock+=1000;
        while(b->music_fade_clock>=900&&b->music_fading){
            b->music_fade_clock-=900;b->fade_db-=b->fade_db_step;
            if(b->fade_db<=-5000){b->fade_db=-5000;b->music_fading=b->music_active=0;b->audio_size=b->audio_cursor=b->audio_loop_start=b->audio_loop_end=0;b->audio_serial++;}
        }
        b->music_gain=pow(10.0,(b->fade_db-b->music_db)/2000.0);
    }
    if(b->video_active&&!b->video_paused){
        if(b->movie_clock<b->movie_pcm_size){size_t n=b->movie_pcm_size-b->movie_clock;b->movie_clock+=n>2940?2940:n;}
        KImage *target=b->choice_active?&b->choice_base:b->message_visible?&b->message_base:&b->layers[0];
        if(!b->video_eof){int rc=kvideo_step(b->video,target,&b->movie_pcm,&b->movie_pcm_size);if(rc<0){error(b,kvideo_error(b->video));return;}b->video_eof=rc==1;}
        if(b->video_eof&&b->movie_clock==b->movie_pcm_size){
            if(b->video_background){
                if(movie_next(b))return;
                /* EOF consumes no display tick: show the next range's first
                   frame now, without holding the old last frame for 1/60 s. */
                if(b->video_active){int rc=kvideo_step(b->video,target,&b->movie_pcm,&b->movie_pcm_size);if(rc<0){error(b,kvideo_error(b->video));return;}b->video_eof=rc==1;}
            }else{b->video_active=0;b->vm->globals[0][50].number&=~0x2000;}
        }
    }
    if(b->logo_phase==1&&b->audio_cursor==b->audio_size){
        if(play_pcm(b,&b->effects,"logo01.wav")||start_logo_track(b,0))return;
        b->logo_phase=2;
    }else if(b->logo_phase>=2){
        b->ax_clock+=1000;
        while(b->ax_clock>=60*AX_TICK_MS){
            b->ax_clock-=60*AX_TICK_MS;
            if(!ax_tick(&b->ax,draw_ax,b)){error(b,"invalid AX instruction");return;}
        }
        if(!ax_waiting(&b->ax)){
            if(b->logo_phase==2){if(start_logo_track(b,1))return;b->logo_phase=3;}
            else b->logo_phase=0;
        }
    }
    if(!b->logo_phase&&b->ax.size){
        b->ax_clock+=1000;
        while(b->ax_clock>=60*AX_TICK_MS){b->ax_clock-=60*AX_TICK_MS;if(!ax_tick(&b->ax,draw_ax,b)){error(b,"invalid story AX instruction");return;}if(b->error[0])return;}
        for(unsigned i=0;i<AX_CELLS;i++)b->animation_status[i]=(uint8_t)b->ax.cells[i].state;
    }
    /* CAnimeManagerEX::virtual_4 (4dd5a0) ticks its independent 320-track
       manager on the same 20 ms cadence.  Keep it separate from the normal
       story AX clock: stopping/reloading one manager must not phase-shift the
       other. */
    if(b->ax_extra.size){
        b->ax_extra_clock+=1000;
        while(b->ax_extra_clock>=60*AX_TICK_MS){
            b->ax_extra_clock-=60*AX_TICK_MS;
            if(!ax_tick(&b->ax_extra,draw_ax_extra,b)){error(b,"invalid extended AX instruction");return;}
            if(b->error[0])return;
        }
    }
    mam_frame(b);
    if(b->title.active){
        b->title.age++;
        if(b->title.age<=32){
            unsigned alpha=(unsigned)((b->title.age-1)*255/32);
            for(size_t i=0;i<640*480;i++)for(unsigned c=0;c<3;c++)b->layers[0].pixels[i*4+c]=(uint8_t)(b->auxiliary.pixels[i*4+c]*(255-alpha)/255);
        }else {
            if(ktitle_draw(&b->title,&b->layers[0])){error(b,"title composition failed");return;}
            if(b->title.age<64){unsigned alpha=(unsigned)((b->title.age-33)*255/32);for(size_t i=0;i<640*480;i++)for(unsigned c=0;c<3;c++)b->layers[0].pixels[i*4+c]=(uint8_t)(b->layers[0].pixels[i*4+c]*alpha/255);}
        }
    }
    if(b->flag_dialog.active&&kflag_dialog_draw(&b->flag_dialog,&b->layers[0])){error(b,"history dialog composition failed");return;}
    if(b->message_visible){
        if(b->message_slide&&++b->message_slide_frame>=b->message_slide){
            b->message_slide=0;
            if(b->message_hiding){b->message_visible=0;memcpy(b->layers[0].pixels,b->message_base.pixels,640*480*4);}
        }
        if(b->message_visible){
            if(b->message_voice_pending&&!b->message_slide){b->message_voice_pending=0;if(voice_play(b))return;}
            if(b->message_active&&b->message_revealing&&!b->message_slide){
                b->message_clock+=1000;
                if(b->message_clock>=60*b->message_delay){
                    b->message_clock=0;
                    int x=b->message_reveal_x,y=b->message_reveal_y,end=y==b->message_end_y?b->message_end_x:608;
                    if(x<end){
                        int source_top=b->vm->globals[0][43].number,source_y=source_top+(y-408);
                        if(source_top<0||source_y<0||source_y+18>(int)b->layers[1].height)return;
                        for(int row=0;row<18&&y+row<462;row++)memcpy(b->message_text.pixels+(y-408+row)*b->message_text.stride+(x-32)*4,b->layers[1].pixels+(source_y+row)*b->layers[1].stride+x*4,(size_t)(x+16<=608?16:608-x)*4);
                        b->message_reveal_x+=16;
                    }else if(y<b->message_end_y){b->message_reveal_x=32;b->message_reveal_y+=18;}
                    else message_copy_text(b);
                }
            }
            message_compose(b);
        }
    }
    if(b->choice_active)choice_draw(b);
    if(b->exec_wipe_active)exec_wipe_frame(b);
    if(b->transition_steps){
        b->transition_frame++;
        unsigned duration=b->transition_steps-1;
        unsigned alpha=duration&&b->transition_frame<duration?b->transition_frame*255/duration:255;
        for(size_t i=0;i<640*480;i++){
            unsigned coverage=b->canvas.pixels[i*4+3]*alpha/255;
            for(unsigned c=0;c<3;c++)b->layers[0].pixels[i*4+c]=(uint8_t)((b->canvas.pixels[i*4+c]*coverage+b->auxiliary.pixels[i*4+c]*(255-coverage))/255);
            b->layers[0].pixels[i*4+3]=(uint8_t)(coverage+b->auxiliary.pixels[i*4+3]*(255-coverage)/255);
        }
        if(b->transition_frame>=b->transition_steps){
            memcpy(b->layers[0].pixels,b->canvas.pixels,640*480*4);b->transition_steps=0;
            movie_stop(b);
        }
        if(b->message_visible){memcpy(b->message_base.pixels,b->layers[0].pixels,640*480*4);message_compose(b);}
    }
    if(b->helper_steps){
        b->helper_frame++;
        unsigned duration=b->helper_steps-1;
        unsigned alpha=duration&&b->helper_frame<duration?b->helper_frame*255/duration:255;
        memcpy(b->layers[0].pixels,b->auxiliary.pixels,640*480*4);
        for(unsigned layer=0;layer<(b->helper_hide?1u:2u);layer++){
            KImage *src=&b->helper_surfaces[layer?0:1];unsigned opacity=layer?alpha:255-alpha;
            for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
                uint8_t *p=src->pixels+y*src->stride+x*4,*out=b->layers[0].pixels+y*b->layers[0].stride+x*4;
                unsigned coverage=p[3]*opacity/255;
                for(unsigned c=0;c<3;c++)out[c]=(uint8_t)((p[c]*coverage+out[c]*(255-coverage))/255);
                out[3]=(uint8_t)(coverage+out[3]*(255-coverage)/255);
            }
        }
        if(b->helper_frame>=b->helper_steps){
            if(b->helper_hide)b->helper_visible=0;
            else for(unsigned y=0;y<480;y++){memcpy(b->helper_surfaces[1].pixels+y*b->helper_surfaces[1].stride,b->helper_surfaces[0].pixels+y*b->helper_surfaces[0].stride,640*4);memset(b->helper_surfaces[0].pixels+y*b->helper_surfaces[0].stride,0,640*4);}
            b->helper_steps=0;
        }
    }
    if(b->scroll_active)scroll_frame(b);
    if(b->distort_count)distort_frame(b);
    if(b->blink_active)blink_frame(b);
    if(!b->restore_pending&&b->message_active&&!b->message_user_hidden&&!b->message_slide&&!b->novel_transition&&!b->message_request){
        if(b->message_timed&&!b->message_revealing){
            b->message_timed_clock+=1000;
            if(b->message_timed_clock>=(uint64_t)b->message_timed_delay*60)bootstrap_confirm(b);
        }else if(b->force_skip||(option(b,"Msg","IsOneMes",0)&&b->message_was_read)){
            message_skip_voice(b);bootstrap_confirm(b);
        }else if(option(b,"Msg","IsAutoMes",0)&&!b->message_revealing&&!b->voice_loading&&!b->voice_active){
            b->message_auto_clock+=1000;
            if(b->message_had_voice||b->message_auto_clock>=(uint64_t)b->message_auto_delay*60)bootstrap_confirm(b);
        }
    }
    if(b->bonus52_active)bonus52_frame(b);
    if(b->credits_active)credits_frame(b);
    if(b->montage_active)montage_frame(b);
    animation522_present(b);
    if(!b->fade_steps)return;
    b->fade_frame++;
    int delta=(int)b->fade_to-(int)b->fade_from;
    b->fade_alpha=(unsigned)((int)b->fade_from+delta*(int)b->fade_frame/(int)b->fade_steps);
    if(b->fade_frame>=b->fade_steps){b->fade_steps=0;b->fade_alpha=b->fade_to;if(b->fade_hide)b->fade_visible=0;}
}

#include "history_reset.inc"

void bootstrap_confirm(KBootstrap *b){
    if(b->message_active&&b->message_user_hidden){bootstrap_message_hide(b,0);return;}
    b->input_events|=1;b->input_event_until=b->frames+3;
    if(ax_modal_skip(b))return;
    if(b->credits_active){credits_finish(b,1);return;}
    if(b->area_active){area_finish(b,0);return;}
    if(b->bonus52_active){if(b->bonus52_active==1)b->bonus52_motion+=240;return;}
    if(b->choice_active){
        /* 47dfbe returns registered item value + 1, distinct from read-history ID. */
        if(b->choice_selected<0||(unsigned)b->choice_selected>=b->choice_count)return;
        int result=b->choice_returns[b->choice_selected];
        if(b->choice_normal){
            b->vm->globals[0][18]=(KValue){result,NULL};
            b->choice_normal=0;b->choice_active=0;
            memcpy(b->layers[0].pixels,b->choice_base.pixels,640*480*4);
            return;
        }
        if(kvm_push(b->vm,(KValue){result,NULL})){error(b,"choice result stack overflow");return;}
        b->choice_active=0;memcpy(b->layers[0].pixels,b->choice_base.pixels,640*480*4);message_init(b);return;
    }
    if(b->flag_dialog.active){
        KFlagDialog *d=&b->flag_dialog;int s=d->selected;
        if(s>=0x100&&s<=0x103){if(d->checked[s-0x100]<2)d->checked[s-0x100]^=1;return;}
        if(s==0){for(unsigned i=0;i<4;i++)if(d->checked[i]==1){b->extra_active=1;b->extra_kind=b->extra_request=15;break;}return;}
        if(s!=1&&s!=2)return;
        /* 428d20/428d35: Start returns 0, Cancel returns -1. Checkbox
           changes alone never clear records; native uses a separate Execute. */
        if(kvm_push(b->vm,(KValue){s==1?0:-1,NULL})){error(b,"history result stack overflow");return;}
        d->active=0;return;
    }
    if(b->title.active){
        if(b->title.age<64||b->title.selected<0)return;
        if(b->title.variant==4){
            if((unsigned)b->title.selected>=b->title.count)return;
            int result=b->title.native_ids[b->title.selected];if(result<0)return;
            b->vm->globals[0][18]=(KValue){result,NULL};
            b->title.selected=-1;
            if(ktitle_draw(&b->title,&b->layers[0])){error(b,"CTitle confirm composition failed");return;}
            b->title.active=0;return;
        }
        if(b->title.variant==3){
            unsigned action=b->title.extra_ids[b->title.selected];
            if(action==1){b->title_load_requested=1;return;}
            if(kvm_push(b->vm,(KValue){action==0?1:action==2?3:4,NULL})){error(b,"bonus title result overflow");return;}
            b->title.active=0;return;
        }
        if(b->title.variant==2){
            if(b->title.unlocked&&b->title.selected==1){b->title_load_requested=1;return;}
            if(!b->title.selected){b->extra_kind=b->extra_request=14;b->extra_active=1;return;}
            if(kvm_push(b->vm,(KValue){2,NULL})){error(b,"bonus title result overflow");return;}
            b->title.active=0;return;
        }
        if(b->title.variant==1){
            /* 491e60 / 4fe824: two stories, optional Load, then Return. */
            if(b->title.unlocked&&b->title.selected==2){b->title_load_requested=1;return;}
            int result=(unsigned)b->title.selected==b->title.count-1?2:b->title.selected;
            if(kvm_push(b->vm,(KValue){result,NULL})){error(b,"bonus title result overflow");return;}
            b->title.active=0;return;
        }
        if(b->title.extra){
            unsigned result=b->title.extra_ids[b->title.selected];
            if(result==5){b->title.extra=0;b->title.count=b->title.main_count;b->title.selected=0;return;}
            if(kvm_push(b->vm,(KValue){(int32_t)result,NULL})){error(b,"extras result overflow");return;}
            b->title.active=0;return;
        }
        if((unsigned)b->title.selected==b->title.count-1){
            if(bootstrap_flush_progress(b))return;
            b->quit_requested=1;return;
        }
        if(b->title.unlocked&&b->title.selected==1){b->title_load_requested=1;return;}
        if(b->title.selected!=0){title_extra(b);return;}
        /* 49148d returns integer zero for New Game through the modal result. */
        if(kvm_push(b->vm,(KValue){0,NULL})){error(b,"title result stack overflow");return;}
        b->title.active=0;return;
    }
    if(b->message_active){
        if(b->novel_mode){
            if(b->novel_transition){b->novel_step=255;novel_fade_frame(b);return;}
            if(kvm_push(b->vm,(KValue){0,NULL})){error(b,"novel result stack overflow");return;}
            b->message_active=0;return;
        }
        if(b->message_slide)return;
        if(b->message_revealing){message_copy_text(b);message_compose(b);return;}
        if(!b->message_keep_on_confirm&&message_init(b))return;
        if(kvm_push(b->vm,(KValue){0,NULL})){error(b,"message result stack overflow");return;}
        b->message_active=0;message_compose(b);return;
    }
    b->wait_input=0;b->wait_clock=0;
}
void bootstrap_message_hide(KBootstrap *b,int hidden){
    if(!b||!b->message_active)return;
    b->message_user_hidden=hidden!=0;
    if(b->novel_mode){const KImage *src=hidden?&b->novel_background:&b->novel_target;if(src->pixels)memcpy(b->layers[0].pixels,src->pixels,640*480*4);}
    else if(b->message_base.pixels)message_compose(b);
}
void bootstrap_cancel(KBootstrap *b){
    if(ax_modal_skip(b))return;
    if(b->credits_active){credits_finish(b,1);return;}
    b->input_events|=2;b->input_event_until=b->frames+3;
    if(b->area_active){area_finish(b,1);return;}
    if(b->title.active&&b->title.extra){b->title.extra=0;b->title.count=b->title.main_count;b->title.selected=0;return;}
    if(b->message_active){setting_put(b,"Msg","IsAutoMes","0");setting_put(b,"Msg","IsOneMes","0");b->message_open=0;bootstrap_message_hide(b,1);}
    if(b->flag_dialog.active){b->flag_dialog.selected=2;bootstrap_confirm(b);}
}
void bootstrap_menu_move(KBootstrap *b,int dx,int dy){
    if(b->area_active){area_move(b,dy?dy:dx);return;}
    if(b->bonus52_active){if(dx||dy)b->bonus52_motion+=100;return;}
    if(b->message_active&&b->message_open&&dx){b->message_hover=(b->message_hover+dx+8)%8;return;}
    if(b->flag_dialog.active){kflag_dialog_move(&b->flag_dialog,dx,dy);return;}
    if(dy)bootstrap_title_move(b,dy);
}
void bootstrap_title_move(KBootstrap *b,int delta){
    if(b->area_active){area_move(b,delta);return;}
    if(b->bonus52_active){if(delta)b->bonus52_motion+=100;return;}
    if(b->choice_active){if(!delta)return;int next=b->choice_selected<0?(delta<0?(int)b->choice_count-1:0):(b->choice_selected+delta)%(int)b->choice_count;if(next<0)next+=(int)b->choice_count;b->choice_selected=next;choice_draw(b);return;}
    if(b->flag_dialog.active){kflag_dialog_move(&b->flag_dialog,0,delta);return;}
    if(!b->title.active||b->title.age<64||!delta)return;
    if(b->title.selected<0)b->title.selected=0;
    else b->title.selected=(b->title.selected+(delta>0?1:(int)b->title.count-1))%(int)b->title.count;
}
void bootstrap_pointer(KBootstrap *b,int x,int y,int click){
    if(b->area_active){b->area_x=x;b->area_y=y;b->area_selected=area_hit(b,x,y);if(click)area_finish(b,0);return;}
    if(b->bonus52_active){
        if(b->bonus52_pointer_valid){int dx=abs(x-b->bonus52_x),dy=abs(y-b->bonus52_y);b->bonus52_motion+=(dx>50?50:dx)+(dy>50?50:dy);}
        b->bonus52_x=x;b->bonus52_y=y;b->bonus52_pointer_valid=1;return;
    }
    if(b->choice_active){
        unsigned page=b->choice_selected<0?0:(unsigned)b->choice_selected/4,count=b->choice_count-page*4;if(count>4)count=4;
        int top=(480-(int)count*52)/2;
        if(x>=72&&x<568&&y>=top&&y<top+(int)count*52){b->choice_selected=(int)page*4+(y-top)/52;choice_draw(b);if(click)bootstrap_confirm(b);}
        else if(click&&b->choice_count>4&&y>=440&&y<480&&x>=224&&x<416){
            unsigned pages=(b->choice_count+3)/4;page=x<320?(page+pages-1)%pages:(page+1)%pages;b->choice_selected=(int)page*4;choice_draw(b);
        }
        return;
    }
    if(b->flag_dialog.active){b->flag_dialog.selected=kflag_dialog_hit(&b->flag_dialog,x,y);if(click)bootstrap_confirm(b);return;}
    if(b->message_active){
        b->message_hover=-1;
        if(y>=464&&y<480)for(unsigned item=b->message_open?0:7;item<8;item++){
            int left=item?320+((int)item-1)*44:278;
            if(x>=left&&x<left+56){b->message_hover=(int)item;break;}
        }
        if(click){if(b->message_hover>=0)bootstrap_message_action(b,(unsigned)b->message_hover);else bootstrap_confirm(b);}return;
    }
    if(!b->title.active||b->title.age<64)return;
    b->title.selected=ktitle_hit(&b->title,x,y);if(click)bootstrap_confirm(b);
}

size_t bootstrap_audio_read(const KBootstrap *b,size_t *position,uint8_t *out,size_t capacity){
    if(!position||!out||!b->audio_pcm)return 0;
    size_t end=b->audio_loop_end?b->audio_loop_end:b->audio_size,total=0;
    if(end>b->audio_size||*position>end||(b->audio_loop_end&&b->audio_loop_start>=end))return 0;
    while(total<capacity){
        if(*position==end){if(!b->audio_loop_end)break;*position=b->audio_loop_start;}
        size_t n=end-*position;if(n>capacity-total)n=capacity-total;
        memcpy(out+total,b->audio_pcm+*position,n);*position+=n;total+=n;
    }
    if(b->music_active&&b->music_gain!=1){
        for(size_t i=0;i+1<total;i+=2){int16_t v=(int16_t)le16(out+i);int16_t q=(int16_t)(v*b->music_gain);out[i]=(uint8_t)q;out[i+1]=(uint8_t)((uint16_t)q>>8);}
    }
    return total;
}

/* The frontend queues a short buffer; each voice begins at the next unqueued
   sample while the BGM read cursor continues through its native loop. */
static void add_samples(uint8_t *out,const uint8_t *source,size_t size,double gain){
    for(size_t i=0;i+1<size;i+=2){int sample=(int16_t)le16(out+i)+(int16_t)((int16_t)le16(source+i)*gain);if(sample>32767)sample=32767;if(sample< -32768)sample=-32768;out[i]=(uint8_t)sample;out[i+1]=(uint8_t)((uint16_t)sample>>8);}
}
size_t bootstrap_audio_mix_read(KBootstrap *b,size_t *position,uint8_t *out,size_t capacity){
    capacity-=capacity%4;
    size_t count=bootstrap_audio_read(b,position,out,capacity);
    if(b->audio_rate!=44100||b->audio_channels!=2)return count;
    memset(out+count,0,capacity-count);
    if(b->voice_pcm&&b->voice_read_cursor<b->voice_size){
        size_t n=b->voice_size-b->voice_read_cursor;if(n>capacity)n=capacity;n-=n%4;
        add_samples(out,b->voice_pcm+b->voice_read_cursor,n,1);b->voice_read_cursor+=n;if(n>count)count=n;
    }
    if(!b->video_paused&&b->movie_pcm&&b->movie_read<b->movie_pcm_size){
        size_t n=b->movie_pcm_size-b->movie_read;if(n>capacity)n=capacity;n-=n%4;
        add_samples(out,b->movie_pcm+b->movie_read,n,1);b->movie_read+=n;if(n>count)count=n;
    }
    for(unsigned i=0;i<65;i++){
        if(i==64&&b->video_paused)continue;
        KEffectTrack *t=i==64?&b->movie_effect:&b->effect_tracks[i];size_t total=0;
        while(t->pcm&&total<capacity){
            size_t end=t->loop_end?t->loop_end:t->size;
            if(t->read>=end){if(!t->loop_end)break;t->read=t->loop_start;}
            size_t n=end-t->read;if(n>capacity-total)n=capacity-total;
            add_samples(out+total,t->pcm+t->read,n,pow(10.0,-t->fade_attenuation/2000.0));total+=n;t->read+=n;
        }
        if(total>count)count=total;
    }
    return count;
}

#include "save_runtime.inc"
#include "scene_replay.inc"

void bootstrap_message_action(KBootstrap *b,unsigned action){
    if(!b||!b->message_active)return;
    if(action<=1){
        const char *key=action?"IsOneMes":"IsAutoMes";int enabled=!option(b,"Msg",key,0);
        if(setting_put(b,"Msg",key,enabled?"1":"0"))return;
        if(enabled)setting_put(b,"Msg",action?"IsAutoMes":"IsOneMes","0");
        b->message_auto_clock=0;
        if(action&&enabled&&!b->message_was_read)setting_put(b,"Msg",key,"0");
        settings_save(b);
    }else if(action==7){b->message_open=!b->message_open;b->message_hover=-1;}
    else if((action>=2&&action<=6)||action==8)b->message_request=action;
}

/* Native music menu uses 4fd878 and unlock bytes 2900..2915, not ARC order. */
static const char *const music_catalog[16]={"BGM01.wav","BGM01B.wav","BGM03.wav","BGM04.wav","BGM06.wav","BGM07.wav","BGM08.wav","BGM09.wav","BGM10.wav","BGM11.wav","BGM12.wav","BGM13.wav","BGM14.wav","BGM15.wav","BGM16.wav","BGM17.wav"};
unsigned bootstrap_music_count(void){return 16;}
const char *bootstrap_music_name(unsigned index){return index<16?music_catalog[index]:NULL;}
int bootstrap_music_unlocked(const KBootstrap *b,unsigned index){return b&&index<16&&b->vm->byte_count>2900+index&&b->vm->bytes[2900+index]==1;}
void bootstrap_music_stop(KBootstrap *b){
    if(!b||!b->extra_active||b->extra_kind!=9)return;
    b->music_active=b->music_fading=0;b->audio_size=b->audio_cursor=b->audio_loop_start=b->audio_loop_end=0;b->audio_serial++;
}
int bootstrap_music_select(KBootstrap *b,unsigned index){
    if(!b||!b->extra_active||b->extra_kind!=9||!bootstrap_music_unlocked(b,index))return -1;
    if(play_pcm(b,&b->music,music_catalog[index]))return -1;
    int enabled=1;b->music_db=music_volume_db(b,&enabled);b->music_gain=1;b->music_active=1;b->music_enabled=enabled;b->music_fading=0;
    int16_t *samples=(int16_t *)b->audio_pcm;double gain=enabled?db_gain(b->music_db):0;
    for(size_t i=0;i<b->audio_size/2;i++)samples[i]=(int16_t)(samples[i]*gain);
    return 0;
}
void bootstrap_extra_close(KBootstrap *b){
    if(!b||!b->extra_active)return;
    if(b->extra_kind==10&&b->gallery_movie_base.pixels)bootstrap_gallery_movie_stop(b);
    b->extra_active=b->extra_request=b->extra_kind=0;b->music_active=b->music_fading=0;
    b->audio_size=b->audio_cursor=b->audio_loop_start=b->audio_loop_end=0;b->audio_serial++;
    if(kvm_push(b->vm,(KValue){0,NULL}))error(b,"extras return overflow");
}

int bootstrap_gallery_image(KBootstrap *b,unsigned index,KImage *out){
    if(!b||!b->extra_active||index>=KGALLERY_COUNT||!b->gallery.flags[b->gallery.ids[index]])return -1;
    uint8_t *data=NULL;size_t size=0;KImage image={0};
    if(read_named(&b->images,b->gallery.names[index],&data,&size)||rmt_decode(data,size,&image)){free(data);return -1;}free(data);
    rmt_free(out);*out=image;return 0;
}

/* 41a934: base image followed by two optional green-keyed layers at RMT origins. */
int bootstrap_gallery_variant(KBootstrap *b,unsigned group,unsigned item,unsigned variant,KImage *out){
    uint16_t indices[3];
    if(!b||!out||!b->extra_active||b->extra_kind!=10||kgallery_variant(&b->gallery,group,item,variant,indices)||
       !kgallery_variant_unlocked(&b->gallery,b->vm->bytes,b->vm->byte_count,indices))return -1;
    KImage result={0};
    for(unsigned k=0;k<3;k++){
        if(k&&!indices[k])continue;
        uint8_t *data=NULL;size_t size=0;KImage image={0};
        if(read_named(&b->images,b->gallery.names[indices[k]],&data,&size)||rmt_decode(data,size,&image)){free(data);rmt_free(&result);return -1;}free(data);
        if(!k){result=image;continue;}
        for(unsigned y=0;y<image.height;y++){
            int64_t dy=(int64_t)image.y+y-result.y;if(dy<0||dy>=result.height)continue;
            for(unsigned x=0;x<image.width;x++){
                int64_t dx=(int64_t)image.x+x-result.x;if(dx<0||dx>=result.width)continue;
                const uint8_t *src=image.pixels+y*image.stride+x*4;
                if(!src[0]&&src[1]==255&&!src[2])continue;
                memcpy(result.pixels+dy*result.stride+dx*4,src,3);
            }
        }
        rmt_free(&image);
    }
    rmt_free(out);*out=result;return 0;
}

/* 41a689..41a751: four second-click scroll presentations, including ev44e,
   which is not itself a cglist.dat variant. Preserve its separate unlock. */
unsigned bootstrap_gallery_scroll_extent(const KBootstrap *b,unsigned group,unsigned item,unsigned variant){
    if(!b||group!=1)return 0;
    if(item==7&&variant==0)return 394;
    if(item==7&&variant==1&&b->gallery.loaded&&b->gallery.flags[b->gallery.ids[1135]])return 480;
    return (item==8&&variant==0)||(item==37&&variant==0)?480:0;
}
int bootstrap_gallery_scroll_image(KBootstrap *b,unsigned group,unsigned item,unsigned variant,KImage *out){
    unsigned extent=bootstrap_gallery_scroll_extent(b,group,item,variant);if(!extent||!out)return -1;
    KImage image={0};if(bootstrap_gallery_variant(b,group,item,variant,&image))return -1;
    if(item==7&&variant==1){
        uint8_t *data=NULL;size_t size=0;KImage extra={0};
        if(read_named(&b->images,"ev44e.rmt",&data,&size)||rmt_decode(data,size,&extra)){free(data);rmt_free(&image);return -1;}
        free(data);rmt_free(&image);image=extra;
    }else if(item==37){
        if(image.width!=640||image.height!=480){rmt_free(&image);return -1;}
        uint8_t *repeat=malloc(640*960*4);if(!repeat){rmt_free(&image);return -1;}
        for(unsigned y=0;y<960;y++)memcpy(repeat+y*2560,image.pixels+(y%480)*image.stride,2560);
        rmt_free(&image);image=(KImage){0,0,640,960,2560,repeat};
    }
    if(image.width!=640||image.height!=480+extent){rmt_free(&image);return -1;}
    rmt_free(out);*out=image;return 0;
}

/* 416b37: category 5's extra page uses these 14 VSD resources. */
int bootstrap_gallery_movie_unlocked(const KBootstrap *b,unsigned item){
    return b&&item<14&&b->vm->byte_count>2950+item&&b->vm->bytes[2950+item]!=0;
}
int bootstrap_gallery_movie(KBootstrap *b,unsigned item){
    static const unsigned ids[]={1,2,3,4,5,6,7,9,11,12,13,14,15,16};
    if(!b||!b->extra_active||b->extra_kind!=10||b->gallery_movie_base.pixels||!bootstrap_gallery_movie_unlocked(b,item))return -1;
    char name[16];snprintf(name,sizeof(name),"m%02u.vsd",ids[item]);uint8_t *data=NULL;size_t size=0;
    if(read_named(&b->movies,name,&data,&size))return -1;
    KVideo *video=kvideo_open(data,size);if(!video){free(data);return -1;}
    uint8_t *base=malloc(640*480*4);if(!base){kvideo_close(video);free(data);return -1;}
    memcpy(base,b->layers[0].pixels,640*480*4);b->gallery_movie_base=(KImage){0,0,640,480,2560,base};
    movie_stop(b);b->video=video;b->video_data=data;b->video_active=1;
    mam_stop(b);kvoice_worker_cancel(b->voice_worker);b->voice_loading=0;
    b->music_active=b->music_fading=b->voice_active=0;
    b->audio_size=b->audio_cursor=b->audio_loop_start=b->audio_loop_end=0;
    b->audio_rate=44100;b->audio_channels=2;b->audio_serial++;
    b->vm->globals[0][50].number|=0x2000;return 0;
}
/* 41a330: h04/04's second row starts ev242.mov entries 0,2,4,6. */
int bootstrap_gallery_animation(KBootstrap *b,unsigned variant){
    uint16_t layers[3];
    if(!b||b->error[0]||!b->extra_active||b->extra_kind!=10||b->gallery_movie_base.pixels||variant>=4||
       kgallery_variant(&b->gallery,5,45,variant,layers)||!kgallery_variant_unlocked(&b->gallery,b->vm->bytes,b->vm->byte_count,layers))return -1;
    uint8_t *base=malloc(640*480*4);if(!base)return -1;
    memcpy(base,b->layers[0].pixels,640*480*4);
    if(movie_open(b,"ev242.mov",variant*2)){movie_stop(b);free(base);b->error[0]=0;return -1;}
    b->gallery_movie_base=(KImage){0,0,640,480,2560,base};b->gallery_animation=1;
    if(play_pcm(b,&b->music,"bgm11.wav")){bootstrap_gallery_movie_stop(b);b->error[0]=0;return -1;}
    int enabled=1;b->music_db=music_volume_db(b,&enabled);b->music_enabled=enabled;b->music_gain=enabled?db_gain(b->music_db):0;b->music_active=1;b->music_fading=0;
    return 0;
}
void bootstrap_gallery_movie_stop(KBootstrap *b){
    if(!b||!b->extra_active||b->extra_kind!=10)return;
    movie_stop(b);b->audio_serial++;
    if(b->gallery_animation){b->gallery_animation=0;b->music_active=b->music_fading=0;b->audio_size=b->audio_cursor=b->audio_loop_start=b->audio_loop_end=0;}
    if(b->gallery_movie_base.pixels){memcpy(b->layers[0].pixels,b->gallery_movie_base.pixels,640*480*4);rmt_free(&b->gallery_movie_base);}
}

/* 486d60: per-character unlock bytes. 486630 returns group*100 + item+1. */
unsigned bootstrap_replay_count(unsigned group){
    const unsigned counts[]={11,11,11,9,7};return group<5?counts[group]:0;
}
int bootstrap_replay_unlocked(const KBootstrap *b,unsigned group,unsigned item){
    const unsigned bases[]={2801,2812,2839,2823,2832};
    return b&&item<bootstrap_replay_count(group)&&b->vm->bytes[bases[group]+item]!=0;
}
static int extra_reply(KBootstrap *b,int value){
    if(kvm_push(b->vm,(KValue){value,NULL}))return error(b,"extras return overflow");
    b->extra_active=b->extra_request=b->extra_kind=0;return 0;
}
int bootstrap_replay_select(KBootstrap *b,unsigned group,unsigned item){
    if(!b||!b->extra_active||b->extra_kind!=11||!bootstrap_replay_unlocked(b,group,item))return -1;
    return extra_reply(b,(int)(group*100+item+1));
}
int bootstrap_replay_style(KBootstrap *b,int style){
    if(!b||!b->extra_active||b->extra_kind!=12||style< -1||style>1)return -1;
    return extra_reply(b,style);
}

/* 47c7d0 / 47cde0: 40 unlock bytes followed by three bonus-game buttons. */
int bootstrap_nawa_unlocked(const KBootstrap *b,unsigned item){
    return b&&item<43&&(item>=40||b->vm->bytes[6321+item]!=0);
}
int bootstrap_nawa_select(KBootstrap *b,unsigned item){
    if(!b||!b->extra_active||b->extra_kind!=13||!bootstrap_nawa_unlocked(b,item))return -1;
    b->vm->globals[0][21]=(KValue){item>=40,NULL};
    b->vm->globals[0][13]=(KValue){(int)(item>=40?item-40:item),NULL};
    return extra_reply(b,1);
}

int bootstrap_name_submit(KBootstrap *b,const char *utf8){
    if(!b||!b->extra_active||b->extra_kind!=14||b->title.variant!=2)return -1;
    if(!utf8){b->extra_active=b->extra_request=b->extra_kind=0;return 0;}
    uint8_t name[33];size_t size;if(ktext_name_encode(utf8,name,&size))return -1;
    memcpy(b->vm->bytes+1950,name,size+1);
    b->extra_active=b->extra_request=b->extra_kind=0;b->title.active=0;
    return kvm_push(b->vm,(KValue){0,NULL});
}
int bootstrap_name_preview(KBootstrap *b,const char *utf8,KImage *out){
    uint8_t name[33];size_t size,count;KTextChar chars[32];
    if(ktext_name_encode(utf8,name,&size)||ktext_decode(KTEXT_CP932,name,size,chars,32,&count))return -1;
    if(!b->font){const char *path=NULL;for(unsigned i=0;i<b->setting_count;i++)if(equal(b->settings[i].section,"Runtime")&&equal(b->settings[i].key,"FontFile"))path=b->settings[i].value;b->font=kfont_open(path,0);}
    if(!b->font)return -1;
    KImage image={0,0,512,40,2048,calloc(512*40,4)};if(!image.pixels)return -1;
    unsigned x=0;for(size_t i=0;i<count;i++){if(kfont_draw(b->font,&image,chars[i].codepoint,(int)x,4,24,24,0xffffff)){rmt_free(&image);return -1;}x+=chars[i].columns*12;}
    rmt_free(out);*out=image;return 0;
}

int bootstrap_scene_draw(KBootstrap *b,unsigned selected,unsigned overview,KImage *out){
    if(!b||!b->scene)return -1;
    KImage *images[]={&b->scene_tiles,&b->scene_parts};const char *names[]={"map_chp.rmt","naviparts.rmt"};
    for(unsigned i=0;i<2;i++)if(!images[i]->pixels){
        uint8_t *data=NULL;size_t size=0;
        if(read_named(&b->images,names[i],&data,&size)||rmt_decode(data,size,images[i])){free(data);return -1;}free(data);
    }
    return kscene_view(b->scene,&b->scene_tiles,&b->scene_parts,selected,overview,out);
}
