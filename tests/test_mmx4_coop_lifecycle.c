/* Ownership-boundary tests with a small deterministic native-call model.
 * These prove projection/lifecycle bookkeeping, not original game behavior;
 * the original executable stage/menu/vehicle matrix remains required. */
#include "mmx4_coop_internal.h"
#include <stdio.h>
static unsigned local_view;
int psx_mod_local_view_scope(void) {return (int)local_view;}
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static uint8_t ram[0x200000],p2[0xE4],p1_backup[0xE4];
static uint8_t p2_vehicle[0xB0],p1_vehicle_backup[0xB0];
static unsigned projected,ready=1,world_calls,observed_campaign,collect_calls[2];
static unsigned pause_commit,pause_exit,world_draws;
static unsigned reward_calls,save_calls,observed_menu_select;
static uint8_t saved_palette_dirty;
static int split_views;
static unsigned stage_script_calls,scene_calls,observed_controls;
static unsigned boundary_calls[2];
static unsigned dialogue_calls,dialogue_edge;
static unsigned dialogue_init_calls,dialogue_init_character,stage_script_projected;
static unsigned initial_arrival_model,initial_control_calls[2],incoming_calls[2];
static unsigned natural_arrivals,direct_arrivals,terrain_calls[2];
static unsigned direct_initializer_calls[2];
static uint8_t terrain_contact;
static unsigned flip_queries[2],flip_hits[2],flip_world_calls,flip_second_calls,flip_camera_calls;
static uint8_t p2_solid_bits;
static uint16_t inputs[2];
typedef struct { uint32_t address;PSXModFunctionFilterCallback filter; } Filter;
static Filter filters[32];static unsigned filter_count;
uint8_t psx_mod_read_byte(uint32_t a) {return ram[a&0x1FFFFFu];}
void psx_mod_write_byte(uint32_t a,uint8_t v) {ram[a&0x1FFFFFu]=v;}
uint16_t psx_mod_read_half(uint32_t a) {
    return (uint16_t)(psx_mod_read_byte(a)|(uint16_t)psx_mod_read_byte(a+1)<<8);
}
void psx_mod_write_half(uint32_t a,uint16_t v) {
    psx_mod_write_byte(a,(uint8_t)v);psx_mod_write_byte(a+1,(uint8_t)(v>>8));
}
uint32_t psx_mod_read_word(uint32_t a) {
    return (uint32_t)psx_mod_read_half(a)|(uint32_t)psx_mod_read_half(a+2)<<16;
}
void psx_mod_write_word(uint32_t a,uint32_t v) {
    psx_mod_write_half(a,(uint16_t)v);psx_mod_write_half(a+2,(uint16_t)(v>>16));
}
int psx_mod_register_function_filter_plugin(const char *id,uint32_t a,PSXModFunctionFilterCallback f) {
    (void)id;assert(filter_count<32);filters[filter_count++]=(Filter){a,f};return 1;
}
int psx_mod_register_function_entry_plugin(const char *id,uint32_t a,PSXModFunctionEntryCallback f) {
    (void)id;(void)a;(void)f;return 1;
}
void psx_mod_counter_add(const char *name,uint32_t amount) {
    if(!strcmp(name,"mmx4.coop.concurrent-stage-arrivals"))natural_arrivals+=amount;
    if(!strcmp(name,"mmx4.coop.concurrent-return-arrivals"))direct_arrivals+=amount;
}
void mmx4_coop_menu_assets(CPUState *cpu,unsigned seat) {(void)cpu;(void)seat;}
int mmx4_coop_split_scene_camera_begin(unsigned owner,uint8_t canonical[0xFC]) {
    (void)owner;(void)canonical;return 0;
}
void mmx4_coop_split_scene_camera_end(unsigned owner,const uint8_t canonical[0xFC],int committed) {
    (void)owner;(void)canonical;(void)committed;
}
uint8_t mmx4_coop_solid_contact_bits(uint32_t actor,unsigned seat) {
    return seat?p2_solid_bits:psx_mod_read_byte(actor+0x72);
}
int mmx4_coop_ready(void) {return ready;}
int mmx4_coop_split_views(void) {return split_views;}
int mmx4_coop_projected(void) {return (int)projected;}
uint8_t *mmx4_coop_second_body(void) {return p2;}
uint8_t *mmx4_coop_first_body(void) {return p1_backup;}
uint8_t *mmx4_coop_second_vehicle(void) {return p2_vehicle;}
uint8_t *mmx4_coop_first_vehicle(void) {return p1_vehicle_backup;}
void mmx4_coop_clear_current_attacks(void) {}
void mmx4_coop_split_flip_cameras(void) {++flip_camera_calls;}
void mmx4_coop_flip_second(void) {
    ++flip_second_calls;
    for(unsigned at=12;at<=28;at+=16) {
        uint32_t y=(uint32_t)p2[at]|(uint32_t)p2[at+1]<<8|(uint32_t)p2[at+2]<<16|(uint32_t)p2[at+3]<<24;
        y=0x08000000u-y;
        for(unsigned i=0;i<4;++i)p2[at+i]=(uint8_t)(y>>(i*8));
    }
}
uint16_t mmx4_coop_input(unsigned seat) {return inputs[seat];}
int mmx4_coop_finish(CPUState *cpu,uint32_t v) {cpu->gpr[2]=v;return 1;}
int mmx4_coop_combat_canonical_call(CPUState *cpu,uint32_t address,
    PSXModFunctionFilterCallback callback) {return callback(cpu,address);}
