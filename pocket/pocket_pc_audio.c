/* PC-speaker PIT and port 61h emulation. */
#include "pocket_pc_audio.h"
#include "of_audio.h"
#include <stdbool.h>
#include <string.h>

#define SR_PIT_HZ 1193182ull
#define SR_AUDIO_RATE 48000ull
#define SR_AUDIO_SYNTH_RATE 24000ull /* 24k PIT sampling, duplicate at 48k output */
#define SR_AUDIO_BUF_PAIRS 512u
#define SR_AUDIO_GAIN 10000
#define SR_AUDIO_MAX_QUEUE 7200 /* at most 150ms queued; preserve responsiveness */
#define SR_AUDIO_LEAD_PAIRS 2880 /* 60ms pre-roll to cover 40-50ms heavy-frame stalls */

static uint8_t port61;
static uint32_t pit2_latch=65536, pit2_count=65536;
static bool out2=true;
static uint64_t out2_next;
static uint64_t rendered, next_sample=SR_PIT_HZ, accum_hi;
static int32_t hp_prev_x,hp_prev_y,lp_prev;
static int16_t pcm[SR_AUDIO_BUF_PAIRS*2];
static unsigned pcm_count;
static int ring_capacity;
static bool synthesis_enabled=true;
static srp_audio_stats_t audio_stats;
static uint32_t audio_pending_min=UINT32_MAX;
 

#define SR_START_FADE_SAMPLES 120u
static unsigned startup_fade_left;
static bool startup_fade_used, first_gate_seen, first_data_seen;

