#include "frame_network.h"
#include "physics_memory.h"
#include <string.h>

static int invoke(physics_memory *m,frame_network_service service,void *ctx,
                     const frame_network_request *request,frame_network_reply *reply)
{
    if (m->error) return m->error;
    return service ? service(ctx,request,reply) : -5;
}
static int payload_read(physics_memory *m,uint32_t address,uint32_t bytes,unsigned char *workspace,uint32_t capacity)
{
    if (bytes>capacity || (bytes && !workspace)) return -3;
    if (m->error) return m->error;
    if (bytes && !m->read(m->ctx,address,workspace,bytes)) return -1;
    return 0;
}
int frame_network_send(frame_data_read read,void *ctx,uint32_t address,uint32_t bytes,uint32_t reliable,
                         frame_network_service service,void *service_ctx,unsigned char *workspace,uint32_t capacity)
{
    physics_memory m={read,0,ctx,0};frame_network_request request={0};frame_network_reply reply={0};int status;
    if (!read) return -1;
    request.object=pm_load(&m,0x58e6ecu,4);request.player=pm_load(&m,0x589520u,4);
    status=payload_read(&m,address,bytes,workspace,capacity);if (status) return status;
    request.operation=FRAME_NET_SEND;request.peer=1;request.flags=!!reliable;request.bytes=bytes;request.data=workspace;
    return invoke(&m,service,service_ctx,&request,&reply);
}
int frame_network_player_data(frame_data_read read,frame_data_write write,void *ctx,uint32_t address,uint32_t bytes,
                                frame_network_service service,void *service_ctx,unsigned char *workspace,
                                uint32_t capacity,uint32_t work_limit)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    frame_network_request request={0};frame_network_reply reply={0};uint32_t i=0,j;int status;
    unsigned char response[32];
    if (!read || !write || !work_limit) return -1;
    request.object=pm_load(m,0x58e6ecu,4);request.player=pm_load(m,0x589520u,4);
    status=payload_read(m,address,bytes,workspace,capacity);if (status) return status;
    request.operation=FRAME_NET_SET_PLAYER;request.flags=2;request.bytes=bytes;request.data=workspace;
    status=invoke(m,service,service_ctx,&request,&reply);if (status) return status;
    while (i<pm_load(m,0x58e7a8u,4)) {
        if (i>=work_limit) return -3;
        if (pm_load(m,0x589928u+i*4u,4)==pm_load(m,0x589520u,4)) {
            uint32_t destination=0x589120u+i*32u;
            for (j=0;j<8;j++) pm_store(m,destination+j*4u,0,4);
            memset(&request,0,sizeof(request));memset(&reply,0,sizeof(reply));
            request.operation=FRAME_NET_GET_PLAYER;request.object=pm_load(m,0x58e6ecu,4);
            request.player=pm_load(m,0x589928u+i*4u,4);request.capacity=32;request.response=response;
            status=invoke(m,service,service_ctx,&request,&reply);if (status) return status;
            if (reply.written>32u) return -5;
            if (reply.written && !write(ctx,destination,response,reply.written)) return -1;
        }
        i++;
    }
    return m->error;
}
static void put32(unsigned char *p,uint32_t value)
{
    unsigned i;for (i=0;i<4;i++) p[i]=(unsigned char)(value>>(i*8u));
}
int frame_network_race_finished(frame_data_read read,frame_data_write write,void *ctx,
                                  frame_network_service service,void *service_ctx,
                                  unsigned char *workspace,uint32_t capacity,uint32_t work_limit)
{
    physics_memory mem={read,write,ctx,0},*m=&mem;
    frame_network_request request={0};frame_network_reply reply={0};unsigned char packet[8];
    uint32_t record,object,total=0,i;int status;
    if (!read || !write || !work_limit) return -1;
    record=pm_load(m,pm_load(m,0x55c694u,4)+pm_load(m,0x53e8b8u,4)*8u,4);
    object=record ? pm_load(m,record+4u,4) : 0;
    for (i=0;i<3;i++) total+=pm_load(m,object+0xcbeu+i*7u,4);
    put32(packet,7);put32(packet+4,total);
    request.operation=FRAME_NET_SEND;request.object=pm_load(m,0x58e6ecu,4);request.player=pm_load(m,0x589520u,4);
    request.peer=1;request.bytes=8;request.data=packet;
    status=invoke(m,service,service_ctx,&request,&reply);if (status) return status;
    request.flags=1;status=invoke(m,service,service_ctx,&request,&reply);if (status) return status;
    pm_store(m,0x543d34u,1000,4);pm_store(m,0x543d10u,3,4);
    if (m->error) return m->error;
    return frame_network_player_data(read,write,ctx,0x543d08u,12,service,service_ctx,workspace,capacity,work_limit);
}
int frame_network_host_init(frame_network_host *host,uint32_t object,frame_network_event *events,
                              uint32_t event_capacity,unsigned char *payload,uint32_t payload_capacity)
{
    if (!host || !object || (event_capacity && !events) || (payload_capacity && !payload)) return -1;
    memset(host,0,sizeof(*host));host->object=object;host->events=events;host->event_capacity=event_capacity;
    host->payload=payload;host->payload_capacity=payload_capacity;return 0;
}
int frame_network_race_eliminated(frame_data_read read,frame_data_write write,void *ctx,
                                    frame_network_service service,void *service_ctx,
                                    unsigned char *workspace,uint32_t capacity,uint32_t limit)
{
    physics_memory m={read,write,ctx,0};frame_network_request request={0};frame_network_reply reply={0};unsigned char packet[4];int status;
    if (!read || !write || !limit) return -1;
    put32(packet,8);request.operation=FRAME_NET_SEND;request.object=pm_load(&m,0x58e6ecu,4);request.player=pm_load(&m,0x589520u,4);request.peer=1;request.bytes=4;request.data=packet;
    status=invoke(&m,service,service_ctx,&request,&reply);if (status) return status;
    request.flags=1;status=invoke(&m,service,service_ctx,&request,&reply);if (status) return status;
    pm_store(&m,0x543d10u,3,4);if (m.error) return m.error;
    status=frame_network_player_data(read,write,ctx,0x543d08u,12,service,service_ctx,workspace,capacity,limit);if (status) return status;
    pm_store(&m,0x543d38u,1000,4);return m.error;
}
int frame_network_host_mount(frame_network_host *host,uint32_t player,const void *data,uint32_t bytes)
{
    frame_network_player *p;unsigned i;
    if (!host || !player || bytes>32 || (bytes && !data)) return -1;
    for (i=0;i<host->player_count;i++) if (host->players[i].id==player) return -1;
    if (host->player_count==FRAME_NET_PLAYERS) return -3;
    p=&host->players[host->player_count++];p->id=player;p->bytes=bytes;p->mounted=1;
    if (bytes) memcpy(p->data,data,bytes);
    return 0;
}
int frame_network_host_call(void *ctx,const frame_network_request *request,frame_network_reply *reply)
{
    frame_network_host *host=(frame_network_host *)ctx;
    frame_network_player *player=0;
    frame_network_event event={0};frame_network_reply result={0};
    const unsigned char *payload=request ? request->data : 0;
    unsigned char player_data[32];
    uint32_t length=request ? request->bytes : 0,i;
    if (!host || !request || !reply || request->object!=host->object) return -5;
    if (request->operation<1 || request->operation>3 || (request->bytes && !request->data) ||
        (request->capacity && !request->response)) return -1;
    if ((request->operation==FRAME_NET_SEND && request->flags>1) ||
        (request->operation==FRAME_NET_SET_PLAYER && (request->flags!=2 || request->bytes>32)) ||
        (request->operation==FRAME_NET_GET_PLAYER && request->flags)) return -1;
    if (request->operation==FRAME_NET_SEND && request->peer!=1) return -1;
    for (i=0;i<host->player_count;i++) if (host->players[i].id==request->player) player=&host->players[i];
    result.hresult=host->forced_hresult;
    if (!player) result.hresult=0x88770096u; /* DPERR_INVALIDPLAYER */
    if (request->operation==FRAME_NET_GET_PLAYER) {
        length=0;result.reported=player ? player->bytes : 0;
        if (!(result.hresult&0x80000000u)) {
            if (request->capacity<player->bytes) result.hresult=0x8877001eu; /* DPERR_BUFFERTOOSMALL */
            else {
                length=player->bytes;if (length) memcpy(player_data,player->data,length);
                payload=player_data;result.written=length;
            }
        }
    }
    if (host->event_count>=host->event_capacity || host->payload_size>host->payload_capacity ||
        length>host->payload_capacity-host->payload_size) return -3;
    if (request->operation==FRAME_NET_SET_PLAYER && length) { memcpy(player_data,request->data,length);payload=player_data; }
    if (request->operation==FRAME_NET_SET_PLAYER && !(result.hresult&0x80000000u)) {
        memset(player->data,0,32);if (length) memcpy(player->data,payload,length);player->bytes=length;
    }
    if (request->operation==FRAME_NET_GET_PLAYER && length) memmove(request->response,payload,length);
    event.operation=request->operation;event.object=request->object;event.player=request->player;event.peer=request->peer;
    event.flags=request->flags;event.offset=host->payload_size;event.bytes=length;event.hresult=result.hresult;
    event.reported=result.reported;event.written=result.written;
    if (length) memmove(host->payload+host->payload_size,payload,length);
    host->payload_size+=length;host->events[host->event_count++]=event;*reply=result;return 0;
}
