#ifndef SRP_VGA_DEADLINE_H
#define SRP_VGA_DEADLINE_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t next_us;
    uint32_t last_entry_us;
    bool valid;
} srp_vga_deadline_t;
typedef struct {
    uint32_t wait_us;
    uint32_t late_us;
    bool caught_up;
    bool reanchored;
} srp_vga_deadline_result_t;
static inline void srp_vga_deadline_reset(srp_vga_deadline_t *s){
    s->next_us=0;s->last_entry_us=0;s->valid=false;
}
static inline srp_vga_deadline_result_t srp_vga_deadline_begin(
        srp_vga_deadline_t *s, uint32_t now, uint32_t epoch,
        uint32_t interval_us){
    srp_vga_deadline_result_t out={0,0,false,false};
    if(interval_us==0)return out;
    bool expired=s->valid && (uint32_t)(now-s->last_entry_us)>250000u;
    bool too_late=s->valid &&
        (int32_t)(now-s->next_us)>(int32_t)(interval_us*4u);
    if(!s->valid || expired || too_late){
        uint32_t phase=(uint32_t)(now-epoch)%interval_us;
        s->next_us=now+(interval_us-phase);
        s->valid=true;
        out.reanchored=true;
    }
    uint32_t target=s->next_us;
    s->next_us=target+interval_us;
    s->last_entry_us=now;
    /* signed subtraction is valid for distances less than half the timer span */
    if((int32_t)(now-target)>=0){
        out.caught_up=true;
        out.late_us=(uint32_t)(now-target);
    }else{
        out.wait_us=(uint32_t)(target-now);
    }
    return out;
}
#endif
