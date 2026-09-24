#ifndef KISAKU_BOOTSTRAP_H
#define KISAKU_BOOTSTRAP_H
#include "voice_worker.h"
#include "image_worker.h"
#include "vm.h"
#include "backlog_replay.h"
#include "ai6arc.h"
#include "rmt.h"
#include "ax.h"
#include "video.h"
#include "mov.h"
#include "flags.h"
#include "gallery.h"
#include "control_store.h"
#include "scene.h"
#include "scene_view.h"
#include "title.h"
#include "flag_dialog.h"
#include "scene_history.h"
#include "mam.h"
#include "text_encoding.h"
#include "font.h"
#include "overlay_state.h"
#include "bowling.h"
#include "bowling_runtime.h"
#include "media_tables.h"
#include "message_skin.h"
#include "param_change.h"
#include "sound_volume.h"
typedef struct {
    uint8_t *data; size_t size,capacity;
    char *text; size_t text_capacity;
    uint8_t flag;
} KMessageRecord;
typedef struct {unsigned state;char name[261];} KAudioSlot;
typedef struct {uint8_t *pcm;size_t size,read,clock_position,loop_start,loop_end;int fade_step,fade_limit,fade_attenuation;unsigned fade_clock;} KEffectTrack;
typedef struct {int shift;unsigned primary,secondary,white;} KDistortFrame;
typedef struct { char section[128],key[128],value[512]; } KSetting;
typedef struct {
    KVM *vm;
    KOverlayState overlays;
    /* CFuncExec 31/522 drives the four process-wide Sungeki sprite windows.
       Keep their decoded DIBs and the last screen backing separately from
       script layers; native 0040c5d0 returns window objects, not layer DIBs. */
    KImage exec522_sprites[4],exec522_backing[4];
    int exec522_x[4],exec522_y[4],exec522_drawn_x[4],exec522_drawn_y[4];
    unsigned exec522_visible[4],exec522_drawn[4],exec522_state;
    int portrait_tracks[2];
    int exec522_current;
    unsigned exec522_motion,exec522_step,exec522_clock;
    unsigned exec522_pulse,exec522_pulse_phase,exec522_pulse_clock;
    int exec522_moving,exec522_previous,exec522_origin[4],exec522_delta[4];
    unsigned exec523_active,exec523_mode,exec523_step,exec523_clock;
    KMediaTables media_tables;
    KMessageSkin message_skin;
    unsigned auxiliary_windows_enabled;
    KImage param_surface,param_atlas,param_backing;int param_rows;int16_t param_values[4],param_markers[2];uint16_t param_total,param_remaining;
    unsigned param_animation_active,param_animation_phase,param_animation_step,param_animation_count;
    unsigned param_animation_chime,param_animation_track2,param_animation_sungeki,param_animation_temporary,param_animation_window;
    unsigned param_animation_clock,param_animation_last_event;
    float param_animation_accum[4];KParamChange param_animation_plan;
    KImage diary_surface;int32_t diary_people[96],diary_events[96];
    int diary_days,diary_content_height,diary_viewport_height,diary_page_height,diary_scroll;
    KBowling bowling;KBowling *current_bowling;
    KBowlingRuntime *bowling_runtime;
    KEffectTrack bowling_effects[27];
    KImage mes_fade_surfaces[3]; /* CFadeSprite and two private CSprite bitmaps. */
    unsigned mes_fade_visible,mes_fade_shade_visible;
    KImage mes_fade_backing;
    unsigned mes_fade_drawn,mes_fade_transition,mes_fade_steps,mes_fade_tick,mes_fade_clock;
    unsigned mes_fade_alpha,mes_fade_shade_alpha;
    unsigned mes_fade_type,mes_fade_rect[4];uint8_t *mes_fade_mask;
    KImage letter_surfaces[3];
    unsigned letter_transition,letter_alpha,letter_increment,letter_clock,letter_mode;
    unsigned letter_active,letter_backlog,letter_exit_pending,letter_phase,letter_reveal_clock,letter_text_increment;
    int letter_cursor_x,letter_cursor_y,letter_reveal_x,letter_reveal_y,letter_end_y;
    uint8_t *letter_mask;
    KImage status_image,status_parts;unsigned status_visible,status_serial;
    int32_t areas[512][5];unsigned area_count,area_active;int area_selected,area_x,area_y;int32_t area_nav[512][3];unsigned area_nav_count;
    char area_name[261];
    KImage bonus52_ui[3];
    unsigned bonus52_active,bonus52_stage,bonus52_pointer_valid;
    int bonus52_target,bonus52_result,bonus52_energy,bonus52_motion,bonus52_x,bonus52_y;
    uint64_t bonus52_clock,bonus52_decay,bonus52_limit;
    KDistortFrame distort[512];unsigned distort_count,distort_index;
    unsigned blink_active,blink_step,blink_clock,blink_wait;
    unsigned montage_active,montage_index,montage_presented;uint64_t montage_clock,montage_elapsed;
    unsigned credits_active,credits_row;uint64_t credits_clock;
    unsigned scroll_active,scroll_offset,scroll_target,scroll_speed,scroll_wrap;
    KImage choice_parts,choice_text,choice_base;
    KImage choice_rows[2][6];unsigned choice_prepared;
    unsigned choice_active,choice_normal,choice_count,choice_rendered_page,choice_page;int choice_selected,choice_page_hover,choice_values[64],choice_returns[64];
    char choice_labels[64][61];unsigned choice_lengths[64];
    KEffectTrack effect_tracks[64],movie_effect;
    char movie_effect_name[264];
    KMov mov;uint8_t *mov_data;
    unsigned video_background,video_wait,video_paused,video_change_wait;
    uint8_t *movie_pcm;size_t movie_pcm_size,movie_read,movie_clock;
    KFlags *flag_files; unsigned flag_file_count;
    KControlStore *control_files;unsigned control_file_count;
    KVoiceWorker *voice_worker;unsigned voice_loading;
    KImageWorker *image_worker;unsigned image_loading;int image_layer,image_offset_x,image_offset_y;
    int restore_read_id;unsigned restore_pending,restore_remaining,restore_mode;
    unsigned read_loaded,read_dirty,read_selector,quit_requested,quit_modal,title_load_requested,file_modal,character_request,native_menu_enabled,title_reset_modal;
    Ai6Archive scripts,images,data,effects,movies,music,voice;
    uint8_t *module_data[KVM_MODULES];
    KImage layers[64]; unsigned layer_count;
    KImage message_base,message_parts;
    KImage novel_original,novel_background,novel_from,novel_target;
    unsigned novel_mode,novel_transition,novel_step,novel_increment;
    uint8_t *novel_mask;unsigned novel_text_reveal,novel_mask_phase;
    int novel_start_x,novel_start_y,novel_reveal_y,novel_end_y;
    KFont *novel_font;
    unsigned message_voice_pending,message_active,message_slide,message_slide_frame,message_slide_clock,message_hiding,message_delay,message_clock,message_revealing;
    unsigned message_keep_on_confirm;
    unsigned message_buttons_motion,message_buttons_tick,message_buttons_clock,message_buttons_steps[4];
    unsigned message_auto_clock,message_auto_delay,message_had_voice,message_was_read,message_open,message_request;
    int message_hover;
    char message_pending[4097];size_t message_pending_size;
    char message_voice_name[261],history_voice[64][261];
    char history[64][4097];unsigned history_next,history_count;
    unsigned message_user_hidden,force_skip,effect_fast;
    const KControlRecord *history_restore;
    struct KBacklogSnapshot *backlog_restore;
    int message_read_id,message_reveal_x,message_reveal_y,message_end_x,message_end_y;
    uint32_t message_color;
    KImage message_text,fade_surface,helper_surfaces[3];
    /* Optional presentation companions; never saved or used by script reads. */
    unsigned present_hires,present_text_valid;
    unsigned present_page_valid,present_page_active;
    unsigned present_fade_valid,present_fade_active,present_fade_alpha,present_fade_incoming;
    KImage present_fade_to,present_fade_from,present_fade_text,present_fade_base,present_fade_reference,present_fade_overlay;
    unsigned present_mes_valid;
    KImage present_mes_source,present_mes_shadow,present_mes_old,present_mes_new,present_mes_reference;
    KImage present_page_source,present_page_text,present_page_shadow,present_page_base,present_page_reference;
    KImage present_source_text,present_message_text,present_choice_text;
    KImage present_shadow,present_clean,present_reference,present_overlay;
    /* CFuncExec 31/526 keeps a separate two-surface working pair.  It is
       distinct from the CLetter and 31/13 helper surfaces; action 1 releases
       this pair and resets its native state. */
    KImage exec526_surfaces[2],exec526_backing; unsigned exec526_active,exec526_drawn;
    unsigned exec526_width,exec526_height; int exec526_x,exec526_y;
    /* CFuncExec 31/524 owns a small sprite object. Keep a backing copy so
       action 1 can remove the object without disturbing the scene below it. */
    KImage overlay524_base,overlay524_sprite; unsigned overlay524_visible,overlay524_drawn;
    unsigned transition_steps,transition_frame;
    /* CFuncExec 31/40 vertical page wipe. The Japanese engine uses layer 2
       as a 640x960 two-page source and copies one 640x480 window to page 0. */
    unsigned exec_wipe_active,exec_wipe_offset,exec_wipe_step,exec_wipe_reverse;
    unsigned helper_steps,helper_frame,helper_visible,helper_hide;
    unsigned fade_visible,fade_alpha,fade_steps,fade_frame,fade_from,fade_to,fade_hide;
    uint64_t frames,wait_clock; unsigned wait_input;
    uint64_t native_wait_clock; unsigned native_wait_skippable,native_wait_pump;
    int message_cursor_x,message_cursor_y;
    unsigned message_initialized,message_visible;
    uint8_t animation_status[320];
    struct ax_player ax,ax_extra;
    uint8_t ax_events[AX_CELLS],ax_extra_events[AX_CELLS];
    unsigned logo_phase,ax_clock,ax_extra_clock,ax_modal,ax_extra_modal;
    /* 4de350 registration vector is a multiset, independent of play state. */
    uint32_t ax_registered[AX_CELLS],ax_extra_registered[AX_CELLS];
    unsigned ax_pause_remove,ax_extra_pause_remove; /* index+1, or AX_CELLS+1 for all */
    int ax_destination;unsigned ax_fast;
    char normal_atlas_name[261];
    KVideo *video;uint8_t *video_data;unsigned video_active,video_eof;
    char media_background_name[1024];
    uint8_t *voice_pcm;size_t voice_size,voice_read_cursor,voice_clock_cursor;unsigned voice_clock;
    uint8_t *audio_pcm; size_t audio_size,audio_cursor,audio_loop_start,audio_loop_end;
    unsigned audio_rate,audio_channels,audio_serial,audio_clock,music_active,music_enabled,voice_active;
    int music_db,fade_db,fade_db_step;unsigned music_fading,music_fade_clock;double music_gain;
    char audio_name[261],voice_playing_name[261];
    KImage canvas,auxiliary;
    uint8_t *animation_data; size_t animation_size;
    char loaded_animation[261];
    int animation_id; char animation_name[261];
    /* CAnimeManagerEX +0x14 (4df540) selects the AX stream used by the
       following extended-manager operation; it does not start the track.
       Keep that selection separate from the AX player's running state. */
    unsigned animation_track_bank,animation_track_cell,animation_track_selected;
    int animation_target_layer;
    uint8_t *raw_variables,*read_flags; size_t raw_size,read_size;
    KMessageRecord *messages; unsigned message_count; int message_index;
    KControlRecord controls[29],saved_controls[29]; unsigned control_count,saved_control_count;
    unsigned scene_mode;
    KTitle title;
    unsigned message_timed,message_timed_delay;uint64_t message_timed_clock;
    unsigned extra_active,extra_request,extra_kind,input_events;uint64_t input_event_until;
    /* 31/527 requests application save/load menus; action 2 queries bank1[60]. */
    uint32_t exec_status;
    struct KNativeCG *native_cg;
    KGallery gallery;char image_name[261];
    KFlagDialog flag_dialog;unsigned reset_pending;
    KSceneHistory scene_history;
    KMam mam;uint8_t *mam_data,*mam_archive;uint32_t mam_archive_offset;size_t mam_archive_size;unsigned mam_playing,mouth_sheet_ready;
    KImage scene_tiles,scene_parts,gallery_movie_base;unsigned gallery_animation;uint32_t text_measure_bytes;
    KScene *scene; unsigned scene_ui_pending,scene_flag_fac,scene_flag_fb4;
    unsigned scene_replay,scene_replay_finished,scene_modal,scene_focus,load_modal;int scene_panel_request;
    KSetting settings[128]; unsigned setting_count;
    char **setting_values; unsigned setting_value_count;
    KAudioSlot *audio_objects[3];void *records;
    unsigned audio_counts[3],record_count;
    int font_width,font_height,last_loaded_layer;
    KFont *font;KFont *ui_font;unsigned text_count;char number_text[257];
    /* Script text encoding: 0 = CP932 (original Japanese), 1 = GBK (translated
     * scripts). text_gbk is the sticky auto-detected mode used when
     * [Runtime] TextEncoding is not configured; font_simplified records which
     * shared font the cached b->font was opened with.  ui_font is deliberately
     * separate: panels/history/name input use the HOS shared font, while
     * story text and choices may use the supplied Runtime FontFile. */
    int text_gbk,font_simplified,ui_font_simplified;
    unsigned handled,missing_read_flags;
    /* Game data is read from root; every save, flag, history and settings file
       is written under save_root.  An installed NSP passes romfs: and save:,
       everything else passes the same directory twice. */
    char root[2048],save_root[2048],error[512];
} KBootstrap;
KBootstrap *bootstrap_create(const char *root);
KBootstrap *bootstrap_create_split(const char *root,const char *save_root);
/* Directory for saves, flags, history and settings.  Objects built by hand
   that only filled root keep their old single-directory behaviour. */
