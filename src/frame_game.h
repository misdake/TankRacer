#ifndef FRAME_GAME_H
#define FRAME_GAME_H
#include "frame_native.h"
#include "frame_session.h"
/* File acquisition is I/O. Namespace2 is DATA raw files,6 is level raw
 * BSM/BGN/DAT. Shape namespaces1/3/5 stay owned by existing resource rules. */
enum { FRAME_GAME_DATA_FILES=2,FRAME_GAME_LEVEL_FILES=6 };
typedef struct frame_game_options {
    const unsigned char *map;uint32_t map_bytes,cup,vehicle,colour,mode;
    const frame_session_devices *devices;
} frame_game_options;
/* Cold process bootstrap into caller-owned zeroed DATA, using installed
 * allocators/material/image/file services. Classes, constants, SURFS,
 * OBPARAMS, new-profile records,16 random route seeds and lists are C. */
int frame_game_initialize(frame_native_context *);
/* Legacy no-opponent40C880 game startup, modes0/1, no replay/audio.
 * Invoke qualified unload first on reload; resident class/schema/file mounts
 * and both random generators survive. No GUI or original callback is used. */
int frame_game_load(frame_native_context *,const frame_game_options *,uint32_t *player);
/* Same startup with the actual seven AITank placements in <map3>TANK.DAT.
 * seed replaces the OS time/srand boundary; game selection remains native. */
int frame_game_load_opponents(frame_native_context *,const frame_game_options *,uint32_t seed,uint32_t *player);
/*413DD0 single-player orchestration after the qualified clock/dt step. All
 * game/AI rules run in C. action:0 continue,1 reload,2 quit,3 collect results,
 *4 attract-demo AI boundary. HUD game state is a separate presentation phase. */
int frame_game_tick(frame_native_context *,const frame_session_devices *,uint32_t *action);
int frame_game_tick_menu_limit(frame_native_context *,const frame_session_devices *,uint32_t,uint32_t *action);
#endif
