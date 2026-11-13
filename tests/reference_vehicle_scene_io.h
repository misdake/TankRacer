/* Semantic after-pose state, appended to the connected-physics oracle. */
static void scene_state(void)
{
    unsigned i,j,k;uint32_t o,p,a;
    for(k=0;k<count;k++){
        o=actors[k];
        for(i=0;i<8;i++){p=o+0x990+i*34u;emit(p,6);for(j=0;j<4;j++)raw(p+24+j*2u,2);raw(p+32,1);raw(p+33,1);}
        emit(o+0xaa0,3);
        for(i=0;i<15;i++){p=o+0x798+i*32u;emit(p,6);for(j=0;j<3;j++)raw(p+24+j*2u,2);raw(p+30,1);raw(p+31,1);}
        emit(o+0x978,4);for(i=0;i<4;i++)raw(o+0xdf1+i,1);emit(o+0x988,2);
        for(i=0;i<3;i++)raw(o+0xe17+i,1);
        emit(o+0xe1a,2);
        raw(o+0xc54,1);raw(o+0xc55,1);emit(o+0xc56,12);emit(o+0xc86,3);
        for(i=0;i<68;i++)emit(o+0xaac+i*6u,1);
        emit(o+0xc44,1);emit(o+0xc4c,1);emit(o+0xc50,1);
        emit(o+0xc92,2);raw(o+0xdef,1);raw(o+0xdce,2);
    }
    for(k=0;k<2;k++){
        a=0x546460+k*264u;
        for(i=0;i<8;i++){p=a+i*32u;emit(p,6);for(j=0;j<4;j++)raw(p+24+j,1);emit(p+28,1);}
        emit(a+256,2);
    }
    for(k=0;k<2;k++){
        a=0x58faa0+k*44u;for(i=0;i<4;i++)raw(a+i,1);oracle_put(owner_entity(word(a+4)));emit(a+8,9);
        emit(0x5450c0+k*4u,1);emit(0x5450c8+k*4u,1);emit(0x5457d8+k*4u,1);
        for(i=0;i<10;i++){p=0x545148+k*840u+i*84u;raw(p,2);raw(p+2,2);emit(p+4,20);}
        for(i=0;i<10;i++)emit(0x5457e0+k*680u+i*68u,17);
    }
    emit(0x54669c,1);emit(0x5461b0,136);
    for(k=0;k<9;k++){
        a=0x58ee20+k*160u;
        for(i=0;i<4;i++){p=a+i*39u;emit(p,6);raw(p+24,1);raw(p+25,2);emit(p+27,3);}
        /* Banks 0/1 tails alias the player-effect words already in snapshot(). */
        if(k>1)emit(a+156,1);
    }
    emit(0x54cc88,2560);emit(0x54cc48,16);
}
