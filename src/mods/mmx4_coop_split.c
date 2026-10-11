#include "mmx4_coop_internal.h"
#include "mod_memory.h"
#include <string.h>

#define COOP_ID "mmx4.coop"
#define CAMERA 0x801419B0u
#define CAMERA_BYTES 0xFCu
#define TRAIN_POOL 0x80169CB8u
#define TRAIN_SLOTS 4u
#define TRAIN_ACTOR_BYTES 0x30u
#define VISITS_OFFSET (16u+2u*CAMERA_BYTES)
#define STATE_BYTES (VISITS_OFFSET+MMX4_COOP_PLACEMENTS/8u)

static uint32_t state;
static unsigned scanning,bounds_call,supplemental,spatial_call,background_call;
static uint32_t background_actor,background_dx;
static unsigned background_parallax;

static void copy_bytes(uint32_t destination,uint32_t source,unsigned size) {
    for(unsigned offset=0;offset<size;++offset)
        psx_mod_write_byte(destination+offset,psx_mod_read_byte(source+offset));
}
static void read_bytes(uint32_t source,uint8_t *bytes,unsigned size) {
    for(unsigned offset=0;offset<size;++offset)bytes[offset]=psx_mod_read_byte(source+offset);
}
static void write_bytes(uint32_t destination,const uint8_t *bytes,unsigned size) {
    for(unsigned offset=0;offset<size;++offset)psx_mod_write_byte(destination+offset,bytes[offset]);
}
static int available(void) {
    return state && mmx4_coop_split_views() && mmx4_coop_ready() &&
        (!mmx4_coop_projected() || mmx4_coop_combat_actor_context() || spatial_call) &&
        psx_mod_read_byte(MMX4_PLAY)==6 &&
        !psx_mod_local_view_scope();
}
void mmx4_coop_split_reset(void) {
    if(!state || mmx4_coop_projected() || psx_mod_local_view_scope())return;
    for(unsigned offset=0;offset<STATE_BYTES;offset+=4)psx_mod_write_word(state+offset,0);
    scanning=bounds_call=supplemental=spatial_call=background_call=0;
    background_actor=background_dx=background_parallax=0;
}
int mmx4_coop_split_activate(void) {
    state=mmx4_coop_split_views()?psx_mod_memory_alloc(STATE_BYTES,4):0;
    mmx4_coop_split_reset();
    if(state)psx_mod_counter_add("mmx4.coop.split-state",state);
    return !mmx4_coop_split_views() || state!=0;
}
uint32_t mmx4_coop_split_digest(uint32_t seed) {
    if(state)for(unsigned offset=0;offset<STATE_BYTES;++offset)
        seed=(seed^psx_mod_read_byte(state+offset))*16777619u;
    return seed;
}
void mmx4_coop_split_actors(Mmx4CoopViewActor actors[2]) {
    unsigned projected=(unsigned)(mmx4_coop_projected()!=0);
    for(unsigned seat=0;seat<2;++seat) {
        const uint8_t *body=seat?mmx4_coop_second_body():mmx4_coop_first_body();
        int16_t world_x=seat==projected?(int16_t)psx_mod_read_half(MMX4_PLAYER+10):
            (int16_t)((uint16_t)body[10]|(uint16_t)body[11]<<8);
        int16_t world_y=seat==projected?(int16_t)psx_mod_read_half(MMX4_PLAYER+14):
            (int16_t)((uint16_t)body[14]|(uint16_t)body[15]<<8);
        actors[seat]=(Mmx4CoopViewActor){world_x,world_y,(unsigned)mmx4_coop_alive(seat)};
    }
}
unsigned mmx4_coop_split_view_seat(unsigned seat) {
    if(seat>1)seat=0;
    int owner=mmx4_coop_lifecycle_script_owner();
    if(owner>=0)return (unsigned)owner;
    if(!mmx4_coop_alive(seat))return mmx4_coop_alive(seat^1u)?seat^1u:0;
    return seat;
}
int mmx4_coop_split_camera_copy(unsigned seat,uint8_t layers[0xFC]) {
    if(!state || !mmx4_coop_split_views() || seat>1 || !psx_mod_read_word(state))return 0;
    seat=mmx4_coop_split_view_seat(seat);
    read_bytes(state+16u+seat*CAMERA_BYTES,layers,CAMERA_BYTES);
    return 1;
}
static void initialize_cameras(void) {
    if(psx_mod_read_word(state))return;
    copy_bytes(state+16u,CAMERA,CAMERA_BYTES);
    copy_bytes(state+16u+CAMERA_BYTES,CAMERA,CAMERA_BYTES);
    psx_mod_write_word(state,1);
}
static uint32_t byte_word(const uint8_t *bytes,unsigned offset) {
    return (uint32_t)bytes[offset]|(uint32_t)bytes[offset+1]<<8|
        (uint32_t)bytes[offset+2]<<16|(uint32_t)bytes[offset+3]<<24;
}
static uint32_t signed_half(uint32_t value) {
    /* Retail sra retains the sign bit, including negative odd fixed units. */
    return (value>>1)|(value&0x80000000u);
}
static int train_foreground(CPUState *cpu,uint32_t address) {
    if(background_call!=1 || cpu->gpr[4]!=background_actor)return 0;
    /* Full native800D964C adds foreground displacement only on this branch.
     * Observe it at the actual dispatch, after the native phase selector.
     * A stopped train's +7 transition does not add parallax on that tick. */
    if(psx_mod_read_byte(background_actor+7) && !psx_mod_read_byte(MMX4_PLAYER+0xBC)) {
        background_parallax=1;
        background_dx=psx_mod_read_word(CAMERA+8)-psx_mod_read_word(CAMERA+20);
    }
    background_call=2;
    uint32_t result=mmx4_coop_call(cpu,address,cpu->gpr[4],cpu->gpr[5]);
    background_call=1;
    return mmx4_coop_finish(cpu,result);
}
static int train_background(CPUState *cpu,uint32_t address) {
    uint32_t actor=cpu->gpr[4];
    unsigned kind=address==0x800D8648u?0u:2u;
    if(background_call || !available() || mmx4_coop_projected() ||
       psx_mod_read_byte(MMX4_PLAY+12)!=8 || actor<TRAIN_POOL ||
       actor>=TRAIN_POOL+TRAIN_SLOTS*TRAIN_ACTOR_BYTES ||
       (actor-TRAIN_POOL)%TRAIN_ACTOR_BYTES ||
       psx_mod_read_byte(actor+1)!=kind)return 0;
    initialize_cameras();
    uint8_t before[CAMERA_BYTES],after[CAMERA_BYTES];
    read_bytes(CAMERA,before,CAMERA_BYTES);
    background_actor=actor;background_dx=background_parallax=0;background_call=1;
    uint32_t result=mmx4_coop_call(cpu,address,actor,cpu->gpr[5]);
    read_bytes(CAMERA,after,CAMERA_BYTES);
    unsigned previous=psx_mod_read_word(state+8u);
    unsigned owner=previous?previous-1u:0u;
    /* QA87 traces D9218 a0=80169CB8, ra=80021BDC. Native80021B34 and its
     * allocator8002B048 agree: four30-byte actors, table800F2AB4, kinds0/2.
     * The train's mode5 helpers cannot recreate these shared actor writes.
     * They run after the current player camera snapshot in80021158.
     * Apply their writes to both saved views, never to canonical RAM again.
     * 0/2 add +18 speed and its arithmetic half. Only D9218's inspected
     * 800D964C branch also adds foreground motion; replace that contribution
     * with the other view's own displacement when the branch actually runs. */
    for(unsigned seat=0;seat<2;++seat) {
        uint32_t shadow=state+16u+seat*CAMERA_BYTES;
        uint32_t seat_dx=psx_mod_read_word(shadow+8)-psx_mod_read_word(shadow+20);
        for(unsigned layer=0;layer<3;++layer) {
            unsigned base=layer*0x54u;
            for(unsigned offset=0;offset<0x54u;++offset) {
                if((offset>=8u && offset<16u) || (offset>=20u && offset<28u))continue;
                if(before[base+offset]!=after[base+offset])
                    psx_mod_write_byte(shadow+base+offset,after[base+offset]);
            }
            /* Shared authored limits/shake metadata are copied above, while
             * each foreground's current/previous origins stay independent. */
            if(!layer)continue;
            for(unsigned axis=8;axis<=12;axis+=4) {
                uint32_t delta=byte_word(after,base+axis)-byte_word(before,base+axis);
                uint32_t value=psx_mod_read_word(shadow+base+axis)+delta;
                if(background_parallax && seat!=owner && axis==8) {
                    uint32_t native_part=layer==1?background_dx:signed_half(background_dx);
                    uint32_t seat_part=layer==1?seat_dx:signed_half(seat_dx);
                    value+=seat_part-native_part;
                }
                psx_mod_write_word(shadow+base+axis,value);
            }
        }
    }
    background_call=background_parallax=0;background_actor=background_dx=0;
    psx_mod_counter_add("mmx4.coop.split-train-background-ticks",1);
    return mmx4_coop_finish(cpu,result);
}
int mmx4_coop_split_scene_camera_begin(unsigned owner,uint8_t canonical[0xFC]) {
    if(!available() || owner>1 || mmx4_coop_lifecycle_script_owner()>=0 ||
       psx_mod_read_byte(MMX4_PLAY+0x10) || psx_mod_read_byte(MMX4_PLAY+0x1C))return 0;
    initialize_cameras();read_bytes(CAMERA,canonical,CAMERA_BYTES);
    copy_bytes(CAMERA,state+16u+owner*CAMERA_BYTES,CAMERA_BYTES);
    return 1;
}
void mmx4_coop_split_scene_camera_end(unsigned owner,const uint8_t canonical[0xFC],int committed) {
    copy_bytes(state+16u+owner*CAMERA_BYTES,CAMERA,CAMERA_BYTES);
    if(committed)psx_mod_write_word(state+8u,owner+1u);
    else write_bytes(CAMERA,canonical,CAMERA_BYTES);
}
void mmx4_coop_split_camera_prepare(unsigned owner) {
    if(!available() || owner>1)return;
    initialize_cameras();
    unsigned previous=psx_mod_read_word(state+8u);
    /* A surviving P2 must start its native camera update in its own room,
     * rather than inherit the fallen P1's origins and clamping targets. */
    if(previous && previous!=owner+1u && mmx4_coop_lifecycle_script_owner()<0 &&
       !psx_mod_read_byte(MMX4_PLAY+0x10) && !psx_mod_read_byte(MMX4_PLAY+0x1C))
        copy_bytes(CAMERA,state+16u+owner*CAMERA_BYTES,CAMERA_BYTES);
    psx_mod_write_word(state+8u,owner+1u);
}
void mmx4_coop_split_flip_cameras(void) {
    if(!available() || !psx_mod_read_word(state))return;
    for(unsigned seat=0;seat<2;++seat)for(unsigned layer=0;layer<3;++layer) {
        uint32_t camera=state+16u+seat*CAMERA_BYTES+layer*0x54u;
        uint32_t axis=layer?0x09000000u:0x07000000u;
        for(unsigned at=12;at<=24;at+=12)
            psx_mod_write_word(camera+at,axis-psx_mod_read_word(camera+at));
        psx_mod_write_byte(camera+0x4C,1);
    }
}
void mmx4_coop_split_camera(CPUState *cpu,unsigned owner) {
    if(!available())return;
    initialize_cameras();
    uint8_t canonical[CAMERA_BYTES];read_bytes(CAMERA,canonical,CAMERA_BYTES);
    mmx4_coop_call(cpu,0x80027D40u,0,0);
    mmx4_coop_call(cpu,0x800281E8u,0,0);
    copy_bytes(state+16u+owner*CAMERA_BYTES,CAMERA,CAMERA_BYTES);
    psx_mod_write_word(state+8u,owner+1u);
    write_bytes(CAMERA,canonical,CAMERA_BYTES);
    unsigned other=owner^1u;
    /* PLAY+10 also freezes the world while a native pickup counts health.
     * That pause must not copy room bounds or clamp the distant partner. */
    if(mmx4_coop_lifecycle_script_owner()>=0 ||
       psx_mod_read_byte(MMX4_PLAY+0x1C) || !mmx4_coop_alive(other)) {
        copy_bytes(state+16u+other*CAMERA_BYTES,state+16u+owner*CAMERA_BYTES,CAMERA_BYTES);
        mmx4_coop_lifecycle_camera_bounds(cpu,owner);
        return;
    }
    uint32_t shadow=state+16u+other*CAMERA_BYTES;
    /* Current/target limits, dead zones and authored room metadata belong
     * to the individual view. Shared scripts synchronize whole records above. */
    for(unsigned offset=0;offset<8u;++offset)psx_mod_write_byte(shadow+offset,canonical[offset]);
    copy_bytes(CAMERA,shadow,CAMERA_BYTES);
    if(other)mmx4_coop_enter_second();
    mmx4_coop_call(cpu,0x80027850u,0,0);
    mmx4_coop_call(cpu,0x80027D40u,0,0);
    mmx4_coop_call(cpu,0x800281E8u,0,0);
    copy_bytes(shadow,CAMERA,CAMERA_BYTES);
    if(other)mmx4_coop_leave_second();
    write_bytes(CAMERA,canonical,CAMERA_BYTES);
    psx_mod_counter_add("mmx4.coop.split-camera-ticks",1);
}
static int ram_range(uint32_t address,unsigned bytes) {
    return address>=0x80010000u && address<0x80200000u && bytes<=0x80200000u-address;
}
static int in_camera_area(uint32_t parameters,Mmx4CoopViewActor actor) {
    return actor.active && ram_range(parameters,10u) &&
        actor.world_x<=(int)psx_mod_read_half(parameters) &&
        actor.world_x>=(int)psx_mod_read_half(parameters+2u) &&
        actor.world_y<=(int)psx_mod_read_half(parameters+4u) &&
        actor.world_y>=(int)psx_mod_read_half(parameters+6u);
}
static void apply_camera_area(uint32_t parameters,uint32_t destination) {
    /* Retail 800B5798: region bounds then IDs in 8010AE74's (variable,value)
     * table, resolved through 8010AF9C. Only camera variables are per view;
     * the one original callback applies any shared variables once. */
    psx_mod_write_byte(destination+0x47u,2);
    for(unsigned index=4;index<36 && ram_range(parameters+index*2u,2);++index) {
        unsigned id=psx_mod_read_half(parameters+index*2u);if(!id)break;
        if(id>74u)break;
        uint32_t pair=0x8010AE74u+(id-1u)*4u;
        unsigned variable=psx_mod_read_half(pair);if(variable>=256u)break;
        uint32_t target=psx_mod_read_word(0x8010AF9Cu+variable*4u);
        if(target>=CAMERA && target+2u<=CAMERA+CAMERA_BYTES && !(target&1u))
            psx_mod_write_half(destination+target-CAMERA,psx_mod_read_half(pair+2u));
    }
}
static int spatial_stage_actor(CPUState *cpu,uint32_t address) {
    if(spatial_call || !mmx4_coop_ready() || mmx4_coop_projected() ||
       psx_mod_read_byte(MMX4_PLAY)!=6 || psx_mod_local_view_scope())return 0;
    int split=mmx4_coop_split_views();
    if(split && !available())return 0;
    uint32_t actor=cpu->gpr[4];
    if(actor<0x80142F98u || actor>=0x80143598u || (actor-0x80142F98u)%0x30u)return 0;
    Mmx4CoopViewActor actors[2];mmx4_coop_split_actors(actors);
    unsigned kind=address==0x800B56F4u?0u:6u;
    if(psx_mod_read_byte(actor+1u)!=kind)return 0;
    if(split)initialize_cameras();
    uint32_t parameters=psx_mod_read_byte(actor+4u)?psx_mod_read_word(actor+0x18u):0;
    Mmx4CoopViewActor eligible[2]={actors[0],actors[1]};
    for(unsigned seat=0;seat<2;++seat) {
        if(!kind && ram_range(parameters,10u))eligible[seat].active=(unsigned)in_camera_area(parameters,actors[seat]);
        if(kind)eligible[seat].active=actors[seat].active &&
            ((psx_mod_read_byte(actor+2u)&0xF0u)?
                actors[seat].world_y>=(int16_t)psx_mod_read_half(actor+14u):
                actors[seat].world_x>=(int16_t)psx_mod_read_half(actor+10u));
    }
    int scene=mmx4_coop_lifecycle_script_owner();
    unsigned target=scene>=0?(unsigned)scene:mmx4_coop_view_nearest(
        eligible[0].active || eligible[1].active?eligible:actors,
        (int16_t)psx_mod_read_half(actor+10u),(int16_t)psx_mod_read_half(actor+14u),0);
    uint8_t canonical[CAMERA_BYTES];
    int16_t old_bounds[4];
    for(unsigned i=0;i<4;++i)old_bounds[i]=(int16_t)psx_mod_read_half(CAMERA+0x24u+i*2u);
    int own_camera=target && mmx4_coop_split_scene_camera_begin(target,canonical);
    spatial_call=1;
    uint8_t campaign=psx_mod_read_byte(MMX4_PLAY+0x43);
    if(target) {mmx4_coop_enter_second();psx_mod_write_byte(MMX4_PLAY+0x43,campaign);}
    uint32_t result=mmx4_coop_call(cpu,address,actor,cpu->gpr[5]);
    if(target) {
        psx_mod_write_byte(MMX4_PLAY+0x43,psx_mod_read_byte(MMX4_PLAYER+2));
        mmx4_coop_leave_second();
    }
    spatial_call=0;
    if(own_camera)mmx4_coop_split_scene_camera_end(target,canonical,0);
    if(!split && !kind && scene<0 && actors[target].active && actors[target^1u].active) {
        int16_t bounds[4];
        for(unsigned i=0;i<4;++i)bounds[i]=(int16_t)psx_mod_read_half(CAMERA+0x24u+i*2u);
        int narrowed=bounds[0]<old_bounds[0] || bounds[1]>old_bounds[1] ||
            bounds[2]<old_bounds[2] || bounds[3]>old_bounds[3];
        Mmx4CoopViewActor other=actors[target^1u];
        if(narrowed && bounds[0]>=bounds[1] && bounds[2]>=bounds[3] &&
           (other.world_x<bounds[1] || other.world_x>bounds[0]+320 ||
            other.world_y<bounds[3] || other.world_y>bounds[2]+256))
            mmx4_coop_lifecycle_room_entry(target);
    }
    if(split && !kind && scene<0 && !psx_mod_read_byte(MMX4_PLAY+0x10) && !psx_mod_read_byte(MMX4_PLAY+0x1C)) {
        parameters=psx_mod_read_word(actor+0x18u);
        for(unsigned seat=0;seat<2;++seat)if(in_camera_area(parameters,actors[seat])) {
            apply_camera_area(parameters,state+16u+seat*CAMERA_BYTES);
            if(seat==(unsigned)(!mmx4_coop_alive(0)))apply_camera_area(parameters,CAMERA);
        }
    }
    psx_mod_counter_add(target?"mmx4.coop.p2-spatial-stage":"mmx4.coop.p1-spatial-stage",1);
    return mmx4_coop_finish(cpu,result);
}
static unsigned eligible_regions(PSXPlacementRect regions[2]) {
    Mmx4CoopView views[2];Mmx4CoopViewActor actors[2];
    initialize_cameras();mmx4_coop_split_actors(actors);
    for(unsigned seat=0;seat<2;++seat) {
        uint32_t shadow=state+16u+seat*CAMERA_BYTES;
        views[seat]=(Mmx4CoopView){(int16_t)psx_mod_read_half(shadow+10),
            (int16_t)psx_mod_read_half(shadow+14)};
    }
    return mmx4_coop_view_regions(views,actors,mmx4_coop_lifecycle_script_owner(),
        psx_mod_widescreen_x_margin(),regions);
}
static uint32_t placement_table(void) {
    int stage=(int8_t)psx_mod_read_byte(MMX4_PLAY+12);
    int section=(int8_t)psx_mod_read_byte(MMX4_PLAY+13);
    if(stage<0 || stage>=15 || section<0 || section>1)return 0;
    uint32_t table=psx_mod_read_word(0x800F43C8u+(unsigned)stage*8u+(unsigned)section*4u);
    return (table&7u) || table<0x80010000u || table>=0x80200000u?0:table;
}
static int placement_index(uint32_t record,unsigned *index) {
    uint32_t table=psx_mod_read_word(state+4u);
    if(!table || record<table || ((record-table)&7u) ||
       record-table>=MMX4_COOP_PLACEMENTS*8u)return 0;
    *index=(record-table)/8u;return 1;
}
static void reset_placements(CPUState *cpu,uint32_t address) {
    (void)cpu;(void)address;mmx4_coop_split_reset();
}
static void placement_spawned(CPUState *cpu,uint32_t address) {
    (void)address;unsigned index;
    if(!available() || !cpu->gpr[17] || !placement_index(cpu->gpr[19],&index))return;
    uint32_t byte=state+VISITS_OFFSET+index/8u;
    psx_mod_write_byte(byte,psx_mod_read_byte(byte)|(1u<<(index&7u)));
    psx_mod_counter_add("mmx4.coop.split-placement-visited",1);
}
static int placement_allocate(CPUState *cpu,uint32_t address) {
    (void)address;unsigned index;
    if(!available() || cpu->gpr[31]!=0x8002916Cu ||
       !placement_index(cpu->gpr[19],&index))return 0;
    uint32_t record=cpu->gpr[19];unsigned category=psx_mod_read_byte(record+3u);
    if((psx_mod_read_byte(state+VISITS_OFFSET+index/8u)&(1u<<(index&7u))) ||
       (supplemental && (psx_mod_read_byte(record)&0x70u)) ||
       (supplemental && !(category<3u ||
            (category==4u && psx_mod_read_byte(record+1u)==4u)))) {
        psx_mod_counter_add("mmx4.coop.split-respawn-suppressed",1);
        return mmx4_coop_finish(cpu,0);
    }
    return 0;
}
static void scan_rectangle(CPUState *cpu,PSXPlacementRect region) {
    CPUState saved=*cpu;
    cpu->gpr[29]-=32u;
    uint32_t argument=cpu->gpr[29]+16u,previous=psx_mod_read_word(argument);
    cpu->gpr[4]=(uint32_t)region.left;cpu->gpr[5]=(uint32_t)region.right;
    cpu->gpr[6]=(uint32_t)region.top;cpu->gpr[7]=(uint32_t)region.bottom;
    psx_mod_write_word(argument,0);
    cpu->gpr[31]=0x80028FA8u;
    psx_dispatch_call(cpu,0x80028FECu,cpu->gpr[31]);
    psx_mod_write_word(argument,previous);
    if(cpu->muldiv_ts_done>saved.muldiv_ts_done)saved.muldiv_ts_done=cpu->muldiv_ts_done;
    if(cpu->gte_ts_done>saved.gte_ts_done)saved.gte_ts_done=cpu->gte_ts_done;
    *cpu=saved;
}
static int scan_players(CPUState *cpu,uint32_t address) {
    if(scanning || !available())return 0;
    uint32_t table=placement_table();if(!table)return 0;
    if(table!=psx_mod_read_word(state+4u)) {
        for(unsigned offset=VISITS_OFFSET;offset<STATE_BYTES;offset+=4)
            psx_mod_write_word(state+offset,0);
        psx_mod_write_word(state+4u,table);
    }
    PSXPlacementRect regions[2];unsigned count=eligible_regions(regions);
    for(unsigned index=0;index<MMX4_COOP_PLACEMENTS && table+index*8u+8u<=0x80200000u;++index) {
        uint32_t record=table+index*8u;
        if(psx_mod_read_byte(record+3u)==255u)break;
        int in_region=mmx4_coop_view_contains(regions,count,
            (int16_t)psx_mod_read_half(record+4u),(int16_t)psx_mod_read_half(record+6u));
        uint32_t byte=state+VISITS_OFFSET+index/8u;unsigned mask=1u<<(index&7u);
        if(!in_region)psx_mod_write_byte(byte,psx_mod_read_byte(byte)&~mask);
        else if(psx_mod_read_byte(record)&1u)psx_mod_write_byte(byte,psx_mod_read_byte(byte)|mask);
    }
    uint8_t canonical[CAMERA_BYTES];read_bytes(CAMERA,canonical,CAMERA_BYTES);
    Mmx4CoopViewActor actors[2];mmx4_coop_split_actors(actors);
    int owner=mmx4_coop_lifecycle_script_owner();scanning=1;
    uint32_t result=0;
    for(unsigned seat=0;seat<2;++seat) {
        if(!actors[seat].active || (owner>=0 && owner!=(int)seat))continue;
        copy_bytes(CAMERA,state+16u+seat*CAMERA_BYTES,CAMERA_BYTES);
        result=mmx4_coop_call(cpu,address,cpu->gpr[4],cpu->gpr[5]);
    }
    write_bytes(CAMERA,canonical,CAMERA_BYTES);
    supplemental=1;
    for(unsigned region=0;region<count;++region)scan_rectangle(cpu,regions[region]);
    supplemental=scanning=0;
    psx_mod_counter_add("mmx4.coop.split-placement-scans",count);
    return mmx4_coop_finish(cpu,result);
}
static int actor_bounds(CPUState *cpu,uint32_t address) {
    uint32_t actor=cpu->gpr[4];
    if(bounds_call || !available() || (int8_t)psx_mod_read_byte(actor+0x14u)<0)return 0;
    uint32_t native=address;
    unsigned draw=address!=0x8002B1E8u && address!=0x8002B160u;
    if(address==0x8002B160u) {native=0x8002B1E8u;cpu->gpr[5]=cpu->gpr[6]=64;}
    if(address==0x8002B288u || address==0x8002B3C0u) {
        native=0x8002B318u;cpu->gpr[5]=address==0x8002B288u?32:96;
        cpu->gpr[6]=address==0x8002B288u?32:80;
    }
    unsigned layer=psx_mod_read_byte(actor+0x14u);if(layer>=3)return 0;
    initialize_cameras();
    uint8_t canonical[CAMERA_BYTES];read_bytes(CAMERA,canonical,CAMERA_BYTES);
    Mmx4CoopViewActor actors[2];mmx4_coop_split_actors(actors);
    int owner=mmx4_coop_lifecycle_script_owner();unsigned visible=0;
    uint32_t result=draw?0:1;bounds_call=1;
    for(unsigned seat=0;seat<2;++seat) {
        if(!actors[seat].active || (owner>=0 && owner!=(int)seat))continue;
        copy_bytes(CAMERA,state+16u+seat*CAMERA_BYTES,CAMERA_BYTES);
        uint32_t candidate=mmx4_coop_call(cpu,native,actor,cpu->gpr[5]);
        if(draw) {
            if(psx_mod_read_byte(actor+3u)) {visible=1;result=candidate;}
        }else if(!candidate)result=0;
    }
    if(draw)psx_mod_write_byte(actor+3u,(uint8_t)visible);
    write_bytes(CAMERA,canonical,CAMERA_BYTES);bounds_call=0;
    return mmx4_coop_finish(cpu,result);
}
PSX_MOD_CONSTRUCTOR(mmx4_register_coop_split) {
    psx_mod_register_function_filter_plugin(COOP_ID,0x800D8648u,train_background);
    psx_mod_register_function_filter_plugin(COOP_ID,0x800D9218u,train_background);
    psx_mod_register_function_filter_plugin(COOP_ID,0x800D964Cu,train_foreground);
    psx_mod_register_function_filter_plugin(COOP_ID,0x800B56F4u,spatial_stage_actor);
    psx_mod_register_function_filter_plugin(COOP_ID,0x800B6A0Cu,spatial_stage_actor);
    psx_mod_register_function_entry_plugin(COOP_ID,0x80028DB4u,reset_placements);
    psx_mod_register_function_filter_plugin(COOP_ID,0x80028E24u,scan_players);
    psx_mod_register_function_filter_plugin(COOP_ID,0x800293E8u,placement_allocate);
    psx_mod_register_instruction_plugin(COOP_ID,0x80029284u,0xA2620000u,placement_spawned);
    psx_mod_register_function_filter_plugin(COOP_ID,0x8002B1E8u,actor_bounds);
    psx_mod_register_function_filter_plugin(COOP_ID,0x8002B160u,actor_bounds);
    psx_mod_register_function_filter_plugin(COOP_ID,0x8002B318u,actor_bounds);
    psx_mod_register_function_filter_plugin(COOP_ID,0x8002B288u,actor_bounds);
    psx_mod_register_function_filter_plugin(COOP_ID,0x8002B3C0u,actor_bounds);
}
