/* Test-only semantic vehicle motion/mesh serialization. Requires object, word, emit and narrow. */
static void state(void)
{
    static const uint32_t poses[]={0x2a6,0x3de,0x446,0x30e,0x376,0x4ae,0x516};unsigned i,j;uint32_t mesh,vertices;
    emit(object+0x78,3);emit(object+0x18,9);emit(object,3);
    for(i=0;i<7;i++)emit(object+poses[i],12);
    emit(object+0x5e6,3);narrow(object+0xdf5,1);narrow(object+0xdf6,1);
    emit(object+0x84,3);emit(object+0x50,3);emit(object+0xb4,3);emit(object+0xc4,1);emit(object+0xcc,1);emit(object+0xd8,3);
    emit(object+0x764,4);emit(object+0x77c,2);emit(object+0x774,2);
    for(i=0;i<4;i++){emit(object+0x128+i*70u,3);emit(object+0x138+i*70u,1);}
    for(i=0;i<4;i++)emit(object+0x148+i*70u,1);
    emit(object+0x722,3);emit(object+0x72e,9);
    mesh=word(object+0x71e);vertices=word(mesh+4);
    for(i=0;i<4;i++)for(j=0;j<3;j++)narrow(vertices+i*8+j*2,2);
    narrow(mesh+12,2);narrow(object+0xde9,1);
    emit(object+0xf8,1);emit(object+0xf0,1);emit(object+0xc0,1);emit(object+0xa0,1);
}
static void gun(int input)
{
    uint32_t mesh=word(object+0x366),n=mesh?reference_game_word(mesh,2):0,vertices=mesh?word(mesh+4):0;unsigned i,j;
    if(input){oracle_put(mesh!=0);for(i=0;i<4;i++)narrow(object+0x784+i,1);for(i=0;i<4;i++)narrow(object+0x788+i*2,2);oracle_put(n);}
    for(i=0;i<n;i++)for(j=0;j<3;j++)narrow(vertices+i*8+j*2,2);
}
