#include "bootstrap.h"
#include "file_store.h"
#include "reset_store.h"
#include "read_flags.h"
#include "save_slot.h"
#include "restore_name.h"
#include "text_layout.h"
#include "voice_character.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
static int record_newline(void *owner,unsigned operand);
static int record_bytes(void *owner,const uint8_t *data,size_t count);
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
    b->vm->record_bytes=record_bytes;b->vm->record_newline=record_newline;b->vm->record_owner=b;
    ax_reset(&b->ax);
    ax_reset(&b->ax_extra);
    for(unsigned i=0;i<320;i++)b->animation_status[i]=255;
    KImage *initial[]={&b->layers[0],&b->canvas,&b->auxiliary};
    for(unsigned i=0;i<3;i++){
        uint8_t *p=calloc(640*480,4);if(!p){error(b,"screen allocation failed");return b;}
        *initial[i]=(KImage){0,0,640,480,640*4,p};
    }
    strcpy(b->root,root);strcpy(b->save_root,save_root);b->last_loaded_layer=-1;b->animation_id=-1;b->message_hover=-1;b->exec522_current=-1;b->portrait_tracks[0]=b->portrait_tracks[1]=-1;
    b->message_index=-1; /* 500a70: no current native backlog record yet. */
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
static int store_native_progress(KBootstrap *b){
    KVM *v=b->vm;
    /* Startup may flush read flags before it has declared the native banks. */
    if(v->byte_count!=9192||v->word_count!=600||v->global_count[1]!=100||
       b->raw_size!=15000||b->restore_pending||b->history_restore)return 0;
    if(v->globals[1][61].string||(unsigned)v->globals[1][61].number>1)return -1;
    unsigned slot=v->globals[1][61].number?201:100;
    KFlags *catalog=kflags_read_slot(bootstrap_save_dir(b),0,slot);
    if(!catalog)return -1;
    KFlags current={0};current.bytes=v->bytes;current.byte_count=v->byte_count;
    current.words=v->words;current.word_count=v->word_count;
    current.globals[1]=v->globals[1];current.counts[1]=v->global_count[1];
    int result=catalog->byte_count!=9192||catalog->word_count!=600||catalog->raw_count!=15000||catalog->counts[1]!=100||
        catalog->globals[1][61].string||catalog->globals[1][61].number!=(int)(slot==201);
    if(!result){
        uint8_t before_bytes[9192];uint16_t before_words[600];
        memcpy(before_bytes,catalog->bytes,sizeof(before_bytes));memcpy(before_words,catalog->words,sizeof(before_words));
        result=kflags_merge(catalog,&current);
        if(!result&&(memcmp(before_bytes,catalog->bytes,sizeof(before_bytes))||memcmp(before_words,catalog->words,sizeof(before_words))))
            result=kflags_write_slot(catalog,bootstrap_save_dir(b),0,slot);
    }
    kflags_free(catalog);return result?-1:0;
}
static int flush_read_history(KBootstrap *b){
    if(!b||b->reset_pending)return -1;
    if(kgallery_flush(&b->gallery,bootstrap_save_dir(b)))return error(b,"CG history save failed");
    if(b->read_loaded&&b->read_dirty){
        if(kread_flags_save(bootstrap_save_dir(b),b->read_selector,b->read_flags,b->read_size))return error(b,"read history save failed");
        b->read_dirty=0;
    }
    return 0;
}
int bootstrap_flush_progress(KBootstrap *b){
    if(flush_read_history(b))return -1;
    if(store_native_progress(b))return error(b,"native progress catalog save failed");
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
    if(b->mes_fade_drawn)return &b->mes_fade_backing;
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
    if(layer==8)snprintf(b->normal_atlas_name,sizeof(b->normal_atlas_name),"%s",name);
    return 0;
}
#include "backlog_store.h"
void bootstrap_destroy(KBootstrap *b){if(!b)return;kbacklog_snapshot_free(b->backlog_restore);kbowling_runtime_free(b->bowling_runtime);for(unsigned i=0;i<27;i++)free(b->bowling_effects[i].pcm);rmt_free(&b->exec526_backing);rmt_free(&b->mes_fade_backing);for(unsigned i=0;i<3;i++)rmt_free(&b->mes_fade_surfaces[i]);rmt_free(&b->param_surface);rmt_free(&b->param_atlas);rmt_free(&b->param_backing);rmt_free(&b->diary_surface);kmessage_skin_free(&b->message_skin);(void)kbowling_release(&b->bowling,&b->current_bowling);for(unsigned i=0;i<3;i++)rmt_free(&b->letter_surfaces[i]);for(unsigned bank=0;bank<2;bank++)for(unsigned i=0;i<6;i++)rmt_free(&b->choice_rows[bank][i]);rmt_free(&b->gallery_movie_base);rmt_free(&b->scene_tiles);rmt_free(&b->scene_parts);free(b->novel_mask);free(b->letter_mask);free(b->mes_fade_mask);free(b->mam_data);free(b->mam_archive);rmt_free(&b->status_image);rmt_free(&b->status_parts);for(unsigned i=0;i<3;i++)rmt_free(&b->bonus52_ui[i]);kimage_worker_destroy(b->image_worker);kvoice_worker_destroy(b->voice_worker);while(b->control_files){KControlStore *s=b->control_files;b->control_files=s->next;kcontrol_free(s);}free(b->mov_data);free(b->movie_effect.pcm);rmt_free(&b->novel_original);rmt_free(&b->novel_background);rmt_free(&b->novel_from);rmt_free(&b->novel_target);kfont_close(b->novel_font);rmt_free(&b->choice_parts);rmt_free(&b->choice_text);rmt_free(&b->choice_base);for(unsigned i=0;i<64;i++)free(b->effect_tracks[i].pcm);kfont_close(b->font);for(unsigned i=0;i<3;i++)rmt_free(&b->helper_surfaces[i]);for(unsigned i=0;i<2;i++)rmt_free(&b->exec526_surfaces[i]);for(unsigned i=0;i<4;i++){rmt_free(&b->exec522_sprites[i]);rmt_free(&b->exec522_backing[i]);}ktitle_free(&b->title);kflag_dialog_free(&b->flag_dialog);free(b->scene);for(unsigned i=0;i<b->setting_value_count;i++)free(b->setting_values[i]);free(b->setting_values);while(b->flag_files){KFlags *f=b->flag_files;b->flag_files=f->next;kflags_free(f);}for(unsigned i=0;i<b->saved_control_count;i++)free(b->saved_controls[i].values);for(unsigned i=0;i<b->control_count;i++)free(b->controls[i].values);for(unsigned i=0;i<b->message_count;i++){free(b->messages[i].data);free(b->messages[i].text);}free(b->messages);rmt_free(&b->canvas);rmt_free(&b->auxiliary);rmt_free(&b->fade_surface);rmt_free(&b->message_text);rmt_free(&b->message_base);rmt_free(&b->message_parts);rmt_free(&b->overlay524_base);rmt_free(&b->overlay524_sprite);for(unsigned i=0;i<64;i++)rmt_free(&b->layers[i]);for(unsigned i=0;i<KVM_MODULES;i++)free(b->module_data[i]);for(unsigned i=0;i<3;i++)free(b->audio_objects[i]);free(b->records);free(b->raw_variables);free(b->read_flags);kvideo_close(b->video);free(b->video_data);free(b->movie_pcm);ai6_close(&b->movies);ai6_close(&b->music);ai6_close(&b->voice);free(b->audio_pcm);free(b->voice_pcm);ai6_close(&b->effects);free(b->animation_data);ai6_close(&b->data);ai6_close(&b->scripts);ai6_close(&b->images);kvm_destroy(b->vm);free(b);}
static int message_init(KBootstrap *b){
    /* 481be0: the layout rectangle is (32,8,560,54), but 481d68/481dca
       clear both complete 640x84 text surfaces. 481a50 copies that surface
       when revealing the remainder, including the right margin. */
    KVM *v=b->vm;KImage *dst=&b->layers[1];
    if(v->global_count[0]<=49||!b->layer_count||!dst->pixels||dst->width<640||dst->height<84)
        return error(b,"message initialization requires system variables and layer 1 >= 640x84");
    if(!b->message_text.pixels){
        uint8_t *p=calloc(576*54,4);if(!p)return error(b,"message surface allocation failed");
        /* Text coordinates are local to the native message sprite. */
        b->message_text=(KImage){32,8,576,54,576*4,p};
    }
    memset(b->message_text.pixels,0,b->message_text.stride*b->message_text.height);
    for(unsigned y=0;y<84;y++)memset(dst->pixels+y*dst->stride,0,640*4);
    const unsigned slots[]={42,43,44,45,46,47,30,31,49};
    const int values[]={32,8,592,62,32,8,16,18,1};
    for(unsigned i=0;i<sizeof(slots)/sizeof(*slots);i++)v->globals[0][slots[i]]=(KValue){values[i],NULL};
    b->font_width=b->font_height=16;
    b->message_pending_size=0;b->message_pending[0]=0;b->message_voice_name[0]=0;
    b->message_buttons_motion=0;
    b->message_cursor_x=32;b->message_cursor_y=8;b->message_initialized=1;
    return 0;
}
static int record_append(KBootstrap *b,const uint8_t *data,size_t count){
    if(b->message_index<0||(unsigned)b->message_index>=b->message_count)return error(b,"message recorder has no current slot");
    KMessageRecord *r=&b->messages[b->message_index];size_t prefix=r->size?r->size-1:0;
    if(count>262144||prefix>262144-count)return error(b,"message recording limit");
    size_t next=prefix+count+1;
    if(next>r->capacity){
        /* Generic capture appends one instruction at a time. Grow in chunks
           instead of reallocating the entire record for every opcode. */
        size_t capacity=r->capacity?r->capacity:64;
        while(capacity<next){if(capacity>262145/2){capacity=262145;break;}capacity*=2;}
        uint8_t *p=realloc(r->data,capacity);if(!p)return error(b,"message recording allocation failed");r->data=p;r->capacity=capacity;
    }
    memcpy(r->data+prefix,data,count);r->data[next-1]=0;r->size=next;return 0;
}
static int record_bytes(void *owner,const uint8_t *data,size_t count){
    return record_append(owner,data,count);
}
/* 4fe4a0 -> 500b30. Repeated begin calls belong to the same record. */
static int record_begin(KBootstrap *b){
    KValue *flags=&b->vm->globals[0][50];
    if(!(flags->number&0x80))return 0;
    if(!(flags->number&0x100)){
        if(!b->message_count)return error(b,"message recorder has no slots (arguments preserved)");
        if(b->message_index < -1 || b->message_index >= (int)b->message_count)
            return error(b,"message recorder index invalid (arguments preserved)");
        if(b->message_index+1==(int)b->message_count){
            free(b->messages[0].data);free(b->messages[0].text);
            memmove(b->messages,b->messages+1,(b->message_count-1)*sizeof(*b->messages));
            memset(&b->messages[b->message_count-1],0,sizeof(*b->messages));
        }else b->message_index++;
    }else if(b->message_index<0||(unsigned)b->message_index>=b->message_count)
        return error(b,"message recorder has no current slot (arguments preserved)");
    flags->number|=0x100;return 0;
}
/* 46f880 starts records for text; 505820 records the bytes including NUL.
   Retain the text opcode as well, so adjacent text commands remain distinct.
   Generic instruction capture is driven by the VM record_bytes callback. */
static int record_text(KBootstrap *b){
    if(!(b->vm->globals[0][50].number&0x80))return 0;
    if(b->vm->text_size>4096)return error(b,"message recording text limit");
    if(record_begin(b))return -1;
    uint8_t command[4099];size_t prefix=1;command[0]=(uint8_t)b->vm->opcode;
    /* 46f880 records the opcode, then 46f3f0 records it again when
       generic capture is enabled. 505820 records the body only once. */
    if(b->vm->globals[0][50].number&0x200)command[prefix++]=command[0];
    memcpy(command+prefix,b->vm->text,b->vm->text_size+1);
    return record_append(b,command,b->vm->text_size+prefix+1);
}
static int record_newline(void *owner,unsigned operand){
    KBootstrap *b=owner;
    if(!(b->vm->globals[0][50].number&0x80))return 0;
    if(record_begin(b))return -1;
    /* 46f880 starts a text record for 1b as well as 0a/0b. Preserve the
       zero operand separately from the record's trailing sentinel. */
    uint8_t command[4]={0x1b,(uint8_t)operand,0x1b,(uint8_t)operand};
    return record_append(b,command,(b->vm->globals[0][50].number&0x200)?4:2);
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
            snprintf(b->voice_playing_name,sizeof(b->voice_playing_name),"%s",b->audio_objects[2][0].name);
            if(mam_prepare(b,b->audio_objects[2][0].name))return -1;
            if(b->voice_worker){
                if(kvoice_worker_submit(b->voice_worker,b->audio_objects[2][0].name,1))return error(b,"voice worker submission failed");
                free(b->voice_pcm);b->voice_pcm=NULL;b->voice_size=b->voice_read_cursor=b->voice_clock_cursor=0;b->voice_clock=0;
                if(!b->music_active){b->audio_size=b->audio_cursor=0;}
                if(b->audio_rate!=44100||b->audio_channels!=2){b->audio_rate=44100;b->audio_channels=2;b->audio_serial++;}
                b->voice_loading=b->voice_active=1;b->audio_objects[2][0].state=0;return 0;
            }
            uint8_t *data=NULL,*pcm=NULL;size_t size=0,bytes=0;
            if(read_named(&b->voice,b->audio_objects[2][0].name,&data,&size)||kaudio_decode(data,size,&pcm,&bytes)){free(data);free(pcm);return error(b,"voice decode failed");}free(data);
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
    if(enabled)*enabled=option((KBootstrap *)b,"Music","IsMusic",1)!=0;
    return kisaku_sound_volume_db(volume,enabled?*enabled:1);
}
static double db_gain(int db){return pow(10.0,db/2000.0);}
/* Keep decoder PCM intact, as DirectSound did. Settings affect the next
   output samples and can unmute an already-playing track without reloading. */
