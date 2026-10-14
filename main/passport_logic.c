#include "passport_logic.h"
int passport_clamp_selection(int selection, int direction, int count) {
    if (count <= 0) return 0;
    int64_t value = (int64_t)selection + direction;
    if (value < 0) return 0;
    return value >= count ? count-1 : (int)value;
}
uint32_t passport_seek_target(uint32_t position, int delta, uint32_t duration) {
    int64_t target=(int64_t)position+delta;
    if (target < 0) target=0;
    if (duration && target >= duration) target=duration-1;
    return target > UINT32_MAX ? UINT32_MAX : (uint32_t)target;
}
passport_power_stage_t passport_power_stage(uint64_t idle_ms, bool active_audio, bool busy, bool provisioning) {
    return passport_power_stage_configured(idle_ms,active_audio,busy,provisioning,60000);
}
passport_power_stage_t passport_power_stage_configured(uint64_t idle_ms, bool active_audio, bool busy, bool provisioning, uint32_t screen_timeout_ms) {
    if (screen_timeout_ms && !active_audio && !busy && !provisioning && idle_ms >= 300000) return PASSPORT_POWER_SLEEP;
    if (screen_timeout_ms && !provisioning && idle_ms >= screen_timeout_ms) return PASSPORT_POWER_DARK;
    if (screen_timeout_ms && !provisioning && idle_ms >= screen_timeout_ms/3) return PASSPORT_POWER_DIM;
    return PASSPORT_POWER_ACTIVE;
}
void passport_scrub_begin(passport_scrub_t *s, uint32_t position, uint32_t duration, int direction, uint64_t now_ms) {
    *s=(passport_scrub_t){.active=true,.direction=direction<0?-1:1,.duration=duration,
        .started_ms=now_ms,.next_ms=now_ms+200};
    s->target=passport_seek_target(position,s->direction*30,duration);
}
bool passport_scrub_tick(passport_scrub_t *s, uint64_t now_ms) {
    if (!s->active || now_ms<s->next_ms) return false;
    int step=now_ms-s->started_ms>=5000?120:now_ms-s->started_ms>=2000?60:30;
    s->target=passport_seek_target(s->target,s->direction*step,s->duration);
    /* Never catch up a stalled UI with an unbounded burst of steps. */
    s->next_ms=now_ms+200;
    return true;
}
bool passport_scrub_commit(passport_scrub_t *s, uint32_t *target) {
    if (!s->active) return false;
    s->active=false; *target=s->target; return true;
}
bool passport_screen_guard_consume(passport_screen_guard_t *s, unsigned key, bool press, bool release, bool dark, uint64_t now_us) {
    if (key>=3) return true;
    if (press) {
        bool waking=false;
        for (unsigned i=0;i<3;i++) waking |= s->held[i] && s->swallow[i];
        s->swallow[key]=dark || waking || now_us<s->until_us;
        s->held[key]=true;
    }
    if (release) s->held[key]=false;
    if (!s->swallow[key]) return false;
    /* RELEASE precedes the component's delayed CLICK/DOUBLE event. */
    if (release) s->until_us=now_us+350000;
    return true;
}
