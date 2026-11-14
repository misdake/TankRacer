/* Test-only semantic player state. Requires reference_motion_io.h and narrow. */
static void player_state(void)
{
    uint32_t i;
    state();
    emit(object+0xe4,3);emit(object+0xa4,1);emit(object+0xac,1);emit(object+0xb0,1);
    narrow(object+0xdeb,1);narrow(object+0xdde,1);emit(object+0xf4,1);
    for(i=0;i<4;i++){
        uint32_t p=object+0x11c+i*70u;emit(p,3);narrow(p+24,2);emit(p+32,2);
        narrow(p+68,2);emit(p+56,3);emit(p+40,1);emit(p+48,2);
    }
    emit(object+0x234,3);emit(object+0x251,3);
    emit(object+0x10be,1);narrow(object+0xd90,1);emit(object+0x10c6,1);emit(object+0x118,1);
    emit(object+0x107e,1);emit(object+0xd95,1);oracle_put(word(object+0xd8c)!=0);emit(object+0x114,1);emit(object+0x108,3);emit(object+0xfc,3);
    emit(object+0xd91,1);emit(object+0x90,1);emit(object+0xa8,1);emit(object+0xc9a,1);
    narrow(object+0xdd0,2);narrow(object+0xdce,2);emit(object+0xdd2,1);emit(object+0x6a6,1);
    emit(object+0xc9e,1);emit(object+0xcaa,1);emit(object+0xcb2,1);narrow(object+0xdd6,2);
    emit(object+0xdbf,1);emit(object+0xdc3,1);emit(object+0xdc7,1);
    narrow(object+0xdcb,1);narrow(object+0xdcc,1);narrow(object+0xdcd,1);
    narrow(object+0xd99,1);narrow(object+0xd9a,1);emit(object+0xd9b,6);narrow(object+0xde4,1);
    gun(1);
    narrow(object+0xe17,1);narrow(object+0xe18,1);narrow(object+0xe19,1);emit(object+0xe1a,2);
    narrow(object+0xc54,1);narrow(object+0xc55,1);emit(object+0xc56,12);emit(object+0xc86,3);
    for(i=0;i<68;i++)emit(object+0xaac+i*6u,1);
    emit(object+0xc44,1);emit(object+0xc4c,1);emit(object+0xc50,1);
    for(i=0;i<8;i++){
        uint32_t p=object+0x990+i*34u;emit(p,6);narrow(p+24,2);narrow(p+26,2);
        narrow(p+28,2);narrow(p+30,2);narrow(p+32,1);narrow(p+33,1);
    }
    emit(object+0xaa0,3);
    for(i=0;i<15;i++){
        uint32_t p=object+0x798+i*32u;emit(p,6);narrow(p+24,2);narrow(p+26,2);
        narrow(p+28,2);narrow(p+30,1);narrow(p+31,1);
    }
    emit(object+0x978,4);
    narrow(object+0xdf1,1);narrow(object+0xdf2,1);narrow(object+0xdf3,1);narrow(object+0xdf4,1);
    emit(object+0x988,2);
    emit(object+0xc92,2);narrow(object+0xdef,1);
}