static double voice_output_gain(const KBootstrap *b){
    if(!option((KBootstrap *)b,"Voice","IsVoice",1))return 0;
    int character=kisaku_voice_character(b->voice_playing_name);
    if(character>=0){char key[24];snprintf(key,sizeof(key),"IsCharVoice%02d",character);if(!option((KBootstrap *)b,"Voice",key,1))return 0;}
    return db_gain(kisaku_sound_volume_db(option((KBootstrap *)b,"Voice","Volume",83),1));
}
static void audio_settings_changed(KBootstrap *b){
    int attenuation=b->music_db-b->fade_db;
    int enabled;b->music_db=music_volume_db(b,&enabled);b->music_enabled=(unsigned)enabled;
    b->fade_db=b->music_db-(b->music_fading?attenuation:0);
    for(unsigned i=0;i<92;i++){
        KEffectTrack *t=i>=65?&b->bowling_effects[i-65]:i==64?&b->movie_effect:&b->effect_tracks[i];
        if(!t->fade_step)t->fade_limit=5000+kisaku_sound_volume_db(option(b,"Effect",i==3||i==64?"HVolume":"Volume",83),1);
    }
}
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
static int message_skin_reload(KBootstrap *b);
static void message_skin_color(KBootstrap *b,KImage *background){
    if(!background->pixels)return;
    uint8_t color[4];const char *keys[]={"Blue","Green","Red","Alpha"};
    for(unsigned c=0;c<4;c++){
        int64_t value=option(b,"Msg",keys[c],c==3?92:0);
        if(c==3)value=224-value;
        if(value<0)value=0;
        if(value>224)value=224;
        value=value*255/184;if(value>255)value=255;color[c]=(uint8_t)value;
    }
    for(unsigned y=0;y<background->height;y++)for(unsigned x=0;x<background->width;x++)
        memcpy(background->pixels+y*background->stride+x*4,color,4);
}
/* CMesWnd controls: open, quit dialog (4b4060), backlog, skip, auto, input toggle. */
static const unsigned message_actions[6]={7,6,5,1,0,9};
/* 46bbe0 / 46bca0: EffectSpeed scales interpolation ticks, each 15 ms.
   Flag 0x4000 preserves duration and forbids input skipping. */
static unsigned message_effect_steps(KBootstrap *b,unsigned duration){
    if(!(b->vm->globals[0][50].number&0x4000)){
        int speed=option(b,"Display","EffectSpeed",0);
        if(speed==0)duration>>=1;else if(speed==1)duration>>=2;else if(speed==2)duration=0;
    }
    return duration;
}
/* 483d10 / 483b80: four independently timed sprites, all start together. */
static void message_buttons_begin(KBootstrap *b){
    b->message_buttons_tick=b->message_buttons_clock=0;b->message_buttons_motion=0;
    for(unsigned i=0;i<4;i++){
        unsigned duration=b->message_open?6+i*2:14-i*2;
        b->message_buttons_steps[i]=message_effect_steps(b,duration);
        if(b->message_buttons_steps[i])b->message_buttons_motion=1;
    }
}
static unsigned message_button_y(const KBootstrap *b,unsigned item){
    if(item==0||item==5)return 464;
    if(!b->message_buttons_motion)return b->message_open?464:480;
    unsigned steps=b->message_buttons_steps[item-1],tick=b->message_buttons_tick;
    unsigned move=steps&&tick<steps?16*tick/steps:16;
    return b->message_open?480-move:464+move;
}
static void message_buttons_frame(KBootstrap *b){
    if(!b->message_buttons_motion)return;
    b->message_buttons_clock+=1000;
    while(b->message_buttons_clock>=900&&b->message_buttons_motion){
        b->message_buttons_clock-=900;b->message_buttons_tick++;
        unsigned pending=0;
        for(unsigned i=0;i<4;i++)pending|=b->message_buttons_tick<b->message_buttons_steps[i];
        b->message_buttons_motion=pending;
    }
}
/* 482b20 compares CP932 816d/816e; single-byte control prefixes do not
   advance its full-width cursor. Bound the scan to avoid malformed strings. */