static inline const char *bootstrap_save_dir(const KBootstrap *b){
    return b->save_root[0]?b->save_root:b->root;
}
void bootstrap_destroy(KBootstrap *b);
/* Run real start.mes; stop on the first unsupported call, never fake completion. */
int bootstrap_run(KBootstrap *b,unsigned budget);
int bootstrap_dispatch(KBootstrap *b);
/* Advances one animation frame; run returns 1 while waiting for these ticks. */
void bootstrap_frame(KBootstrap *b);
int bootstrap_history_reset(KBootstrap *b,int confirm);
void bootstrap_confirm(KBootstrap *b);
void bootstrap_menu_move(KBootstrap *b,int dx,int dy);
int bootstrap_enable_async_voice(KBootstrap *b);
int bootstrap_enable_async_images(KBootstrap *b);
int bootstrap_flush_progress(KBootstrap *b);
int bootstrap_title_reset_close(KBootstrap *b,int accept);
int bootstrap_quit_dialog_close(KBootstrap *b,int accept);
void bootstrap_bowling_pointer(KBootstrap *b,int x,int y,unsigned held);
int bootstrap_bowling_active(const KBootstrap *b);
int bootstrap_can_save(const KBootstrap *b);
int bootstrap_save_slot(KBootstrap *b,unsigned slot);
int bootstrap_save_slot_comment(KBootstrap *b,unsigned slot,const char *utf8);
int bootstrap_saved_param_image(KBootstrap *b,const KFlags *saved,KImage *out);
int bootstrap_prepare_animation_switch(KBootstrap *b,unsigned mode);
int bootstrap_load_animation_switch(KBootstrap *b,unsigned selector);
/* Only for a fresh runtime that has completed startup to the title. */
int bootstrap_load_slot(KBootstrap *b,unsigned selector,unsigned slot);
void bootstrap_cancel(KBootstrap *b);
void bootstrap_message_hide(KBootstrap *b,int hidden);
int bootstrap_letter_exit(KBootstrap *b,int confirm);
void bootstrap_message_action(KBootstrap *b,unsigned action);
const char *bootstrap_character_image(unsigned role,unsigned item);
const char *bootstrap_history_voice(const KBootstrap *b,unsigned back);
int bootstrap_scene_draw(KBootstrap *b,unsigned selected,unsigned overview,KImage *out);
/* Prepare a fresh title runtime from a live owner; copied string values are owned. */
int bootstrap_scene_replay_begin(KBootstrap *b,const KBootstrap *owner,unsigned slot);
int bootstrap_scene_replay_merge(KBootstrap *owner,KBootstrap *replay);
unsigned bootstrap_backlog_count(const KBootstrap *b);
int bootstrap_backlog_has_voice(const KBootstrap *b,unsigned back);
int bootstrap_backlog_native(const KBootstrap *b);
int bootstrap_backlog_replay(KBootstrap *b,unsigned back,KImage *row,KBacklogVoices *voices,char error[256]);
int bootstrap_history_draw(KBootstrap *b,unsigned back,KImage *out);
/* Encoding and font the UI must use for script bytes it renders itself
   (backlog, history page).  These follow the same auto-detected, sticky GBK
   state as the story text, so a translated script pack is not re-decoded as
   CP932 by the panels. */
