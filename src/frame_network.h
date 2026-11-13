#ifndef FRAME_NETWORK_H
#define FRAME_NETWORK_H
#include "frame_data.h"
enum { FRAME_NET_SEND=1,FRAME_NET_SET_PLAYER=2,FRAME_NET_GET_PLAYER=3 };
typedef struct frame_network_request {
    uint32_t operation,object,player,peer,flags,bytes,capacity;
    const unsigned char *data;
    unsigned char *response;
} frame_network_request;
typedef struct frame_network_reply { uint32_t hresult,written,reported; } frame_network_reply;
/* 0 means modeled transport completed, regardless of HRESULT. Negative means
 * unavailable/unmodeled host. The original callers ignore the HRESULT. */
typedef int (*frame_network_service)(void *ctx,const frame_network_request *request,frame_network_reply *reply);

/* Full 51D550/570 send wrappers and 51D010 player-data/cache rule. Workspace is
 * caller owned, explicitly bounded; insufficient payload space is -3. */
int frame_network_send(frame_data_read read,void *ctx,uint32_t address,uint32_t bytes,uint32_t reliable,
                         frame_network_service service,void *service_ctx,unsigned char *workspace,uint32_t capacity);
int frame_network_player_data(frame_data_read read,frame_data_write write,void *ctx,uint32_t address,uint32_t bytes,
                                frame_network_service service,void *service_ctx,unsigned char *workspace,
                                uint32_t capacity,uint32_t work_limit);
/* Full 418AC0: sum THREE packed lap records, send type7 twice, change race
 * state and submit the 12-byte local-player state, then refresh cache. */
int frame_network_race_finished(frame_data_read read,frame_data_write write,void *ctx,
                                  frame_network_service service,void *service_ctx,
                                  unsigned char *workspace,uint32_t capacity,uint32_t work_limit);
/*418B50 mode4 elimination: type8 unreliable/reliable, state3, player data,
 * then1000ms result timer. Transport remains the same explicit I/O service. */
int frame_network_race_eliminated(frame_data_read read,frame_data_write write,void *ctx,
                                    frame_network_service service,void *service_ctx,
                                    unsigned char *workspace,uint32_t capacity,uint32_t work_limit);

#define FRAME_NET_PLAYERS 8u
#define FRAME_NET_PLAYER_BYTES 32u
typedef struct frame_network_player { uint32_t id,bytes,mounted;unsigned char data[FRAME_NET_PLAYER_BYTES]; } frame_network_player;
typedef struct frame_network_event {
    uint32_t operation,object,player,peer,flags,offset,bytes,hresult,reported,written;
} frame_network_event;
typedef struct frame_network_host {
    uint32_t object,player_count,forced_hresult,event_count,payload_size;
    frame_network_player players[FRAME_NET_PLAYERS];
    frame_network_event *events;uint32_t event_capacity;
    unsigned char *payload;uint32_t payload_capacity;
} frame_network_host;
/* Deterministic DirectPlay subset, no socket/session creation. Event/payload
 * arenas are caller owned; overflow and unknown interface tokens stop. */
int frame_network_host_init(frame_network_host *host,uint32_t object,frame_network_event *events,
                              uint32_t event_capacity,unsigned char *payload,uint32_t payload_capacity);
int frame_network_host_mount(frame_network_host *host,uint32_t player,const void *data,uint32_t bytes);
int frame_network_host_call(void *ctx,const frame_network_request *request,frame_network_reply *reply);
#endif