static unsigned message_name_cells(const uint8_t *text,size_t size){
    size_t i=0;unsigned cells=1;
    if(!text)return 0;
    while(i<size&&!((text[i]>=0x81&&text[i]<=0x9f)||(text[i]>=0xe0&&text[i]<=0xef)))i++;
    if(i+1>=size||text[i]!=0x81||text[i+1]!=0x6d)return 0;
    i+=2;
    while(i<size){
        if((text[i]>=0x81&&text[i]<=0x9f)||(text[i]>=0xe0&&text[i]<=0xef)){
            if(i+1>=size)return 0;
            cells++;
            if(text[i]==0x81&&text[i+1]==0x6e)return cells;
            i+=2;
        }else i++;
    }
    return 0;
}
static void message_name_reveal(KBootstrap *b){
    const uint8_t *text=(const uint8_t *)(b->message_pending_size?b->message_pending:b->vm->text);
    size_t size=b->message_pending_size?b->message_pending_size:b->vm->text_size;
    unsigned cells=message_name_cells(text,size);
    int advance=b->vm->globals[0][30].number,height=b->vm->globals[0][31].number;
    if(!cells||advance<=0||height<=0)return;
    uint64_t width=(uint64_t)cells*(unsigned)advance;
    unsigned limit=b->message_end_y==408?(unsigned)b->message_end_x-32:576;
    if(width>limit)width=limit;
    if(height>54)height=54;
    KImage *source=&b->layers[1];int top=b->vm->globals[0][43].number;
    if(top<0||top+height>(int)source->height)return;
    for(int row=0;row<height;row++)memcpy(b->message_text.pixels+row*b->message_text.stride,
        source->pixels+(top+row)*source->stride+32*4,(size_t)width*4);
    b->message_reveal_x=32+(int)width;
}
static void message_slide_finish(KBootstrap *b){
    b->message_slide=0;
    if(b->message_hiding){b->message_visible=0;memcpy(b->layers[0].pixels,b->message_base.pixels,640*480*4);}
}
static void message_slide_begin(KBootstrap *b,int hiding){
    unsigned steps=message_effect_steps(b,12);
    b->message_slide=steps?steps+1:0;b->message_slide_frame=b->message_slide_clock=0;b->message_hiding=hiding;
    if(!steps)message_slide_finish(b);
}
static void message_compose(KBootstrap *b){
    unsigned offset=0;
    if(b->message_slide){unsigned f=b->message_slide_frame,d=b->message_slide-1;unsigned move=d&&f<d?84*f/d:84;offset=b->message_hiding?move:84-move;}
    memcpy(b->layers[0].pixels,b->message_base.pixels,640*480*4);
    if(b->message_user_hidden)return;
    const uint8_t *color=b->message_skin.background.pixels;
    unsigned alpha=color[3];
    for(unsigned y=396+offset;y<480;y++)for(unsigned x=0;x<640;x++){
        uint8_t *out=b->layers[0].pixels+y*b->layers[0].stride+x*4;
        for(unsigned c=0;c<3;c++)out[c]=(uint8_t)(color[c]*alpha/255+out[c]*(255-alpha)/255);
    }
    for(unsigned y=0;y<54&&404+offset+y<480;y++)for(unsigned x=0;x<576;x++){
        uint8_t *src=b->message_text.pixels+y*b->message_text.stride+x*4,*out=b->layers[0].pixels+(404+offset+y)*b->layers[0].stride+(32+x)*4;
        for(unsigned c=0;c<3;c++)out[c]=(uint8_t)(src[c]*src[3]/255+out[c]*(255-src[3])/255);
    }
    /* 481df0 / 482450: six independent sprites, with native atlas states. */
    const unsigned sx[]={76,532,152,76,0,152},sy[]={148,84,84,84,84,148};
    for(unsigned item=0;item<6;item++){
        unsigned button_y=message_button_y(b,item);
        if(button_y>=480)continue;
        KImage *button=&b->message_skin.buttons[item],*atlas=&b->message_skin.atlas;
        unsigned state=b->message_hover==(int)message_actions[item]?1:0;
        if((item==4&&option(b,"Msg","IsAutoMes",0))||(item==3&&option(b,"Msg","IsOneMes",0))||
           (item==5&&!state&&(b->vm->globals[0][50].number&0x8000)))state=2;
        unsigned row=sy[item]+state*16;
        if(row+16>atlas->height){error(b,"CMesWnd button state outside atlas");return;}
        for(unsigned y=0;y<16&&button_y+offset+y<480;y++)for(unsigned x=0;x<button->width;x++){
            uint8_t *src=atlas->pixels+(row+y)*atlas->stride+(sx[item]+x)*4,*out=b->layers[0].pixels+(button_y+offset+y)*b->layers[0].stride+(button->x+x)*4;
            for(unsigned c=0;c<3;c++)out[c]=(uint8_t)(src[c]*src[3]/255+out[c]*(255-src[3])/255);
        }
    }
}
static int message_slide_skip(KBootstrap *b){
    if(!b->message_slide&&!b->message_buttons_motion)return 0;
    if(!(b->vm->globals[0][50].number&0x4000)){
        if(b->message_slide)message_slide_finish(b);
        b->message_buttons_motion=0;
        if(b->message_visible)message_compose(b);
    }
    return 1;
}
#include "message_history.inc"
#include "backlog_runtime.inc"
static int message_begin(KBootstrap *b,int id){
    KVM *v=b->vm;

    if(id< -1||(id>=0&&(size_t)id>=b->read_size*8)||v->globals[0][49].number!=1||v->globals[0][42].number!=32||v->globals[0][43].number!=8||v->globals[0][44].number!=592||v->globals[0][45].number!=62)return error(b,"message region/read id unsupported");
    if(!b->message_skin.atlas.pixels&&message_skin_reload(b))return -1;
    if(!b->message_base.pixels){b->message_base=(KImage){0,0,640,480,2560,malloc(640*480*4)};if(!b->message_base.pixels)return error(b,"message backdrop allocation failed");}
    if(!b->message_visible)memcpy(b->message_base.pixels,b->layers[0].pixels,640*480*4);
    int a=option(b,"Msg","Alpha",92),r=option(b,"Msg","Red",0),g=option(b,"Msg","Green",0),blue=option(b,"Msg","Blue",0),speed=option(b,"Msg","ShowSpeed",86);
    if(a<0||a>255||r<0||r>255||g<0||g>255||blue<0||blue>255||speed<0||speed>255)return error(b,"message settings range");
    b->message_color=((uint32_t)(255-a)<<24)|((uint32_t)r<<16)|((uint32_t)g<<8)|(unsigned)blue;
    /* 484b22..484b81: double intermediates, then truncation toward zero.
       124 is the explicit instant-text setting (484bc8). Older portable
       settings allowed 255; normalize those to the native instant endpoint. */
    if(speed>124)speed=124;
    double quadratic=(speed*0.01365)*speed;
    int delay=(int)(250.0-(quadratic+speed));
    b->message_delay=delay>0?(unsigned)delay:0;
    b->message_clock=b->message_delay*60;b->message_revealing=1;
    int top=v->globals[0][43].number,end_x=v->globals[0][46].number,end_y=v->globals[0][47].number;
    b->message_reveal_x=32;b->message_reveal_y=408;b->message_end_x=end_x;b->message_end_y=408+end_y-top;
    if(end_y<top||end_y>v->globals[0][45].number||end_x<32||end_x>608)return error(b,"message cursor range");
    memset(b->message_text.pixels,0,b->message_text.stride*b->message_text.height);
    if(speed==124)message_copy_text(b);else message_name_reveal(b);
    if(!b->message_visible)message_slide_begin(b,0);
    b->message_active=b->message_visible=1;b->message_read_id=id;
    message_history_record(b);
    b->message_auto_clock=0;
    int auto_speed=option(b,"Msg","AutoMesSpeed",52);if(auto_speed<0)auto_speed=0;if(auto_speed>104)auto_speed=104;
    size_t message_bytes=b->message_pending_size?b->message_pending_size:v->text_size;
    b->message_auto_delay=500+(unsigned)((int)((104-auto_speed)*1.7))*(unsigned)(message_bytes/2);
    b->message_had_voice=b->audio_counts[2]&&b->audio_objects[2][0].state;
    b->message_was_read=id>=0&&(b->read_flags[(unsigned)id>>3]&(0x80u>>(id&7)))!=0;
    if(!b->message_was_read&&option(b,"Msg","IsOneMes",0))setting_put(b,"Msg","IsOneMes","0");
    b->message_open=option(b,"Msg","EnableOpen",0)!=0||option(b,"Msg","IsOneMes",0)!=0;
    b->message_buttons_motion=0;
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
/* 4f68e0 -> 4f5ef0 -> 48e110 -> 48c0c0: the ninth operand is the
 * RGB key; the tenth selects Alpha copying. Do not interchange them. */
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
    uint32_t key=(uint32_t)q[8]&0xffffffu;
    for(int64_t y=0;y<h;y++)for(int64_t x=0;x<w;x++){
        uint8_t *s=copy+(y*w+x)*4,*d=dst->pixels+(dy+y)*dst->stride+(dx+x)*4;
        if((((uint32_t)s[0]|((uint32_t)s[1]<<8)|((uint32_t)s[2]<<16))&0xffffffu)==key)continue;
        if(q[9]==1)memcpy(d,s,4);
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
/* 4f9eb0 -> 45c940: independent 120x128 date badge at (16,16).
   Keep its private DIB separate from scene and choice redraws. */
static void overlay524_restore(KBootstrap *b){
    if(!b->overlay524_drawn)return;
    KImage *dst=&b->layers[0];
    if(dst->pixels&&dst->width>=136&&dst->height>=144)
        for(unsigned y=0;y<128;y++)memcpy(dst->pixels+(16+y)*dst->stride+16*4,
            b->overlay524_base.pixels+y*b->overlay524_base.stride,120*4);
    b->overlay524_drawn=0;
}
static void overlay524_present(KBootstrap *b){
    if(!b->overlay524_visible||!b->overlays.badge_visible||b->overlay524_drawn)return;
    KImage *dst=&b->layers[0],*src=&b->overlay524_sprite;
    if(!dst->pixels||dst->width<136||dst->height<144||!src->pixels)return;
    for(unsigned y=0;y<128;y++){
        memcpy(b->overlay524_base.pixels+y*b->overlay524_base.stride,
            dst->pixels+(16+y)*dst->stride+16*4,120*4);
        for(unsigned x=0;x<120;x++){
            const uint8_t *p=src->pixels+y*src->stride+x*4;
            uint8_t *d=dst->pixels+(16+y)*dst->stride+(16+x)*4;
            for(unsigned c=0;c<3;c++)d[c]=(uint8_t)(d[c]*(255-p[3])/255+p[c]*p[3]/255);
        }
    }
    b->overlay524_drawn=1;
}
static int overlay524_draw(KBootstrap *b,int first,int second,int third){
    static const unsigned first_xy[][2]={
        {0,0},{0,32},{0,64},{0,96},{0,128},
        {76,0},{80,32},{80,64},{80,96},{80,128},{160,0},{160,32}
    };
    static const unsigned second_xy[][2]={{240,0},{240,28},{240,56},{240,84}};
    static const unsigned third_xy[][2]={{160,120},{0,0},{0,0},{0,0},{160,64},{0,0},{160,92}};
    KImage *src=&b->layers[7];
    if(first<0||first>=12||second<0||second>=4||third<0||third>=7)
        return error(b,"31/524 sprite index out of range");
    if(!src->pixels||src->width<640||src->height<400)
        return error(b,"31/524 overlay layer 7 missing");
    if(!b->overlay524_sprite.pixels){
        uint8_t *p=calloc(120*128,4),*q=calloc(120*128,4);
        if(!p||!q){free(p);free(q);return error(b,"31/524 private surface allocation failed");}
        b->overlay524_sprite=(KImage){0,0,120,128,480,p};
        b->overlay524_base=(KImage){0,0,120,128,480,q};
    }
    overlay524_restore(b);
    KImage *dst=&b->overlay524_sprite;memset(dst->pixels,0,128*480);
    for(unsigned y=0;y<128;y++)for(unsigned x=0;x<120;x++){
        const uint8_t *p=src->pixels+(272+y)*src->stride+(520+x)*4;
        if(p[0]==0&&p[1]==255&&p[2]==0)continue;
        memcpy(dst->pixels+y*dst->stride+x*4,p,4);
    }
    unsigned rects[3][6]={
        {25,24,80,32,first_xy[first][0],first_xy[first][1]},
        {29,56,72,28,second_xy[second][0],second_xy[second][1]},
        {29,84,72,28,third_xy[third][0],third_xy[third][1]}};
    for(unsigned i=0;i<3;i++)for(unsigned y=0;y<rects[i][3];y++)
        memcpy(dst->pixels+(rects[i][1]+y)*dst->stride+rects[i][0]*4,
            src->pixels+(rects[i][5]+y)*src->stride+rects[i][4]*4,rects[i][2]*4);
    b->overlay524_visible=b->overlays.badge_visible=b->overlays.badge_saved=1;return 0;
}
static int overlay524_clear(KBootstrap *b){
    overlay524_restore(b);b->overlay524_visible=b->overlays.badge_visible=b->overlays.badge_saved=0;return 0;
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
    int scene_sound=track==&b->effect_tracks[3]||track==&b->movie_effect;
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
    int volume_db=kisaku_sound_volume_db(option(b,"Effect",scene_sound?"HVolume":"Volume",83),1);
    free(track->pcm);*track=(KEffectTrack){.pcm=pcm,.size=pcm_size,.loop_start=first,.loop_end=last,.fade_limit=5000+volume_db};
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
    unsigned theme=b->vm->bytes[1000]==1;
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
static int choice_top(const KBootstrap *b,unsigned count){
    unsigned rows=b->choice_normal?(b->choice_count<4?b->choice_count:4):count;
    return (480-(int)rows*52)/2;
}
static unsigned choice_native_state(KBootstrap *b,unsigned item){
    if(!b->vm->bytes[1000]||b->vm->globals[1][61].number!=0)return 0;
    int id=b->choice_values[item];
    return id<0?0:b->vm->bytes[2000+id];
}
static int choice_enabled(KBootstrap *b,unsigned item){
    if(!b->choice_normal||!b->vm->bytes[1000]||!b->vm->bytes[4010])return 1;
    int id=(int16_t)b->choice_values[item];if(id==-1)return 1;
    if(id<0)return 0;
    return b->vm->bytes[(b->vm->globals[1][61].number==0?2000:0)+id]!=0;
}
static unsigned choice_native_color(KBootstrap *b,unsigned state){
    static const unsigned colors[]={0x000000,0x00fe00,0x00feff,0xff8080,0xfffe00};
    unsigned color=0;const char channels[]="BGR";
    for(unsigned c=0;c<3;c++){
        char key[]={'C',(char)('0'+state),channels[c],0};
        color|=((unsigned)option(b,"SELECT",key,(colors[state]>>(c*8))&255)&255)<<(c*8);
    }
    return color;
}
/* 4f0970: expand each 34-pixel half into 8 + 18 + 18 + 8.
   48c010 recolors the red key; green is transparent in the sprite copy. */
static void choice_native_pixel(KBootstrap *b,unsigned state,int hover,unsigned x,unsigned y,int stretch,unsigned color,unsigned alpha,uint8_t out[4]){
    KImage *atlas=&b->layers[5];
    unsigned row=!stretch?y:y<8?y:y<44?8+(y-8)%18:26+y-44;
    const uint8_t *src=atlas->pixels+(68+state*68+(hover?34:0)+row)*atlas->stride+x*4;
    memcpy(out,src,4);
    unsigned rgb=out[0]|((unsigned)out[1]<<8)|((unsigned)out[2]<<16);
    if(rgb==0x00ff00){out[3]=0;return;}
    if(!hover&&rgb==0xff0000){
        out[0]=(uint8_t)color;out[1]=(uint8_t)(color>>8);out[2]=(uint8_t)(color>>16);
        out[3]=(uint8_t)alpha;
    }
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
        unsigned color=b->choice_normal?((unsigned)b->vm->globals[0][33].number&0xffffff):
            b->vm->bytes[8100]==0&&b->vm->bytes[2000+b->choice_values[item]]?0xffb400:0xffffff;
        if(!choice_enabled(b,item))color=0x808080;
        for(size_t j=0;j<n;j++){
            if(kfont_draw(b->font,&b->choice_text,chars[j].codepoint,x,top+(int)i*52+(b->choice_normal?17:18),16,16,color))return error(b,"choice glyph unavailable");
            x+=chars[j].columns*8;
        }
    }
    if(b->choice_normal){
        /* 523d64/523d68: native CP932 ▲ / ▼, centered in 496x34 rows. */
        if(page&&kfont_draw(b->font,&b->choice_text,0x25b2,312,top-34+8,16,16,0xffffff))return error(b,"choice previous-page glyph unavailable");
        if(page*4+4<b->choice_count&&kfont_draw(b->font,&b->choice_text,0x25bc,312,top+208+8,16,16,0xffffff))return error(b,"choice next-page glyph unavailable");
    }else if(b->choice_count>4){
        char label[48];snprintf(label,sizeof(label),"<    %u / %u    >",page+1,(b->choice_count+3)/4);
        int x=(640-(int)strlen(label)*8)/2;
        for(unsigned i=0;label[i];i++)if(kfont_draw(b->font,&b->choice_text,(unsigned char)label[i],x+(int)i*8,448,16,16,0xffffff))return error(b,"choice page glyph unavailable");
    }
    b->choice_rendered_page=page;return 0;
}
static void choice_draw(KBootstrap *b){
    if(!b->choice_active)return;
    if(b->choice_selected< -1||(b->choice_selected>=0&&(unsigned)b->choice_selected>=b->choice_count)){error(b,"choice selection invalid");return;}
    unsigned page=b->choice_selected<0?b->choice_page:(unsigned)b->choice_selected/4,count=b->choice_count-page*4;if(count>4)count=4;
    b->choice_page=page;
    int top=choice_top(b,count);
    if(b->choice_rendered_page!=page&&choice_page_text(b,page,count,top))return;
    memcpy(b->layers[0].pixels,b->choice_base.pixels,640*480*4);
    for(unsigned i=0;i<count;i++){
        unsigned item=page*4+i;int seen=b->vm->bytes[2000+b->choice_values[item]]!=0;
        /* Main-story seen rows in selparts2 are cyan. Keep the requested
           black background for both states; seen labels carry the distinction.
           Appendix games retain every original atlas state. */
        int sy=(seen&&b->vm->bytes[8100]!=0?156:0)+((int)item==b->choice_selected?52:0);
        unsigned native_state=b->choice_normal?choice_native_state(b,item):0;
        if(native_state>4){error(b,"CNormalSelect palette index outside native atlas");return;}
        unsigned color=b->choice_normal?choice_native_color(b,native_state):0,alpha=(unsigned)option(b,"SELECT","MIXED",112)&255;
        for(unsigned y=0;y<52;y++)for(unsigned x=0;x<496;x++){
            uint8_t native[4];
            if(b->choice_normal)choice_native_pixel(b,native_state,(int)item==b->choice_selected,x,y,1,color,alpha,native);
            const uint8_t *src=b->choice_normal?native:b->choice_parts.pixels+(sy+y)*b->choice_parts.stride+x*4;
            uint8_t *dst=b->layers[0].pixels+(top+i*52+y)*b->layers[0].stride+(72+x)*4;
            unsigned a=src[3];
            for(unsigned c=0;c<3;c++)dst[c]=(uint8_t)(src[c]*a/255+dst[c]*(255-a)/255);
            dst[3]=(uint8_t)(a+dst[3]*(255-a)/255);
        }
    }
    if(b->choice_normal)for(unsigned nav=0;nav<2;nav++){
        if(nav?page*4+4>=b->choice_count:!page)continue;
        int y0=top+(nav?208:-34);unsigned alpha=(unsigned)option(b,"SELECT","MIXED",112)&255;
        for(unsigned y=0;y<34;y++)for(unsigned x=0;x<496;x++){
            uint8_t src[4];choice_native_pixel(b,0,b->choice_page_hover==(int)nav+1,x,y,0,0,alpha,src);
            uint8_t *dst=b->layers[0].pixels+(y0+y)*b->layers[0].stride+(72+x)*4;
            for(unsigned c=0;c<3;c++)dst[c]=(uint8_t)(src[c]*src[3]/255+dst[c]*(255-src[3])/255);
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
        memcpy(eval,v,sizeof(*eval));
        /* This speculative label evaluator must not append through callbacks
           whose owner is the live runtime, including on rejected bodies. */
        eval->record_bytes=NULL;eval->record_newline=NULL;eval->record_owner=NULL;
        eval->stack=NULL;eval->stack_capacity=0;eval->raw=NULL;eval->raw_size=0;eval->module=list->items[i].module;eval->ip=list->items[i].ip;eval->sp=eval->depth=eval->script_depth=0;eval->status=KVM_READY;
        unsigned texts=0;KStatus state=KVM_READY;
        for(unsigned n=0;n<1000;n++){
            state=kvm_run(eval,1000);
            if(state==KVM_TEXT){labels[i]=eval->text;lengths[i]=eval->text_size;texts++;kvm_resume(eval);}
            else break;
        }
        /* Modules are borrowed, but evaluation owns its growable stack. */
        free(eval->stack);eval->stack=NULL;
        if(state!=KVM_YIELD||texts!=1||eval->globals[0][16].string||lengths[i]>60){free(eval);return error(b,"choice body unsupported");}
        b->choice_values[i]=eval->globals[0][16].number;
        b->choice_returns[i]=(int32_t)((uint32_t)list->items[i].value+1);
        if(b->choice_values[i]<0||2000u+(unsigned)b->choice_values[i]>=v->byte_count){free(eval);return error(b,"choice value out of range");}
        /* Actual bodies assign only return slot 16 and emit one text literal. */
        eval->globals[0][16]=v->globals[0][16];
        if(memcmp(eval->globals,v->globals,sizeof(v->globals))||memcmp(eval->bytes,v->bytes,sizeof(v->bytes))||memcmp(eval->words,v->words,sizeof(v->words))){free(eval);return error(b,"choice body side effects unsupported");}
    }
    free(eval);
    if(b->choice_normal){if(choice_initialize(b))return -1;}
    else{
        rmt_free(&b->choice_parts);uint8_t *data=NULL;size_t size=0;
        const char *parts=(v->bytes[8100]==1||v->bytes[8100]==3)?"selparts.rmt":"selparts2.rmt";
        if(read_named(&b->images,parts,&data,&size)||rmt_decode(data,size,&b->choice_parts)){free(data);return error(b,"choice parts load failed");}free(data);
        if(b->choice_parts.width<496||b->choice_parts.height<312)return error(b,"choice parts dimensions invalid");
    }
    KImage *images[]={&b->choice_text,&b->choice_base};
    for(unsigned i=0;i<2;i++){if(!images[i]->pixels){uint8_t *p=calloc(640*480,4);if(!p)return error(b,"choice surface allocation failed");*images[i]=(KImage){0,0,640,480,640*4,p};}}
    memcpy(b->choice_base.pixels,b->layers[0].pixels,640*480*4);memset(b->choice_text.pixels,0,640*480*4);
    b->choice_count=list->count;b->choice_selected=-1;b->choice_page=b->choice_page_hover=0;b->choice_rendered_page=~0u;
    for(unsigned i=0;i<list->count;i++){memcpy(b->choice_labels[i],labels[i],lengths[i]);b->choice_labels[i][lengths[i]]=0;b->choice_lengths[i]=(unsigned)lengths[i];}
    b->choice_active=1;choice_draw(b);return b->error[0]?-1:0;
}
static void draw_ax(const uint32_t d[7],unsigned cell,void *context){
    (void)cell;KBootstrap *b=context;
    /* Kisaku CAnimeManager: 4de390 defaults source=8/destination=0;
       4de760 copies RGB, 4de650 keys green and unwraps destination Y.
       The former layer5/sprite4/background2 compositor belonged to the
       reference executable and must not be used for Kisaku's AX files. */
    if(d[0]==2||d[0]==3)return; /* 4dd8a0 / 4dd890 */
    if(d[0]>3){error(b,"AX descriptor kind unsupported");return;}
    int32_t y=(int32_t)d[6];if(d[0]==1&&y>479)y-=480;
    int32_t q[9]={(int32_t)d[5],y,(int32_t)d[3],(int32_t)d[4],b->ax_destination,
                 (int32_t)d[1],(int32_t)d[2],8,d[0]==1?0xff00:0};
    blit_args(b,d[0]==1?1:0,q);
}
/* The manager returned by 41cc70 is initialized at 4b5661 with source
 * selector (0,9), overriding the constructor's layer 8 default. 5031b0
 * changes only the destination for its synchronous first-frame draw. */
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
                 (int32_t)d[1],(int32_t)d[2],9,d[0]==1?0xff00:0};
    if(blit_args(b,d[0]==1?1:0,q))return;
}
#include "ax_runtime.inc"
#include "mam_runtime.inc"
#include "bowling_runtime.inc"
#include "credits.inc"
#include "montage.inc"
#include "novel.inc"
#include "letter.inc"
#include "message_fade.inc"
#include "message_skin.inc"
#include "diary.inc"
#include "param_window.inc"
#include "param_change.h"
#include "title_initial.inc"
#include "../build/media_tables.h"
const char *bootstrap_character_image(unsigned role,unsigned item){
    return role<8&&item<9?kisaku_character_images[role][item]:NULL;
}
#include "distort.inc"
#include "movie.inc"

/* 4a00f0 owns the parameter window and the normal CAnimeManager together.
   Keep the window backing separate so a modal animation never permanently
   paints over the message/scene underneath it. */
static int param_animation_restore(KBootstrap *b){
    if(!b->param_animation_window||!b->param_backing.pixels)return 0;
    KImage *screen=&b->layers[0];
    if(!screen->pixels||screen->width<620||screen->height<452)return error(b,"CKisakuParamWnd screen bounds");
    unsigned height=b->param_rows==4?112:b->param_rows?30+29*(b->param_rows-1):0;
    for(unsigned y=0;y<height;y++)memcpy(screen->pixels+(340+y)*screen->stride+18*4,
        b->param_backing.pixels+y*b->param_backing.stride,b->param_backing.width*4);
    return 0;
}
static int param_animation_present(KBootstrap *b){
    if(!b->param_animation_window||!b->param_surface.pixels)return 0;
    KImage *screen=&b->layers[0];
    if(!screen->pixels||screen->width<620||screen->height<452)return error(b,"CKisakuParamWnd screen bounds");
    /* 49fc40 clips the 602x112 DIB to the native client height. */
    unsigned height=b->param_rows==4?112:b->param_rows?30+29*(b->param_rows-1):0;
    for(unsigned y=0;y<height;y++)memcpy(screen->pixels+(340+y)*screen->stride+18*4,
        b->param_surface.pixels+y*b->param_surface.stride,b->param_surface.width*4);
    return 0;
}
static int param_animation_track_reset(KBootstrap *b,unsigned cell){
    if(cell>=AX_CELLS||!b->ax.size)return error(b,"CKisakuParamWnd AX track range");
    struct ax_cell *c=&b->ax.cells[cell];uint32_t start=c->start;
    if(start<0x500||start>=b->ax.size)return error(b,"CKisakuParamWnd AX track start");
    memset(c,0,sizeof(*c));c->start=start;c->state=0;
    b->ax_events[cell]=0;return 0;
}
static int param_animation_commit(KBootstrap *b){
    int32_t values[4];for(unsigned i=0;i<4;i++)values[i]=b->param_animation_plan.target[i];
    if(param_window_values(b,values))return -1;
    b->param_total=b->param_animation_plan.total_target;
    if(param_window_rows(b,b->param_rows,2))return -1;
    return 0;
}
static int param_animation_apply(KBootstrap *b){
    int32_t values[4];int deltas[4]={0,0,0,0};for(unsigned i=0;i<4;i++)values[i]=b->param_values[i];
    for(unsigned i=0;i<4;i++){
        int before=(int)b->param_animation_accum[i];
        b->param_animation_accum[i]=(float)(b->param_animation_accum[i]+b->param_animation_plan.increment[i]);
        int after=(int)b->param_animation_accum[i];int delta=after-before;deltas[i]=delta;
        if(!delta)continue;
        values[i]+=b->param_animation_plan.target[i]>b->param_animation_plan.start[i]?delta:-delta;
    }
    if(param_window_values(b,values))return -1;
    if(b->param_values[3]!=values[3])return error(b,"CKisakuParamWnd animation value mismatch");
    /* The fourth row changes the total counter by exactly the applied delta. */
    unsigned target_total=b->param_total;
    if(deltas[3])target_total=(uint16_t)(target_total+(b->param_animation_plan.target[3]>b->param_animation_plan.start[3]?deltas[3]:-deltas[3]));
    b->param_total=(uint16_t)target_total;
    return param_window_rows(b,b->param_rows,2);
}
static void param_animation_stop_tracks(KBootstrap *b){
    b->ax.cells[0].state=AX_STOPPED;b->ax.cells[2].state=AX_STOPPED;b->ax.wait_cell=0;
    b->ax_events[0]=b->ax_events[2]=10;
}
static int param_animation_begin(KBootstrap *b,const int32_t encoded[4],int32_t duration,int chime,int start2){
    if(b->param_animation_active)return error(b,"CKisakuParamWnd animation already active");
    int32_t count=0;if(!ax_count_boundaries(&b->ax,0,&count))return error(b,"CKisakuParamWnd invalid AX boundary program (arguments preserved)");
    int32_t steps=count?count:duration;if(steps<0)steps=(int16_t)steps;
    if(kparam_change_plan(&b->param_animation_plan,b->param_values,b->param_total,steps,encoded))return error(b,"CKisakuParamWnd invalid initial parameters (arguments preserved)");
    if(!b->param_surface.pixels||!b->param_atlas.pixels||b->param_rows<1)return error(b,"CKisakuParamWnd window is not initialized (arguments preserved)");
    if(param_animation_track_reset(b,0))return -1;
    if(start2&&param_animation_track_reset(b,2))return -1;
    if(!b->param_backing.pixels){uint8_t *p=malloc(602*112*4);if(!p)return error(b,"CKisakuParamWnd backing allocation failed");b->param_backing=(KImage){18,340,602,112,602*4,p};}
    for(unsigned y=0;y<112;y++)memcpy(b->param_backing.pixels+y*b->param_backing.stride,b->layers[0].pixels+(340+y)*b->layers[0].stride+18*4,602*4);
    b->param_animation_window=b->vm->globals[1][61].number==0;b->param_animation_temporary=b->param_animation_window;
    b->param_animation_chime=chime!=0;b->param_animation_track2=start2!=0;
    b->param_animation_sungeki=option(b,"CONFIG","IsSungekiSE",1)!=0;b->param_animation_step=0;b->param_animation_count=0;
    b->param_animation_phase=b->param_animation_plan.steps?1:2;b->param_animation_active=1;b->param_animation_clock=0;b->param_animation_last_event=0;
    for(unsigned i=0;i<4;i++)b->param_animation_accum[i]=0;
    return param_animation_present(b);
}
static int param_animation_finish(KBootstrap *b,int skipped){
    if(param_animation_commit(b))return -1;
    param_animation_stop_tracks(b);
    if(b->param_animation_chime&&!skipped&&b->param_animation_phase!=3){
        if(effect_play(b,0,b->param_animation_sungeki?"ti-n.wav":"ti-n2.wav"))return -1;
        b->param_animation_phase=3;return 0;
    }
    int result=param_animation_restore(b);
    b->param_animation_phase=0;b->param_animation_active=0;b->param_animation_window=0;
    return result;
}
static int param_animation_frame(KBootstrap *b){
    if(!b->param_animation_active)return 0;
    if(b->param_animation_phase==1){
        uint8_t event=b->ax_events[0];
        if((event==2||event==5||event==10)&&b->param_animation_step<b->param_animation_plan.steps){
            if(param_animation_apply(b))return -1;
            b->param_animation_step++;
            if(b->param_animation_step>=b->param_animation_plan.steps)b->param_animation_phase=2;
        }
    }
    if(b->param_animation_phase==2){
        /* 4a00f0 waits for track 0 here. Track 2 is only waited on in the
           IsSungekiSE==0 branch; it is stopped together with track 0 after
           the normal branch, even when its script is still running. */
        if(b->ax.cells[0].state==AX_STOPPED&&
           (b->param_animation_sungeki||!b->param_animation_track2||b->ax.cells[2].state==AX_STOPPED))
            return param_animation_finish(b,0);
    }else if(b->param_animation_phase==3){
        const KEffectTrack *chime=&b->effect_tracks[0];
        /* PCM remains resident after playback for the output mixer. */
        if(!chime->pcm||(!chime->loop_end&&chime->clock_position>=chime->size))
            return param_animation_finish(b,0);
    }
    return param_animation_present(b);
}
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
#include "location_label.inc"
int bootstrap_dispatch(KBootstrap *b){
    KVM *v=b->vm;int32_t main=v->syscall,sub,a,c,d,e;
    if(v->status!=KVM_SYSCALL)return error(b,"VM is not at a syscall");
    /* Peek first: unsupported handlers preserve their arguments for diagnostics. */
    if(!v->sp||v->stack[v->sp-1].string)return error(b,"missing integer subcall");
    sub=v->stack[v->sp-1].number;
    if(main==23&&sub==8){
        /* 4fe060 -> 40b470 -> 406280 reads the slot vector count.
           4fe5f0 discards EAX: no extra operand, VM result or state change. */
        v->sp--;b->handled++;return kvm_resume(v);
    }
    if(main==23&&(sub==2||sub==3)){
        /* Validate and finish any allocation before consuming the selector. */
        if(sub==2){if(record_begin(b))return -1;}
        else if(v->globals[0][50].number&0x80){
            if(v->globals[0][50].number&0x100){uint8_t zero=0;if(record_append(b,&zero,1))return -1;}
            v->globals[0][50].number&=~0x100;
        }
        v->sp--;b->handled++;return kvm_resume(v);
    }
    if(main==23&&(sub==0||sub==1||sub==4||sub==6)){
        /* Kisaku 5061c0 case 0x17 -> 4fe5f0 (CFuncBackLog).
           Validate before consuming operands; queries replace their operands
           in-place so returning a value cannot fail after changing state. */
        unsigned operands=(sub==0||sub==6)?2:1;
        if(v->sp<operands||(operands==2&&v->stack[v->sp-2].string))
            return error(b,"backlog numeric operand required (arguments preserved)");
        int value=operands==2?v->stack[v->sp-2].number:0;
        if(sub==0){
            /* 500c10 appends count empty records, retaining the current index. */
            if(value<0||b->message_count>4096||(unsigned)value>4096-b->message_count)
                return error(b,"message record limit (arguments preserved)");
            if(value){
                KMessageRecord *p=realloc(b->messages,(b->message_count+(unsigned)value)*sizeof(*p));
                if(!p)return error(b,"message allocation failed (arguments preserved)");
                b->messages=p;memset(p+b->message_count,0,(size_t)value*sizeof(*p));b->message_count+=(unsigned)value;
            }
        }else if(sub==1){
            /* 500960 -> 40c550 clears commands, text AND voice flag. */
            b->message_index=-1;
            for(unsigned i=0;i<b->message_count;i++){
                KMessageRecord *r=&b->messages[i];r->size=0;r->flag=0;
                if(r->data)r->data[0]=0;
                if(r->text)r->text[0]=0;
            }
            /* The portable fallback must not resurrect entries explicitly
               cleared by the native backlog lifecycle. Restore metadata is
               applied after the script reconstruction, as before. */
            memset(b->history,0,sizeof(b->history));memset(b->history_voice,0,sizeof(b->history_voice));
            b->history_count=b->history_next=0;
        }else if(sub==4){
            /* 4fe3b0 -> 500900 counts nonempty command vectors, not slots. */
            value=0;
            for(unsigned i=0;i<b->message_count;i++)if(b->messages[i].size)value++;
        }else{
            /* 4fe270 -> 4fe0b0: invalid slot returns false, no dereference. */
            value=value>=0&&(unsigned)value<b->message_count&&b->messages[value].flag!=0;
        }
        v->sp-=operands;
        if(sub==4||sub==6)v->stack[v->sp++]=(KValue){value,NULL};
        b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==111){
        if(b->title_reset_modal||b->message_request)return error(b,"31/111 dialog already pending (arguments preserved)");
        b->title_reset_modal=1;b->message_request=21;
        v->sp--;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==3){
        /* 4fbdb0 -> 4b4060: no script arguments or return value. The
           frontend owns CDialog mode 0; the VM remains suspended until No. */
        if(b->quit_modal||b->message_request)return error(b,"31/3 dialog already pending (arguments preserved)");
        b->quit_modal=1;b->message_request=6;
        v->sp--;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==812){
        /* 4f9bb0: sys70 <-> byte3570..3593; the local 24-byte buffer is
           zeroed, and byte23 is explicitly terminated in both directions. */
        if(v->sp<2||v->stack[v->sp-2].string)return error(b,"31/812 numeric action required (arguments preserved)");
        int action=v->stack[v->sp-2].number;
        if(action<0||action>1)return error(b,"31/812 action unsupported (arguments preserved)");
        if(v->byte_count<3594||v->global_count[0]<=70)return error(b,"31/812 storage missing (arguments preserved)");
        char name[24]={0};
        if(!action){
            const char *src=v->globals[0][70].string;
            if(!src)return error(b,"31/812 sys70 must be a string (arguments preserved)");
            size_t length=0;while(length<24&&src[length])length++;
            /* 41cfa0 calls strcpy_s(dst,24,src), not a truncating copy. */
            if(length==24)return error(b,"31/812 scene name exceeds 23 bytes (arguments preserved)");
            memcpy(name,src,length);memcpy(v->bytes+3570,name,24);
        }else{
            memcpy(name,v->bytes+3570,24);name[23]=0;
            if(owned_value(b,&v->globals[0][70],(KValue){0,name}))return -1;
        }
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==41){
        /* 4f9680: snapshot layer 0 into layer 2, then CDIB+8c applies
           48b810 to layer 3. B=mean(B,G,R), G=R=min(mean+16,255).
           The snapshot copies Alpha; the tinted output preserves its Alpha. */
        KImage *src=surface(b,0),*copy=surface(b,2),*tint=surface(b,3);
        KImage *images[3]={src,copy,tint};
        for(unsigned i=0;i<3;i++)if(!images[i]||!images[i]->pixels||
            images[i]->width<640||images[i]->height<480||images[i]->stride<(size_t)images[i]->width*4)
            return error(b,"31/41 requires three 640x480 surfaces (arguments preserved)");
        for(unsigned y=0;y<480;y++)memcpy(copy->pixels+y*copy->stride,src->pixels+y*src->stride,640*4);
        for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
            const uint8_t *in=copy->pixels+y*copy->stride+x*4;
            uint8_t *out=tint->pixels+y*tint->stride+x*4;
            unsigned mean=((unsigned)in[0]+in[1]+in[2])/3;
            out[0]=(uint8_t)mean;out[1]=out[2]=(uint8_t)(mean>239?255:mean+16);
        }
        v->sp--;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==1012){
        /* 4f98b0 pops one string after the selector. 5040a0 uppercases
           it and queries the auxiliary map initialized by 503db0, NOT the
           reference game's bmptbl.dat gallery. A missing key returns zero
           (50419e); a match writes both native byte indices and returns one. */
        if(v->sp<2||!v->stack[v->sp-2].string)
            return error(b,"31/1012 media name required (arguments preserved)");
        const char *src=v->stack[v->sp-2].string;
        char name[1024];size_t n=0;
        for(;n<sizeof(name)&&src[n];n++){
            unsigned char ch=(unsigned char)src[n];
            name[n]=(char)(ch>='a'&&ch<='z'?ch-'a'+'A':ch);
        }
        if(n==sizeof(name))return error(b,"31/1012 media name too long (arguments preserved)");
        name[n]=0;
        const KMediaLink *link=kmedia_link(&b->media_tables,name);
        if(link){
            /* Validate every write before committing any of them. */
            if(v->byte_count<=4004||link->flag<0||(unsigned)link->flag>=v->byte_count||
               (link->related_flag!=-1&&(link->related_flag<0||(unsigned)link->related_flag>=v->byte_count)))
                return error(b,"31/1012 media flag bounds (arguments preserved)");
            v->bytes[link->flag]=1;
            if(link->related_flag!=-1)v->bytes[link->related_flag]=1;
            v->bytes[4001]=1;v->bytes[4004]=1;
        }
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==320){
        /* 4fae70 constructs CScMode/CHageScMode with no script arguments.
           The native modal returns its selected scene value through the VM
           stack; the frontend owns the input loop while scene_modal is set. */
        if(v->sp<1)return error(b,"31/320 subcall value required (arguments preserved)");
        b->scene_modal=1;b->scene_panel_request=3;b->scene_focus=0;
        v->sp--;b->handled++;return kvm_resume(v);
    }
    if(main==29&&(sub==0||sub==1)){
        /* 5061c0 case 0x1d -> CFuncWait 4f2970 -> 4f2900, NOT
           CFuncBackLog. Two operands: milliseconds, then update flag.
           4f2890 uses 4e2ee0 for Shift/Ctrl/script-skippable waits.
           The optional 4e3a20 application-menu pump is not yet portable. */
        if(v->sp<3||v->stack[v->sp-2].string||v->stack[v->sp-3].string)
            return error(b,"29 wait requires two numeric operands (arguments preserved)");
        int32_t duration=v->stack[v->sp-2].number;
        if(duration<0)
            return error(b,"29 negative native wait unsupported (arguments preserved)");
        if(duration&&v->stack[v->sp-3].number)
            return error(b,"29 application-menu pump unsupported (arguments preserved)");
        b->native_wait_clock=(uint64_t)duration*60;
        b->native_wait_skippable=(unsigned)sub;
        v->sp-=3;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==810){
        /* 4fd3f0 initializes the Japanese name-part object after name.mes
           has loaded namepart.akb.  The constructor owns transient UI state;
           expose the same CP932 name editor used by the title frontend and
           consume the action word without manufacturing a script result. */
        b->extra_active=1;b->extra_kind=b->extra_request=14;
        v->sp--;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==811){
        /* 4fd6c0 tears down the name-part helper created by 4fd3f0.  It has
           no return value or additional variants; the entered CP932 bytes
           remain in the VM name buffer for later 31/63 attachment. */
        b->extra_active=b->extra_request=b->extra_kind=0;
        v->sp--;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==43){
        if(v->sp<2||v->stack[v->sp-2].string)return error(b,"CMesFadeSprite mode required (arguments preserved)");
        int mode=v->stack[v->sp-2].number;
        unsigned count=mode==3?5:mode>=4&&mode<=6?3:2;
        if(v->sp<count)return error(b,"CMesFadeSprite missing parameters (arguments preserved)");
        for(unsigned i=2;i<count;i++)if(v->stack[v->sp-1-i].string)
            return error(b,"CMesFadeSprite numeric parameters required (arguments preserved)");
        if(message_fade_call(b,mode))return -1;
        v->sp-=count;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==110&&v->sp>=3&&!v->stack[v->sp-2].string&&!v->stack[v->sp-3].string&&
       (v->stack[v->sp-2].number==0||v->stack[v->sp-2].number==1)&&v->stack[v->sp-3].number>=0&&v->stack[v->sp-3].number<=3){
        if(v->stack[v->sp-2].number==0?title_initial(b,(unsigned)v->stack[v->sp-3].number):title_open_native(b,(unsigned)v->stack[v->sp-3].number))return -1;
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
    if(main==31&&sub==528&&v->sp>=2&&!v->stack[v->sp-2].string&&
       (v->stack[v->sp-2].number==20||v->stack[v->sp-2].number==21)){
        int action=v->stack[v->sp-2].number;unsigned needed=action==20?7:4;
        if(v->sp<needed||v->word_count<396)return error(b,"CDiaryWnd arguments/state missing (arguments preserved)");
        for(unsigned i=2;i<needed;i++)if(v->stack[v->sp-1-i].string)return error(b,"CDiaryWnd numeric values required (arguments preserved)");
        int index=v->stack[v->sp-3].number;
        if(index<0||index>=(action==20?24:96))return error(b,"CDiaryWnd record range (arguments preserved)");
        int32_t people[96],events[96];memcpy(people,b->diary_people,sizeof(people));memcpy(events,b->diary_events,sizeof(events));
        if(action==20)for(unsigned i=0;i<4;i++)people[index*4+i]=(uint16_t)v->stack[v->sp-4-i].number;
        else events[index]=(uint16_t)v->stack[v->sp-4].number;
        if(diary_draw(b,people,events,action==20?(unsigned)index+1:b->diary_days? (unsigned)b->diary_days:1))return -1;
        if(action==20)for(unsigned i=0;i<4;i++)v->words[200+index*4+i]=(uint16_t)people[index*4+i];
        else v->words[300+index]=(uint16_t)events[index];
        v->sp-=needed;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==528&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==9){
        /* 4fd950 case 528 -> 4fc080 case 9 -> 4a00f0 is an animated
           parameter update. 4fa9e0 belongs to 31/10, not this interface. */
        if(v->sp<9)return error(b,"CKisakuParamWnd animation requires seven parameters (arguments preserved)");
        for(unsigned i=3;i<=9;i++)if(v->stack[v->sp-i].string)
            return error(b,"CKisakuParamWnd animation requires numeric parameters (arguments preserved)");
        /* The VM pushes parameters in native call order and then sub/main:
           [start-track2, chime, encoded3..encoded0, duration, 9, 528]. */
        int32_t encoded[4];for(unsigned i=0;i<4;i++)encoded[i]=v->stack[v->sp-4-i].number;
        int32_t duration=(int16_t)(uint16_t)v->stack[v->sp-3].number;
        int chime=v->stack[v->sp-8].number,start2=v->stack[v->sp-9].number;
        if(param_animation_begin(b,encoded,duration,chime,start2))return -1;
        v->sp-=9;b->handled++;return kvm_resume(v);
    }
    /* 4fa6a0 -> 4ffef0/4ffe40/4ffdc0. Portrait setup queues TWO
       bank/cell pairs; action 3 starts them together, it does not stop them. */
    if(main==31&&sub==11&&v->sp>=2&&!v->stack[v->sp-2].string){
        int action=v->stack[v->sp-2].number;
        if(action<0||action>5)return error(b,"31/11 unknown action (arguments preserved)");
        unsigned count=action==0?1:action==1?3:(action==2||action==5)?2:0;
        if(v->sp<2+count)return error(b,"31/11 missing operands (arguments preserved)");
        if(action==0){
            const char *name=v->stack[v->sp-3].string;
            if(!name||strlen(name)>=sizeof(b->animation_name))return error(b,"31/11 context invalid (arguments preserved)");
            strcpy(b->animation_name,name);
        }else if(action==1||action==2||action==5){
            for(unsigned n=3;n<=2+count;n++)if(v->stack[v->sp-n].string)
                return error(b,"31/11 numeric operands required (arguments preserved)");
            int bank=v->stack[v->sp-3].number,cell=v->stack[v->sp-4].number;
            if(bank<0||bank>=10||cell<0||cell>=32)return error(b,"31/11 track range (arguments preserved)");
            unsigned index=(unsigned)bank*32+(unsigned)cell;
            struct ax_cell *track=&b->ax.cells[index];
            if(action!=5&&(track->start<0x500||track->start>=b->ax.size))
                return error(b,"31/11 track unavailable (arguments preserved)");
            if(action==5){
                int result=track->state==0?0:255;
                v->sp-=4;if(kvm_push(v,(KValue){result,NULL}))return error(b,"31/11 result stack allocation failed");
                b->handled++;return kvm_resume(v);
            }
            if(action==1){
                int target=v->stack[v->sp-5].number;
                KImage *dst=surface(b,target);
                if(!dst||!dst->pixels)return error(b,"31/11 destination invalid (arguments preserved)");
                uint32_t start=track->start;memset(track,0,sizeof(*track));track->start=start;track->state=AX_STOPPED;
                b->ax_destination=target;int ok=ax_first_frame(&b->ax,index,draw_ax,b);b->ax_destination=0;
                if(!ok||b->error[0])return error(b,"31/11 first frame invalid (arguments preserved)");
            }else{
                uint32_t start=track->start;memset(track,0,sizeof(*track));track->start=start;track->state=AX_STOPPED;
                if(b->portrait_tracks[0]<0)b->portrait_tracks[0]=(int)index;
                else if(b->portrait_tracks[1]<0)b->portrait_tracks[1]=(int)index;
            }
        }else if(action==3){
            for(unsigned n=0;n<2;n++)if(b->portrait_tracks[n]>=0){
                unsigned index=(unsigned)b->portrait_tracks[n];
                if(b->ax_registered[index]==UINT32_MAX)return error(b,"31/11 registration overflow (arguments preserved)");
            }
            for(unsigned n=0;n<2;n++)if(b->portrait_tracks[n]>=0){
                unsigned index=(unsigned)b->portrait_tracks[n];b->ax.cells[index].state=0;b->ax_registered[index]++;
            }
            b->portrait_tracks[0]=b->portrait_tracks[1]=-1;
        }else{
            b->animation_name[0]=0;b->portrait_tracks[0]=b->portrait_tracks[1]=-1;
        }
        v->sp-=2+count;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==612&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==0){
        if(v->sp<7)return error(b,"31/612 create requires five numeric values (arguments preserved)");
        int32_t args[5];
        for(unsigned i=0;i<5;i++){
            if(v->stack[v->sp-3-i].string)return error(b,"31/612 create values must be numeric (arguments preserved)");
            args[i]=v->stack[v->sp-3-i].number;
        }
        if(bowling_begin(b,args))return -1;
        v->sp-=7;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==612&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==1){
        if(bowling_second(b))return -1;
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==612&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==2){
        if(kbowling_release(&b->bowling,&b->current_bowling))return error(b,"CBowling resource ownership invalid (arguments preserved)");
        bowling_free(b);v->sp-=2;b->handled++;return kvm_resume(v);
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
        /* 4fcef0: mode 4 pops item first, then group; mode 5
           pops packed date first, then variant. */
        if(count==1)args[0]=v->stack[v->sp-3].number;
        else if(count==2){args[0]=v->stack[v->sp-3].number;args[1]=v->stack[v->sp-4].number;}
        if(animation522_draw(b,mode,args,(unsigned)count))return -1;
        v->sp-=2+(unsigned)count;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==523){
        /* 4fce50 dispatches the two Sungeki board renderers and then
           releases its temporary actor.  The renderer performs the same
           first layer copy immediately, then yields until all six waits finish. */
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
    if(main==31&&sub==525){
        if(v->sp<2||v->stack[v->sp-2].string)return error(b,"CLetter numeric action required (arguments preserved)");
        int action=v->stack[v->sp-2].number;
        if(action==1){
            if(v->sp<3||v->stack[v->sp-3].string)return error(b,"CLetter numeric read id required (arguments preserved)");
            if(letter_text_begin(b,v->stack[v->sp-3].number))return -1;
            v->sp-=3;
        }else {if(letter_call(b,action))return -1;v->sp-=2;}
        b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==524&&v->sp>=2&&!v->stack[v->sp-2].string){
        int action=v->stack[v->sp-2].number;
        if(action==29||action==30){
            /* 4f9eb0 -> 46c170/45c800 and 46c080/45c7d0:
               suspend/restore the independent week windows and date badge. */
            if(action==29){animation522_restore(b);overlay524_restore(b);koverlay_suspend(&b->overlays);}
            else koverlay_restore(&b->overlays);
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
    if(main==14&&sub==12){
        /* 4f7890 -> 4f91c0 -> 4f8d40 -> 503920: restore all flag
           banks and raw data, without changing the executing MES or stack. */
        if(v->sp<2||v->stack[v->sp-2].string||v->stack[v->sp-2].number<0||v->stack[v->sp-2].number>999)
            return error(b,"14/12 slot invalid (arguments preserved)");
        KFlags *f=kflags_read_slot(bootstrap_save_dir(b),0,(unsigned)v->stack[v->sp-2].number);
        if(!f||f->byte_count!=9192||f->word_count!=600||f->raw_count!=15000||f->counts[0]<51||f->counts[0]>8192||f->counts[1]!=100){
            kflags_free(f);return error(b,"14/12 full FLAG invalid (arguments preserved)");
        }
        for(unsigned bank=0;bank<2;bank++){
            memset(v->globals[bank],0,sizeof(v->globals[bank]));
            memcpy(v->globals[bank],f->globals[bank],f->counts[bank]*sizeof(KValue));v->global_count[bank]=f->counts[bank];
        }
        memcpy(v->bytes,f->bytes,f->byte_count);v->byte_count=f->byte_count;
        memcpy(v->words,f->words,f->word_count*sizeof(uint16_t));v->word_count=f->word_count;
        free(b->raw_variables);b->raw_variables=f->raw;b->raw_size=f->raw_count;f->raw=NULL;f->raw_count=0;
        v->raw=b->raw_variables;v->raw_size=b->raw_size;
        f->next=b->flag_files;b->flag_files=f;b->flag_file_count++;
        v->sp-=2;b->handled++;return kvm_resume(v);
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
    if(main==13&&sub>=1&&sub<=12){
        if(ax_script_command(b,sub))return -1;
        v->sp--;b->handled++;return kvm_resume(v);
    }
    if(main==13&&(sub<0||sub>12))return error(b,"AX script command unsupported (arguments preserved)");
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
            /* 5042d0: membership AND exact registration count. Repeated
               starts are duplicate registrations; stopped state alone is not
               evidence that a track was removed (4ddc00 / 4057c0). */
            uint64_t registered=0;
            for(unsigned i=0;i<AX_CELLS;i++)registered+=b->ax_registered[i];
            size_t cursor=0;const KMediaRecord *record;
            while((record=kmedia_find(&b->media_tables,b->media_background_name,&cursor))){
                unsigned conditions=0;int matches=1;
                for(;conditions<4&&record->conditions[conditions*2]!=-1;conditions++){
                    int bank=record->conditions[conditions*2],cell=record->conditions[conditions*2+1];
                    if(bank<0||bank>=10||cell<0||cell>=32||!b->ax_registered[bank*32+cell]){matches=0;break;}
                }
                if(!matches||registered!=conditions)continue;
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
    if(main==31&&sub==527){
        /* 4f9dc0 pops an action. 0/1 set application save/load bits;
           2 returns an IFlag copy of bank1[60]. The caller's preceding
           value remains on the stack (open.mes writes it to sys18). */
        if(v->sp<2||v->stack[v->sp-2].string)
            return error(b,"31/527 numeric action required (arguments preserved)");
        int action=v->stack[v->sp-2].number;
        if(action<0||action>2||b->file_modal||b->message_request)
            return error(b,"31/527 invalid action or pending menu (arguments preserved)");
        if(action==2){
            if(v->global_count[1]<=60)return error(b,"31/527 flag bank missing (arguments preserved)");
            v->sp-=2;if(kvm_push(v,v->globals[1][60]))return error(b,"31/527 return allocation failed");
        }else{
            b->exec_status|=action?8:16;b->file_modal=1;b->message_request=action?3:2;v->sp-=2;
        }
        b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==1020){
        /* 4f95d0 -> 4655a0/465580 deletes the two weekly INI sections. */
        void *previous=malloc(sizeof(b->settings));if(!previous)return error(b,"weekly settings allocation failed");
        memcpy(previous,b->settings,sizeof(b->settings));unsigned old_count=b->setting_count,next=0;
        for(unsigned i=0;i<old_count;i++)if(!equal(b->settings[i].section,"WEEK_DATA")&&!equal(b->settings[i].section,"WEEKEND_DATA"))
            b->settings[next++]=b->settings[i];
        b->setting_count=next;
        if(settings_save(b)){memcpy(b->settings,previous,sizeof(b->settings));b->setting_count=old_count;free(previous);return -1;}
        free(previous);v->sp--;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==1013){
        /* 4f97c0 changes the application menu enable state. */
        if(v->sp<2||v->stack[v->sp-2].string)return error(b,"31/1013 numeric menu state required (arguments preserved)");
        b->native_menu_enabled=v->stack[v->sp-2].number!=0;
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
            b->ax_extra_clock=0;b->ax_extra.wait_cell=0;b->ax_extra_modal=0;b->ax_extra_pause_remove=0;
            ax_unregister(b->ax_extra_registered,AX_CELLS);
            b->animation_track_selected=0;
            v->sp-=2;b->handled++;return kvm_resume(v);
        }
        if(command.number==10||command.number==11){
            /* 503480 -> 4de9a0 pauses all at a boundary; 503230 ->
               4de230 resumes only state 4, retaining cursor and delays. */
            if(command.number==11)for(unsigned i=0;i<AX_CELLS;i++)
                if(b->ax_extra.cells[i].state==4&&b->ax_extra_registered[i]==UINT32_MAX)
                    return error(b,"extended AX registration overflow (arguments preserved)");
            for(unsigned i=0;i<AX_CELLS;i++){
                struct ax_cell *c=&b->ax_extra.cells[i];
                if(command.number==10&&c->state==0)c->state=3;
                if(command.number==11&&c->state==4){c->state=0;b->ax_extra_registered[i]++;}
            }
            if(command.number==10){b->ax_extra_modal=2;b->ax_extra_pause_remove=AX_CELLS+1;}
            b->animation_track_selected=0;
            v->sp-=2;b->handled++;return kvm_resume(v);
        }
        if(command.number==5||command.number==8||command.number==9){
            /* 5034f0/5034b0/503260 -> 4deb80/4dea60/4de2e0.
               Action 8 waits for the whole manager, not just this track. */
            if(v->sp<4)return error(b,"31/520 wait track values required (arguments preserved)");
            if(v->stack[v->sp-3].string||v->stack[v->sp-4].string)
                return error(b,"31/520 wait track values must be numeric (arguments preserved)");
            int bank=v->stack[v->sp-3].number,cell=v->stack[v->sp-4].number;
            if(bank<0||bank>=10||cell<0||cell>=32)
                return error(b,"31/520 wait track range (arguments preserved)");
            unsigned index=(unsigned)bank*32u+(unsigned)cell;
            struct ax_cell *c=&b->ax_extra.cells[index];
            if(command.number==5){
                if(!ax_control(&b->ax_extra,1,(unsigned)bank,(unsigned)cell))
                    return error(b,"31/520 wait track unavailable (arguments preserved)");
                b->ax_extra.wait_cell=index+1;b->ax_extra_modal=1;
            }else if(command.number==8){
                if(c->state==0)c->state=3;
                b->ax_extra_modal=2;b->ax_extra_pause_remove=index+1;
            }else if(c->state==4){
                if(b->ax_extra_registered[index]==UINT32_MAX)
                    return error(b,"extended AX registration overflow (arguments preserved)");
                c->state=0;b->ax_extra_registered[index]++;
            }
            b->animation_track_bank=(unsigned)bank;b->animation_track_cell=(unsigned)cell;b->animation_track_selected=1;
            v->sp-=4;b->handled++;return kvm_resume(v);
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
            b->animation_track_selected=0;b->ax_extra_modal=0;b->ax_extra_pause_remove=0;
            memset(b->ax_extra_events,0,sizeof(b->ax_extra_events));
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
            int bank=v->stack[v->sp-3].number,cell=v->stack[v->sp-4].number;
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
            /* 4fee60 -> 4fed00 -> 5033a0 -> manager +0x18 ->
               4de350/405270 starts state 0. State 1 is action 3's
               pending stop at the next boundary, not normal playback. */
            if(v->sp<4)return error(b,"31/520 action 2 track values required (arguments preserved)");
            if(v->stack[v->sp-3].string||v->stack[v->sp-4].string)
                return error(b,"31/520 action 2 numeric track values required (arguments preserved)");
            int bank=v->stack[v->sp-3].number,cell=v->stack[v->sp-4].number;
            if(bank<0||bank>=10||cell<0||cell>=32)
                return error(b,"31/520 action 2 track range (arguments preserved)");
            unsigned index=(unsigned)bank*32u+(unsigned)cell;
            if(b->ax_extra_registered[index]==UINT32_MAX)
                return error(b,"extended AX registration overflow (arguments preserved)");
            if(!ax_control(&b->ax_extra,1,(unsigned)bank,(unsigned)cell))
                return error(b,"31/520 action 2 track unavailable (arguments preserved)");
            b->ax_extra_registered[index]++;
            b->animation_track_bank=(unsigned)bank;b->animation_track_cell=(unsigned)cell;b->animation_track_selected=1;
            v->sp-=4;b->handled++;return kvm_resume(v);
        }
        if(command.number==3||command.number==4){
            /* 4fec90/4fec20 consume the same (bank,cell) pair and call
               CAnimeManager +0x1c/+0x20: change the track state and remove
               one registration; an AX stream need not be loaded. */
            if(v->sp<4)return error(b,"31/520 action track values required (arguments preserved)");
            if(v->stack[v->sp-3].string||v->stack[v->sp-4].string)
                return error(b,"31/520 action track values must be numeric (arguments preserved)");
            int bank=v->stack[v->sp-3].number,cell=v->stack[v->sp-4].number;
            if(bank<0||bank>=10||cell<0||cell>=32)
                return error(b,"31/520 action track range (arguments preserved)");
            struct ax_cell *track=&b->ax_extra.cells[(unsigned)bank*32u+(unsigned)cell];
            track->state=command.number==3?1:AX_STOPPED;
            ax_unregister(b->ax_extra_registered,(unsigned)bank*32u+(unsigned)cell);
            b->animation_track_bank=(unsigned)bank;b->animation_track_cell=(unsigned)cell;b->animation_track_selected=1;
            v->sp-=4;b->handled++;return kvm_resume(v);
        }
        if(command.number==6){
            /* 5032e0 -> CAnimeManagerEX +0x24 (4df140): every track except
               the stopped sentinel requests a stop at its next boundary. */
            for(unsigned i=0;i<AX_CELLS;i++)
                if(b->ax_extra.cells[i].state!=AX_STOPPED)b->ax_extra.cells[i].state=1;
            ax_unregister(b->ax_extra_registered,AX_CELLS);
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
            int bank=v->stack[v->sp-3].number,cell=v->stack[v->sp-4].number;
            if(bank<0||bank>=10||cell<0||cell>=32)
                return error(b,"31/520 action 12 track range (arguments preserved)");
            int target=v->stack[v->sp-5].number;
            KImage *dst=surface(b,target);
            if(!dst||!dst->pixels)return error(b,"31/520 action 12 target missing (arguments preserved)");
            b->animation_target_layer=target;
            b->animation_track_bank=(unsigned)bank;b->animation_track_cell=(unsigned)cell;b->animation_track_selected=1;
            int drawn=ax_first_frame(&b->ax_extra,(unsigned)bank*32u+(unsigned)cell,draw_ax_extra,b);
            b->animation_target_layer=0; /* 5031b0 restores the screen selector. */
            if(!drawn)return error(b,"31/520 action 12 extended AX first frame invalid (arguments preserved)");
            if(b->error[0])return -1;
            v->sp-=5;b->handled++;return kvm_resume(v);
        }
        return error(b,"31/520 animation action unsupported (arguments preserved)");
    }
    if(main==31&&sub==526&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==1){
        /* 4fd915 calls 47ae90 directly: no argument after the action. */
        exec526_release(b);
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==526&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==2){
        if(exec526_location(b))return -1;
        v->sp-=2;b->handled++;return kvm_resume(v);
    }
    if(main==31&&sub==526&&v->sp>=2&&!v->stack[v->sp-2].string&&v->stack[v->sp-2].number==0){
        /* 4fd790 case 0 reads five numeric variants before 47b220.  Keep
           the native order [source-x, source-y, width, height, right-bytes]
           and do not consume anything until every value is validated. */
        if(v->sp<7)return error(b,"31/526 create arguments required (arguments preserved)");
        int q[5];for(unsigned i=0;i<5;i++){
            if(v->stack[v->sp-3-i].string)return error(b,"31/526 create arguments numeric (arguments preserved)");
            q[i]=v->stack[v->sp-3-i].number;
        }
        if(exec526_create(b,q[0],q[1],q[2],q[3],q[4]))return -1;
        v->sp-=7;b->handled++;return kvm_resume(v);
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
    if(!((main==31&&(sub<0||sub>67||(sub>=2&&sub<=9)||(sub>=31&&sub<=39)||(sub>=41&&sub<=59)||sub==61))||(main==31&&(sub==20||sub==22||sub==40))||(main==31&&sub==15)||(main==31&&sub==17)||(main==31&&sub==16)||(main==31&&sub==18)||(main==31&&sub==14)||(main==1&&sub==0)||(main==16&&(sub==1||sub==3||sub==5||sub==6||sub==7))||(main==31&&(sub==0||sub==1))||(main==30&&sub==0)||(main==17&&(sub==1||sub==6||sub==7))||voice_register||scene_register||(main==31&&sub==29)||(main==31&&(sub==65||sub==66||sub==67))||bonus54_menu||area_open||name_attach||bonus_credits||bonus_meter||offset_image||title_open||(main==31&&sub==19)||scene_ui||scene_export||scene_hide||(main==31&&(sub==25||sub==26||sub==27||sub==28||sub==30))||(main==31&&sub==523)||scene_flags||scene_restore||helper_reset||helper_present||helper_hide||effects_idle||message_hidden||scene_reset||(main==31&&sub==24)||animation_reset||message_timed||message_reset||(main==21&&(sub==0||sub==1||sub==4))||(main==13)||(main==24&&sub>=0&&sub<=6)||(main==28&&sub>=0&&sub<=11)||(main==23&&(sub==0||sub==1))||(main==27&&(sub>=0&&sub<=3))||(main==26&&(sub==0||sub==1))||(main==14&&(sub==0||sub==2||sub==3||sub==6||sub==11||sub==13))||((main>=15&&main<=18)&&sub==0)||((main>=15&&main<=17)&&sub==2)||(main==15&&(sub==1||sub==3||sub==5||sub==6))||((main==10||main==11)&&sub==0)||(main==19&&(sub>=0&&sub<=9))||(main==25&&sub>=0&&sub<=3)||(main==22&&sub>=0&&sub<=2))){
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
                /* The stage must hold the VM's whole byte area, not one particular
                   capacity. The startup `14/0` call declares the living story VM as
                   9192 bytes (a=9192, c=600, d=15000, e=100; see scene_replay.inc /
                   history_reset.inc), and that same entry accepts any
                   a <= sizeof(v->bytes). The copy used to be a fixed 8192, so
                   kflags_progress merged `byte_count` bytes (9192 here) out of a
                   stack buffer that ended at 8192: ~1000 bytes read past it.
                   Sizing it from the VM's own area keeps every capacity that entry
                   admits working; the explicit bound stays as a guard. */
                uint8_t bytes[sizeof(v->bytes)];if(v->byte_count>sizeof(bytes)){free(next);return error(b,"scene progress capacity");}
                memset(bytes,0,sizeof(bytes));memcpy(bytes,v->bytes,v->byte_count);
                memcpy(bytes+3000,next->visited,1000);memcpy(bytes+4500,next->status,370);memcpy(bytes+5000,next->flags,370);
                unsigned selector=v->bytes[8100];if(selector>3)selector=3;
                if(kflags_progress(bootstrap_save_dir(b),selector,bytes,v->byte_count)){free(next);return error(b,"scene progress save failed");}
                memcpy(v->bytes,bytes,v->byte_count);memcpy(v->words+11,next->counters,sizeof(next->counters));*b->scene=*next;free(next);
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
            if(b->message_revealing)message_copy_text(b);
            message_slide_begin(b,1);
        }
    }else if(scene_reset){
        if(integer(b,&a))return -1;
        /* 42cbe9/42cc0e set scene mode zero/one; 48f080 has no UI effects before
           navigation initialization (constructor 48cffd sets active=0). */
        b->scene_mode=a==4;
    }else if(main==31&&sub==24){
        if(!b->animation_data||!ax_load(&b->ax,b->loaded_animation,b->animation_data,b->animation_size))return error(b,"invalid AX data");
        /* The Japanese archive contains logo.wav; logo01/logo02 were names
           from the reference runtime and are absent from Kisaku's effect.arc. */
        if(play_pcm(b,&b->effects,"logo.wav"))return -1;
        b->logo_phase=1;b->ax_clock=0;
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
        /* Kisaku 4e2e00 reconstructs the outer script chain; active script starts
           at its restore preamble, then sub10 selects the checkpoint. */
        int ids[29];size_t offsets[29];unsigned count=0;
        for(unsigned i=0;i<b->control_count;i++){
            KControlRecord *r=&b->controls[i];if(r->type!=0xffff)continue;
            if(r->id!=count||r->count!=3||!r->values[0].string||r->values[1].string||r->values[2].string)return error(b,"saved script record invalid");
            char target[261];
            if(krestore_name(target,r->values[0].string))return error(b,"saved script name invalid");
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
            /* 4b6350 -> 466630 -> 464140: per-character Kisaku switches.
               The reference game's filename/gender heuristic is unrelated. */
            int character=kisaku_voice_character(name);
            if(character>=0)enabled=bootstrap_message_setting(b,KSET_CHARACTER_VOICE+(unsigned)character,0)!=0;
        }
        if(main==17)snprintf(b->message_voice_name,sizeof(b->message_voice_name),"%s",name);
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
        /* 4f60e0 -> 502c00 permits display layer0. install_image routes
           it through the active backing surface, including async loads. */
        if(a<0||(unsigned)a>b->layer_count||!b->layers[a].pixels)return error(b,"unallocated layer");
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
        if(flush_read_history(b))return -1;
    }else if(main==25&&sub==0){
        if(flush_read_history(b))return -1;
        if(integer(b,&a))return -1;
        if(a<0||a>134217728)return error(b,"read flags capacity limit");
        if(resize_bytes(&b->read_flags,&b->read_size,((size_t)a+7)/8))return error(b,"read flags allocation failed");
    }else if(main==25&&sub==2){
        /* Original onemes.dat remains read-only; each selector owns a port file. */
        if(!b->read_size||v->byte_count<=8100)return error(b,"read flag buffer/selector uninitialized");
        if(flush_read_history(b))return -1;
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
    /* Kisaku 5059b0: measurement counts encoded bytes without drawing or
       cursor writes; 505820 still records the input including its NUL. */
    if(v->globals[0][50].number&0x80000000u){
        if(record_text(b))return -1;
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
    if(b->novel_mode||b->letter_mode){
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
    if(record_text(b))return -1;
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
/* Full-page message checkpoints can begin a page containing several prompts. Rebuild
   preceding paragraphs through the real script before exposing the saved one. */
static int restore_message(KBootstrap *b){
    if(!b->restore_pending)return 0;
    if(b->choice_active)return error(b,"page restore encountered an unexpected choice");
    if(!b->message_active)return 0;
    if((b->restore_mode==2&&!b->letter_active)||(b->restore_mode!=2&&!b->novel_mode))
        return error(b,"page restore reached a different message mode");
    if(b->message_read_id==b->restore_read_id){b->restore_pending=0;return 0;}
    if(!b->restore_remaining)return error(b,"page restore target not reached");
    b->restore_remaining--;
    if(b->novel_transition){b->novel_step=255;novel_fade_frame(b);}
    bootstrap_confirm(b);return b->error[0]?-1:0;
}
static int bootstrap_run_inner(KBootstrap *b,unsigned budget){
    if(!b||b->error[0])return -1;
    b->vm->raw=b->raw_variables;b->vm->raw_size=b->raw_size;
    if(restore_message(b))return -1;
    history_restore_apply(b);
    if(b->scene_replay_finished)return 1;
    if(bootstrap_bowling_active(b)||b->quit_modal||b->quit_requested||b->exec523_active||b->exec522_motion||b->param_animation_active||b->mes_fade_transition||b->letter_transition||b->load_modal||b->file_modal||b->title_reset_modal||b->scene_modal||ax_modal_wait(b)||b->montage_active||b->credits_active||b->area_active||b->bonus52_active||b->extra_active||b->image_loading||b->scroll_active||b->blink_active||b->distort_count||b->novel_transition||b->choice_active||b->message_active||b->message_slide||b->flag_dialog.active||b->title.active||b->transition_steps||b->exec_wipe_active||b->helper_steps||b->fade_steps||b->logo_phase||b->native_wait_clock||b->wait_clock||b->wait_input||b->video_wait||b->video_change_wait||(b->video_active&&!b->video_background))return 1;
    while(budget--){b->vm->raw=b->raw_variables;b->vm->raw_size=b->raw_size;int old_module=b->vm->module;unsigned old_scripts=b->vm->script_depth;KStatus s=kvm_run(b->vm,1);
        /* Native 408060 notifies navigation on a script return, but library
           function calls only change the VM instruction source. */
        if(b->vm->script_depth<old_scripts&&scene_transition(b,b->vm->modules[old_module].name,b->vm->modules[b->vm->module].name))return -1;
        if(s==KVM_SYSCALL){if(bootstrap_dispatch(b))return -1;b->vm->raw=b->raw_variables;b->vm->raw_size=b->raw_size;if(restore_message(b))return -1;history_restore_apply(b);if(b->scene_replay_finished)return 1;if(bootstrap_bowling_active(b)||b->quit_modal||b->quit_requested||b->exec523_active||b->exec522_motion||b->param_animation_active||b->mes_fade_transition||b->letter_transition||b->load_modal||b->file_modal||b->title_reset_modal||b->scene_modal||ax_modal_wait(b)||b->montage_active||b->credits_active||b->area_active||b->bonus52_active||b->extra_active||b->image_loading||b->scroll_active||b->blink_active||b->distort_count||b->novel_transition||b->choice_active||b->message_active||b->message_slide||b->flag_dialog.active||b->title.active||b->transition_steps||b->exec_wipe_active||b->helper_steps||b->fade_steps||b->logo_phase||b->native_wait_clock||b->wait_clock||b->wait_input||b->video_wait||b->video_change_wait||(b->video_active&&!b->video_background))return 1;}
        else if(s==KVM_TEXT){if(draw_text(b))return -1;}
        else if(s==KVM_BUDGET)kvm_resume(b->vm);
        else if(s==KVM_ERROR)return error(b,b->vm->error);
        else if(s==KVM_DONE||s==KVM_YIELD)return 0;
    }return error(b,"instruction budget exhausted");
}

int bootstrap_run(KBootstrap *b,unsigned budget){
    if(!b)return -1;
    exec526_restore(b);animation522_restore(b);overlay524_restore(b);message_fade_restore(b);param_animation_restore(b);
    int result=bootstrap_run_inner(b,budget);
    animation522_restore(b);message_fade_present(b);overlay524_present(b);animation522_present(b);param_animation_present(b);exec526_present(b);bowling_present(b);
    return result;
}
void bootstrap_frame(KBootstrap *b){
    if(b->quit_modal||b->quit_requested)return;
    b->frames++;if(b->frames>b->input_event_until)b->input_events=0;
    exec526_restore(b);animation522_begin_frame(b);overlay524_restore(b);message_fade_restore(b);param_animation_restore(b);
    if(b->image_loading){
        KImage im={0};int ready=kimage_worker_poll(b->image_worker,&im);
        if(ready){
            b->image_loading=0;
            if(ready<0){rmt_free(&im);error(b,"background RMT load failed");return;}
            im.x+=b->image_offset_x;im.y+=b->image_offset_y;int rc=install_image(b,b->image_layer,&im,b->image_name);rmt_free(&im);if(rc)return;
        }
    }
    if(b->mes_fade_transition)message_fade_frame(b);
    if(b->letter_transition)letter_frame(b);
    if(b->novel_transition)novel_frame(b);
    if(b->voice_loading){
        uint8_t *pcm=NULL;size_t size=0;int ready=kvoice_worker_poll(b->voice_worker,&pcm,&size);
        if(ready){b->voice_loading=0;if(ready<0){free(pcm);error(b,"background voice decode failed");return;}
            free(b->voice_pcm);b->voice_pcm=pcm;b->voice_size=size;b->voice_read_cursor=b->voice_clock_cursor=0;b->voice_clock=0;b->voice_active=1;
        }
    }
    if(b->native_wait_clock){
        unsigned flags=(unsigned)b->vm->globals[0][50].number;
        int script_skip=(flags&0x8000)&&!(b->vm->byte_count>4012&&b->vm->bytes[4012]==1);
        int key_skip=b->effect_fast&&!(flags&0x4000);
        if(b->native_wait_skippable&&(script_skip||key_skip))b->native_wait_clock=0;
        else b->native_wait_clock=b->native_wait_clock>1000?b->native_wait_clock-1000:0;
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
    for(unsigned i=0;i<92;i++){
        if(i==64&&b->video_paused)continue;
        KEffectTrack *t=i>=65?&b->bowling_effects[i-65]:i==64?&b->movie_effect:&b->effect_tracks[i];if(!t->pcm)continue;
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
        if(play_pcm(b,&b->effects,"potapota.wav")||start_logo_track(b,0))return;
        b->logo_phase=2;
    }else if(b->logo_phase>=2){
        b->ax_clock+=1000;
        while(b->ax_clock>=60*AX_TICK_MS){
            b->ax_clock-=60*AX_TICK_MS;
            if(!ax_tick_native(&b->ax,b->ax_events,draw_ax,b)){error(b,"invalid AX instruction");return;}
        }
        if(!ax_waiting(&b->ax)){
            if(b->logo_phase==2){if(start_logo_track(b,1))return;b->logo_phase=3;}
            else b->logo_phase=0;
        }
    }
    if(!b->logo_phase&&b->ax.size&&(b->vm->globals[0][50].number&0x10)){
        b->ax_clock+=1000;
        while(b->ax_clock>=60*AX_TICK_MS){b->ax_clock-=60*AX_TICK_MS;if(!ax_tick_native(&b->ax,b->ax_events,draw_ax,b)){error(b,"invalid story AX instruction");return;}if(b->error[0])return;}
        for(unsigned i=0;i<AX_CELLS;i++)b->animation_status[i]=(uint8_t)b->ax.cells[i].state;
    }
    /* CAnimeManagerEX::virtual_4 (4dd5a0) ticks its independent 320-track
       manager on the same 20 ms cadence.  Keep it separate from the normal
       story AX clock: stopping/reloading one manager must not phase-shift the
       other. */
    if(b->ax_extra.size&&(b->vm->globals[0][50].number&0x10)){
        b->ax_extra_clock+=1000;
        while(b->ax_extra_clock>=60*AX_TICK_MS){
            b->ax_extra_clock-=60*AX_TICK_MS;
            if(!ax_tick_native(&b->ax_extra,b->ax_extra_events,draw_ax_extra,b)){error(b,"invalid extended AX instruction");return;}
            if(b->error[0])return;
        }
    }
    bowling_frame(b);
    animation522_frame(b);
    if(animation523_frame(b))return;
    if(param_animation_frame(b))return;
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
    /* 484a48 sets an absolute auto deadline before opening/revealing the
       message. Typing and window motion are part of that interval. */
    if(b->message_active&&option(b,"Msg","IsAutoMes",0))b->message_auto_clock+=1000;
    if(b->message_visible){
        if(b->message_slide){
            b->message_slide_clock+=1000;
            while(b->message_slide&&b->message_slide_clock>=900){
                b->message_slide_clock-=900;
                if(++b->message_slide_frame>=b->message_slide-1)message_slide_finish(b);
            }
        }
        if(b->message_visible){
            message_buttons_frame(b);
            if(b->message_voice_pending&&!b->message_slide){b->message_voice_pending=0;if(voice_play(b))return;}
            if(b->message_active&&b->message_revealing&&!b->message_slide&&!b->message_buttons_motion){
                b->message_clock+=1000;
                if(b->message_clock>=60*b->message_delay){
                    int x=b->message_reveal_x,y=b->message_reveal_y,end=y==b->message_end_y?b->message_end_x:608;
                    if(x<end){
                        b->message_clock=0;
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
    if(!bootstrap_bowling_active(b)&&!b->letter_active&&!b->restore_pending&&b->message_active&&!b->message_user_hidden&&!b->message_slide&&!b->message_buttons_motion&&!b->novel_transition&&!b->message_request){
        if(b->message_timed&&!b->message_revealing&&!b->message_buttons_motion){
            b->message_timed_clock+=1000;
            if(b->message_timed_clock>=(uint64_t)b->message_timed_delay*60)bootstrap_confirm(b);
        }else if(b->force_skip||(option(b,"Msg","IsOneMes",0)&&b->message_was_read)){
            message_skip_voice(b);bootstrap_confirm(b);
        }else if(option(b,"Msg","IsAutoMes",0)){
            if(!b->message_revealing&&!b->message_buttons_motion&&!b->voice_loading&&!b->voice_active&&
               (b->message_had_voice||b->message_auto_clock>=(uint64_t)b->message_auto_delay*60))bootstrap_confirm(b);
        }
    }
    letter_text_frame(b);
    if(b->bonus52_active)bonus52_frame(b);
    if(b->credits_active)credits_frame(b);
    if(b->montage_active)montage_frame(b);
    message_fade_present(b);overlay524_present(b);animation522_present(b);param_animation_present(b);exec526_present(b);bowling_present(b);
    if(!b->fade_steps)return;
    b->fade_frame++;
    int delta=(int)b->fade_to-(int)b->fade_from;
    b->fade_alpha=(unsigned)((int)b->fade_from+delta*(int)b->fade_frame/(int)b->fade_steps);
    if(b->fade_frame>=b->fade_steps){b->fade_steps=0;b->fade_alpha=b->fade_to;if(b->fade_hide)b->fade_visible=0;}
}

#include "history_reset.inc"

void bootstrap_confirm(KBootstrap *b){
    if(b&&b->native_wait_clock)return;
    if(bootstrap_bowling_active(b)){bowling_confirm(b);return;}
    if(b&&(b->param_animation_active||b->exec523_active||b->exec522_motion))return;
    if(b&&(b->quit_modal||b->quit_requested))return;
    if(b->letter_transition||b->letter_exit_pending)return;
    if(b->letter_active){if(b->message_user_hidden)letter_text_hide(b,0);else letter_text_confirm(b);return;}
    if(b->message_active&&b->message_user_hidden){bootstrap_message_hide(b,0);return;}
    b->input_events|=1;b->input_event_until=b->frames+3;
    if(b->mes_fade_transition&&!(b->vm->globals[0][50].number&0x4000)){
        exec526_restore(b);animation522_restore(b);overlay524_restore(b);message_fade_restore(b);message_fade_finish(b);
        message_fade_present(b);overlay524_present(b);animation522_present(b);exec526_present(b);return;
    }
    if(message_slide_skip(b)||ax_modal_skip(b,0))return;
    if(b->credits_active){credits_finish(b,1);return;}
    if(b->area_active){area_finish(b,0);return;}
    if(b->bonus52_active){if(b->bonus52_active==1)b->bonus52_motion+=240;return;}
    if(b->choice_active){
        /* 47dfbe returns registered item value + 1, distinct from read-history ID. */
        if(b->choice_selected<0||(unsigned)b->choice_selected>=b->choice_count||!choice_enabled(b,(unsigned)b->choice_selected))return;
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
    if(b&&b->native_wait_clock)return;
    if(b&&b->letter_active){if(!hidden)b->letter_backlog=0;letter_text_hide(b,hidden);return;}
    if(!b||!b->message_active)return;
    b->message_user_hidden=hidden!=0;
    if(b->novel_mode){const KImage *src=hidden?&b->novel_background:&b->novel_target;if(src->pixels)memcpy(b->layers[0].pixels,src->pixels,640*480*4);}
    else if(b->message_base.pixels)message_compose(b);
}
void bootstrap_cancel(KBootstrap *b){
    if(b&&b->native_wait_clock)return;
    if(bootstrap_bowling_active(b))return;
    if(b&&(b->param_animation_active||b->exec523_active||b->exec522_motion))return;
    if(b&&(b->quit_modal||b->quit_requested))return;
    if(b->letter_transition||b->letter_exit_pending)return;
    if(b->letter_active){
        if(b->message_user_hidden&&b->vm->byte_count>4010&&b->vm->bytes[4010]){
            b->letter_exit_pending=1;b->message_request=19;return;
        }
        letter_text_hide(b,!b->message_user_hidden);return;
    }
    if(message_slide_skip(b)||ax_modal_skip(b,1))return;
    if(b->credits_active){credits_finish(b,1);return;}
    b->input_events|=2;b->input_event_until=b->frames+3;
    if(b->area_active){area_finish(b,1);return;}
    if(b->title.active&&b->title.extra){b->title.extra=0;b->title.count=b->title.main_count;b->title.selected=0;return;}
    if(b->message_active){setting_put(b,"Msg","IsAutoMes","0");setting_put(b,"Msg","IsOneMes","0");b->message_open=0;bootstrap_message_hide(b,1);}
    if(b->flag_dialog.active){b->flag_dialog.selected=2;bootstrap_confirm(b);}
}
void bootstrap_menu_move(KBootstrap *b,int dx,int dy){
    if(b&&b->native_wait_clock)return;
    if(b&&(b->quit_modal||b->quit_requested))return;
    if(b->area_active){area_move(b,dy?dy:dx);return;}
    if(b->bonus52_active){if(dx||dy)b->bonus52_motion+=100;return;}
    if(b->message_active&&b->message_buttons_motion)return;
    if(b->message_active&&b->message_open&&dx){
        int item=0;for(;item<6;item++)if(b->message_hover==(int)message_actions[item])break;
        /* The atlas sprites run right to left; controller movement follows X. */
        item=item==6?(dx>0?5:0):(item+(dx>0?5:1))%6;
        b->message_hover=(int)message_actions[item];return;
    }
    if(b->flag_dialog.active){kflag_dialog_move(&b->flag_dialog,dx,dy);return;}
    if(dy)bootstrap_title_move(b,dy);
}
void bootstrap_title_move(KBootstrap *b,int delta){
    if(b&&b->native_wait_clock)return;
    if(b->area_active){area_move(b,delta);return;}
    if(b->bonus52_active){if(delta)b->bonus52_motion+=100;return;}
    if(b->choice_active){if(!delta)return;int next=b->choice_selected<0?(delta<0?(int)b->choice_count-1:0):(b->choice_selected+delta)%(int)b->choice_count;if(next<0)next+=(int)b->choice_count;b->choice_selected=next;choice_draw(b);return;}
    if(b->flag_dialog.active){kflag_dialog_move(&b->flag_dialog,0,delta);return;}
    if(!b->title.active||b->title.age<64||!delta)return;
    if(b->title.selected<0)b->title.selected=0;
    else b->title.selected=(b->title.selected+(delta>0?1:(int)b->title.count-1))%(int)b->title.count;
}
void bootstrap_pointer(KBootstrap *b,int x,int y,int click){
    if(b&&b->native_wait_clock)return;
    if(bootstrap_bowling_active(b)){bootstrap_bowling_pointer(b,x,y,click!=0);return;}
    if(b&&(b->param_animation_active||b->exec523_active||b->exec522_motion))return;
    if(b&&(b->quit_modal||b->quit_requested))return;
    if(b->letter_active||b->letter_transition){if(click)bootstrap_confirm(b);return;}
    if(b->area_active){b->area_x=x;b->area_y=y;b->area_selected=area_hit(b,x,y);if(click)area_finish(b,0);return;}
    if(b->bonus52_active){
        if(b->bonus52_pointer_valid){int dx=abs(x-b->bonus52_x),dy=abs(y-b->bonus52_y);b->bonus52_motion+=(dx>50?50:dx)+(dy>50?50:dy);}
        b->bonus52_x=x;b->bonus52_y=y;b->bonus52_pointer_valid=1;return;
    }
    if(b->choice_active){
        unsigned page=b->choice_selected<0?b->choice_page:(unsigned)b->choice_selected/4,count=b->choice_count-page*4;if(count>4)count=4;
        int top=choice_top(b,count);
        if(b->choice_normal){
            /* 4f0910: only the inner 480x36 (navigation: 480x18) is hot. */
            b->choice_selected=-1;b->choice_page=page;b->choice_page_hover=0;
            if(x>=80&&x<560){
                if(page&&y>=top-26&&y<top-8)b->choice_page_hover=1;
                else if(page*4+4<b->choice_count&&y>=top+216&&y<top+234)b->choice_page_hover=2;
                else if(y>=top+8){int row=(y-top-8)/52;
                    if(row<(int)count&&(y-top-8)%52<36)b->choice_selected=(int)page*4+row;
                }
            }
            if(click&&b->choice_page_hover){b->choice_page+=b->choice_page_hover==1?-1:1;b->choice_page_hover=0;}
            choice_draw(b);if(click&&b->choice_selected>=0)bootstrap_confirm(b);
        }
        else if(x>=72&&x<568&&y>=top&&y<top+(int)count*52){b->choice_selected=(int)page*4+(y-top)/52;choice_draw(b);if(click)bootstrap_confirm(b);}
        else if(click&&b->choice_count>4&&y>=440&&y<480&&x>=224&&x<416){
            unsigned pages=(b->choice_count+3)/4;page=x<320?(page+pages-1)%pages:(page+1)%pages;b->choice_selected=(int)page*4;choice_draw(b);
        }
        return;
    }
    if(b->flag_dialog.active){b->flag_dialog.selected=kflag_dialog_hit(&b->flag_dialog,x,y);if(click)bootstrap_confirm(b);return;}
    if(b->message_active){
        if(b->message_buttons_motion||b->message_slide){if(click)bootstrap_confirm(b);return;}
        b->message_hover=-1;
        if(y>=464&&y<480)for(unsigned item=0;item<6;item++){
            if(!b->message_open&&item>0&&item<5)continue;
            KImage *button=&b->message_skin.buttons[item];
            if(x>=button->x&&x<button->x+(int)button->width){b->message_hover=(int)message_actions[item];break;}
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
    double gain=1;
    if(b->music_active){int enabled;int db=music_volume_db(b,&enabled);gain=enabled?db_gain(db)*b->music_gain:0;}
    else if(b->voice_active)gain=voice_output_gain(b);
    if(gain!=1){
        for(size_t i=0;i+1<total;i+=2){int16_t v=(int16_t)le16(out+i);int16_t q=(int16_t)(v*gain);out[i]=(uint8_t)q;out[i+1]=(uint8_t)((uint16_t)q>>8);}
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
        add_samples(out,b->voice_pcm+b->voice_read_cursor,n,voice_output_gain(b));b->voice_read_cursor+=n;if(n>count)count=n;
    }
    if(!b->video_paused&&b->movie_pcm&&b->movie_read<b->movie_pcm_size){
        size_t n=b->movie_pcm_size-b->movie_read;if(n>capacity)n=capacity;n-=n%4;
        add_samples(out,b->movie_pcm+b->movie_read,n,1);b->movie_read+=n;if(n>count)count=n;
    }
    for(unsigned i=0;i<92;i++){
        if(i==64&&b->video_paused)continue;
        KEffectTrack *t=i>=65?&b->bowling_effects[i-65]:i==64?&b->movie_effect:&b->effect_tracks[i];size_t total=0;
        if(!t->pcm)continue;
        int scene_sound=i==3||i==64,enabled=option(b,"Effect",scene_sound?"IsHEffect":"IsEffect",1);
        double gain=enabled?db_gain(kisaku_sound_volume_db(option(b,"Effect",scene_sound?"HVolume":"Volume",83),1))*db_gain(-t->fade_attenuation):0;
        while(t->pcm&&total<capacity){
            size_t end=t->loop_end?t->loop_end:t->size;
            if(t->read>=end){if(!t->loop_end)break;t->read=t->loop_start;}
            size_t n=end-t->read;if(n>capacity-total)n=capacity-total;
            add_samples(out+total,t->pcm+t->read,n,gain);total+=n;t->read+=n;
        }
        if(total>count)count=total;
    }
    return count;
}

#include "save_runtime.inc"
#include "scene_replay.inc"

int bootstrap_title_reset_close(KBootstrap *b,int accept){
    if(!b||!b->title_reset_modal)return -1;
    if(kvm_push(b->vm,(KValue){accept!=0,NULL}))return -1;
    if(accept){
        /* The original script runs FLAGINI/HAGE_FLAGINI after Yes. Commit
           tombstones for this mode's portable checkpoint indices as well;
           otherwise those newer files would resurrect the initialized saves.
           No source-game files or the other mode's checkpoints are touched. */
        unsigned selector=b->vm->globals[1][61].number==1;
        KResetFile files[100];char names[100][64];
        static const char empty[]="KISAKU-SLOT-1 0\n";
        for(unsigned i=0;i<100;i++){
            snprintf(names[i],sizeof(names[i]),"kisaku-slot-%u-%03u.index",selector,i+1);
            files[i]=(KResetFile){names[i],empty,sizeof(empty)-1};
        }
        b->reset_pending=1;
        if(kreset_prepare(bootstrap_save_dir(b),files,100)||kreset_recover(bootstrap_save_dir(b))){b->vm->sp--;return -1;}
        b->reset_pending=0;
    }
    b->title_reset_modal=0;return 0;
}
int bootstrap_quit_dialog_close(KBootstrap *b,int accept){
    if(!b)return -1;
    if(accept){
        if(bootstrap_flush_progress(b))return -1;
        b->quit_requested=1;
    }
    b->quit_modal=0;
    if(b->message_request==6)b->message_request=0;
    return 0;
}

void bootstrap_message_action(KBootstrap *b,unsigned action){
    if(b&&b->native_wait_clock)return;
    if(!b||bootstrap_bowling_active(b)||b->param_animation_active||b->exec523_active||b->exec522_motion)return;
    if(b&&(b->quit_modal||b->quit_requested))return;
    if(!b||!b->message_active||b->letter_transition||b->letter_exit_pending||b->message_slide||b->message_buttons_motion)return;
    if(b->letter_active&&(action==4||action==7))return;
    if(b->letter_active&&action==5){
        if(b->message_user_hidden||!b->history_count)return;
        b->letter_backlog=1;letter_text_hide(b,1);return;
    }
    if(action<=1){
        const char *key=action?"IsOneMes":"IsAutoMes";int enabled=!option(b,"Msg",key,0);
        if(setting_put(b,"Msg",key,enabled?"1":"0"))return;
        if(enabled)setting_put(b,"Msg",action?"IsAutoMes":"IsOneMes","0");
        b->message_auto_clock=0;
        if(action&&enabled&&!b->message_was_read)setting_put(b,"Msg",key,"0");
        settings_save(b);
    }else if(action==7){
        if(b->message_open&&option(b,"Msg","IsOneMes",0))return;
        b->message_open=!b->message_open;b->message_hover=-1;message_buttons_begin(b);
        if(setting_put(b,"Msg","EnableOpen",b->message_open?"1":"0"))return;
        settings_save(b);
    }
    else if(action==9)b->vm->globals[0][50].number^=0x8000;
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
    int enabled=1;b->music_db=music_volume_db(b,&enabled);b->music_enabled=enabled;b->music_gain=1;b->music_active=1;b->music_fading=0;
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
    if(!b||!b->extra_active||b->extra_kind!=14)return -1;
    if(!utf8){b->extra_active=b->extra_request=b->extra_kind=0;return 0;}
    uint8_t name[33];size_t size;if(ktext_name_encode(utf8,name,&size))return -1;
    memcpy(b->vm->bytes+1950,name,size+1);
    b->extra_active=b->extra_request=b->extra_kind=0;
    if(b->title.variant==2){b->title.active=0;return kvm_push(b->vm,(KValue){0,NULL});}
    return owned_value(b,&b->vm->globals[0][70],(KValue){1,(const char *)name});
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