static uint32_t half_period(uint32_t count, bool high){
    uint32_t h=high?(count+1u)/2u:count/2u;
    return h?h:1u;
}
static void flush_pcm(void){
    if(!pcm_count)return;
    /* Do not accumulate seconds of sound after a slowdown or SD I/O stall. */
    int free_pairs=of_audio_free();
    int pending=(ring_capacity>free_pairs)?ring_capacity-free_pairs:0;
    if(pending>=0){
        if((uint32_t)pending<audio_pending_min)audio_pending_min=(uint32_t)pending;
        if((uint32_t)pending>audio_stats.maximum_pending_pairs)
            audio_stats.maximum_pending_pairs=(uint32_t)pending;
    }
    if(free_pairs>=ring_capacity && ring_capacity>0)audio_stats.empty_checks++;
    if(free_pairs>=(int)pcm_count && pending<SR_AUDIO_MAX_QUEUE){
        (void)of_audio_write(pcm,(int)pcm_count);
        audio_stats.writes++;
    }else{
        audio_stats.dropped_buffers++;
    }
    pcm_count=0;
}
static void emit_sample(void){

    int32_t raw=(port61&2u)?
        (int32_t)((((int64_t)2*(int64_t)accum_hi-(int64_t)SR_PIT_HZ)
                    *SR_AUDIO_GAIN)/(int64_t)SR_PIT_HZ):0;
    int32_t hp=raw-hp_prev_x+(int32_t)(((int64_t)hp_prev_y*32673)>>15);
    hp_prev_x=raw;hp_prev_y=hp;
    lp_prev+=(int32_t)(((int64_t)(hp-lp_prev)*24913)>>15);
    int32_t v=lp_prev;
    if(startup_fade_left){
        unsigned elapsed=SR_START_FADE_SAMPLES-startup_fade_left+1u;
        v=(int32_t)((int64_t)v*elapsed/SR_START_FADE_SAMPLES);
        startup_fade_left--;
    }
    if(v>32767)v=32767;
    if(v< -32768)v=-32768;

    for(unsigned repeat=0;repeat<SR_AUDIO_RATE/SR_AUDIO_SYNTH_RATE;repeat++){
        pcm[pcm_count*2]=(int16_t)v;
        pcm[pcm_count*2+1]=(int16_t)v;
        if(++pcm_count==SR_AUDIO_BUF_PAIRS)flush_pcm();
    }
    accum_hi=0;
    next_sample+=SR_PIT_HZ;
}
void srp_audio_advance(uint64_t pit_clocks){
    uint64_t target=pit_clocks*SR_AUDIO_SYNTH_RATE;
    if(target<rendered)return;

    if(!synthesis_enabled){
        rendered=target;
        next_sample=(target/SR_PIT_HZ+1u)*SR_PIT_HZ;
        accum_hi=0;pcm_count=0;
        out2=true;
        out2_next=pit_clocks+half_period(pit2_latch,true);
        return;
    }
    while(rendered<target){
        bool gate=(port61&1u)!=0;
        uint64_t end=target<next_sample?target:next_sample;
        uint64_t transition=out2_next*SR_AUDIO_SYNTH_RATE;
        if(gate && transition<end)end=transition;
        /* Guard against zero-progress if a new gate transition equals rendered. */
        if(end<rendered)end=rendered;
        if((port61&2u) && (!gate||out2))accum_hi+=end-rendered;
        rendered=end;
        if(gate&&rendered==transition){
            out2=!out2;
            pit2_count=pit2_latch;
            out2_next+=half_period(pit2_count,out2);
        }
        if(rendered==next_sample)emit_sample();
    }
}
void srp_audio_flush(void){flush_pcm();}
void srp_audio_set_enabled(bool on){
    if(synthesis_enabled==on)return;
    synthesis_enabled=on;
    pcm_count=0;
    accum_hi=0;
    hp_prev_x=hp_prev_y=lp_prev=0;
}
void srp_audio_pit2(uint64_t at,uint16_t divisor){
    srp_audio_advance(at);
    pit2_latch=divisor?divisor:65536u;
    audio_stats.pit2_writes++;
}
void srp_audio_port61(uint64_t at,uint8_t value){
    srp_audio_advance(at);
    bool was=(port61&1u)!=0, gate=(value&1u)!=0;
    bool old_data=(port61&2u)!=0, new_data=(value&2u)!=0;
    port61=value;
    audio_stats.port61_writes++;
    if(gate&&!was){
        audio_stats.gate_starts++;
        if(!first_gate_seen){first_gate_seen=true;audio_stats.first_gate_ms=(uint32_t)(at*1000u/SR_PIT_HZ);}
    }
    if(new_data&&!old_data){
        audio_stats.data_enables++;
        if(!first_data_seen){
            first_data_seen=true;
            audio_stats.first_data_ms=(uint32_t)(at*1000u/SR_PIT_HZ);
        }
        if(!startup_fade_used){
            startup_fade_used=true;
            startup_fade_left=SR_START_FADE_SAMPLES;
            audio_stats.startup_fades++;
        }
    }
    if(gate&&!was){
        out2=true;
        pit2_count=pit2_latch;
        out2_next=at+half_period(pit2_count,true);
    }else if(!gate){
        out2=true;
    }
}
void srp_audio_init(void){
    of_audio_init();
    synthesis_enabled=true;
    port61=0;
    pit2_latch=pit2_count=65536;
    out2=true;out2_next=0;
    rendered=0;next_sample=SR_PIT_HZ;accum_hi=0;
    hp_prev_x=hp_prev_y=lp_prev=0;
    pcm_count=0;
    startup_fade_left=0;
    startup_fade_used=first_gate_seen=first_data_seen=false;
    memset(&audio_stats,0,sizeof(audio_stats));
    audio_pending_min=UINT32_MAX;
    ring_capacity=of_audio_free();
     

    static const int16_t silence[SR_AUDIO_LEAD_PAIRS*2]={0};
    unsigned room=ring_capacity>0?(unsigned)ring_capacity:0u;
    unsigned lead=(room>=SR_AUDIO_LEAD_PAIRS+SR_AUDIO_BUF_PAIRS)?
        SR_AUDIO_LEAD_PAIRS:(room>=960u+SR_AUDIO_BUF_PAIRS?960u:0u);
    if(lead)(void)of_audio_write(silence,(int)lead);
    audio_stats.initial_lead_pairs=lead;
}
void srp_audio_snapshot(srp_audio_stats_t *out){
    if(!out)return;
    *out=audio_stats;
    out->minimum_pending_pairs=(audio_pending_min==UINT32_MAX)?0u:audio_pending_min;
    audio_stats.writes=audio_stats.dropped_buffers=audio_stats.empty_checks=0;
    audio_stats.pit2_writes=audio_stats.port61_writes=0;
    audio_stats.gate_starts=audio_stats.data_enables=0;
    audio_stats.startup_fades=0;
    audio_stats.maximum_pending_pairs=0;
    audio_pending_min=UINT32_MAX;
}
