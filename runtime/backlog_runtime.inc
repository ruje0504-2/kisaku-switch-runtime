static unsigned backlog_native_count(const KBootstrap *b){
    unsigned count=0;for(unsigned i=0;i<b->message_count;i++)if(b->messages[i].size)count++;return count;
}
/* Old portable saves have only flattened history. Keep that history until
   native records cover at least the same number of entries. */
static int backlog_use_native(const KBootstrap *b){unsigned n=backlog_native_count(b);return n&&n>=b->history_count;}
unsigned bootstrap_backlog_count(const KBootstrap *b){return b?(backlog_use_native(b)?backlog_native_count(b):b->history_count):0;}
static const KMessageRecord *backlog_native_record(const KBootstrap *b,unsigned back){
    for(unsigned i=b->message_count;i;i--)if(b->messages[i-1].size){if(!back)return &b->messages[i-1];back--;}
    return NULL;
}
int bootstrap_backlog_has_voice(const KBootstrap *b,unsigned back){
    if(!b)return 0;
    if(!backlog_use_native(b))return bootstrap_history_voice(b,back)!=NULL;
    const KMessageRecord *r=backlog_native_record(b,back);return r&&r->flag;
}
typedef struct {KBootstrap *b;KImage *row;} KBacklogRender;
static int backlog_replay_text(void *owner,KVM *v,unsigned width,unsigned height){
    KBacklogRender *r=owner;KTextChar chars[4096];KTextPosition positions[4096];size_t count=0;
    if(v->text_size>4096||bootstrap_decode_ui_text(r->b,v->text,v->text_size,chars,4096,&count))return -1;
    int x=v->globals[0][46].number,y=v->globals[0][47].number;
    int left=v->globals[0][42].number,top=v->globals[0][43].number,right=v->globals[0][44].number,bottom=v->globals[0][45].number;
    int advance=v->globals[0][30].number/2,line=v->globals[0][31].number;
    if(left<0||top<0||right>640||bottom>54||advance<1||advance>128||line<1||line>256)return -1;
    if(ktext_layout_native(chars,count,left,top,right,bottom,advance,line,&x,&y,positions))return -1;
    if(r->row)for(size_t i=0;i<count;i++)if(kfont_draw(r->b->font,r->row,chars[i].codepoint,positions[i].x,positions[i].y,width,height,(uint32_t)v->globals[0][33].number))return -1;
    v->globals[0][46]=(KValue){x,NULL};v->globals[0][47]=(KValue){y,NULL};return 0;
}
typedef struct {KBacklogRender render;size_t target_ip;} KBacklogSequenceRender;
static int backlog_replay_sequence_text(void *owner,KVM *v,unsigned width,unsigned height){
    KBacklogSequenceRender *sequence=owner;
    KImage *saved=sequence->render.row;
    /* Execute earlier rows for their VM side effects, while keeping their
       pixels out of the selected 640x54 row. */
    if(v->instruction_ip<sequence->target_ip)sequence->render.row=NULL;
    int result=backlog_replay_text(&sequence->render,v,width,height);
    sequence->render.row=saved;
    return result;
}
/* Render into a staged 640x54 row, committing neither pixels nor voices on
   failure. Only the UI font cache may initialize; gameplay is untouched. */
int bootstrap_backlog_replay(KBootstrap *b,unsigned back,KImage *row,KBacklogVoices *voices,char error[256]){
    if(!b||!voices||!error||!backlog_use_native(b))return -1;
    unsigned native=backlog_native_count(b);if(back>=native)return -1;
    /* CBackLog's row index is newest-first, while its command vectors are
       stateful in chronological order.  Replay every earlier vector into an
       isolated VM before drawing the requested row; this preserves font,
       colour, cursor and rectangle changes that intentionally span records. */
    unsigned target=native-1-back,seq_count=0;
    KBacklogReplayRecord *sequence=calloc(target+1,sizeof(*sequence));
    if(!sequence){snprintf(error,256,"backlog sequence allocation failed");return -1;}
    for(unsigned i=0;i<b->message_count&&seq_count<=target;i++)if(b->messages[i].size){
        sequence[seq_count++]=(KBacklogReplayRecord){b->messages[i].data,b->messages[i].size};
    }
    if(seq_count!=target+1){free(sequence);return -1;}
    KImage staged={0};if(row){
        if(!row->pixels||row->width!=640||row->height!=54||row->stride<2560||!bootstrap_ui_font(b)){free(sequence);return -1;}
        staged=(KImage){0,0,640,54,2560,malloc(640*54*4)};if(!staged.pixels){free(sequence);return -1;}
        for(unsigned y=0;y<54;y++)memcpy(staged.pixels+y*2560,row->pixels+y*row->stride,2560);
    }
    KBacklogSequenceRender render={{b,row?&staged:NULL},0};
    /* target_ip is the offset of the selected chronological row in the
       concatenated stream.  The replay gate uses the same instruction IP. */
    size_t offset=0;
    for(unsigned i=0;i<target;i++){
        size_t n=kbacklog_payload_size(&sequence[i]);
        offset+=n;
    }
    render.target_ip=offset;
    int result=kbacklog_replay_sequence(sequence,seq_count,target,b->vm,backlog_replay_sequence_text,&render,voices,error);
    if(!result&&row)for(unsigned y=0;y<54;y++)memcpy(row->pixels+y*row->stride,staged.pixels+y*2560,2560);
    rmt_free(&staged);free(sequence);return result;
}
int bootstrap_backlog_native(const KBootstrap *b){return b&&backlog_use_native(b);}