KTextEncoding bootstrap_text_encoding(const KBootstrap *b);
KFont *bootstrap_ui_font(KBootstrap *b);
int bootstrap_decode_ui_text(KBootstrap *b,const char *text,size_t size,KTextChar *out,size_t capacity,size_t *count);
enum { KSET_H_VOLUME=23, KSET_MOVIE_VSYNC, KSET_SUNGEKI_ANIME, KSET_SCHEDULE_CHECK,
       KSET_MINIGAME_DIFFICULTY, KSET_SUNGEKI_SE, KSET_SHOW_SPEED, KSET_CONFIG_PAGE,
       KSET_CHARACTER_VOICE, KSET_COUNT=KSET_CHARACTER_VOICE+33 };
unsigned bootstrap_setting_count(void);
const char *bootstrap_setting_label(unsigned item);
int bootstrap_setting_limit(unsigned item);
int bootstrap_setting_default(unsigned item);
int bootstrap_settings_apply(KBootstrap *b,const int *values,unsigned count);
int bootstrap_message_setting(KBootstrap *b,unsigned item,int change);
/* Display/CASStrength is an optional presentation setting, deliberately kept
   outside the native CConfig index table so old save/settings layouts remain
   byte-for-byte compatible.  The value is returned as a clamped percentage
   (0..100); integer values are percentages and decimal values in 0..1 are
   accepted as normalized strength.  Zero disables sharpening; the viewer still uses bilinear scaling. */
