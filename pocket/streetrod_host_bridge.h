#ifndef STREETROD_POCKET_HOST_BRIDGE_H
#define STREETROD_POCKET_HOST_BRIDGE_H
/* Pointer input and DOS timer bridge for handheld controls. */
#include <stdint.h>

typedef struct {
    int16_t pointer_x, pointer_y;
    uint8_t mouse_down, click_pressed, escape_pressed;
    uint32_t ticks; /* DOS IRQ0/PIT-equivalent count since bridge started */
    uint64_t tick_remainder;
    uint32_t last_microseconds;
    uint8_t initialized;
} SrpHostBridge;

static void srp_bridge_init(SrpHostBridge *b, uint32_t now) {
    b->pointer_x=160;b->pointer_y=100;
    b->mouse_down=0;b->click_pressed=0;b->escape_pressed=0;
    b->ticks=0;b->tick_remainder=0;b->last_microseconds=now;b->initialized=1;
}
static void srp_bridge_tick(SrpHostBridge *b, uint32_t now) {
    if(!b->initialized){srp_bridge_init(b,now);return;}

    uint32_t elapsed=now-b->last_microseconds;
    b->last_microseconds=now;
    if(elapsed>100000u)elapsed=100000u; /* no runaway after platform sleep */
    b->tick_remainder+=(uint64_t)elapsed*728u;
    while(b->tick_remainder>=10000000ULL) {
        b->ticks++;
        b->tick_remainder-=10000000ULL;
    }
}
static void srp_bridge_inputs(SrpHostBridge *b, int left,int right,int up,int down,
                              int click_down,int click_pressed,int escape_pressed) {
    b->pointer_x+=(int16_t)((right?2:0)-(left?2:0));
    b->pointer_y+=(int16_t)((down?2:0)-(up?2:0));
    if(b->pointer_x<0)b->pointer_x=0;
    if(b->pointer_x>319)b->pointer_x=319;
    if(b->pointer_y<0)b->pointer_y=0;
    if(b->pointer_y>199)b->pointer_y=199;
    b->mouse_down=(uint8_t)!!click_down;
    b->click_pressed=(uint8_t)!!click_pressed;
    b->escape_pressed=(uint8_t)!!escape_pressed;
}
#endif