int mmx4_coop_alive(unsigned seat) {
    if(mmx4_coop_lifecycle_hidden(seat))return 0;
    uint8_t *body=seat?p2:p1_backup;
    if((!projected && !seat) || (projected && seat))
        return psx_mod_read_byte(MMX4_PLAYER) &&
            (psx_mod_read_byte(MMX4_PLAYER+0x5C)&127u) &&
            psx_mod_read_byte(MMX4_PLAYER+4)<2;
    return body[0] && (body[0x5C]&127u) && body[4]<2;
}
void mmx4_coop_enter_second(void) {
    assert(!projected);
    saved_palette_dirty=psx_mod_read_byte(0x80166BB0u);
    for(unsigned i=0;i<sizeof p2;++i) {
        p1_backup[i]=psx_mod_read_byte(MMX4_PLAYER+i);
        psx_mod_write_byte(MMX4_PLAYER+i,p2[i]);
    }
    projected=1;mmx4_coop_lifecycle_project();
    for(unsigned i=0;i<sizeof p2_vehicle;++i) {
        p1_vehicle_backup[i]=psx_mod_read_byte(MMX4_VEHICLE+i);
        psx_mod_write_byte(MMX4_VEHICLE+i,p2_vehicle[i]);
    }
}
void mmx4_coop_leave_second(void) {
    assert(projected);mmx4_coop_lifecycle_restore();
    for(unsigned i=0;i<sizeof p2;++i) {
        p2[i]=psx_mod_read_byte(MMX4_PLAYER+i);
        psx_mod_write_byte(MMX4_PLAYER+i,p1_backup[i]);
    }
    projected=0;
    psx_mod_write_byte(0x80166BB0u,saved_palette_dirty);
    for(unsigned i=0;i<sizeof p2_vehicle;++i) {
        p2_vehicle[i]=psx_mod_read_byte(MMX4_VEHICLE+i);
        psx_mod_write_byte(MMX4_VEHICLE+i,p1_vehicle_backup[i]);
    }
}
uint32_t mmx4_coop_call(CPUState *cpu,uint32_t address,uint32_t a0,uint32_t a1) {
    uint32_t old4=cpu->gpr[4],old5=cpu->gpr[5],old2=cpu->gpr[2],result=0;
    cpu->gpr[4]=a0;cpu->gpr[5]=a1;
    for(unsigned i=0;i<filter_count;++i)if(filters[i].address==address && filters[i].filter(cpu,address)) {
        result=cpu->gpr[2];goto done;
    }
    switch(address) {
    case 0x80027BE4: {
        ++boundary_calls[projected];
        int camera_x=(int16_t)psx_mod_read_half(a0+10);
        int left=(int16_t)psx_mod_read_half(a0+0x1E);
        int right=(int16_t)psx_mod_read_half(a0+0x1C);
        int player_x=(int16_t)psx_mod_read_half(MMX4_PLAYER+10);
        if(camera_x<=left && player_x-8<left)
            psx_mod_write_half(MMX4_PLAYER+10,(uint16_t)(left+8));
        if(camera_x>right && player_x+8>=right+320)
            psx_mod_write_half(MMX4_PLAYER+10,(uint16_t)(right+312));
        break;
    }
    case 0x800350A4:
        psx_mod_write_byte(MMX4_PLAYER+0x46,2);break;
    case 0x80035048:
        psx_mod_write_byte(MMX4_PLAYER+5,1);psx_mod_write_byte(MMX4_PLAYER+6,0);break;
    case 0x80035848:
        ++incoming_calls[projected];
        psx_mod_write_byte(MMX4_PLAYER+5,0);psx_mod_write_byte(MMX4_PLAYER+6,0);
        psx_mod_write_byte(MMX4_PLAYER+0x67,255);psx_mod_write_byte(MMX4_PLAYER+0x17,1);
        psx_mod_write_word(MMX4_PLAYER+0x24,0xFFF80000u);break;
    case 0x800358A4: {
        ++direct_initializer_calls[projected];
        unsigned y=psx_mod_read_byte(MMX4_PLAY+0x0D)?2507u:715u;
        if(psx_mod_read_byte(MMX4_PLAY+0x0C)==3)y=171u;
        y-=psx_mod_read_byte(MMX4_PLAYER+2)!=0;
        psx_mod_write_word(MMX4_PLAYER+12,y<<16);
        mmx4_coop_call(cpu,0x80035EA4u,MMX4_PLAYER,0);
        psx_mod_write_byte(MMX4_PLAYER+5,2);psx_mod_write_byte(MMX4_PLAYER+6,0);
        psx_mod_write_byte(MMX4_PLAY+0x1C,0);
        break;
    }
    case 0x80035EA4:
        psx_mod_write_word(MMX4_PLAYER+0x68,
            psx_mod_read_byte(MMX4_PLAYER+2)?0x800F8BC8u:0x800F8BC4u);break;
    case 0x8002C614:
        ++terrain_calls[projected];
        psx_mod_write_byte(MMX4_PLAYER+0x70,terrain_contact);break;
    case 0x80031540:
        if(!psx_mod_read_byte(MMX4_PLAYER+6))psx_mod_write_byte(MMX4_PLAYER+6,1);
        else {psx_mod_write_byte(MMX4_PLAYER+4,3);psx_mod_write_byte(MMX4_PLAYER+3,0);}
        break;
    case 0x80031410:
        psx_mod_write_byte(MMX4_PLAYER+4,1);psx_mod_write_byte(MMX4_PLAYER+5,2);
        psx_mod_write_byte(MMX4_PLAYER+6,0);psx_mod_write_byte(MMX4_PLAYER+0x46,0);
        break;
    case 0x8001FF8C:
        if(psx_mod_read_half(0x80166C0Cu)&0x800u)psx_mod_write_byte(MMX4_PLAY+1,2);
        else if(psx_mod_read_byte(MMX4_PLAYER+4)==3)psx_mod_write_byte(MMX4_PLAY+1,1);
        else ++world_calls;
        break;
    case 0x80021158:
        ++world_calls;observed_campaign=psx_mod_read_byte(MMX4_PLAY+0x43);break;
    case 0x80023D68:
        ++world_draws;assert(!projected);
        assert(psx_mod_read_byte(MMX4_PLAY+0x43)==psx_mod_read_byte(MMX4_PLAYER+2));
        break;
    case 0x80021D20:
        ++dialogue_calls;dialogue_edge=psx_mod_read_half(0x80166C0Cu);
        observed_campaign=psx_mod_read_byte(MMX4_PLAY+0x43);assert(!projected);break;
    case 0x8002217C:
        ++dialogue_init_calls;assert(!projected);
        dialogue_init_character=psx_mod_read_byte(MMX4_PLAYER+2);
        observed_campaign=psx_mod_read_byte(MMX4_PLAY+0x43);
        psx_mod_write_byte(0x80166BB0u,(uint8_t)(psx_mod_read_byte(0x80166BB0u)|1u));
        break;
    case 0x800C4EC8:
        ++flip_queries[projected];
        if(flip_hits[projected])psx_mod_write_byte(a0+5,1);
        break;
    case 0x800C4F40:
        ++flip_world_calls;
        for(unsigned at=12;at<=28;at+=16)
            psx_mod_write_word(MMX4_PLAYER+at,0x08000000u-psx_mod_read_word(MMX4_PLAYER+at));
        break;
    case 0x8002FCAC:
        observed_campaign=psx_mod_read_byte(MMX4_PLAY+0x43);
        observed_menu_select=psx_mod_read_half(0x80166C0Cu)&0x100u;
        if(pause_commit) {
            psx_mod_write_byte(MMX4_PLAYER+0x5C,(uint8_t)(psx_mod_read_byte(MMX4_PLAYER+0x5C)+1));
            psx_mod_write_byte(MMX4_PLAY+0x5C,(uint8_t)(psx_mod_read_byte(MMX4_PLAY+0x5C)-1));
        }
        if(pause_exit) {
            /* Native 80031014 requests a palette upload, 80031064 updates
             * the world, and 80031130 draws it after changing PLAY+1. */
            psx_mod_write_byte(0x80166BB0u,
                (uint8_t)(psx_mod_read_byte(0x80166BB0u)|1u));
            mmx4_coop_call(cpu,0x80021158u,0,0);
            psx_mod_write_byte(MMX4_PLAY+1,0);
            mmx4_coop_call(cpu,0x80023D68u,0,0);
        }else {
            mmx4_coop_call(cpu,0x80023D68u,0,0);
        }
        break;
    case 0x800C00BC:
        ++collect_calls[projected?1:0];
        if(psx_mod_read_word(MMX4_PLAYER+8)==psx_mod_read_word(a0+8)) {
            psx_mod_write_byte(a0+4,2);psx_mod_write_byte(a0+0x80,2);
            psx_mod_write_byte(MMX4_PLAY+0x5C,(uint8_t)(psx_mod_read_byte(MMX4_PLAY+0x5C)+1));
            mmx4_coop_call(cpu,0x800C03BCu,1,0);
        }
        break;
    case 0x800BF730:
        if(psx_mod_read_byte(a0+4)==2) {
            psx_mod_write_byte(MMX4_PLAYER+0x5C,(uint8_t)(psx_mod_read_byte(MMX4_PLAYER+0x5C)+1));
            uint8_t n=(uint8_t)(psx_mod_read_byte(a0+0x80)-1);
            psx_mod_write_byte(a0+0x80,n);if(!n) {
                psx_mod_write_byte(a0+4,3);mmx4_coop_call(cpu,0x800C03BCu,0,0);
            }
        }
        break;
    case 0x800C03BC:
        for(unsigned i=0x10;i<0x18;++i)psx_mod_write_byte(MMX4_PLAY+i,(uint8_t)a0);
        break;
    case 0x80035A6C:
        for(unsigned i=0x12;i<=0x1A;++i)psx_mod_write_byte(MMX4_PLAY+i,1);
        psx_mod_write_byte(MMX4_PLAY+0x1C,1);psx_mod_write_byte(MMX4_PLAYER+4,3);
        break;
    case 0x80033BC8:
        if(psx_mod_read_byte(MMX4_PLAYER+0x5C)==128) {
            psx_mod_write_byte(MMX4_PLAYER+0x5C,0);
            psx_mod_write_byte(MMX4_PLAYER+4,2);
            psx_mod_write_byte(MMX4_PLAY+0x1C,1);
        }
        break;
    case 0x8001FA24:
        ++reward_calls;
        psx_mod_write_byte(MMX4_PLAY+0x59,(uint8_t)(psx_mod_read_byte(MMX4_PLAY+0x59)|
            (1u<<(psx_mod_read_byte(MMX4_PLAY+0x26)-1u))));
        psx_mod_write_byte(MMX4_PLAYER+0xB9,(uint8_t)(psx_mod_read_byte(MMX4_PLAYER+0xB9)|
            (1u<<(psx_mod_read_byte(MMX4_PLAY+0x26)-1u))));
        psx_mod_write_word(MMX4_PLAY,9);
        for(unsigned at=0x26;at<0x36;++at)psx_mod_write_byte(MMX4_PLAY+at,0);
        break;
    case 0x8001C07C:
    case 0x8001C3E8: {
        ++save_calls;assert(!projected);
        /* Original native record offsets: character, maxHP, additional
         * upgrade, armor, completed stages, story and heart/tank flags. */
        static const unsigned fields[]={0x43,0x46,0x48,0x47,0x59,0x5F,0x5A,0x5B};
        uint32_t record=address==0x8001C3E8?0x800F1D90u:0x80168000u;
        for(unsigned i=0;i<8;++i)psx_mod_write_byte(record+i,psx_mod_read_byte(MMX4_PLAY+fields[i]));
        break;
    }
    case 0x800C6EDC:
        psx_mod_write_byte(MMX4_PLAY+0x47,2);psx_mod_write_byte(MMX4_PLAYER+0xA7,2);break;
    case 0x8002166C:
        ++stage_script_calls;observed_campaign=psx_mod_read_byte(MMX4_PLAY+0x43);
        stage_script_projected=projected;
        if(psx_mod_read_byte(MMX4_PLAY)==3)assert(!projected);
        mmx4_coop_call(cpu,0x80036AE4u,0x15,0x40);
        psx_mod_write_byte(MMX4_PLAY+0x1D,9);break;
    case 0x800C2BE0:
        ++scene_calls;observed_campaign=psx_mod_read_byte(MMX4_PLAY+0x43);
        if(psx_mod_read_byte(a0+0x72)&8u) {
            psx_mod_write_word(MMX4_PLAYER+8,psx_mod_read_word(a0+8));
            psx_mod_write_word(MMX4_PLAYER+12,psx_mod_read_word(a0+12));
            mmx4_coop_call(cpu,0x80036AE4u,0x14,0x40);
            psx_mod_write_byte(a0+5,1);
        }
        break;
    case 0x800BD654:
        ++scene_calls;observed_campaign=psx_mod_read_byte(MMX4_PLAY+0x43);
        if(psx_mod_read_word(MMX4_PLAYER+8)>=0x1000000u) {
            mmx4_coop_call(cpu,0x80036AE4u,0x15,0x40);
            psx_mod_write_byte(MMX4_PLAY+0x1D,8);
        }
        break;
    case 0x80036AE4:
        psx_mod_write_byte(MMX4_PLAYER+0xC0,1);psx_mod_write_byte(MMX4_PLAY+0x1C,1);break;
    case 0x80036B18:
        psx_mod_write_byte(MMX4_PLAYER+0xC0,0);psx_mod_write_byte(MMX4_PLAY+0x1C,0);break;
    case 0x800311EC:
        observed_controls=psx_mod_read_half(MMX4_PLAYER+0x7C);
        if(initial_arrival_model && psx_mod_read_byte(MMX4_PLAYER+4)==1 &&
           !psx_mod_read_byte(MMX4_PLAYER+5)) {
            if(initial_arrival_model==2 && psx_mod_read_byte(MMX4_PLAYER+0xBE)) {
                /* Native 80031434..6C postpones all grounding checks until
                 * the authored geometry countdown reaches zero. A normal
                 * collision box in this modeled ceiling kills the body. */
                unsigned wait=psx_mod_read_byte(MMX4_PLAYER+0xBE);
                if(psx_mod_read_word(MMX4_PLAYER+0x68))
                    psx_mod_write_byte(MMX4_PLAYER+0x5C,0);
                psx_mod_write_byte(MMX4_PLAYER+0xBE,(uint8_t)(wait-1));
                if(wait==1)mmx4_coop_call(cpu,0x80035EA4u,MMX4_PLAYER,0);
                psx_mod_write_word(MMX4_PLAYER+12,
                    psx_mod_read_word(MMX4_PLAYER+12)+8u*65536u);
                break;
            }
            /* Model the distinct native touchdown and animation-completion
             * boundaries; leader input cannot advance this private body. */
            ++initial_control_calls[projected];
            psx_mod_write_byte(MMX4_PLAYER+6,1);psx_mod_write_byte(MMX4_PLAYER+0x67,0);
            for(unsigned i=0;i<3;++i)psx_mod_write_byte(0x801419F4u+i*0x54u,1);
            if(initial_control_calls[projected]>=3) {
                psx_mod_write_byte(MMX4_PLAYER+5,2);psx_mod_write_byte(MMX4_PLAYER+6,0);
                psx_mod_write_byte(MMX4_PLAY+0x1C,0);
            }
        }
        break;
    case 0x800C1E7C:
        result=psx_mod_read_word(MMX4_PLAYER+8)==psx_mod_read_word(a0+8);break;
    case 0x800C1994:
        ++scene_calls;observed_campaign=psx_mod_read_byte(MMX4_PLAY+0x43);
        if(!psx_mod_read_byte(a0+5) &&
           psx_mod_read_word(MMX4_PLAYER+8)==psx_mod_read_word(a0+8)) {
            psx_mod_write_byte(MMX4_PLAYER+0xC4,1);psx_mod_write_byte(a0+5,1);
        }else if(psx_mod_read_byte(a0+5)==3) {
            psx_mod_write_word(MMX4_PLAYER+8,psx_mod_read_word(MMX4_PLAYER+8)+0x10000u);
            psx_mod_write_byte(MMX4_PLAYER+0xC4,0);psx_mod_write_byte(a0+5,4);
        }
        break;
    default: break;
    }
done:
    cpu->gpr[4]=old4;cpu->gpr[5]=old5;cpu->gpr[2]=old2;return result;
}