int bootstrap_cas_strength(const KBootstrap *b);
/* Display/EdgeStrength: output-pixel GPU sharpening, 0..100, default 55. */
int bootstrap_edge_strength(const KBootstrap *b);
/* PresentFilter=fsr1 (default) or edge; FSRSharpness=0..100, default80. */
int bootstrap_fsr_enabled(const KBootstrap *b);
int bootstrap_fsr_strength(const KBootstrap *b);
/* Returns a 640x480 clean presentation copy plus a 960x720 text overlay.
   Unsupported/modified text surfaces safely retain their authored pixels. */
const KImage *bootstrap_present_layers(KBootstrap *b,const KImage **overlay);
unsigned bootstrap_video_count(void);
const char *bootstrap_video_preview_name(unsigned index);
int bootstrap_video_unlocked(const KBootstrap *b,unsigned index);
int bootstrap_video_select(KBootstrap *b,int index);
unsigned bootstrap_music_count(void);
const char *bootstrap_music_name(unsigned index);
int bootstrap_music_unlocked(const KBootstrap *b,unsigned index);
void bootstrap_music_stop(KBootstrap *b);
int bootstrap_music_select(KBootstrap *b,unsigned index);
const KImage *bootstrap_native_cg_image(const KBootstrap *b);
int bootstrap_native_cg_focus(const KBootstrap *b,int rect[4]);
unsigned bootstrap_native_cg_categories(const KBootstrap *b);
int bootstrap_native_cg_action(KBootstrap *b,int action);
int bootstrap_native_cg_pointer(KBootstrap *b,int x,int y,int click);
int bootstrap_gallery_movie_unlocked(const KBootstrap *b,unsigned item);
int bootstrap_gallery_movie(KBootstrap *b,unsigned item);
int bootstrap_gallery_animation(KBootstrap *b,unsigned variant);
void bootstrap_gallery_movie_stop(KBootstrap *b);
int bootstrap_gallery_variant(KBootstrap *b,unsigned group,unsigned item,unsigned variant,KImage *out);
unsigned bootstrap_gallery_scroll_extent(const KBootstrap *b,unsigned group,unsigned item,unsigned variant);
int bootstrap_gallery_scroll_image(KBootstrap *b,unsigned group,unsigned item,unsigned variant,KImage *out);
int bootstrap_gallery_image(KBootstrap *b,unsigned index,KImage *out);
void bootstrap_extra_close(KBootstrap *b);
unsigned bootstrap_replay_count(unsigned group);
int bootstrap_replay_unlocked(const KBootstrap *b,unsigned group,unsigned item);
int bootstrap_replay_select(KBootstrap *b,unsigned group,unsigned item);
int bootstrap_replay_style(KBootstrap *b,int style);
int bootstrap_nawa_unlocked(const KBootstrap *b,unsigned item);
int bootstrap_name_submit(KBootstrap *b,const char *utf8);
int bootstrap_name_preview(KBootstrap *b,const char *utf8,KImage *out);
int bootstrap_nawa_select(KBootstrap *b,unsigned item);
void bootstrap_title_move(KBootstrap *b,int delta);
void bootstrap_pointer(KBootstrap *b,int x,int y,int click);
size_t bootstrap_audio_mix_read(KBootstrap *b,size_t *position,uint8_t *out,size_t capacity);
size_t bootstrap_audio_read(const KBootstrap *b,size_t *position,uint8_t *out,size_t capacity);
#endif
