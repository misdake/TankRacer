#ifndef FRAME_SESSION_H
#define FRAME_SESSION_H
#include "frame_native.h"
#include "frame_time.h"
#include "frame_texture.h"
/* The device owns this8-byte settings record (volume words at4/6).
 * Disabled sound still supplies an explicit record; no guessed pointer.
 * Video-quality reconfiguration is a graphics command, not a game callback. */
typedef int (*frame_session_video)(void *ctx,uint32_t quality);
typedef struct frame_session_devices {
    uint32_t audio_settings;
    frame_session_video video;void *video_ctx;
} frame_session_devices;
enum { FRAME_SESSION_CONTINUE=0,FRAME_SESSION_RESTART=1,FRAME_SESSION_EXIT=2 };
/*413DD0 pause-key block and410920 pause menu, including confirmations,
 * edge latches, packed setting ranges and exact game DATA stores. */
int frame_session_pause_key(frame_data_read,frame_data_write,void *,const frame_session_devices *);
int frame_session_menu(frame_data_read,frame_data_write,void *,const frame_session_devices *);
/* Browser capability limit; zero preserves all original items. */
int frame_session_menu_limited(frame_data_read,frame_data_write,void *,const frame_session_devices *,uint32_t);
/*413DD0 fade DATA and overlays; paused frames skip the fade-in branch. */
int frame_session_fades(frame_data_read,frame_data_write,void *);
/*413DD0 terminal request precedence; the host performs the returned lifecycle
 * transaction, never a hidden original callback. */
int frame_session_action(frame_data_read,frame_data_write,void *,uint32_t *);
/*40BD90 and40C880's game-state reset/final publication. */
int frame_session_clock_reset(frame_data_write,void *);
int frame_session_begin(frame_data_read,frame_data_write,void *);
int frame_session_begin_with_devices(frame_data_read,frame_data_write,void *,const frame_session_devices *);
/*40C880 video-detail policy and51E6A0 private CRT random generator. This is
 * separate from the scene's503C90 LCG; video calls are external graphics I/O. */
int frame_session_video_initialize(frame_data_read,frame_data_write,void *,const frame_session_devices *);
/*40C880 loading prefix must precede resource/object initialization. In
 * particular it must not erase the swamp light flags after their creation. */
int frame_session_loading_reset(frame_data_read,frame_data_write,void *);
/*413DD0 results delay/skip/fade and scoreboard release. Returns collect=1
 * only at the original results transaction boundary; no early lap shortcut. */
int frame_session_results_wait(frame_data_read,frame_data_write,void *,const frame_session_devices *,uint32_t *collect);
int frame_session_scoreboard(frame_data_read,frame_data_write,void *);
/*413EE7..414098 input/idle policy. request=1 reaches the attract-mode AI
 * transaction boundary; caller may defer the AI demo without running it. */
int frame_session_idle(frame_data_read,frame_data_write,void *,uint32_t *request);
/*5044F0 /504C70 /504CB0: true queue/destructor/free-ID cleanup. */
int frame_session_stop_entities(frame_data_read,frame_data_write,void *,const frame_entity_factory_services *);
int frame_session_clear_entities(frame_data_read,frame_data_write,void *,const frame_entity_factory_services *);
/* Single-player40DBA0 game/resource teardown. Sound and OS/UI ownership are
 * external. Real entity destructors run; symbols, lists, grids, caches and
 * texture descriptors are retired in original order, retaining class schema. */
int frame_session_unload(frame_data_read,frame_data_write,void *,const frame_entity_factory_services *,const frame_texture_services *);
int frame_session_end(frame_data_read,frame_data_write,void *,const frame_entity_factory_services *,const frame_texture_services *,const frame_session_devices *);
/* Host restart event also works while paused. No seed/time source reset;
 * caller reloads immutable level assets, then publishes frame_session_begin. */
int frame_session_prepare_restart(frame_data_read,frame_data_write,void *,const frame_entity_factory_services *,const frame_texture_services *,const frame_session_devices *);
#endif
