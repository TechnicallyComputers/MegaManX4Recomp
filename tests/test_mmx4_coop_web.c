/* Original-backed helper contracts verify seat ownership and contact-only
 * retries. Hidden native gameplay remains required for actual wall jumps. */
#include "mmx4_coop_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
#define SHOT 0x801406F8u
#define WEB_SOLID 0x80108800u
#define ZERO_SOLID 0x80118000u
#define MIRROR 0x801F1000u
#define SOLID 0x8002E184u
#define RESPONSE 0x8002E294u
#define CARRY 0x8002E380u
#define SIDES 0x8002C36Cu
#define NATIVE_RESULT 0x13579BDFu

int mmx4_coop_web_activate(void);
void mmx4_coop_web_reset(void);
void mmx4_coop_web_forget(uint32_t actor,unsigned owner);
int mmx4_coop_web_busy(void);
int mmx4_coop_web_contact(CPUState *cpu,uint32_t address);
uint32_t mmx4_coop_web_digest(uint32_t seed);

static uint8_t ram[0x200000],first[0xE4],second[0xE4];
static unsigned projected,ready=1,local_scope,hidden[2],allocation_failure;
static unsigned originals,responses,carries,side_queries,dirty_mirror;
static unsigned queries,wall_contacts;
static uint32_t observed_carry_sides;
static int script_owner=-1;
uint8_t psx_mod_read_byte(uint32_t address) {return ram[address&0x1FFFFFu];}
void psx_mod_write_byte(uint32_t address,uint8_t value) {ram[address&0x1FFFFFu]=value;}
uint16_t psx_mod_read_half(uint32_t address) {
    return psx_mod_read_byte(address)|(uint16_t)psx_mod_read_byte(address+1)<<8;
}
void psx_mod_write_half(uint32_t address,uint16_t value) {
    psx_mod_write_byte(address,(uint8_t)value);psx_mod_write_byte(address+1,(uint8_t)(value>>8));
}
uint32_t psx_mod_read_word(uint32_t address) {
    return psx_mod_read_half(address)|(uint32_t)psx_mod_read_half(address+2)<<16;
}
void psx_mod_write_word(uint32_t address,uint32_t value) {
    psx_mod_write_half(address,(uint16_t)value);psx_mod_write_half(address+2,(uint16_t)(value>>16));
}
uint32_t psx_mod_alloc_guest_memory(uint32_t size,uint32_t alignment) {
    CHECK(size==0xE4u && alignment==16);return allocation_failure?0:MIRROR;
}
int psx_mod_local_view_scope(void) {return (int)local_scope;}
int mmx4_coop_ready(void) {return (int)ready;}
int mmx4_coop_projected(void) {return (int)projected;}
uint8_t *mmx4_coop_second_body(void) {return second;}
uint8_t *mmx4_coop_first_body(void) {return first;}
int mmx4_coop_lifecycle_script_owner(void) {return script_owner;}
int mmx4_coop_alive(unsigned seat) {
    if(hidden[seat])return 0;
    if(seat==projected)return psx_mod_read_byte(MMX4_PLAYER) &&
        psx_mod_read_byte(MMX4_PLAYER+4)<2 && (psx_mod_read_byte(MMX4_PLAYER+0x5C)&127u);
    const uint8_t *body=seat?second:first;
    return body[0] && body[4]<2 && (body[0x5C]&127u);
}
void psx_mod_counter_add(const char *name,uint32_t value) {
    if(!strcmp(name,"mmx4.coop.zero-web-queries"))queries+=value;
    else if(!strcmp(name,"mmx4.coop.zero-web-side-contacts"))wall_contacts+=value;
    else if(!strcmp(name,"mmx4.coop.zero-web-body-mirror"))CHECK(value==MIRROR);
    else CHECK(0);
}
static uint32_t body_word(const uint8_t *body,unsigned offset) {
    return (uint32_t)body[offset]|(uint32_t)body[offset+1]<<8|
        (uint32_t)body[offset+2]<<16|(uint32_t)body[offset+3]<<24;
}
static void body_put(uint8_t *body,unsigned offset,uint32_t value) {
    for(unsigned i=0;i<4;++i)body[offset+i]=(uint8_t)(value>>(i*8));
}
static uint8_t *partner(void) {return projected?first:second;}
static void zero_body(uint8_t *body,int x,int y) {
    memset(body,0,0xE4u);body[0]=body[3]=body[4]=body[2]=1;body[5]=2;body[0x5C]=32;
    body_put(body,8,((uint32_t)x<<16)|0x1234u);
    body_put(body,12,((uint32_t)y<<16)|0x5678u);
    body_put(body,24,body_word(body,8));body_put(body,28,body_word(body,12));
    body_put(body,0x68,ZERO_SOLID);body[0xA7]=0x55;body[0xB9]=0xAA;
}
static void shot_position(int x,int y,int previous_x,int previous_y) {
    psx_mod_write_word(SHOT+8,(uint32_t)x<<16);psx_mod_write_word(SHOT+12,(uint32_t)y<<16);
    psx_mod_write_word(SHOT+24,(uint32_t)previous_x<<16);
    psx_mod_write_word(SHOT+28,(uint32_t)previous_y<<16);
}
/* The three native helpers take an explicit body pointer. This small geometry
 * model uses a 2x20 web half-box and a 6x18 body half-box. It exercises side
 * response, carry history and vertical cleanup without a second actor update. */
