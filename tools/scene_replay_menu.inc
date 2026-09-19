typedef struct {KBootstrap *owner,*next;unsigned phase,slot,ticks,cancel,selected,viewing;size_t queued;KHistoryAudio sound;} SceneReplayMenu;
/* Return 1 when entering replay, 2 when returning to the retained story. */
static int scene_replay_step(SceneReplayMenu *m,KBootstrap **live,MessagePanel *p){
    KBootstrap *b=*live;
    if(p->scene_cancel){m->cancel=1;p->scene_cancel=0;}
    if(m->phase==3){
        if(!m->cancel&&!b->scene_replay_finished&&!b->error[0])return 0;
        const char *status=m->cancel?"Returned to story":b->error[0]?"Replay failed; original story retained":"Replay complete";
        if(!m->cancel&&!b->error[0]&&bootstrap_scene_replay_merge(m->owner,b))status="Returned; could not merge replay progress";
        history_voice_stop(p);bootstrap_destroy(b);*live=m->owner;m->owner=NULL;m->phase=m->cancel=0;p->kind=16;p->selected=m->selected;p->viewing=m->viewing;p->back=0;
        snprintf(p->status,sizeof(p->status),"%s",status);return 2;
    }
    if(m->cancel){bootstrap_destroy(m->next);m->next=NULL;m->phase=m->cancel=0;p->kind=16;snprintf(p->status,sizeof(p->status),"Replay cancelled");return 0;}
    if(p->scene_request){
        m->slot=p->scene_request-1;p->scene_request=0;m->selected=p->selected;m->viewing=p->viewing;
        if(m->phase||bootstrap_flush_progress(b)){snprintf(p->status,sizeof(p->status),"Cannot prepare replay");return 0;}
        m->next=bootstrap_create_split(b->root,bootstrap_save_dir(b));
        if(!m->next||m->next->error[0]||bootstrap_enable_async_voice(m->next)||bootstrap_enable_async_images(m->next)){
            bootstrap_destroy(m->next);m->next=NULL;snprintf(p->status,sizeof(p->status),"Cannot initialize replay");return 0;
        }
        m->phase=1;m->ticks=0;p->kind=17;
    }
    if(!m->phase)return 0;
    bootstrap_frame(m->next);int state=bootstrap_run(m->next,100000);
    if(state<0||state==0||++m->ticks>36000){
        snprintf(p->status,sizeof(p->status),"Replay unavailable; original story retained");
        bootstrap_destroy(m->next);m->next=NULL;m->phase=0;p->kind=16;return 0;
    }
    if(m->phase==1&&m->next->title.active){
        if(bootstrap_scene_replay_begin(m->next,b,m->slot)){
            bootstrap_destroy(m->next);m->next=NULL;m->phase=0;p->kind=16;snprintf(p->status,sizeof(p->status),"Checkpoint does not match this game version");
        }else m->phase=2;
    }else if(m->phase==2&&(m->next->message_active||m->next->choice_active||m->next->scene_replay_finished)){
        m->owner=b;*live=m->next;m->next=NULL;m->phase=3;p->kind=0;return 1;
    }
    return 0;
}
