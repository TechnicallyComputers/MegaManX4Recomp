/* Lightning Web offers the other Zero body its native solid contact only.
 * Both private shot pools and the original projectile update remain intact.
 * See docs/COOP_STAGE_INTERACTIONS_EVIDENCE.md for original helper contracts. */
#include "mmx4_coop_internal.h"
#include <string.h>

#define WEB_SOLID 0x80108800u
#define SHOTS 0x801406F8u
#define SHOT_COUNT 16u
#define SHOT_STRIDE 0x9Cu
#define BODY_BYTES 0xE4u
#define SOLID_CONTACT 0x8002E184u
#define BODY_RESPONSE 0x8002E294u
#define BODY_CARRY 0x8002E380u
#define BODY_SIDES 0x8002C36Cu

typedef struct { uint8_t sides,carried; } WebContact;
static WebContact contacts[2][SHOT_COUNT];
static uint32_t mirror;
static unsigned busy;

static int shot_slot(uint32_t actor) {
    if(actor<SHOTS || actor>=SHOTS+SHOT_COUNT*SHOT_STRIDE ||
       (actor-SHOTS)%SHOT_STRIDE)return -1;
    return (int)((actor-SHOTS)/SHOT_STRIDE);
}
void mmx4_coop_web_reset(void) {
    /* Counterpart enrollment invokes the native player initializer privately. */
    if(mmx4_coop_projected())return;
    memset(contacts,0,sizeof contacts);busy=0;
    if(mirror)for(unsigned i=0;i<BODY_BYTES;++i)psx_mod_write_byte(mirror+i,0);
}
int mmx4_coop_web_activate(void) {
    mirror=psx_mod_alloc_guest_memory(BODY_BYTES,16);
    mmx4_coop_web_reset();
    if(mirror)psx_mod_counter_add("mmx4.coop.zero-web-body-mirror",mirror);
    return mirror!=0;
}
int mmx4_coop_web_busy(void) {return (int)busy;}
void mmx4_coop_web_forget(uint32_t actor,unsigned owner) {
    int slot=shot_slot(actor);
    if(slot>=0 && owner<2)memset(&contacts[owner][slot],0,sizeof contacts[owner][slot]);
}
static int ordinary_control(unsigned action,unsigned control) {
    /* +67 also records ordinary airborne movement: native Jump/Fall and
     * WallJump/WallSlide initialize these exact action/control pairs. */
    return !control || (control==1 && (action==6 || action==10)) ||
        (control==0xFF && (action==7 || action==11));
}
static int partner_eligible(unsigned owner,const uint8_t *body) {
    return mmx4_coop_alive(owner) && mmx4_coop_alive(owner^1u) &&
        psx_mod_read_byte(MMX4_PLAYER+2)==0 &&
        psx_mod_read_byte(MMX4_PLAYER+4)==1 && psx_mod_read_byte(MMX4_PLAYER+5)>=2 &&
        ordinary_control(psx_mod_read_byte(MMX4_PLAYER+5),psx_mod_read_byte(MMX4_PLAYER+0x67)) &&
        !psx_mod_read_byte(MMX4_PLAYER+0x79) &&
        !psx_mod_read_byte(MMX4_PLAYER+0xBC) && !psx_mod_read_byte(MMX4_PLAYER+0xC0) &&
        !psx_mod_read_byte(MMX4_PLAYER+0xC3) && !psx_mod_read_byte(MMX4_PLAYER+0xC4) &&
        body[2]==1 && body[4]==1 &&
        body[5]>=2 && ordinary_control(body[5],body[0x67]) && !body[0x79] && !body[0xBC] &&
        !body[0xC0] && !body[0xC3] && !body[0xC4] && !body[0xC5] &&
        !psx_mod_read_byte(MMX4_PLAYER+0xC5) &&
        ((uint32_t)body[0x68]|(uint32_t)body[0x69]<<8|
         (uint32_t)body[0x6A]<<16|(uint32_t)body[0x6B]<<24) &&
        mmx4_coop_lifecycle_script_owner()<0 &&
        !psx_mod_read_byte(MMX4_PLAY+0x0F) && !psx_mod_read_byte(MMX4_PLAY+0x10) &&
        !psx_mod_read_byte(MMX4_PLAY+0x1C);
}
int mmx4_coop_web_contact(CPUState *cpu,uint32_t address) {
    uint32_t actor=cpu->gpr[4];int slot=shot_slot(actor);
    if(busy || !mirror || !mmx4_coop_ready() || psx_mod_local_view_scope() ||
       psx_mod_read_byte(MMX4_PLAY)!=6 || address!=SOLID_CONTACT || slot<0 ||
       !psx_mod_read_byte(actor) || psx_mod_read_byte(actor+1)!=1 ||
       !psx_mod_read_byte(actor+0x75) || psx_mod_read_word(actor+0x68)!=WEB_SOLID)return 0;
    unsigned owner=(unsigned)(mmx4_coop_projected()!=0);
    WebContact *contact=&contacts[owner][slot];
    uint8_t *body=owner?mmx4_coop_first_body():mmx4_coop_second_body();
    busy=1;
    /* The combat filter bypasses itself while busy(), allowing exactly one
     * original contact for the current X/Double/vehicle contexts. Its result
     * is returned to the web caller unchanged. No player projection occurs. */
    uint32_t result=mmx4_coop_call(cpu,address,actor,cpu->gpr[5]);
    if(partner_eligible(owner,body)) {
        for(unsigned i=0;i<BODY_BYTES;++i)psx_mod_write_byte(mirror+i,body[i]);
        uint8_t owner_sides=psx_mod_read_byte(actor+0x72);
        uint8_t owner_carried=psx_mod_read_byte(actor+0x76);
        psx_mod_write_byte(actor+0x72,contact->sides);
        psx_mod_write_byte(actor+0x76,contact->carried);
        mmx4_coop_call(cpu,BODY_RESPONSE,actor,mirror);
        uint32_t argument=cpu->gpr[6];
        if((int8_t)contact->carried>0) {
            cpu->gpr[6]=contact->sides;
            mmx4_coop_call(cpu,BODY_CARRY,actor,mirror);
        }
        cpu->gpr[6]=0;
        mmx4_coop_call(cpu,BODY_SIDES,actor,mirror);
        cpu->gpr[6]=argument;
        contact->sides=psx_mod_read_byte(actor+0x72);
        contact->carried=psx_mod_read_byte(actor+0x76);
        /* Native80092FA8..FFC permits wall contact but removes the web's
         * top/bottom flags. Apply the same cleanup to the partner body. */
        uint8_t sides=psx_mod_read_byte(mirror+0x71);
        if(contact->sides&4u)sides&=0x0Bu;
        if(contact->sides&8u)sides&=0x07u;
        psx_mod_write_byte(mirror+0x71,sides);
        /* These are the only body bytes written by the three inspected
         * helpers: integer XY, separation XY, wall contact kind and sides.
         * Fractional XY, logical action, health and inventory stay intact. */
        static const unsigned changed[]={10,11,14,15,0x6C,0x6D,0x6E,0x6F,0x4A,0x71};
        for(unsigned i=0;i<sizeof changed/sizeof changed[0];++i)
            body[changed[i]]=psx_mod_read_byte(mirror+changed[i]);
        psx_mod_write_byte(actor+0x72,owner_sides);
        psx_mod_write_byte(actor+0x76,owner_carried);
        psx_mod_counter_add("mmx4.coop.zero-web-queries",1);
        if(contact->sides&3u)psx_mod_counter_add("mmx4.coop.zero-web-side-contacts",1);
    }else memset(contact,0,sizeof *contact);
    busy=0;
    return mmx4_coop_finish(cpu,result);
}
uint32_t mmx4_coop_web_digest(uint32_t seed) {
    for(unsigned owner=0;owner<2;++owner)for(unsigned slot=0;slot<SHOT_COUNT;++slot) {
        seed=(seed^contacts[owner][slot].sides)*16777619u;
        seed=(seed^contacts[owner][slot].carried)*16777619u;
    }
    return (seed^(uint8_t)busy)*16777619u;
}
