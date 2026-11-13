#ifndef TANKRACER_GAME_HOST_H
#define TANKRACER_GAME_HOST_H

#include <stdint.h>
#include "frame_audio.h"

/* One game and one frontend per instance. Returned buffers remain owned by
 * the host and are valid until the next corresponding update or destruction.
 * Game data is an 8 MiB, little-endian address space; addresses are identities,
 * never pointers into the original executable. */
#define TANKRACER_DATA_BYTES 0x800000u

int tank_create(uint32_t map,uint32_t cup,uint32_t vehicle,uint32_t mode,uint32_t opponents);
int tank_mount(uint32_t slot,const unsigned char *bytes,uint32_t length);
int tank_start(void);
int tank_step(uint32_t frames,uint32_t bits,uint32_t focused);
int tank_advance(uint32_t map,const unsigned char *bytes,uint32_t length,uint32_t opponents);
const unsigned char *tank_packet(void);
uint32_t tank_packet_size(void);
const unsigned char *tank_data(void);
const frame_audio_command *tank_audio(void);
uint32_t tank_audio_count(void);
uint32_t tank_audio_gain(void);
void tank_destroy(void);

int tank_menu_create(const unsigned char *bytes,uint32_t length);
int tank_menu_open(void);
int tank_menu_finish(void);
int tank_menu_unlock(void);
int tank_menu_step(uint32_t bits,uint32_t dt,int32_t typed);
const unsigned char *tank_menu_packet(void);
uint32_t tank_menu_packet_size(void);
const unsigned char *tank_menu_data(void);
const frame_audio_command *tank_menu_audio(void);
uint32_t tank_menu_audio_count(void);

#endif
