#include "frame_profile.h"
#include "physics_memory.h"
static const uint32_t key_slots[11]={5,6,12,9,10,11,14,13,15,1,2};
static uint32_t load(const unsigned char *b,uint32_t at,uint32_t n)
{ uint32_t i,v=0;for (i=0;i<n;i++) v|=(uint32_t)b[at+i]<<(i*8u);return v; }
static void store(unsigned char *b,uint32_t at,uint32_t value,uint32_t n)
{ uint32_t i;for (i=0;i<n;i++) b[at+i]=(unsigned char)(value>>(i*8u)); }
/* Wire field offsets are the original file layout, not host structure ABI. */
static const struct { uint32_t wire,data; } fields[]={
    {0x12a0,0x53ccdc},{0x12a4,0x53cce0},{0x12a8,0x52e518},{0x12ac,0x53cce4},{0x12b0,0x52e51c},{0x12b4,0x545ffc},{0x12b8,0x546000},
    {0x1334,0x53ccc8},{0x1338,0x53cccc},{0x133c,0x53cca0},{0x1340,0x53cca4},{0x134c,0x54b4bc},{0x1350,0x54b4c0},
    {0x135c,0x53e404},{0x1360,0x53e408},{0x1364,0x53e40c},{0x1368,0x53e410},{0x136c,0x53e414},{0x1370,0x53e964}};
int frame_profile_encode(frame_data_read read,void *ctx,const frame_profile_devices *d,unsigned char *bytes,uint32_t length)
{
    physics_memory m={read,0,ctx,0};uint32_t i,j;if (!read || !d || !d->audio_settings || !bytes || length<FRAME_PROFILE_BYTES) return -1;
    if (!pm_load(&m,0x53e424u,4)) return m.error ? m.error : -4;
    for (i=0;i<2u;i++) { store(bytes,0x1354u+i*4u,pm_load(&m,0x530458u+i*4u,2),2);store(bytes,0x1356u+i*4u,pm_load(&m,0x53045au+i*4u,1),1); }
    for (i=0;i<0x486u;i++) store(bytes,4u+i*4u,pm_load(&m,0x53d190u+i*4u,4),4);
    for (i=0;i<sizeof(fields)/sizeof(fields[0]);i++) store(bytes,fields[i].wire,pm_load(&m,fields[i].data,4),4);
    store(bytes,0,102,4);
    for (i=0;i<11u;i++) store(bytes,0x1274u+i*4u,pm_load(&m,0x592900u+key_slots[i]*4u,4),4);
    for (j=0;j<2u;j++) for (i=0;i<11u;i++) store(bytes,0x121cu+(j*11u+i)*4u,pm_load(&m,0x592000u+(key_slots[i]+j*25u)*4u,4),4);
    store(bytes,0x1344,pm_load(&m,d->audio_settings,4),4);store(bytes,0x1348,pm_load(&m,d->audio_settings+4u,4),4);
    /* Original save reads53044C but load publishes the persisted value450. */
    store(bytes,0x1374,pm_load(&m,0x53044cu,4),4);return m.error;
}
int frame_profile_decode(frame_data_read read,frame_data_write write,void *ctx,const frame_profile_devices *d,const unsigned char *bytes,uint32_t length)
{
    physics_memory m={read,write,ctx,0};uint32_t i,j,value,acquired,enabled;int status;
    if (!read || !write || !d || !d->audio_settings || !bytes || length<FRAME_PROFILE_BYTES) return -1;
    if (load(bytes,0,4)!=102u) return -4;
    for (i=0;i<2u;i++) { pm_store(&m,0x530458u+i*4u,load(bytes,0x1354u+i*4u,2),2);pm_store(&m,0x53045au+i*4u,load(bytes,0x1356u+i*4u,1),1); }
    pm_store(&m,0x53ccc8u,load(bytes,0x1334,4),4);pm_store(&m,0x53ccccu,load(bytes,0x1338,4),4);
    for (i=0;i<0x486u;i++) pm_store(&m,0x53d190u+i*4u,load(bytes,4u+i*4u,4),4);
    pm_store(&m,0x53cca4u,load(bytes,0x1340,4),4);pm_store(&m,0x53cca0u,load(bytes,0x133c,4),4);
    for (i=0;i<11u;i++) pm_store(&m,0x592900u+key_slots[i]*4u,load(bytes,0x1274u+i*4u,4),4);
    for (j=0;j<2u;j++) for (i=0;i<11u;i++) pm_store(&m,0x592000u+(key_slots[i]+j*25u)*4u,load(bytes,0x121cu+(j*11u+i)*4u,4),4);
    pm_store(&m,0x53cce0u,load(bytes,0x12a4,4),4);pm_store(&m,0x53ccdcu,load(bytes,0x12a0,4),4);pm_store(&m,0x52e518u,load(bytes,0x12a8,4),4);
    if (m.error) return m.error;
    enabled=load(bytes,0x12a0,4)!=0u && load(bytes,0x12a8,4)!=0u;if (!d->feedback) return -5;
    status=d->feedback(d->ctx,enabled,&acquired);if (status) return status;
    if (!enabled) pm_store(&m,0x58e7ccu,0,4);else if (acquired) pm_store(&m,0x58e7ccu,1,4);
    pm_store(&m,0x53cce4u,load(bytes,0x12ac,4),4);pm_store(&m,0x52e51cu,load(bytes,0x12b0,4),4);pm_store(&m,0x545ffcu,load(bytes,0x12b4,4),4);pm_store(&m,0x546000u,load(bytes,0x12b8,4),4);
    pm_store(&m,d->audio_settings,load(bytes,0x1344,4),4);pm_store(&m,d->audio_settings+4u,load(bytes,0x1348,4),4);
    pm_store(&m,0x54b4bcu,load(bytes,0x134c,4),4);pm_store(&m,0x54b4c0u,load(bytes,0x1350,4),4);
    for (i=13u;i<sizeof(fields)/sizeof(fields[0]);i++) pm_store(&m,fields[i].data,load(bytes,fields[i].wire,4),4);
    pm_store(&m,0x530450u,load(bytes,0x1374,4),4);pm_store(&m,0x53ccd8u,1,4);pm_store(&m,0x53e400u,0,4);
    value=pm_load(&m,0x53d19au,4);if (value&4096u) { if (!(value&32768u)) pm_store(&m,0x54671cu,1,4);else pm_store(&m,0x53d19au,value&~32768u,2); }return m.error;
}
