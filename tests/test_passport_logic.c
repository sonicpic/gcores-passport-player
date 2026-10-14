#include <assert.h>
#include <stdio.h>
#include "passport_logic.h"
int main(void) {
    assert(passport_clamp_selection(0,-1,5)==0);
    assert(passport_clamp_selection(4,1,5)==4);
    assert(passport_clamp_selection(9,1,0)==0);
    assert(passport_clamp_selection(0,1,5)==1);
    assert(passport_seek_target(12,-30,3600)==0);
    assert(passport_seek_target(3590,30,3600)==3599);
    assert(passport_seek_target(UINT32_MAX,30,0)==UINT32_MAX);
    assert(passport_power_stage(300000,true,false,false)==PASSPORT_POWER_DARK);
    assert(passport_power_stage(300000,false,true,false)==PASSPORT_POWER_DARK);
    assert(passport_power_stage(300000,false,false,true)==PASSPORT_POWER_ACTIVE);
    assert(passport_power_stage(300000,false,false,false)==PASSPORT_POWER_SLEEP);
    assert(passport_power_stage(20000,false,false,false)==PASSPORT_POWER_DIM);
    assert(passport_power_stage(60000,false,false,false)==PASSPORT_POWER_DARK);
    assert(passport_power_stage_configured(15000,true,false,false,15000)==PASSPORT_POWER_DARK);
    assert(passport_power_stage_configured(300000,true,false,false,0)==PASSPORT_POWER_ACTIVE);
    assert(passport_power_stage_configured(300000,false,false,false,0)==PASSPORT_POWER_ACTIVE);
    assert(passport_power_stage_configured(300000,false,false,true,15000)==PASSPORT_POWER_ACTIVE);
    passport_scrub_t scrub={0}; uint32_t target=0;
    assert(!passport_scrub_commit(&scrub,&target));
    passport_scrub_begin(&scrub,100,3600,1,1000);
    assert(scrub.target==130 && !passport_scrub_tick(&scrub,1199));
    assert(passport_scrub_tick(&scrub,1200) && scrub.target==160);
    assert(passport_scrub_tick(&scrub,3000) && scrub.target==220);
    assert(passport_scrub_tick(&scrub,6000) && scrub.target==340);
    assert(passport_scrub_commit(&scrub,&target) && target==340);
    assert(!passport_scrub_commit(&scrub,&target));
    assert(!passport_scrub_tick(&scrub,7000));
    passport_scrub_begin(&scrub,10,3600,-1,0);
    assert(scrub.target==0); passport_scrub_tick(&scrub,200); assert(scrub.target==0);
    passport_scrub_begin(&scrub,3590,3600,1,0); assert(scrub.target==3599);
    passport_screen_guard_t guard={0};
    assert(passport_screen_guard_consume(&guard,0,true,false,true,0));
    assert(passport_screen_guard_consume(&guard,0,false,false,false,500000)); /* long wake */
    assert(passport_screen_guard_consume(&guard,0,false,true,false,600000));
    assert(passport_screen_guard_consume(&guard,0,false,false,false,780000)); /* delayed click */
    assert(passport_screen_guard_consume(&guard,0,true,false,false,800000)); /* second wake click */
    assert(passport_screen_guard_consume(&guard,0,false,true,false,850000));
    assert(passport_screen_guard_consume(&guard,0,false,false,false,1000000)); /* double */
    assert(!passport_screen_guard_consume(&guard,0,true,false,false,1300000));
    assert(!passport_screen_guard_consume(&guard,0,false,false,false,1500000));
    guard=(passport_screen_guard_t){0};
    assert(passport_screen_guard_consume(&guard,0,true,false,true,0));
    assert(passport_screen_guard_consume(&guard,1,true,false,false,500000));
    assert(passport_screen_guard_consume(&guard,0,false,true,false,500001));
    assert(passport_screen_guard_consume(&guard,1,false,false,false,1000000));
    assert(passport_screen_guard_consume(&guard,1,false,true,false,1000001));
    puts("Passport input / seek / power policy: PASS");
}