static uint32_t native(CPUState *cpu,uint32_t address) {
    uint32_t actor=cpu->gpr[4],body=cpu->gpr[5];
    CHECK(mmx4_coop_web_busy());
    if(address==SOLID) {
        ++originals;CHECK(actor==SHOT && !mmx4_coop_web_contact(cpu,address));
        psx_mod_write_byte(actor+0x72,2);psx_mod_write_byte(actor+0x76,1);
        return NATIVE_RESULT;
    }
    CHECK(actor==SHOT && body==MIRROR);
    int ax=(int16_t)psx_mod_read_half(actor+10),ay=(int16_t)psx_mod_read_half(actor+14);
    int bx=(int16_t)psx_mod_read_half(body+10),by=(int16_t)psx_mod_read_half(body+14);
    int dx=ax-bx,dy=ay-by,abs_x=abs(dx),abs_y=abs(dy);
    if(address==RESPONSE) {
        ++responses;
        if(abs_x<8 && abs_y<38) {
            int separation_x=dx>0?dx-8:dx+8;
            int separation_y=dy>0?dy-38:dy+38;
            psx_mod_write_half(body+0x6C,(uint16_t)separation_x);
            psx_mod_write_half(body+0x6E,(uint16_t)separation_y);
            if(abs(separation_x)<abs(separation_y))
                psx_mod_write_half(body+10,(uint16_t)(bx+separation_x));
            else psx_mod_write_half(body+14,(uint16_t)(by+separation_y));
        }
    }else if(address==CARRY) {
        ++carries;observed_carry_sides=cpu->gpr[6];
        int movement=ax-(int16_t)psx_mod_read_half(actor+26);
        unsigned opposing=movement>0?2u:1u;
        if(movement && !(cpu->gpr[6]&opposing))
            psx_mod_write_half(body+10,(uint16_t)(bx+movement));
        movement=ay-(int16_t)psx_mod_read_half(actor+30);
        if(movement>0)psx_mod_write_half(body+14,(uint16_t)(by+movement));
    }else if(address==SIDES) {
        ++side_queries;CHECK(cpu->gpr[6]==0);
        unsigned sides=0;
        if(abs_y==38 && abs_x<8)sides|=ay<by?4u:8u;
        if(abs_x==8 && abs_y<38) {
            sides|=ax<bx?2u:1u;
            psx_mod_write_byte(body+0x4A,psx_mod_read_byte(actor+0x75));
        }
        psx_mod_write_byte(actor+0x72,(uint8_t)sides);
        psx_mod_write_byte(actor+0x76,(uint8_t)((sides&0xBu)!=0));
        psx_mod_write_byte(body+0x71,psx_mod_read_byte(body+0x71)|(uint8_t)sides);
        if(dirty_mirror) { /* Copyback must stay within proven mutable fields. */
            psx_mod_write_byte(body+5,99);psx_mod_write_byte(body+0x5C,0);
            psx_mod_write_byte(body+0xA7,0);psx_mod_write_byte(body+0xB9,0);
        }
    }else CHECK(0);
    return 0xDEADC0DEu;
}
uint32_t mmx4_coop_call(CPUState *cpu,uint32_t address,uint32_t a0,uint32_t a1) {
    CPUState saved=*cpu;cpu->gpr[4]=a0;cpu->gpr[5]=a1;
    uint32_t result=native(cpu,address);*cpu=saved;return result;
}
int mmx4_coop_finish(CPUState *cpu,uint32_t result) {cpu->gpr[2]=result;return 1;}
static void setup(void) {
    memset(ram,0,sizeof ram);projected=local_scope=dirty_mirror=0;ready=1;
    hidden[0]=hidden[1]=0;script_owner=-1;
    psx_mod_write_byte(MMX4_PLAY,6);psx_mod_write_byte(MMX4_PLAYER,1);
    psx_mod_write_byte(MMX4_PLAYER+4,1);psx_mod_write_byte(MMX4_PLAYER+5,2);
    psx_mod_write_byte(MMX4_PLAYER+0x5C,32);
    zero_body(first,95,100);zero_body(second,95,100);
    psx_mod_write_byte(SHOT,1);psx_mod_write_byte(SHOT+1,1);psx_mod_write_byte(SHOT+0x75,1);
    psx_mod_write_word(SHOT+0x68,WEB_SOLID);shot_position(100,100,100,100);
    originals=responses=carries=side_queries=queries=wall_contacts=0;
    mmx4_coop_web_reset();
}
static void contact(CPUState *cpu) {
    cpu->gpr[4]=SHOT;cpu->gpr[5]=0xCAFEBABEu;cpu->gpr[6]=0xA5A5A5A5u;
    unsigned owner=projected;uint8_t native_body[0xE4];
    for(unsigned i=0;i<sizeof native_body;++i)native_body[i]=psx_mod_read_byte(MMX4_PLAYER+i);
    CPUState saved=*cpu;unsigned calls=originals;
    CHECK(mmx4_coop_web_contact(cpu,SOLID));CHECK(originals==calls+1 && cpu->gpr[2]==NATIVE_RESULT);
    CHECK(!mmx4_coop_web_busy() && projected==owner && cpu->gpr[6]==saved.gpr[6]);
    saved.gpr[2]=NATIVE_RESULT;CHECK(!memcmp(cpu,&saved,sizeof saved));
    for(unsigned i=0;i<sizeof native_body;++i)CHECK(native_body[i]==psx_mod_read_byte(MMX4_PLAYER+i));
    CHECK(psx_mod_read_byte(SHOT+0x72)==2 && psx_mod_read_byte(SHOT+0x76)==1);
    CHECK(psx_mod_read_word(SHOT+0x68)==WEB_SOLID && psx_mod_read_byte(SHOT+1)==1);
}
int main(void) {
    CPUState cpu={0};CHECK(mmx4_coop_web_activate());setup();
    uint32_t baseline_digest=mmx4_coop_web_digest(2166136261u);
    uint32_t original_fraction=body_word(second,8)&0xFFFFu;
    dirty_mirror=1;contact(&cpu);
    CHECK(responses==1 && side_queries==1 && carries==0 && queries==1 && wall_contacts==1);
    CHECK((body_word(second,8)>>16)==92 && (body_word(second,8)&0xFFFFu)==original_fraction);
    CHECK(second[0x71]==1 && second[0x4A]==1 && second[5]==2 && second[0x5C]==32);
    CHECK(second[0xA7]==0x55 && second[0xB9]==0xAA);dirty_mirror=0;
    uint32_t active_digest=mmx4_coop_web_digest(2166136261u);
    CHECK(active_digest!=baseline_digest);
    /* Prior side contact carries Zero with this exact owner's moving anchor. */
    shot_position(102,100,100,100);contact(&cpu);
    CHECK(carries==1 && observed_carry_sides==1 && (body_word(second,8)>>16)==94);
    /* The same guest shot address in P2's private pool has independent history. */
    projected=1;zero_body(first,92,100);unsigned previous_carries=carries;
    contact(&cpu);CHECK(carries==previous_carries && (body_word(first,8)>>16)==92);
    CHECK(first[0x71]==0);shot_position(100,100,100,100);zero_body(first,105,100);contact(&cpu);
    CHECK((body_word(first,8)>>16)==108 && first[0x71]==2);
    projected=0;zero_body(second,92,100);shot_position(102,100,100,100);contact(&cpu);
    CHECK(carries==previous_carries+1 && (body_word(second,8)>>16)==94);
    /* Allocation reuse forgets one owner's history without losing the other. */
    mmx4_coop_web_forget(SHOT,0);zero_body(second,92,100);previous_carries=carries;
    contact(&cpu);CHECK(carries==previous_carries && (body_word(second,8)>>16)==92);
    projected=1;zero_body(first,108,100);shot_position(98,100,100,100);contact(&cpu);
    CHECK(carries==previous_carries+1 && observed_carry_sides==2 && (body_word(first,8)>>16)==106);
    uint32_t digest=mmx4_coop_web_digest(2166136261u);
    mmx4_coop_web_forget(SHOT+1,0);mmx4_coop_web_forget(SHOT,2);
    CHECK(mmx4_coop_web_digest(2166136261u)==digest);
    /* Projectile top/bottom contact is cleaned just as it is for X, while
     * normal horizontal flags and integer-only position response survive. */
    projected=0;mmx4_coop_web_forget(SHOT,0);shot_position(100,100,100,100);
    zero_body(second,100,65);second[0x71]=1;contact(&cpu);
    CHECK((body_word(second,12)>>16)==62 && second[0x71]==1);
    mmx4_coop_web_forget(SHOT,0);zero_body(second,100,135);second[0x71]=2;contact(&cpu);
    CHECK((body_word(second,12)>>16)==138 && second[0x71]==2);
    /* Active combat poses are normal bodies; native arrivals and shared
     * scripts are excluded before a partner query, with one owner call. */
    setup();second[5]=48;contact(&cpu);CHECK(responses==1);
    /* Native Jump/Fall/WallSlide/WallJump set nonzero movement control.
     * Sustained contact must survive this observed airborne progression. */
    setup();
    const unsigned airborne[][2]={{6,1},{7,0xFF},{11,0xFF},{10,1}};
    for(unsigned i=0;i<4;++i) {
        second[5]=(uint8_t)airborne[i][0];second[0x67]=(uint8_t)airborne[i][1];
        contact(&cpu);CHECK(responses==i+1 && (second[0x71]&3u) && second[0x4A]==1);
        CHECK(second[5]==airborne[i][0] && second[0x67]==airborne[i][1]);
    }
    for(unsigned i=0;i<4;++i) {
        setup();psx_mod_write_byte(MMX4_PLAYER+5,(uint8_t)airborne[i][0]);
        psx_mod_write_byte(MMX4_PLAYER+0x67,(uint8_t)airborne[i][1]);
        contact(&cpu);CHECK(responses==1);
        setup();second[5]=(uint8_t)airborne[i][0];
        second[0x67]=(uint8_t)(airborne[i][1]==1?0xFF:1);
        contact(&cpu);CHECK(!responses);
    }
    const unsigned excluded[]={0x67,0x79,0xBC,0xC0,0xC3,0xC4,0xC5};
    for(unsigned i=0;i<sizeof excluded/sizeof excluded[0];++i) {
        setup();partner()[excluded[i]]=0xFF;contact(&cpu);CHECK(!responses && originals==1);
        setup();psx_mod_write_byte(MMX4_PLAYER+excluded[i],0xFF);contact(&cpu);
        CHECK(!responses && originals==1);
    }
    const unsigned locks[]={0x0F,0x10,0x1C};
    for(unsigned i=0;i<sizeof locks/sizeof locks[0];++i) {
        setup();psx_mod_write_byte(MMX4_PLAY+locks[i],1);contact(&cpu);CHECK(!responses);
    }
    setup();script_owner=1;contact(&cpu);CHECK(!responses);
    setup();hidden[1]=1;contact(&cpu);CHECK(!responses);
    setup();second[4]=2;contact(&cpu);CHECK(!responses);
    setup();second[5]=0;contact(&cpu);CHECK(!responses);
    setup();second[5]=1;contact(&cpu);CHECK(!responses);
    setup();psx_mod_write_byte(MMX4_PLAYER+5,0);contact(&cpu);CHECK(!responses);
    setup();second[0x5C]=0;contact(&cpu);CHECK(!responses);
    setup();second[2]=0;contact(&cpu);CHECK(!responses);
    setup();body_put(second,0x68,0);contact(&cpu);CHECK(!responses);
    setup();psx_mod_write_byte(MMX4_PLAYER+0x5C,0);contact(&cpu);CHECK(!responses);
    /* Unopened/expired/other shots and enemy webs stay on the existing path. */
    setup();cpu.gpr[4]=SHOT;psx_mod_write_byte(SHOT+0x75,0);
    CHECK(!mmx4_coop_web_contact(&cpu,SOLID) && !originals);
    psx_mod_write_byte(SHOT+0x75,1);psx_mod_write_word(SHOT+0x68,WEB_SOLID+4);
    CHECK(!mmx4_coop_web_contact(&cpu,SOLID) && !originals);
    psx_mod_write_word(SHOT+0x68,WEB_SOLID);psx_mod_write_byte(SHOT+1,22);
    CHECK(!mmx4_coop_web_contact(&cpu,SOLID) && !originals);
    psx_mod_write_byte(SHOT+1,1);cpu.gpr[4]=0x8013F328u;
    CHECK(!mmx4_coop_web_contact(&cpu,SOLID) && !originals);
    cpu.gpr[4]=SHOT;local_scope=1;CHECK(!mmx4_coop_web_contact(&cpu,SOLID));local_scope=0;
    ready=0;CHECK(!mmx4_coop_web_contact(&cpu,SOLID));ready=1;
    CHECK(!mmx4_coop_web_contact(&cpu,RESPONSE));
    /* Reset discards old contact history; counterpart initialization does not. */
    setup();contact(&cpu);digest=mmx4_coop_web_digest(2166136261u);
    projected=1;mmx4_coop_web_reset();CHECK(mmx4_coop_web_digest(2166136261u)==digest);
    projected=0;mmx4_coop_web_reset();CHECK(mmx4_coop_web_digest(2166136261u)!=digest);
    shot_position(102,100,100,100);zero_body(second,92,100);previous_carries=carries;
    contact(&cpu);CHECK(carries==previous_carries);
    allocation_failure=1;CHECK(!mmx4_coop_web_activate());
    CHECK(!mmx4_coop_web_contact(&cpu,SOLID));
    puts("mmx4_coop_web_test: PASS");return 0;
}
