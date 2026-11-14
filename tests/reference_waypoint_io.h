/* Shared test-only route snapshot. Requires reference_game.h and word(). */
static void wp_emit(uint32_t a,unsigned n){uint32_t i;for(i=0;i<n;i++)oracle_put(word(a+i*4u));}
static void wp_narrow(uint32_t a,unsigned n,int sign){uint32_t v=reference_game_word(a,n);oracle_put(sign&&n==2&&v>32767?v|0xffff0000u:v);}
static void wp_list(uint32_t global)
{
    uint32_t head=word(global),p=word(head),count=0;
    while(p!=head){if(count++>=100000)exit(8);p=word(p);}
    oracle_put(count);for(p=word(head);p!=head;p=word(p))oracle_put(word(p+8));
}
static void waypoint_state(void)
{
    uint32_t head=word(0x5468a0),p,id,o,i,lap_start=0;
    wp_emit(0x546870,4);wp_emit(0x546888,2);wp_emit(0x54689c,1);wp_emit(0x546898,1);wp_emit(0x546894,1);
    wp_list(0x5468a0);
    for(p=word(head);p!=head;p=word(p)){
        id=word(p+8);o=object_of(id);oracle_put(id);wp_emit(o+16,1);wp_emit(o,3);wp_emit(o+24,9);
        wp_narrow(o+0x4e,2,1);wp_narrow(o+0x4c,2,0);oracle_put(word(o+72)!=0);
        wp_emit(o+0x78,2);wp_narrow(o+0x80,1,0);wp_narrow(o+0x81,1,0);wp_narrow(o+0x82,2,0);
        wp_emit(o+0x84,4);for(i=0;i<8;i++)wp_narrow(o+0x94+i,1,0);
        if(reference_game_word(o+0x80,1)&&word(o+0x90)==0)lap_start=id;
    }
    oracle_put(lap_start);
    for(i=0;i<64;i++)wp_list(0x546770+i*4u);
    for(i=0;i<192;i++)wp_narrow(0x58ed40+i,1,0);
    wp_list(0x55b6a8);wp_list(0x55b6b8);wp_list(0x55b6bc);wp_list(0x54b48c);
}