#include "../src/mods/mmx4_coop_lifecycle.c"
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void reset(void) {
    CPUState cpu={0};memset(ram,0,sizeof ram);memset(p2,0,sizeof p2);
    memset(p2_vehicle,0,sizeof p2_vehicle);
    fresh_game(&cpu,0);projected=local_view=0;inputs[0]=inputs[1]=0;
    world_calls=observed_campaign=pause_commit=pause_exit=world_draws=0;
    stage_script_calls=scene_calls=observed_controls=p2_solid_bits=0;
    memset(boundary_calls,0,sizeof boundary_calls);
    dialogue_calls=dialogue_edge=dialogue_init_calls=dialogue_init_character=stage_script_projected=0;
    initial_arrival_model=0;
    memset(initial_control_calls,0,sizeof initial_control_calls);
    memset(incoming_calls,0,sizeof incoming_calls);
    memset(terrain_calls,0,sizeof terrain_calls);
    memset(direct_initializer_calls,0,sizeof direct_initializer_calls);
    natural_arrivals=direct_arrivals=0;terrain_contact=8;
    collect_calls[0]=collect_calls[1]=0;ready=1;split_views=0;
    psx_mod_write_byte(MMX4_PLAYER,1);psx_mod_write_byte(MMX4_PLAYER+3,1);
    psx_mod_write_byte(MMX4_PLAYER+4,1);psx_mod_write_byte(MMX4_PLAYER+0x5C,16);
    psx_mod_write_byte(MMX4_PLAYER+5,2);
    p2[0]=p2[3]=p2[4]=1;p2[5]=2;p2[2]=1;p2[0x5C]=12;
    psx_mod_write_byte(MMX4_PLAY,6);psx_mod_write_byte(MMX4_PLAY+0x46,32);
    psx_mod_write_byte(MMX4_PLAY+0x44,2);
    mmx4_coop_enter_second();mmx4_coop_leave_second();
}
static void prepare_initial_arrival(void) {
    reset();
    psx_mod_write_byte(PLAY+0x1C,1);psx_mod_write_byte(PLAY+0x1E,1);
    psx_mod_write_byte(PLAY+0x1F,1);psx_mod_write_byte(PLAYER+5,0);
    psx_mod_write_byte(PLAYER+0x67,255);
    psx_mod_write_word(PLAYER+8,72u<<16);psx_mod_write_word(PLAYER+12,202u<<16);
    p2[10]=96;p2[14]=202;
}
static void prepare_direct_spawn(CPUState *cpu,unsigned campaign) {
    reset();
    psx_mod_write_byte(PLAY+0x0C,6);psx_mod_write_byte(PLAY+0x1D,2);
    psx_mod_write_byte(PLAY+0x1C,1);psx_mod_write_byte(PLAY+0x1E,255);
    psx_mod_write_byte(PLAY+0x1F,1);psx_mod_write_byte(PLAYER+5,0);
    psx_mod_write_byte(PLAYER+2,(uint8_t)campaign);
    psx_mod_write_byte(PLAY+0x43,(uint8_t)campaign);p2[2]=(uint8_t)(campaign^1u);
    cpu->gpr[31]=0x80035834u;cpu->gpr[4]=PLAYER;
    direct_spawn(cpu,0x800358A4u);
    /* Original358A4 at Cyber6/0 checkpoint2 replaces authoredY512 with
     * X715/Zero714, installs the character box and releases PLAY+1C. P2's
     * ordinary enrollment starts with that same Y; X/P2 can remain one pixel
     * above its floor until its own original direct initializer runs. */
    psx_mod_write_word(PLAYER+8,72u<<16);
    psx_mod_write_word(PLAYER+12,(715u-campaign)<<16);
    psx_mod_write_byte(PLAYER+5,2);psx_mod_write_byte(PLAY+0x1C,0);
    psx_mod_write_byte(PLAYER+0xBE,3);
    mmx4_coop_call(cpu,0x80035EA4u,PLAYER,0);
    mmx4_coop_enter_second();
    psx_mod_write_word(PLAYER+8,96u<<16);
    psx_mod_write_word(PLAYER+12,(715u-campaign)<<16);
    psx_mod_write_byte(PLAYER+0xBE,3);
    mmx4_coop_call(cpu,0x80035EA4u,PLAYER,0);
    terrain_contact=(uint8_t)(campaign?0:8);
    mmx4_coop_call(cpu,0x8002C614u,PLAYER,0);terrain_contact=8;
    mmx4_coop_leave_second();
    for(unsigned layer=0;layer<3;++layer)psx_mod_write_byte(0x801419F4u+layer*0x54u,1);
    psx_mod_write_half(0x801419B0u+0x1C,1888);
}
int main(void) {
    CPUState cpu={0};reset();
    /* The ordinary unlocked enrollment policy is retained. READY has not
     * begun the native beam yet, and genuine scene locks remain ineligible. */
    CHECK(mmx4_coop_lifecycle_can_enroll());
    psx_mod_write_byte(PLAY+0x1C,1);CHECK(!mmx4_coop_lifecycle_can_enroll());
    for(unsigned gate=0;gate<22;++gate) {
        prepare_initial_arrival();CHECK(mmx4_coop_lifecycle_can_enroll());
        switch(gate) {
        case 0:psx_mod_write_byte(PLAY,5);break;
        case 1:psx_mod_write_byte(PLAY+1,1);break;
        case 2:psx_mod_write_byte(PLAY+0x1C,2);break;
        case 3:psx_mod_write_byte(PLAY+0x1E,0);break;
        case 4:psx_mod_write_byte(PLAY+0x1F,0);break;
        case 5:psx_mod_write_byte(PLAYER,0);break;
        case 6:psx_mod_write_byte(PLAYER+3,0);break;
        case 7:psx_mod_write_byte(PLAYER+4,2);break;
        case 8:psx_mod_write_byte(PLAYER+5,2);break;
        case 9:psx_mod_write_byte(PLAYER+0x67,1);break;
        case 10:psx_mod_write_byte(PLAYER+0x5C,0);break;
        case 11:psx_mod_write_byte(PLAYER+0xC0,1);break;
        case 12:psx_mod_write_byte(PLAYER+0xC3,1);break;
        case 13:psx_mod_write_byte(PLAYER+0xC4,1);break;
        case 14:psx_mod_write_byte(PLAYER+0xC5,255);break;
        case 15:psx_mod_write_byte(PLAYER+0xD9,1);break;
        case 16:psx_mod_write_byte(PLAY+0x0F,1);break;
        case 17:psx_mod_write_byte(PLAY+0x10,1);break;
        case 18:script_active=1;break;
        case 19:carry_dead=1;break;
        case 20:carry_first_dead=1;break;
        case 21:departed=carry_departure=1;break;
        }
        CHECK(!mmx4_coop_lifecycle_can_enroll());
    }
    /* Both characters begin their own native beam once, at their initialized
     * positions. P1 walking/dashing after touchdown cannot hide or restart
     * P2's remaining animation; P2 can also finish before P1 releases +1C. */
    for(unsigned campaign=0;campaign<2;++campaign)for(unsigned finish_first=0;finish_first<2;++finish_first) {
        prepare_initial_arrival();
        psx_mod_write_byte(PLAYER+2,(uint8_t)campaign);
        psx_mod_write_byte(PLAY+0x43,(uint8_t)campaign);p2[2]=(uint8_t)(campaign^1u);
        initial_arrival_model=1;
        mmx4_coop_lifecycle_enrolled(&cpu);
        CHECK(initial_arrival && !warp_phase && !mmx4_coop_lifecycle_hidden(1));
        CHECK(natural_arrivals==1 && !direct_arrivals);
        CHECK(p2[5]==0 && p2[0x67]==255 && p2[0x17]==1 && incoming_calls[1]==1);
        CHECK(body_word(p2,8)==96u<<16 && body_word(p2,12)==202u<<16);
        CHECK(body_word(p2,8)==psx_mod_read_word(PLAYER+8)+(24u<<16));
        CHECK(psx_mod_read_byte(PLAYER+5)==0 && psx_mod_read_byte(PLAYER+0x67)==255);
        for(unsigned tick=0;tick<3;++tick) {
            if(tick && !finish_first) {
                psx_mod_write_byte(PLAYER+5,(uint8_t)(tick==1?4:13));
                psx_mod_write_byte(PLAYER+0x67,0);psx_mod_write_byte(PLAYER+0x89,8);
                psx_mod_write_byte(PLAY+0x1C,0);
            }
            uint8_t first_body[0xE4];
            for(unsigned i=0;i<sizeof first_body;++i)first_body[i]=psx_mod_read_byte(PLAYER+i);
            mmx4_coop_lifecycle_tick(&cpu);
            CHECK(mmx4_coop_lifecycle_can_tick() && !warp_phase && p2[0] && p2[3]);
            mmx4_coop_enter_second();mmx4_coop_call(&cpu,0x800311ECu,PLAYER,0);mmx4_coop_leave_second();
            CHECK(initial_control_calls[1]==tick+1 && incoming_calls[1]==1);
            CHECK(psx_mod_read_byte(PLAY+0x1C)==(finish_first || !tick));
            for(unsigned i=0;i<sizeof first_body;++i)CHECK(first_body[i]==psx_mod_read_byte(PLAYER+i));
            for(unsigned layer=0;layer<3;++layer)CHECK(!psx_mod_read_byte(0x801419F4u+layer*0x54u));
            CHECK(body_word(p2,8)==96u<<16 && body_word(p2,12)==202u<<16);
            CHECK(initial_arrival==(tick<2));
        }
        CHECK(p2[5]==2 && !p2[0x67] && !warp_phase && !projected);
        CHECK(p2[0x5C]==12 && psx_mod_read_byte(PLAYER+0x5C)==16);
    }
    /* Cyber 6/1 begins at Y768 with an authored eight-tick geometry wait;
     * a delayed enrollment retains the remaining wait at its current Y.
     * The counterpart uses its own character box when native movement
     * enables collision, including an enrollment after the wait expired. */
    for(unsigned campaign=0;campaign<2;++campaign)for(unsigned remaining=0;remaining<=8;++remaining) {
        prepare_initial_arrival();
        psx_mod_write_byte(PLAYER+2,(uint8_t)campaign);
        psx_mod_write_byte(PLAY+0x43,(uint8_t)campaign);p2[2]=(uint8_t)(campaign^1u);
        psx_mod_write_byte(PLAYER+0xBE,(uint8_t)remaining);
        psx_mod_write_word(PLAYER+8,328u<<16);
        uint32_t x=352u<<16;
        for(unsigned i=0;i<4;++i)p2[8+i]=(uint8_t)(x>>(8*i));
        uint32_t y=(768u+(8u-remaining)*8u)<<16;
        psx_mod_write_word(PLAYER+12,y);
        for(unsigned i=0;i<4;++i)p2[12+i]=(uint8_t)(y>>(8*i));
        /* Model an already initialized ordinary box, as the old enrollment
         * path installed before entering the native initial beam. */
        p2[0x68]=0xC4;p2[0x69]=0x8B;p2[0x6A]=0x0F;p2[0x6B]=0x80;
        initial_arrival_model=2;mmx4_coop_lifecycle_enrolled(&cpu);
        CHECK(p2[0xBE]==remaining && psx_mod_read_byte(PLAYER+0xBE)==remaining);
        CHECK(body_word(p2,8)==x && psx_mod_read_word(PLAYER+8)==328u<<16);
        CHECK(body_word(p2,12)==y && body_word(p2,0x68)==
            (remaining?0u:(campaign?0x800F8BC4u:0x800F8BC8u)));
        for(unsigned tick=0;tick<remaining;++tick) {
            mmx4_coop_enter_second();mmx4_coop_call(&cpu,0x800311ECu,PLAYER,0);mmx4_coop_leave_second();
            CHECK(p2[0xBE]==remaining-tick-1 && p2[0x5C]==12);
            CHECK(p2[5]==0 && p2[6]==0 && p2[0x67]==255 && initial_arrival);
            CHECK(body_word(p2,12)==y+(tick+1)*8u*65536u);
            CHECK(body_word(p2,8)==x && psx_mod_read_word(PLAYER+8)==328u<<16);
            CHECK(body_word(p2,0x68)==(tick+1==remaining?
                (campaign?0x800F8BC4u:0x800F8BC8u):0u));
            CHECK(psx_mod_read_byte(PLAYER+0xBE)==remaining && psx_mod_read_word(PLAYER+12)==y);
        }
        CHECK(!p2[0xBE] && body_word(p2,12)==832u<<16 && incoming_calls[1]==1);
        for(unsigned tick=0;tick<3;++tick) {
            mmx4_coop_enter_second();mmx4_coop_call(&cpu,0x800311ECu,PLAYER,0);mmx4_coop_leave_second();
        }
        CHECK(!initial_arrival && p2[5]==2 && !p2[0x67] && p2[0x5C]==12);
    }
    prepare_initial_arrival();mmx4_coop_lifecycle_enrolled(&cpu);
    psx_mod_write_byte(PLAY+0x1C,0);p2[0x67]=0;inputs[1]=SELECT;
    for(unsigned tick=0;tick<100;++tick)mmx4_coop_lifecycle_tick(&cpu);
    CHECK(initial_arrival && !departed && !warp_phase);
    mmx4_coop_lifecycle_reset();CHECK(!initial_arrival);
    /* The native direct-init caller is the one-shot authority. A private
     * projection cannot overwrite that marker; unrelated/story entries,
     * frontend scopes, and normal policies never authorize another P1 beam. */
    for(unsigned gate=0;gate<12;++gate) {
        prepare_direct_spawn(&cpu,0);CHECK(direct_initial_spawn);
        direct_initial_spawn=0;
        psx_mod_write_byte(PLAY+0x1C,1);psx_mod_write_byte(PLAYER+5,0);
        switch(gate) {
        case 0:cpu.gpr[31]=0x80035838u;break;
        case 1:cpu.gpr[4]=PLAYER+0x100;break;
        case 2:psx_mod_write_byte(PLAY+0x0C,12);break;
        case 3:psx_mod_write_byte(PLAY+0x0C,1);break;
        case 4:psx_mod_write_byte(PLAY+0x1E,0);break;
        case 5:psx_mod_write_byte(PLAY+0x1F,0);break;
        case 6:psx_mod_write_byte(PLAY+0x1C,0);break;
        case 7:psx_mod_write_byte(PLAY,5);break;
        case 8:psx_mod_write_byte(PLAY+1,1);break;
        case 9:mmx4_coop_enter_second();break;
        case 10:local_view=1;break;
        case 11:psx_mod_write_byte(PLAY+0x0C,3);psx_mod_write_byte(PLAY+0x1D,0);break;
        }
        direct_spawn(&cpu,0x800358A4u);CHECK(!direct_initial_spawn);
        if(projected)mmx4_coop_leave_second();
    }
    prepare_direct_spawn(&cpu,0);mmx4_coop_enter_second();
    direct_spawn(&cpu,0x800358A4u);CHECK(direct_initial_spawn);mmx4_coop_leave_second();
    uint32_t marker_digest=mmx4_coop_lifecycle_digest(2166136261u);
    direct_initial_spawn=0;
    CHECK(mmx4_coop_lifecycle_digest(2166136261u)!=marker_digest);
    prepare_direct_spawn(&cpu,0);psx_mod_write_byte(PLAY+0x0C,3);
    psx_mod_write_byte(PLAY+0x1D,1);psx_mod_write_byte(PLAY+0x1C,1);
    direct_spawn(&cpu,0x800358A4u);CHECK(direct_initial_spawn);
    for(unsigned boundary=0;boundary<5;++boundary) {
        prepare_direct_spawn(&cpu,0);CHECK(direct_initial_spawn);
        switch(boundary) {
        case 0:fresh_game(&cpu,0);break;
        case 1:mmx4_coop_lifecycle_reset();break;
        case 2:stage_selection(&cpu,0);break;
        case 3:warp_pending=1;warp_begin(&cpu);break;
        case 4:stage_initialization(&cpu,0);break;
        }
        CHECK(!direct_initial_spawn);
    }
    for(unsigned campaign=0;campaign<2;++campaign)for(unsigned finish_first=0;finish_first<2;++finish_first) {
        prepare_direct_spawn(&cpu,campaign);CHECK(direct_pair_candidate());
        uint32_t first_x=psx_mod_read_word(PLAYER+8),first_y=psx_mod_read_word(PLAYER+12);
        uint32_t second_x=body_word(p2,8),second_y=(714u+campaign)<<16;
        uint32_t first_box=psx_mod_read_word(PLAYER+0x68),second_box=body_word(p2,0x68);
        initial_arrival_model=1;mmx4_coop_lifecycle_enrolled(&cpu);
        CHECK(initial_arrival && !direct_initial_spawn && !warp_phase && !projected);
        CHECK(incoming_calls[0]==1 && incoming_calls[1]==1 && direct_arrivals==1 && !natural_arrivals);
        CHECK(!direct_initializer_calls[0] && direct_initializer_calls[1]==1);
        CHECK(psx_mod_read_byte(PLAY+0x1C)==1);
        CHECK(!psx_mod_read_byte(PLAYER+0xBE) && !p2[0xBE]);
        CHECK(psx_mod_read_word(PLAYER+0x68)==first_box && body_word(p2,0x68)==second_box);
        CHECK(psx_mod_read_word(PLAYER+8)==first_x && body_word(p2,8)==second_x);
        CHECK(psx_mod_read_word(PLAYER+12)==first_y && body_word(p2,12)==second_y);
        CHECK(second_x==first_x+(24u<<16) && !mmx4_coop_lifecycle_hidden(1));
        CHECK(boundary_calls[1]==1 && terrain_calls[1]==2);
        unsigned shared_released=0;
        for(unsigned pass=0;pass<2;++pass) {
            unsigned seat=pass?finish_first:(finish_first^1u);
            for(unsigned tick=0;tick<3;++tick) {
                if(seat)mmx4_coop_enter_second();
                mmx4_coop_call(&cpu,0x800311ECu,PLAYER,0);
                if(seat)mmx4_coop_leave_second();
                if(!seat && tick==2)shared_released=1;
                CHECK(psx_mod_read_byte(PLAY+0x1C)==!shared_released);
            }
        }
        CHECK(!initial_arrival && !psx_mod_read_byte(PLAY+0x1C));
        CHECK(psx_mod_read_byte(PLAYER+5)==2 && p2[5]==2);
        CHECK(!psx_mod_read_byte(PLAYER+0x67) && !p2[0x67]);
        CHECK(psx_mod_read_byte(PLAYER+0x5C)==16 && p2[0x5C]==12);
        CHECK(psx_mod_read_word(PLAYER+8)==first_x && body_word(p2,8)==second_x);
        CHECK(psx_mod_read_word(PLAYER+12)==first_y && body_word(p2,12)==second_y);
    }
    /* A bounds clamp uses native terrain at the resulting position. Fresh
     * terrain +70, not a stale +89 ground bit, decides whether a pair is safe. */
    for(unsigned floor=0;floor<2;++floor) {
        prepare_direct_spawn(&cpu,0);
        mmx4_coop_enter_second();psx_mod_write_word(PLAYER+8,424u<<16);mmx4_coop_leave_second();
        psx_mod_write_half(0x801419B0u+10,101);psx_mod_write_half(0x801419B0u+0x1C,100);
        terrain_contact=(uint8_t)(floor?8:0);
        mmx4_coop_lifecycle_enrolled(&cpu);
        CHECK(body_word(p2,8)==412u<<16 && terrain_calls[1]==2 && boundary_calls[1]==1);
        CHECK(incoming_calls[0]==floor && incoming_calls[1]==1 && direct_arrivals==floor);
        CHECK(!direct_initial_spawn && initial_arrival==floor);
    }
    for(unsigned gate=0;gate<42;++gate) {
        prepare_direct_spawn(&cpu,0);
        switch(gate) {
        case 0:psx_mod_write_byte(PLAYER,0);break;
        case 1:psx_mod_write_byte(PLAYER+3,0);break;
        case 2:psx_mod_write_byte(PLAYER+4,3);break;
        case 3:psx_mod_write_byte(PLAYER+5,4);break;
        case 4:psx_mod_write_byte(PLAYER+0x5C,0);break;
        case 5:psx_mod_write_word(PLAYER+0x68,0);break;
        case 6:psx_mod_write_byte(PLAYER+0x67,255);break;
        case 7:psx_mod_write_byte(PLAYER+0xBC,1);break;
        case 8:psx_mod_write_byte(PLAYER+0xC0,1);break;
        case 9:psx_mod_write_byte(PLAYER+0xC3,1);break;
        case 10:psx_mod_write_byte(PLAYER+0xC4,1);break;
        case 11:psx_mod_write_byte(PLAYER+0xC5,255);break;
        case 12:psx_mod_write_byte(PLAYER+0xD9,1);break;
        case 13:p2[0]=0;break;
        case 14:p2[3]=0;break;
        case 15:p2[4]=3;break;
        case 16:p2[5]=4;break;
        case 17:p2[0x5C]=0;break;
        case 18:memset(p2+0x68,0,4);break;
        case 19:p2[0x67]=255;break;
        case 20:p2[0xBC]=1;break;
        case 21:p2[0xC0]=1;break;
        case 22:p2[0xC3]=1;break;
        case 23:p2[0xC4]=1;break;
        case 24:p2[0xC5]=255;break;
        case 25:p2[0xD9]=1;break;
        case 26:psx_mod_write_byte(PLAY+0x0F,1);break;
        case 27:psx_mod_write_byte(PLAY+0x10,1);break;
        case 28:psx_mod_write_byte(PLAY,5);break;
        case 29:psx_mod_write_byte(PLAY+1,1);break;
        case 30:psx_mod_write_byte(PLAY+0x1E,0);break;
        case 31:psx_mod_write_byte(PLAY+0x1F,0);break;
        case 32:psx_mod_write_byte(PLAY+0x1C,1);break;
        case 33:script_active=1;break;
        case 34:warp_pending=1;break;
        case 35:warp_phase=1;break;
        case 36:departed=1;break;
        case 37:carry_dead=1;break;
        case 38:carry_first_dead=1;break;
        case 39:carry_departure=1;break;
        case 40:terrain_contact=0;p2[0x89]=8;break;
        case 41:terrain_contact=12;break;
        }
        mmx4_coop_lifecycle_enrolled(&cpu);
        CHECK(!direct_initial_spawn && !initial_arrival && !incoming_calls[0] && !direct_arrivals);
        /* An ordinary later return cannot inherit the rejected marker. */
        psx_mod_write_byte(PLAYER+4,1);psx_mod_write_byte(PLAYER+5,2);
        psx_mod_write_byte(PLAYER+0x67,0);psx_mod_write_byte(PLAY+0x1C,0);
        CHECK(!direct_pair_candidate());
    }
    reset();
    for(unsigned owner=0;owner<2;++owner) {
        reset();memset(flip_queries,0,sizeof flip_queries);memset(flip_hits,0,sizeof flip_hits);
        flip_world_calls=flip_second_calls=flip_camera_calls=0;
        psx_mod_write_byte(PLAY+0x0C,6);psx_mod_write_byte(PLAY+0x0D,1);
        const uint32_t actor=0x80165A30u;flip_hits[owner]=1;
        mmx4_coop_call(&cpu,0x800C4EC8u,actor,0);
        CHECK(psx_mod_read_byte(actor+5)==1 && flip_queries[0]==1 && flip_queries[1]==owner);
        CHECK(!projected && psx_mod_read_byte(PLAYER+0x5C)==16 && p2[0x5C]==12);
        psx_mod_write_word(PLAYER+12,971u<<16);psx_mod_write_word(PLAYER+28,(970u<<16)+123);
        p2[14]=0xCA;p2[15]=3; /* Y970. */
        uint8_t original[4];memcpy(original,p2+12,4);
        mmx4_coop_call(&cpu,0x800C4F40u,actor,0);
        CHECK(flip_world_calls==1 && flip_second_calls==1 && flip_camera_calls==1);
        CHECK(psx_mod_read_word(PLAYER+12)==1077u<<16 && body_word(p2,12)==1078u<<16);
        mmx4_coop_call(&cpu,0x800C4F40u,actor,0);
        CHECK(psx_mod_read_word(PLAYER+12)==971u<<16 && !memcmp(original,p2+12,4));
        CHECK(flip_world_calls==2 && flip_second_calls==2 && !projected);
    }
    reset();
    for(unsigned seat=0;seat<2;++seat)for(unsigned lock=0;lock<2;++lock)for(unsigned hidden=0;hidden<2;++hidden) {
        reset();psx_mod_write_byte(PLAY+0x1C,(uint8_t)lock);
        if(hidden) {warp_phase=2;warp_owner=seat;}
        if(seat)mmx4_coop_enter_second();
        psx_mod_write_byte(PLAYER+0x5C,128);
        mmx4_coop_call(&cpu,0x80033BC8u,PLAYER,0);
        CHECK(psx_mod_read_byte(PLAYER+4)==2 && !psx_mod_read_byte(PLAYER+0x5C));
        CHECK(psx_mod_read_byte(PLAY+0x1C)==lock);
        CHECK(living_death_partner());
        if(seat)mmx4_coop_leave_second();
        CHECK(seat?psx_mod_read_byte(PLAYER+0x5C)==16:p2[0x5C]==12);
    }
    reset();p2[0x5C]=0;p2[4]=3;psx_mod_write_byte(PLAYER+0x5C,128);
    mmx4_coop_call(&cpu,0x80033BC8u,PLAYER,0);
    CHECK(psx_mod_read_byte(PLAY+0x1C)==1); /* Native team-wipe lock. */
    reset();
    Inventory original_second=second_inventory,original_saved=saved_inventory;
    uint32_t inventory_digest=mmx4_coop_lifecycle_digest(2166136261u);
    mmx4_coop_lifecycle_view_save();
    mmx4_coop_enter_second();psx_mod_write_byte(PLAY+0x49,7);mmx4_coop_leave_second();
    mmx4_coop_lifecycle_view_restore();
    CHECK(!memcmp(&original_second,&second_inventory,sizeof original_second));
    CHECK(!memcmp(&original_saved,&saved_inventory,sizeof original_saved));
    CHECK(mmx4_coop_lifecycle_digest(2166136261u)==inventory_digest);
    for(unsigned campaign=0;campaign<2;++campaign) {
        reset();psx_mod_write_byte(PLAY+0x43,(uint8_t)campaign);
        psx_mod_write_byte(PLAYER+2,(uint8_t)campaign);p2[2]=(uint8_t)(campaign^1u);
        psx_mod_write_byte(0x801419F4u,1);
        psx_mod_write_half(0x801419B0u+0x1C,100);
        psx_mod_write_word(PLAYER+8,0x00081234u);
        mmx4_coop_enter_second();psx_mod_write_word(PLAYER+8,0xFFEC5678u);
        mmx4_coop_leave_second();
        mmx4_coop_lifecycle_camera_bounds(&cpu,0);
        CHECK(p2[10]==8 && p2[11]==0 && p2[8]==0x78 && p2[9]==0x56);
        CHECK(psx_mod_read_word(PLAYER+8)==0x00081234u);
        CHECK(!boundary_calls[0] && boundary_calls[1]==1 && !world_calls && !projected);
        psx_mod_write_half(0x801419B0u+10,101);
        mmx4_coop_enter_second();psx_mod_write_word(PLAYER+8,0x01C25678u);
        mmx4_coop_leave_second();mmx4_coop_lifecycle_camera_bounds(&cpu,0);
        CHECK(p2[10]==0x9C && p2[11]==1 && p2[8]==0x78 && p2[9]==0x56);
        CHECK(psx_mod_read_word(PLAYER+8)==0x00081234u);
    }
    reset();psx_mod_write_byte(0x801419F4u,1);
    psx_mod_write_word(PLAYER+8,0xFFEC1234u);
    mmx4_coop_lifecycle_camera_bounds(&cpu,1);
    CHECK(psx_mod_read_word(PLAYER+8)==0x00081234u);
    CHECK(boundary_calls[0]==1 && !boundary_calls[1] && !projected);
    reset();psx_mod_write_byte(0x801419F4u,1);p2[0xBC]=5;
    mmx4_coop_lifecycle_camera_bounds(&cpu,0);CHECK(!boundary_calls[1]);
    p2[0xBC]=0;p2[4]=3;p2[0x5C]=0;
    mmx4_coop_lifecycle_camera_bounds(&cpu,0);CHECK(!boundary_calls[1]);
    p2[4]=1;p2[0x5C]=12;warp_owner=0;warp_phase=2;
    mmx4_coop_lifecycle_camera_bounds(&cpu,0);CHECK(!boundary_calls[1]);
    warp_phase=0;psx_mod_write_byte(0x801419F4u,0);
    mmx4_coop_lifecycle_camera_bounds(&cpu,0);CHECK(!boundary_calls[1]);
    reset();
    /* Permanent upgrades belong to one campaign regardless of collector.
     * Projection must preserve a new heart, maxHP and shared tank energy. */
    psx_mod_write_byte(MMX4_PLAY+0x5C,0x89);psx_mod_write_half(MMX4_PLAY+0x5A,0x3000);
    mmx4_coop_enter_second();
    psx_mod_write_byte(MMX4_PLAY+0x46,34);psx_mod_write_byte(MMX4_PLAY+0x5A,4);
    psx_mod_write_byte(MMX4_PLAY+0x5C,0x88);mmx4_coop_lifecycle_project();
    mmx4_coop_leave_second();
    CHECK(psx_mod_read_byte(MMX4_PLAY+0x46)==34);
    CHECK(psx_mod_read_half(MMX4_PLAY+0x5A)==0x3004);
    CHECK(psx_mod_read_byte(MMX4_PLAY+0x5C)==0x88);
    mmx4_coop_enter_second();CHECK(psx_mod_read_byte(MMX4_PLAY+0x46)==34);
    CHECK(psx_mod_read_byte(MMX4_PLAY+0x5A)==4);mmx4_coop_leave_second();
    uint32_t digest=mmx4_coop_lifecycle_digest(2166136261u);
    mmx4_coop_enter_second();mmx4_coop_leave_second();
    CHECK(digest==mmx4_coop_lifecycle_digest(2166136261u));
    mmx4_coop_enter_second();digest=mmx4_coop_lifecycle_digest(2166136261u);
    saved_inventory.hp=7;
    CHECK(digest!=mmx4_coop_lifecycle_digest(2166136261u));
    saved_inventory.hp=0;mmx4_coop_leave_second();
    /* Changing the selected campaign changes P2's character, with the same
     * permanent maxHP/heart progress and fresh transient inventory. */
    psx_mod_write_byte(MMX4_PLAY+0x43,1);mmx4_coop_enter_second();
    CHECK(psx_mod_read_byte(MMX4_PLAY+0x43)==0);
    CHECK(psx_mod_read_byte(MMX4_PLAY+0x46)==34);mmx4_coop_leave_second();

    reset();inputs[0]=inputs[1]=START;mmx4_coop_call(&cpu,0x8001FF8Cu,PLAY,0);
    CHECK(menu_active && menu_owner==0 && psx_mod_read_byte(PLAY+1)==2);
    reset();inputs[1]=START;mmx4_coop_call(&cpu,0x8001FF8Cu,PLAY,0);
    CHECK(menu_active && menu_owner==1 && psx_mod_read_byte(PLAY+1)==2);
    inputs[1]=0;pause_commit=1;psx_mod_write_byte(PLAY+0x5C,0x88);
    mmx4_coop_call(&cpu,0x8002FCACu,MENU,0);
    CHECK(p2[0x5C]==13 && psx_mod_read_byte(PLAYER+0x5C)==16);
    CHECK(psx_mod_read_byte(PLAY+0x5C)==0x87 && observed_campaign==1);
    CHECK(world_draws==1);
    psx_mod_write_byte(0x80166BB0u,2);
    pause_commit=0;pause_exit=1;mmx4_coop_call(&cpu,0x8002FCACu,MENU,0);
    CHECK(!menu_active && !projected && world_calls==1 && observed_campaign==0);
    CHECK(world_draws==2 && psx_mod_read_byte(0x80166BB0u)==3);

    /* A P2 collection heals P2 on later native actor ticks. */
    reset();uint32_t actor=0x8013D000u;
    psx_mod_write_byte(actor+4,1);psx_mod_write_word(actor+8,100);
    p2[8]=100;mmx4_coop_call(&cpu,0x800C00BCu,actor,0);
    CHECK(collect_calls[0]==1 && collect_calls[1]==1);
    mmx4_coop_call(&cpu,0x800BF730u,actor,0);
    CHECK(p2[0x5C]==13 && psx_mod_read_byte(PLAYER+0x5C)==16);
    mmx4_coop_call(&cpu,0x800BF730u,actor,0);CHECK(p2[0x5C]==14);
    CHECK(!pickup_record(actor,0));
    /* Split refills remain owned after the collector moves away. Each native
     * update heals once, with no world pause or unrelated scene lock changes.
     * Unified keeps the original whole-world refill pause. */
    for(unsigned split=0;split<2;++split)for(unsigned owner=0;owner<2;++owner) {
        reset();split_views=(int)split;
        psx_mod_write_byte(actor+4,1);psx_mod_write_word(actor+8,owner?100:0);
        p2[8]=100;mmx4_coop_call(&cpu,0x800C00BCu,actor,0);
        CHECK(psx_mod_read_byte(PLAY+0x10)==!split);
        if(split)CHECK(pickup_record(actor,0) && pickup_record(actor,0)->owner==owner);
        psx_mod_write_word(PLAYER+8,200);p2[8]=0;
        for(unsigned i=0;i<2;++i)mmx4_coop_call(&cpu,0x800BF730u,actor,0);
        CHECK(psx_mod_read_byte(PLAYER+0x5C)==(owner?16:18));
        CHECK(p2[0x5C]==(owner?14:12));
        CHECK(!psx_mod_read_byte(PLAY+0x10) && !pickup_record(actor,0));
    }
    for(unsigned owner=0;owner<2;++owner)for(unsigned die=0;die<2;++die) {
        reset();split_views=1;
        for(unsigned i=0x10;i<0x18;++i)psx_mod_write_byte(PLAY+i,(uint8_t)(i+1));
        psx_mod_write_byte(actor+4,1);psx_mod_write_word(actor+8,owner?100:0);p2[8]=100;
        mmx4_coop_call(&cpu,0x800C00BCu,actor,0);
        if(die) {
            if(owner) {p2[0x5C]=0;p2[4]=3;}
            else {psx_mod_write_byte(PLAYER+0x5C,0);psx_mod_write_byte(PLAYER+4,3);}
        }
        for(unsigned i=0;i<2;++i)mmx4_coop_call(&cpu,0x800BF730u,actor,0);
        for(unsigned i=0x10;i<0x18;++i)CHECK(psx_mod_read_byte(PLAY+i)==i+1);
        if(die)CHECK(owner?!p2[0x5C]:!psx_mod_read_byte(PLAYER+0x5C));
    }
    reset();psx_mod_write_byte(PLAYER+4,3);psx_mod_write_byte(PLAYER+0x5C,0);
    psx_mod_write_byte(actor+4,1);psx_mod_write_word(actor+8,100);p2[8]=100;
    mmx4_coop_call(&cpu,0x800C00BCu,actor,0);CHECK(!collect_calls[0] && collect_calls[1]==1);

    /* One corpse neither ends the stage nor freezes the survivor. */
    reset();psx_mod_write_byte(PLAYER+4,2);psx_mod_write_byte(PLAYER+0x5C,0);
    mmx4_coop_call(&cpu,0x80035A6Cu,PLAYER,0);
    CHECK(!psx_mod_read_byte(PLAY+0x12) && !psx_mod_read_byte(PLAY+0x1C));
    mmx4_coop_call(&cpu,0x8001FF8Cu,PLAY,0);
    CHECK(!psx_mod_read_byte(PLAY+1) && world_calls==1);
    p2[4]=3;p2[0x5C]=0;mmx4_coop_call(&cpu,0x8001FF8Cu,PLAY,0);
    CHECK(psx_mod_read_byte(PLAY+1)==1);
    reset();p2[4]=2;p2[0x5C]=0;mmx4_coop_enter_second();
    mmx4_coop_call(&cpu,0x80035A6Cu,PLAYER,0);mmx4_coop_leave_second();
    CHECK(!psx_mod_read_byte(PLAY+0x12) && !psx_mod_read_byte(PLAY+0x1C));

    reset();psx_mod_write_byte(PLAY+0x59,4);psx_mod_write_byte(PLAYER+0xB9,4);
    psx_mod_write_byte(PLAY+0x26,1);mmx4_coop_call(&cpu,0x8001FA24u,PLAY,0);
    CHECK(psx_mod_read_byte(PLAYER+0xB9)==5 && p2[0xB9]==5);
    CHECK(psx_mod_read_byte(0x800F1D94u)==5);
    psx_mod_write_byte(PLAYER+2,1);psx_mod_write_byte(PLAY+0x43,1);p2[2]=0;
    mmx4_coop_call(&cpu,0x800C6EDCu,actor,0);
    CHECK(p2[0xA7]==2 && !psx_mod_read_byte(PLAYER+0xA7));
    CHECK(psx_mod_read_byte(PLAY+0x47)==2);

    /* A P2-triggered completion updates canonical body, campaign, Continue
     * record and both ability copies once, in either character order. */
    for(unsigned campaign=0;campaign<2;++campaign) {
        reset();reward_calls=save_calls=0;
        psx_mod_write_byte(PLAY+0x43,(uint8_t)campaign);
        psx_mod_write_byte(PLAYER+2,(uint8_t)campaign);p2[2]=(uint8_t)(campaign^1u);
        psx_mod_write_byte(PLAY+0x59,1);psx_mod_write_byte(PLAYER+0xB9,1);p2[0xB9]=1;
        psx_mod_write_byte(PLAY+0x26,5);
        mmx4_coop_enter_second();mmx4_coop_call(&cpu,0x8001FA24u,PLAY,0);
        CHECK(projected && psx_mod_read_byte(PLAY+0x43)==(campaign^1u));
        mmx4_coop_leave_second();
        CHECK(psx_mod_read_byte(PLAY+0x43)==campaign);
        CHECK(psx_mod_read_byte(PLAY+0x59)==17 && psx_mod_read_byte(PLAYER+0xB9)==17 && p2[0xB9]==17);
        CHECK(psx_mod_read_byte(0x800F1D90u)==campaign && psx_mod_read_byte(0x800F1D94u)==17);
        mmx4_coop_enter_second();mmx4_coop_call(&cpu,0x8001FA24u,PLAY,0);mmx4_coop_leave_second();
        CHECK(reward_calls==1 && save_calls==1);
        for(unsigned writer=0;writer<2;++writer) {
            psx_mod_write_byte(PLAY+0x46,36);psx_mod_write_byte(PLAY+0x47,3);
            psx_mod_write_byte(PLAY+0x5A,3);psx_mod_write_byte(PLAY+0x45,9);
            mmx4_coop_enter_second();psx_mod_write_byte(PLAY+0x45,4);
            mmx4_coop_call(&cpu,writer?0x8001C07Cu:0x8001C3E8u,0,0);
            CHECK(projected && psx_mod_read_byte(PLAY+0x45)==4);
            mmx4_coop_leave_second();
            uint32_t record=writer?0x80168000u:0x800F1D90u;
            CHECK(psx_mod_read_byte(record)==campaign && psx_mod_read_byte(record+1)==36);
            CHECK(psx_mod_read_byte(record+3)==3 && psx_mod_read_byte(record+4)==17);
            CHECK(psx_mod_read_byte(record+6)==3 && psx_mod_read_byte(PLAY+0x45)==9);
        }
        psx_mod_write_word(PLAY,6);psx_mod_write_byte(PLAY+1,2);
        menu_active=1;menu_owner=1;inputs[1]=SELECT;menu_previous[1]=0;
        mmx4_coop_call(&cpu,0x8002FCACu,MENU,0);CHECK(!observed_menu_select);
        menu_owner=0;inputs[0]=SELECT;menu_previous[0]=0;
        mmx4_coop_call(&cpu,0x8002FCACu,MENU,0);CHECK(observed_menu_select==SELECT);
    }

    reset();mmx4_coop_lifecycle_reset();p2[4]=3;p2[0x5C]=0;
    psx_mod_write_byte(PLAY+0x0D,1);mmx4_coop_lifecycle_reset();
    memset(p2,0,sizeof p2);p2[0]=p2[3]=p2[4]=1;p2[0x5C]=32;
    p2_vehicle[0]=p2_vehicle[3]=1;
    mmx4_coop_lifecycle_enrolled(&cpu);CHECK(p2[4]==3 && !p2[0x5C] && !p2[3]);
    CHECK(!p2_vehicle[0] && !p2_vehicle[3]);
    full_stage(&cpu,0);mmx4_coop_lifecycle_reset();
    p2[4]=1;p2[3]=1;p2[0x5C]=32;mmx4_coop_lifecycle_enrolled(&cpu);
    CHECK(p2[4]==1 && p2[0x5C]==32);
    /* Native section mode 10 transitions through full loader mode 4 too;
     * retain a dead P1 when the same stage's section changed. */
    reset();mmx4_coop_lifecycle_reset();
    psx_mod_write_byte(PLAYER+4,3);psx_mod_write_byte(PLAYER+0x5C,0);
    psx_mod_write_byte(PLAY+0x0D,1);full_stage(&cpu,0);
    stage_initialization(&cpu,0);
    /* Original 8002A7D0 clears P1 before native 80035240; the callback
     * must retain the preceding death, not infer status from that storage. */
    psx_mod_write_byte(PLAYER+4,0);psx_mod_write_byte(PLAYER+0x5C,0);
    mmx4_coop_lifecycle_reset();
    psx_mod_write_byte(PLAYER+4,1);psx_mod_write_byte(PLAYER+0x5C,32);
    psx_mod_write_byte(MMX4_VEHICLE,1);psx_mod_write_byte(MMX4_VEHICLE+3,1);
    p2[0x5C]=32; /* Native counterpart initializer has run. */
    mmx4_coop_lifecycle_enrolled(&cpu);
    CHECK(psx_mod_read_byte(PLAYER+4)==3 && !psx_mod_read_byte(PLAYER+0x5C));
    CHECK(!psx_mod_read_byte(MMX4_VEHICLE) && !psx_mod_read_byte(MMX4_VEHICLE+3));
    CHECK(p2[0x5C]==32 && p2[4]==1);

    /* A living P1 must not turn into a corpse when the same native clear
     * runs during section transfer or same-assets checkpoint initialization. */
    for(unsigned full=0;full<2;++full) {
        reset();mmx4_coop_lifecycle_reset();
        if(full) {psx_mod_write_byte(PLAY+0x0D,1);full_stage(&cpu,0);}
        uint32_t before_capture=mmx4_coop_lifecycle_digest(2166136261u);
        stage_initialization(&cpu,0);
        CHECK(first_before_clear_valid && !first_before_clear_dead);
        CHECK(before_capture!=mmx4_coop_lifecycle_digest(2166136261u));
        psx_mod_write_byte(PLAYER+4,0);psx_mod_write_byte(PLAYER+0x5C,0);
        mmx4_coop_lifecycle_reset();CHECK(!first_before_clear_valid);
        psx_mod_write_byte(PLAYER+4,1);psx_mod_write_byte(PLAYER+0x5C,32);
        p2[0x5C]=32;
        mmx4_coop_lifecycle_enrolled(&cpu);
        CHECK(psx_mod_read_byte(PLAYER+4)==1 && psx_mod_read_byte(PLAYER+0x5C)==32);
        CHECK(p2[4]==1 && p2[0x5C]==(full?32:12));
    }

    /* A healthy scripted OWNER also ends in native state3, without being
     * the hidden passenger. Native Jungle type5 arg0 requests PLAY0F=40
     * before section0->1. Neither owner may be carried as a death. */
    for(unsigned owner=0;owner<2;++owner)for(unsigned full=0;full<2;++full) {
        reset();mmx4_coop_lifecycle_reset();
        psx_mod_write_byte(PLAY+0x0F,0x40);
        if(owner)p2[4]=3;else psx_mod_write_byte(PLAYER+4,3);
        if(full) {psx_mod_write_byte(PLAY+0x0D,1);full_stage(&cpu,0);}
        stage_initialization(&cpu,0);
        mmx4_coop_lifecycle_reset();
        CHECK(carry_resources && !carry_dead && !carry_first_dead);
        psx_mod_write_byte(PLAYER+4,1);psx_mod_write_byte(PLAYER+0x5C,16);
        p2[0]=p2[3]=p2[4]=1;p2[0x5C]=32;
        mmx4_coop_lifecycle_enrolled(&cpu);
        CHECK(p2[4]==1 && p2[0x5C]==(full?32:12));
        CHECK(psx_mod_read_byte(PLAYER+4)==1 && psx_mod_read_byte(PLAYER+0x5C)==16);
    }
    for(unsigned seat=0;seat<2;++seat) {
        reset();
        if(seat)p2[4]=2;else psx_mod_write_byte(PLAYER+4,2);
        CHECK(seat_was_dead(seat));
        if(seat) {p2[4]=3;p2[0x5C]=0;}
        else {psx_mod_write_byte(PLAYER+4,3);psx_mod_write_byte(PLAYER+0x5C,0);}
        CHECK(seat_was_dead(seat));
        if(seat) {p2[4]=4;p2[0x5C]=12;}
        else {psx_mod_write_byte(PLAYER+4,4);psx_mod_write_byte(PLAYER+0x5C,16);}
        CHECK(!seat_was_dead(seat));
    }

    /* Frontend PLAYER clearing must not carry deaths/withdrawal into a
     * fresh entry of the same stage. Forward transfers above bypass it. */
    for(unsigned previous=0;previous<3;++previous) {
        reset();psx_mod_write_byte(PLAY+0x0D,1);mmx4_coop_lifecycle_reset();
        psx_mod_write_byte(PLAY+0x43,1);psx_mod_write_byte(PLAY+0x47,3);
        psx_mod_write_byte(PLAY+0x48,2);psx_mod_write_byte(PLAY+0x59,17);
        if(previous==1) {p2[4]=3;p2[0x5C]=0;}
        if(previous==2) {departed=1;p2[0]=p2[3]=0;}
        stage_selection(&cpu,0x8002E420u);
        CHECK(!stage_known && !first_before_clear_valid);
        psx_mod_write_byte(PLAYER+4,0);psx_mod_write_byte(PLAYER+0x5C,0);
        psx_mod_write_byte(PLAY+0x0D,0);full_stage(&cpu,0);stage_initialization(&cpu,0);
        mmx4_coop_lifecycle_reset();
        CHECK(!carry_resources && !carry_first_dead && !carry_dead && !carry_departure);
        CHECK(psx_mod_read_byte(PLAY+0x43)==1 && psx_mod_read_byte(PLAY+0x47)==3);
        CHECK(psx_mod_read_byte(PLAY+0x48)==2 && psx_mod_read_byte(PLAY+0x59)==17);
        psx_mod_write_byte(PLAYER+4,1);psx_mod_write_byte(PLAYER+0x5C,32);
        p2[0]=p2[3]=p2[4]=1;p2[0x5C]=32;
        mmx4_coop_lifecycle_enrolled(&cpu);
        CHECK(psx_mod_read_byte(PLAYER+4)==1 && psx_mod_read_byte(PLAYER+0x5C)==32);
        CHECK(p2[0x5C]==32 && !departed);
    }

    /* Permanent upgrades follow P1; X's native weapon override stays local.
     * Native8003718C treats any nonzero A6 as a forced weapon13 selection. */
    reset();p2[0xA6]=0x55;p2[0xB8]=0x66;
    psx_mod_write_byte(PLAY+0x47,3);psx_mod_write_byte(PLAY+0x59,17);
    for(unsigned upgrade=1;upgrade<=2;++upgrade) {
        psx_mod_write_byte(PLAY+0x48,(uint8_t)upgrade);
        mmx4_coop_lifecycle_follow_progress(p2,0);
        CHECK(p2[0xA7]==3 && p2[0xB8]==upgrade && p2[0xB9]==17 && p2[0xA6]==0x55);
    }
    p2[0xA7]=0x88;p2[0xB8]=0x66;
    mmx4_coop_lifecycle_follow_progress(p2,1);
    CHECK(p2[0xA7]==0x88 && p2[0xB8]==0x66 && p2[0xA6]==0x55 && p2[0xB9]==17);

    /* An outgoing passenger uses the native finished-beam state 3 while
     * still having health. An area transfer must preserve that living seat. */
    for(unsigned owner=0;owner<2;++owner) {
        reset();mmx4_coop_lifecycle_reset();
        script_active=1;script_owner=owner;request_script_departure(owner);
        for(unsigned i=0;i<3;++i)mmx4_coop_lifecycle_tick(&cpu);
        CHECK(warp_phase==1);
        CHECK(owner?psx_mod_read_byte(PLAYER+4)==3:p2[4]==3);
        psx_mod_write_byte(PLAY+0x0D,1);full_stage(&cpu,0);stage_initialization(&cpu,0);
        mmx4_coop_lifecycle_reset();
        CHECK(carry_resources && !carry_dead && !carry_first_dead);
        psx_mod_write_byte(PLAYER+4,1);psx_mod_write_byte(PLAYER+0x5C,16);
        p2[0]=p2[3]=p2[4]=1;p2[0x5C]=32;
        mmx4_coop_lifecycle_enrolled(&cpu);
        CHECK(p2[0x5C]==32 && p2[4]==1);
        CHECK(psx_mod_read_byte(PLAYER+4)==1 && psx_mod_read_byte(PLAYER+0x5C)==16);
    }
    reset();mmx4_coop_lifecycle_reset();departed=1;p2[0]=0;p2[4]=3;
    psx_mod_write_byte(PLAY+0x0D,1);full_stage(&cpu,0);stage_initialization(&cpu,0);
    mmx4_coop_lifecycle_reset();CHECK(carry_resources && carry_departure && !carry_dead);
    /* A native extra life does not make a section transfer revive a corpse. */
    reset();mmx4_coop_lifecycle_reset();p2[4]=3;p2[0x5C]=0;
    psx_mod_write_byte(PLAY+0x44,3);psx_mod_write_byte(PLAY+0x0D,1);
    full_stage(&cpu,0);stage_initialization(&cpu,0);
    psx_mod_write_byte(PLAYER+4,0);psx_mod_write_byte(PLAYER+0x5C,0);
    mmx4_coop_lifecycle_reset();CHECK(carry_resources && carry_dead);
    psx_mod_write_byte(PLAYER+4,1);psx_mod_write_byte(PLAYER+0x5C,32);
    p2[4]=1;p2[3]=1;p2[0x5C]=32;mmx4_coop_lifecycle_enrolled(&cpu);
    CHECK(p2[4]==3 && !p2[0x5C]);CHECK(psx_mod_read_byte(PLAYER+4)==1);

    /* Native forward-section refill policies0/-2 heal living P2 to the
     * initialized upgraded maximum, while -1 retains personal resources. */
    for(unsigned campaign=0;campaign<2;++campaign)for(unsigned policy=0;policy<3;++policy) {
        reset();psx_mod_write_byte(PLAYER+2,(uint8_t)campaign);
        psx_mod_write_byte(PLAY+0x43,(uint8_t)campaign);p2[2]=(uint8_t)(campaign^1u);
        mmx4_coop_enter_second();mmx4_coop_leave_second();
        mmx4_coop_lifecycle_reset();p2[0xA8]=7;p2[0x93]=3;
        psx_mod_write_byte(PLAY+0x0D,1);
        psx_mod_write_byte(PLAY+0x1E,(uint8_t)(policy?253+policy:0));
        mmx4_coop_lifecycle_reset();
        p2[0]=p2[3]=p2[4]=1;p2[0x5C]=40;
        mmx4_coop_lifecycle_enrolled(&cpu);
        CHECK(p2[0x5C]==(policy==2?12:40));
        CHECK(p2[0x5D]==p2[0x5C] && p2[0x5E]==p2[0x5C]);
        CHECK(p2[0xA8]==7 && p2[0x93]==3);
        CHECK(psx_mod_read_byte(PLAY+0x44)==2);
        CHECK(!carry_refill);
    }

    /* Game-over Continue increases 0 to 2 after a team wipe; it is still a
     * team respawn. The native 0->255 underflow must not preserve corpses. */
    for(unsigned new_lives=2;new_lives<=255;new_lives+=253) {
        reset();psx_mod_write_byte(PLAY+0x44,0);mmx4_coop_lifecycle_reset();
        psx_mod_write_byte(PLAYER+4,3);psx_mod_write_byte(PLAYER+0x5C,0);
        p2[4]=3;p2[0x5C]=0;psx_mod_write_byte(PLAY+0x44,(uint8_t)new_lives);
        stage_initialization(&cpu,0);
        psx_mod_write_byte(PLAYER+4,0);mmx4_coop_lifecycle_reset();
        CHECK(!carry_resources && !carry_first_dead && !carry_dead);
        psx_mod_write_byte(PLAYER+4,1);psx_mod_write_byte(PLAYER+0x5C,32);
        p2[4]=p2[3]=1;p2[0x5C]=32;mmx4_coop_lifecycle_enrolled(&cpu);
        CHECK(psx_mod_read_byte(PLAYER+4)==1 && p2[4]==1 && p2[0x5C]==32);
    }

    reset();inputs[1]=SELECT;
    for(unsigned i=0;i<89;++i)mmx4_coop_lifecycle_tick(&cpu);
    CHECK(!departed && mmx4_coop_alive(1));

    mmx4_coop_lifecycle_tick(&cpu);
    CHECK(departed && !mmx4_coop_alive(1) && !mmx4_coop_lifecycle_can_tick());
    inputs[1]=0;mmx4_coop_lifecycle_tick(&cpu);
    inputs[1]=SELECT;mmx4_coop_lifecycle_tick(&cpu);CHECK(departed && rejoin_pending);
    psx_mod_write_byte(PLAYER+0x89,8);psx_mod_write_word(PLAYER+8,0x12340000u);
    psx_mod_write_word(PLAYER+12,0x34560000u);mmx4_coop_lifecycle_tick(&cpu);
    mmx4_coop_lifecycle_tick(&cpu); /* Native returning-pose completion. */
    CHECK(!departed && mmx4_coop_alive(1) && p2[0x5C]==12);
    CHECK(p2[10]==0x34 && p2[11]==0x12 && p2[14]==0x56 && p2[15]==0x34);
    CHECK(p2[0x15]==0x40 && p2_vehicle[0x15]==0x40);
    CHECK(!projected);

    /* A passenger starts the native incoming pose at the owner's landing
     * height, facing right. Preserve its camera index and P1's facing. */
    reset();p2[0x14]=2;p2[0x15]=p2_vehicle[0x15]=0;
    psx_mod_write_byte(PLAYER+0x15,0);
    position_return(&cpu,1,320u<<16,144u<<16,1,1,1,1);
    start_arrival(&cpu,1);
    CHECK(body_word(p2,12)==144u<<16 && body_word(p2,0x1C)==144u<<16);
    CHECK(p2[0x14]==2 && p2[0x15]==0x40 && p2_vehicle[0x15]==0x40);
    CHECK(!psx_mod_read_byte(PLAYER+0x15) && !projected);

    /* A winner's state3 is a completed departure, not a dead owner to rescue.
     * Finish either passenger's outgoing beam during completion, so native
     * stage termination sees canonical state3; keep the passenger outside. */
    for(unsigned owner=0;owner<2;++owner) {
        reset();script_active=1;script_owner=owner;request_script_departure(owner);
        mmx4_coop_lifecycle_tick(&cpu);CHECK(warp_phase==3);
        psx_mod_write_byte(PLAY+0x0F,1);
        if(owner)p2[4]=3;else psx_mod_write_byte(PLAYER+4,3);
        mmx4_coop_lifecycle_tick(&cpu);CHECK(warp_phase==1);
        CHECK(psx_mod_read_byte(PLAYER+4)==3);
        CHECK(owner==0 || !psx_mod_read_byte(PLAYER));
        CHECK(owner==1 || !p2[0]);
        CHECK(p2[4]==3);
        mmx4_coop_lifecycle_tick(&cpu);CHECK(warp_phase==1);
    }

    /* Native room fades and victory actions cannot reopen the passenger. */
    for(unsigned gate=0;gate<3;++gate) {
        reset();warp_owner=0;warp_phase=1;warp_active=warp_visible=1;
        p2[0]=p2[3]=0;psx_mod_write_byte(PLAYER+0x89,8);
        if(gate==0)psx_mod_write_byte(PLAY+1,1);
        if(gate==1)psx_mod_write_byte(PLAY+0x0F,1);
        if(gate==2)psx_mod_write_byte(PLAYER+5,0x14);
        for(unsigned i=0;i<6;++i)mmx4_coop_lifecycle_tick(&cpu);
        CHECK(warp_phase==1 && !p2[0] && !p2[3]);
        psx_mod_write_byte(PLAY+1,0);psx_mod_write_byte(PLAY+0x0F,0);
        psx_mod_write_byte(PLAYER+5,2);psx_mod_write_word(PLAYER+12,144u<<16);
        for(unsigned i=0;i<3;++i)mmx4_coop_lifecycle_tick(&cpu);
        CHECK(warp_phase==2 && body_word(p2,12)==144u<<16);
        mmx4_coop_lifecycle_tick(&cpu);CHECK(!warp_phase && mmx4_coop_alive(1));
    }

    /* A grounded walking/dashing leader releases the arrival for either seat.
     * Native victory actions above remain gated through completion. */
    for(unsigned owner=0;owner<2;++owner)for(unsigned action=4;action<=13;action+=9) {
        reset();warp_owner=owner;warp_phase=1;warp_active=warp_visible=1;
        if(owner) {
            psx_mod_write_byte(PLAYER,0);psx_mod_write_byte(PLAYER+3,0);
            p2[5]=(uint8_t)action;p2[0x89]=8;
        }else {
            p2[0]=p2[3]=0;psx_mod_write_byte(PLAYER+5,(uint8_t)action);
            psx_mod_write_byte(PLAYER+0x89,8);
        }
        for(unsigned i=0;i<3;++i)mmx4_coop_lifecycle_tick(&cpu);
        CHECK(warp_phase==2);
        mmx4_coop_lifecycle_tick(&cpu);
        CHECK(!warp_phase && mmx4_coop_alive(owner^1u));
    }

    /* Death during a committed arrival keeps the survivor in the new
     * room, rather than restoring the pre-door departure coordinates. */
    reset();warp_owner=0;warp_phase=1;warp_active=warp_visible=1;
    warp_origin_x=40u<<16;warp_origin_y=20u<<16;p2[0]=p2[3]=0;
    psx_mod_write_word(PLAYER+8,640u<<16);psx_mod_write_word(PLAYER+12,180u<<16);
    psx_mod_write_byte(PLAYER+0x89,8);
    for(unsigned i=0;i<3;++i)mmx4_coop_lifecycle_tick(&cpu);
    CHECK(warp_phase==2);
    psx_mod_write_byte(PLAYER+0x5C,0);psx_mod_write_byte(PLAYER+4,2);
    mmx4_coop_lifecycle_tick(&cpu);
    CHECK(!warp_phase && mmx4_coop_alive(1));
    CHECK(body_word(p2,8)==640u<<16 && body_word(p2,12)==180u<<16);

    /* A native door can lose its initiator during auto-walk. Transfer its
     * lock to the living partner and leave the corpse to native death. */
    for(unsigned dead=0;dead<2;++dead) {
        reset();script_active=1;script_owner=dead;script_serial=5;
        scenes[0]=(SceneOwner){actor,5,(uint8_t)dead};
        if(dead) {
            p2[0x5C]=0x80;p2[5]=0x13;p2[0xC3]=p2[0xC4]=0xFF;
            p2[10]=0x80;p2[11]=2;p2[14]=144;
        }else {
            psx_mod_write_byte(PLAYER+0x5C,0x80);psx_mod_write_byte(PLAYER+5,0x13);
            psx_mod_write_byte(PLAYER+0xC3,0xFF);psx_mod_write_byte(PLAYER+0xC4,0xFF);
            psx_mod_write_word(PLAYER+8,640u<<16);psx_mod_write_word(PLAYER+12,144u<<16);
        }
        mmx4_coop_lifecycle_tick(&cpu);
        CHECK(script_active && script_owner==(dead^1u));
        CHECK(scenes[0].owner==(dead^1u) && scenes[0].recovered);
        CHECK((dead?p2[4]:psx_mod_read_byte(PLAYER+4))==2);
        CHECK(!(dead?p2[0xC4]:psx_mod_read_byte(PLAYER+0xC4)));
        CHECK((dead?psx_mod_read_byte(PLAYER+0xC4):p2[0xC4])==1);
        CHECK(mmx4_coop_alive(dead^1u) && !mmx4_coop_alive(dead));
        CHECK((dead?psx_mod_read_word(PLAYER+8):body_word(p2,8))==640u<<16);
        CHECK((dead?psx_mod_read_word(PLAYER+12):body_word(p2,12))==144u<<16);
    }

    reset();script_active=1;script_owner=0;cpu.gpr[4]=PLAYER;
    psx_mod_write_byte(PLAYER+0xC4,1);
    scenes[0]=(SceneOwner){actor,1,0,0};
    CHECK(!hidden_terrain(&cpu,0x8002C614u)); /* Native door, not a recovery. */
    scenes[0].recovered=1;psx_mod_write_byte(PLAYER+0x70,12);
    psx_mod_write_byte(PLAYER+0x71,3);
    CHECK(hidden_terrain(&cpu,0x8002C614u));
    CHECK(psx_mod_read_byte(PLAYER+0x70)==8 && !psx_mod_read_byte(PLAYER+0x71));
    psx_mod_write_byte(PLAYER+0xC4,0);
    CHECK(!hidden_terrain(&cpu,0x8002C614u));

    /* Personal C4 doors do not acquire PLAY1C; general C0 scenes do.
     * A partner's native death must preserve either preexisting gate. */
    for(unsigned gate=0;gate<2;++gate) {
        reset();script_active=1;script_owner=0;
        psx_mod_write_byte(PLAY+0x1C,(uint8_t)gate);
        mmx4_coop_enter_second();mmx4_coop_call(&cpu,0x80035A6Cu,PLAYER,0);
        mmx4_coop_leave_second();
        CHECK(psx_mod_read_byte(PLAY+0x1C)==gate);
    }

    /* Marine Base prohibits withdrawal for the whole bike sequence, even
     * during frames where native mounting fields temporarily clear. */
    reset();inputs[1]=SELECT;psx_mod_write_byte(PLAYER+0xC5,0xFF);
    psx_mod_write_byte(PLAY+0x0C,5);
    psx_mod_write_byte(PLAYER+0x89,0);p2[0xC5]=0xFF;
    p2_vehicle[0]=p2_vehicle[3]=1;p2_vehicle[0x5C]=23;
    psx_mod_write_byte(MMX4_VEHICLE,1);psx_mod_write_byte(MMX4_VEHICLE+3,1);
    psx_mod_write_byte(MMX4_VEHICLE+0x5C,31);
    for(unsigned i=0;i<90;++i)mmx4_coop_lifecycle_tick(&cpu);
    CHECK(!departed && p2_vehicle[0]==1 && psx_mod_read_byte(MMX4_VEHICLE)==1);
    p2[0xC5]=0;psx_mod_write_byte(PLAYER+0xC5,0);
    for(unsigned i=0;i<100;++i)mmx4_coop_lifecycle_tick(&cpu);
    CHECK(!departed && mmx4_coop_alive(1) && p2_vehicle[0x5C]==23);
    psx_mod_write_byte(PLAY+0x1D,1);
    for(unsigned i=0;i<90;++i)mmx4_coop_lifecycle_tick(&cpu);
    CHECK(departed); /* Foot control after the bike sequence remains eligible. */

    /* Personal native control locks cannot be escaped by Select. A queued
     * voluntary return also waits until fades/victory actually finish. */
    static const unsigned lock_offsets[]={0xC0,0xC3,0xC4,0x67,0xBC};
    for(unsigned k=0;k<sizeof lock_offsets/sizeof lock_offsets[0];++k) {
        reset();inputs[1]=SELECT;p2[lock_offsets[k]]=1;
        for(unsigned i=0;i<100;++i)mmx4_coop_lifecycle_tick(&cpu);
        CHECK(!departed && !select_ticks && mmx4_coop_alive(1));
    }
    reset();departed=rejoin_pending=1;psx_mod_write_byte(PLAYER+0x89,8);
    psx_mod_write_byte(PLAYER+5,0x13);
    for(unsigned i=0;i<4;++i)mmx4_coop_lifecycle_tick(&cpu);
    CHECK(departed && rejoin_pending && !warp_phase);
    psx_mod_write_byte(PLAYER+5,2);psx_mod_write_byte(PLAY+0x0F,1);
    mmx4_coop_lifecycle_tick(&cpu);CHECK(departed && !warp_phase);
    psx_mod_write_byte(PLAY+0x0F,0);mmx4_coop_lifecycle_tick(&cpu);
    CHECK(!departed && warp_phase==2);

    reset();psx_mod_write_byte(PLAYER+4,3);psx_mod_write_byte(PLAYER+0x5C,0);
    inputs[1]=SELECT;for(unsigned i=0;i<100;++i)mmx4_coop_lifecycle_tick(&cpu);
    CHECK(!departed && mmx4_coop_alive(1));

    /* Death during the incoming pose must leave the returning survivor
     * playable, rather than treating the temporary control lock as death. */
    reset();inputs[1]=SELECT;
    for(unsigned i=0;i<90;++i)mmx4_coop_lifecycle_tick(&cpu);
    inputs[1]=0;mmx4_coop_lifecycle_tick(&cpu);
    psx_mod_write_byte(PLAYER+0x89,8);inputs[1]=SELECT;
    mmx4_coop_lifecycle_tick(&cpu);CHECK(warp_phase==2);
    psx_mod_write_byte(PLAYER+4,3);psx_mod_write_byte(PLAYER+0x5C,0);
    mmx4_coop_call(&cpu,0x8001FF8Cu,0,0);
    CHECK(!warp_phase && mmx4_coop_alive(1) && p2[0x5C]==12 && world_calls==1);

    /* P2 may trigger a native portal first. Its scene sees the original
     * campaign, while only P2 receives native body/action writes. */
    reset();psx_mod_write_byte(actor,1);psx_mod_write_byte(actor+4,1);
    psx_mod_write_word(actor+8,0x02000000u);psx_mod_write_word(actor+12,0x03000000u);
    p2_solid_bits=8;mmx4_coop_call(&cpu,0x800C2BE0u,actor,0);
    CHECK(scene_calls==1 && observed_campaign==0 && script_owner==1 && script_active);
    CHECK(!psx_mod_read_byte(actor+0x72) && p2[0xC0]==1);
    CHECK(!psx_mod_read_byte(PLAYER+0xC0) && p2[0x5C]==12);
    mmx4_coop_lifecycle_tick(&cpu);
    CHECK(warp_phase==3 && mmx4_coop_lifecycle_hidden(0));
    mmx4_coop_lifecycle_tick(&cpu); /* Native departure finishes before hiding. */
    CHECK(!psx_mod_read_byte(PLAYER) && mmx4_coop_lifecycle_hidden(0));
    CHECK(psx_mod_read_word(PLAYER+8)==0);
    CHECK(psx_mod_read_byte(PLAYER+0x5C)==16);
    psx_mod_write_half(PLAYER+0x7C,0x2000);
    mmx4_coop_call(&cpu,0x800311ECu,0,0);CHECK(!observed_controls);
    mmx4_coop_enter_second();mmx4_coop_call(&cpu,0x80036B18u,0,0);mmx4_coop_leave_second();
    CHECK(!script_active && !p2[0xC0] && psx_mod_read_byte(PLAY+0x43)==0);
    /* Chained dialogue holds the passenger outside until actual landing. */
    psx_mod_write_byte(PLAY+0x1C,1);p2[0x89]=8;
    mmx4_coop_lifecycle_tick(&cpu);CHECK(!psx_mod_read_byte(PLAYER));
    psx_mod_write_byte(PLAY+0x1C,0);mmx4_coop_lifecycle_tick(&cpu);
    mmx4_coop_lifecycle_tick(&cpu);mmx4_coop_lifecycle_tick(&cpu);
    CHECK(psx_mod_read_word(PLAYER+8)==0x02000000u && warp_phase==2);
    mmx4_coop_lifecycle_tick(&cpu);
    CHECK(!warp_phase && mmx4_coop_alive(0) && psx_mod_read_byte(PLAYER+0x5C)==16);

    /* A dead P1 stays dead while P2 advances native checkpoint scripts once. */
    reset();psx_mod_write_byte(PLAYER+4,3);psx_mod_write_byte(PLAYER+0x5C,0);
    mmx4_coop_call(&cpu,0x8002166Cu,0,0);
    CHECK(stage_script_calls==1 && observed_campaign==0 && psx_mod_read_byte(PLAY+0x1D)==9);
    CHECK(p2[0xC0]==1 && !psx_mod_read_byte(PLAYER+0xC0));
    CHECK(psx_mod_read_byte(PLAYER+4)==3 && !psx_mod_read_byte(PLAYER+0x5C));
    CHECK(!warp_pending && !projected && psx_mod_read_byte(PLAY+0x43)==0);
    mmx4_coop_call(&cpu,0x80036B18u,0,0);
    CHECK(!p2[0xC0] && !script_active && psx_mod_read_byte(PLAYER+4)==3);

    reset();psx_mod_write_byte(PLAYER+4,3);psx_mod_write_byte(PLAYER+0x5C,0);
    p2[11]=2;psx_mod_write_byte(actor,1);psx_mod_write_byte(actor+4,1);
    mmx4_coop_call(&cpu,0x800BD654u,actor,0);
    CHECK(scene_calls==1 && observed_campaign==0 && psx_mod_read_byte(PLAY+0x1D)==8);
    CHECK(p2[0x5C]==12 && psx_mod_read_byte(PLAYER+4)==3 && !projected);

    /* Boss doors use C4 directly, not the general C0 script command pair.
     * Independent P2 entry must serialize the native auto-walk once. */
    reset();psx_mod_write_byte(actor,1);psx_mod_write_byte(actor+4,1);
    psx_mod_write_word(actor+8,0x02000000u);p2[11]=2;
    mmx4_coop_call(&cpu,0x800C1994u,actor,0);
    CHECK(scene_calls==1 && script_active && script_owner==1 && p2[0xC4]==1);
    CHECK(mmx4_coop_lifecycle_script_owner()==1);
    CHECK(!psx_mod_read_byte(PLAYER+0xC4) && observed_campaign==0);
    mmx4_coop_lifecycle_tick(&cpu);
    CHECK(warp_phase==3 && mmx4_coop_lifecycle_hidden(0));
    mmx4_coop_lifecycle_tick(&cpu);
    CHECK(!psx_mod_read_byte(PLAYER) && psx_mod_read_word(PLAYER+8)==0);
    psx_mod_write_byte(actor+5,3);mmx4_coop_call(&cpu,0x800C1994u,actor,0);
    CHECK(scene_calls==2 && !script_active && !p2[0xC4]);
    CHECK(mmx4_coop_lifecycle_script_owner()==-1);
    p2[0x89]=8;for(unsigned i=0;i<4;++i)mmx4_coop_lifecycle_tick(&cpu);
    CHECK(psx_mod_read_word(PLAYER+8)==0x02010000u);
    CHECK(!scene_record(actor,0));

    /* P1 wins simultaneous eligibility; a corpse cannot enter or be revived. */
    reset();psx_mod_write_byte(actor,1);psx_mod_write_byte(actor+4,1);
    mmx4_coop_call(&cpu,0x800C1994u,actor,0);
    CHECK(scene_calls==1 && script_owner==0 && psx_mod_read_byte(PLAYER+0xC4)==1);
    CHECK(mmx4_coop_lifecycle_script_owner()==0);
    CHECK(!p2[0xC4]);
    reset();psx_mod_write_byte(PLAYER+4,3);psx_mod_write_byte(PLAYER+0x5C,0);
    psx_mod_write_byte(actor,1);psx_mod_write_byte(actor+4,1);
    mmx4_coop_call(&cpu,0x800C1994u,actor,0);
    CHECK(scene_calls==1 && script_owner==1 && p2[0xC4]==1);
    CHECK(psx_mod_read_byte(PLAYER+4)==3 && !warp_pending);
    psx_mod_write_byte(actor+5,3);mmx4_coop_call(&cpu,0x800C1994u,actor,0);
    CHECK(!script_active && psx_mod_read_byte(PLAYER+4)==3 && !p2[0xC4]);
    reset();script_active=script_owner=1;
    story_pad[0]=0x40;story_pad[1]=0;story_pad[2]=0x40;
    psx_mod_write_half(PAD,2);psx_mod_write_half(PAD+2,2);psx_mod_write_half(PAD+4,0);
    mmx4_coop_call(&cpu,0x80021D20u,0,0);
    CHECK(dialogue_calls==1 && dialogue_edge==0x40 && observed_campaign==0);
    CHECK(psx_mod_read_half(PAD)==2 && !psx_mod_read_half(PAD+4) && !projected);
    script_owner=0;mmx4_coop_call(&cpu,0x80021D20u,0,0);
    CHECK(dialogue_calls==2 && !dialogue_edge);
    script_active=0;mmx4_coop_call(&cpu,0x80021D20u,0,0);
    CHECK(dialogue_calls==3 && !dialogue_edge);

    /* Returning to stage selection clears P1's gameplay body. The native
     * frontend still updates its own stage-script pool; P2 has no campaign
     * and must not replace its palette/resources or acquire its story input. */
    reset();psx_mod_write_word(PLAY,3);psx_mod_write_byte(PLAYER+4,0);
    psx_mod_write_byte(PLAYER+0x5C,0);uint8_t front_p2[0xE4];memcpy(front_p2,p2,sizeof front_p2);
    mmx4_coop_call(&cpu,0x8002166Cu,0,0);
    CHECK(stage_script_calls==1 && !stage_script_projected && !projected && observed_campaign==0);
    CHECK(!script_active && !memcmp(front_p2,p2,sizeof front_p2));
    script_active=script_owner=1;story_pad[2]=0x40;psx_mod_write_half(PAD+4,0x800);
    mmx4_coop_call(&cpu,0x80021D20u,0,0);
    CHECK(dialogue_calls==1 && dialogue_edge==0x800);

    for(unsigned campaign=0;campaign<2;++campaign)for(unsigned story=0;story<2;++story) {
        reset();psx_mod_write_byte(PLAY+0x43,(uint8_t)campaign);
        psx_mod_write_byte(PLAYER+2,(uint8_t)campaign);p2[2]=(uint8_t)(campaign^1u);
        uint8_t hp=p2[0x5C];mmx4_coop_enter_second();
        uint8_t projected_campaign=story?(uint8_t)campaign:(uint8_t)(campaign^1u);
        psx_mod_write_byte(PLAY+0x43,projected_campaign);
        mmx4_coop_call(&cpu,0x8002217Cu,16,255);
        CHECK(dialogue_init_calls==1 && observed_campaign==campaign && dialogue_init_character==campaign && projected);
        CHECK(psx_mod_read_byte(PLAY+0x43)==projected_campaign && psx_mod_read_byte(PLAYER+0x5C)==hp);
        psx_mod_write_byte(PLAY+0x43,(uint8_t)(campaign^1u));mmx4_coop_leave_second();
        CHECK(psx_mod_read_byte(PLAY+0x43)==campaign && p2[0x5C]==hp);
        CHECK(psx_mod_read_byte(0x80166BB0u)&1u);
    }
    reset();psx_mod_write_word(PLAYER+8,0x1000000u);p2[11]=2;
    psx_mod_write_word(actor+8,0x2000000u);
    psx_mod_write_byte(actor,1);psx_mod_write_byte(actor+4,1);
    mmx4_coop_call(&cpu,0x800C1994u,actor,0);
    CHECK(script_active && script_owner==1 && p2[0xC4]);
    mmx4_coop_enter_second();mmx4_coop_call(&cpu,0x80036AE4u,0x14,0x40);
    mmx4_coop_leave_second();
    unsigned chained_serial=script_serial;
    psx_mod_write_byte(actor+5,3);mmx4_coop_call(&cpu,0x800C1994u,actor,0);
    CHECK(script_active && script_owner==1 && script_serial==chained_serial && !p2[0xC4]);
    CHECK(p2[0xC0]==1);
    /* P2's bike waits for the same READY actor without allocating/restarting
     * it. Ordinary allocations and the campaign owner's bike remain native. */
    reset();uint32_t ready=0x80142F98u;
    psx_mod_write_word(MMX4_VEHICLE+0xA0,ready);
    psx_mod_write_byte(ready,1);psx_mod_write_byte(ready+1,0x1B);
    psx_mod_write_byte(ready+5,2);cpu.gpr[31]=0x8003B64Cu;
    CHECK(!shared_ride_ready(&cpu,0x8002AD7Cu));
    mmx4_coop_enter_second();
    CHECK(shared_ride_ready(&cpu,0x8002AD7Cu) && cpu.gpr[2]==0);
    CHECK(psx_mod_read_word(MMX4_VEHICLE+0xA0)==ready);
    CHECK(psx_mod_read_byte(ready+5)==2);
    cpu.gpr[31]=0x8001FE8Cu;CHECK(!shared_ride_ready(&cpu,0x8002AD7Cu));
    mmx4_coop_leave_second();CHECK(body_word(p2_vehicle,0xA0)==ready);
    puts("co-op lifecycle ownership checks passed");return 0;
}
