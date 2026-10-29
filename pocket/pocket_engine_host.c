 


/* Expose the POSIX usleep declaration under the C11 compiler mode. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#include "of.h"
#include "of_timer.h"
#include "of_file.h"
#define SRP_TRACE_HOST_IMPL 1
#include "engine/host.h"
#include "engine/platform/ega.h"
#include "pocket_pc_audio.h"
#include "pocket_controls.h"
#include "srp_scanline_cache.h"
#include "srp_output_row_cache.h"
#include "srp_vga_deadline.h"
#include "srp_engine_profiler.h"
#include "srp_save_id.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>

#define SCREEN_W 320
#define SCREEN_H 200
#define FRAME_PIXELS (SCREEN_W * SCREEN_H)
#define VIDEO_INTERVAL_US 14268u  /* emulated VGA retrace: ~70.1 Hz */
#define PRESENT_INTERVAL_US 33333u  /* responsive menu input; only present when engine marks display dirty */

static void (*on_tick)(void);
static void (*on_key)(u8);
static void (*on_focus_lost)(void);
static bool (*compose)(u32 *);
/* Fast path supplied by the Pocket-specific EGA renderer; avoids RGB8888. */
static bool (*compose_indexed)(u8 *);
static bool (*pointer_query)(s16 *,s16 *);
static u32 rgb_frame[FRAME_PIXELS];
static u8 indexed_frame[FRAME_PIXELS] __attribute__((aligned(4)));
 

static srp_output_row_cache_t output_rows;
static uint64_t output_rows_copied,output_rows_reused,output_batched_runs;
static uint64_t output_source_rows_visited,output_copy_calls;
static uint32_t output_new_surfaces,output_replaced_surfaces;
 

static u32 previous_rgb_frame[FRAME_PIXELS];
static bool previous_rgb_valid;
static u8 rgb565_to_ega[65536]; /* fixed index: never recolor an already displayed buffer */
static bool retrace_bypass_diagnostic;
static bool retrace_chord_previous;
static u32 tick_anchor_us, frame_anchor_us, vga_epoch_us, now_us;
static uint64_t audio_tick_clock, audio_now_clock;
static u32 pit_div = PIT_DIV_GAME;
static uint64_t ticks_run;
static int mouse_x = 160, mouse_y = 100;
static int motion_dx, motion_dy, mickey_dx, mickey_dy;
static u8 mouse_buttons;
static u32 speaker_div = 0x10000u;
static u8 speaker_port;
static bool relative_mouse;
static bool realtime_mode=true;
static bool inside_tick;
static bool inside_pump;

static bool inside_retrace_wait;
static uint32_t retrace_boundary_scans, retrace_deferred_frames;
/* Prioritize engine retrace activity over periodic menu refresh. */
#define RETRACE_ACTIVE_GRACE_US 65000u
static bool retrace_seen;
static uint32_t last_retrace_entry_us;
 

static srp_vga_deadline_t vga_deadline;
static uint32_t retrace_catchups, retrace_reanchors, retrace_max_late_us;
static uint64_t retrace_catchup_late_us;
static uint32_t retrace_pace_skips, pump_pace_suppressed;

static bool inside_io_audio_hook;
static uint32_t io_audio_anchor_us, io_audio_last_service_us;
static uint64_t io_audio_anchor_clock;
static uint32_t io_idle_calls, io_audio_pumps;
static uint64_t io_audio_advanced_clocks;

static bool key_start,key_y,key_x;
static bool key_left,key_right,key_up,key_down;

#define INPUT_POLL_INTERVAL_US 4000u
static bool input_poll_throttled=true;
static bool input_chord_previous;
static bool input_poll_valid;
static uint32_t input_poll_anchor;
static bool audio_debug_chord_previous;
static bool audio_debug_enabled=true;
 

static bool profile_reporting_enabled=false;
static bool profile_chord_previous=false;
/* Restore cursor updates after long input polls. */
static srp_cursor_state cursor;
static bool keyboard_open,keyboard_dirty;
static bool keyboard_entry_active,keyboard_wait_field_click,keyboard_at_top,keyboard_numeric;
static bool keyboard_field_armed; /* prevent inherited A from auto-opening licence */
static int keyboard_field_x,keyboard_field_y,keyboard_field_w;
static const char *keyboard_text; /* live DOS name buffer, only valid during text_input */
static bool keyboard_suppress_until_release;
static unsigned keyboard_col,keyboard_row;
static srp_vk_nav_state vk_nav;
static bool vk_nav_wait_neutral;
static bool vk_prev_start,vk_prev_up,vk_prev_down,vk_prev_left,vk_prev_right;
static bool vk_prev_a,vk_prev_b,vk_prev_x,vk_prev_y;
static bool vk_prev_chord;


/* Passive timing counters for Developer Key profiling. */
typedef struct {
    uint64_t input_us, tick_us, compose_us, convert_us, flip_us, retrace_us;
    uint32_t pumps, ticks, composes, frame_flips, mouse_pixels;
    uint32_t max_pump_us, max_tick_backlog, catchup_capped, timer_resyncs;
    uint32_t retrace_calls;
    uint32_t dirty_scanlines, reused_scanlines, zero_pixel_change_frames;
    uint32_t ega_groups_decoded,ega_groups_reused;
    uint32_t input_poll_samples, input_poll_skips;
    uint32_t gap_max_us, gaps_over_20ms, gaps_over_100ms;
} srp_profile_t;
static srp_profile_t profile;
static uint32_t scanout_max_us;
static uint32_t micros(void);
static uint32_t profile_epoch_us, profile_seconds;

#define SRP_PROFILE_REPEAT_INTERVAL_US 4000000u
static uint32_t profile_last_emit_us;
static uint32_t profile_max_emit_us;
static uint32_t profile_live_last_us;
static uint64_t engine_host_total_us;
typedef struct {
    uint64_t elapsed_us, nonhost_us;
    uint32_t count, max_us, max_nonhost_us;
} srp_engine_counter_t;
static srp_engine_counter_t engine_counter[SRP_OP_COUNT];
srp_engine_scope_t srp_engine_scope_begin(unsigned id){
    srp_engine_scope_t r={micros(),engine_host_total_us,id};
    return r;
}
void srp_engine_scope_end(srp_engine_scope_t *scope){
    if(scope->id>=SRP_OP_COUNT)return;
    uint32_t delta=(uint32_t)(micros()-scope->start_us);
    uint64_t host=engine_host_total_us-scope->host_us;
    uint32_t nonhost=delta>(uint64_t)host?(uint32_t)(delta-host):0;
    srp_engine_counter_t *c=&engine_counter[scope->id];
    c->count++;
    c->elapsed_us+=delta;
    c->nonhost_us+=nonhost;
    if(delta>c->max_us)c->max_us=delta;
    if(nonhost>c->max_nonhost_us)c->max_nonhost_us=nonhost;
}
static uint64_t retrace_pump_total_us, retrace_idle_total_us, retrace_overrun_total_us;
static uint32_t retrace_overshoots;
static uint32_t last_pump_start_us;
static bool previous_pump_valid;
 

static uint32_t delay_api_calls, delay_api_inside_tick, delay_api_max_us;
static uint64_t delay_api_requested_us;
static uint32_t tick_direct_calls;
 
static const char *engine_pump_file, *engine_retrace_file;
static unsigned engine_pump_line, engine_retrace_line;
static const char *last_pump_file, *last_api_file;
static unsigned last_pump_line, last_api_line;
static const char *last_api_kind;
static uint32_t last_api_at_us;
static uint32_t gap_trace_max_us, gap_trace_count, gap_api_age_us;

static uint32_t ui_phase_start_us, ui_phase_max_us[4], ui_phase_count[4];
static unsigned ui_phase, ui_gap_phase;
static uint32_t ui_gap_phase_age_us;

static bool ui_scope_active, ui_gap_active;
static uint32_t ui_last_exit_us, ui_gap_since_exit_us, ui_gap_outside_ui_us;
static unsigned ui_last_exit_line, ui_gap_exit_line;
static uint32_t ui_scope_entries, ui_scope_exits;
void srp_trace_ui_enter(void){
    ui_scope_active=true;
    ui_phase=0;
    ui_phase_start_us=micros();
    ui_scope_entries++;
}
void srp_trace_ui_leave(unsigned line){
    ui_scope_active=false;
    ui_phase=0;
    ui_last_exit_us=micros();
    ui_last_exit_line=line;
    ui_scope_exits++;
}

void srp_trace_ui_phase(unsigned stage){
    if(stage<1 || stage>3)return;
    uint32_t now=micros();
    if(ui_phase>=1 && ui_phase<=2 && stage==ui_phase+1){
        uint32_t dt=(uint32_t)(now-ui_phase_start_us);
        if(dt>ui_phase_max_us[ui_phase])ui_phase_max_us[ui_phase]=dt;
        ui_phase_count[ui_phase]++;
    }
    ui_phase=stage;
    ui_phase_start_us=now;
}

#define SRP_STARTUP_STALL_US 750000u
static bool startup_stall_rebased;
static uint32_t startup_rebase_gap_us, startup_rebases;
static uint32_t last_pump_end_us;
static uint32_t outside_max_us, host_max_us, pit2_max_us, port61_max_us;
static uint32_t outside_count_100ms;
static const char *gap_trace_prev_file, *gap_trace_next_file, *gap_trace_api_file;
static const char *gap_trace_api_kind;
static unsigned gap_trace_prev_line, gap_trace_next_line, gap_trace_api_line;
 

typedef struct {
    uint32_t at_ms;
    uint32_t value;
    uint8_t kind;
} srp_boot_audio_event_t;
#define SRP_BOOT_AUDIO_EVENTS 16
static srp_boot_audio_event_t boot_audio_events[SRP_BOOT_AUDIO_EVENTS];
static unsigned boot_audio_count, boot_audio_printed;
static void trace_audio_event(unsigned kind, uint32_t value){
    if(boot_audio_count>=SRP_BOOT_AUDIO_EVENTS)return;
    srp_boot_audio_event_t *e=&boot_audio_events[boot_audio_count++];
    e->at_ms=(uint32_t)(audio_now_clock*1000ull/PIT_HZ);
    e->value=value;
    e->kind=(uint8_t)kind;
}
static void trace_api(const char *kind,const char *file,unsigned line){
    last_api_kind=kind;
    last_api_file=file;
    last_api_line=line;
    last_api_at_us=micros();
}
/* Kept outside engine source files; FILE and LINE identify game call sites. */
void host_pump_site(const char *file,unsigned line){
    engine_pump_file=file;
    engine_pump_line=line;
    host_pump();
    engine_pump_file=NULL;
    engine_pump_line=0;
}
void host_wait_vretrace_site(const char *file,unsigned line){
    trace_api("vretrace",file,line);
    engine_retrace_file=file;
    engine_retrace_line=line;
    host_wait_vretrace();
    engine_retrace_file=NULL;
    engine_retrace_line=0;
}
void host_pit2_divisor_site(const char *file,unsigned line,u16 div){
    trace_api("pit2",file,line);
    uint32_t t=micros();
    host_pit2_divisor(div);
    uint32_t dt=(uint32_t)(micros()-t);
    if(dt>pit2_max_us)pit2_max_us=dt;
}
void host_port61_site(const char *file,unsigned line,u8 value){
    trace_api("port61",file,line);
    uint32_t t=micros();
    host_port61(value);
    uint32_t dt=(uint32_t)(micros()-t);
    if(dt>port61_max_us)port61_max_us=dt;
}

static bool profile_live_emit(uint32_t now){
    if(!profile_reporting_enabled)return false;
    if((uint32_t)(now-profile_live_last_us)<SRP_PROFILE_REPEAT_INTERVAL_US)return false;
    profile_live_last_us=now;
    uint32_t span=(uint32_t)(now-profile_epoch_us);
    if(!span)return false;
    uint32_t hz10=(uint32_t)((uint64_t)profile.ticks*10000000ull/span);
    printf("[SRP-LIVE] sec=%lu tickHz=%lu.%lu ticks=%lu pumps=%lu gaps100=%lu gapMaxUs=%lu animMs=%lu retraceCalls=%lu\n",
        (unsigned long)(span/1000000u),
        (unsigned long)(hz10/10u),(unsigned long)(hz10%10u),
        (unsigned long)profile.ticks,(unsigned long)profile.pumps,
        (unsigned long)profile.gaps_over_100ms,
        (unsigned long)profile.gap_max_us,
        (unsigned long)(engine_counter[SRP_OP_CAR_ANIM].nonhost_us/1000ull),
        (unsigned long)profile.retrace_calls);
    return true;
}
 

static bool profile_emit(uint32_t now) {
    if(!profile_reporting_enabled)return false;
    uint32_t elapsed=(uint32_t)(now-profile_epoch_us);
    uint32_t span_ms=elapsed/1000u;
    uint32_t hz10=(uint32_t)((uint64_t)profile.ticks*10000000ull/elapsed);
    printf("[SRP-PROF] sec=%lu spanMs=%lu tickHz=%lu.%lu ticks=%lu pumps=%lu moves=%lu composes=%lu flips=%lu mode=%s dirtyRows=%lu reusedRows=%lu identical=%lu backlog=%lu cap=%lu resync=%lu inputSamples=%lu inputSkips=%lu gapMax=%luus gaps20=%lu gaps100=%lu\n",
        (unsigned long)(profile_seconds+=elapsed/1000000u),(unsigned long)span_ms,(unsigned long)(hz10/10),(unsigned long)(hz10%10),
        (unsigned long)profile.ticks,(unsigned long)profile.pumps,
        (unsigned long)profile.mouse_pixels,(unsigned long)profile.composes,
        (unsigned long)profile.frame_flips,
        compose_indexed?"idx":"rgb",
        (unsigned long)profile.dirty_scanlines,(unsigned long)profile.reused_scanlines,
        (unsigned long)profile.zero_pixel_change_frames,(unsigned long)profile.max_tick_backlog,
        (unsigned long)profile.catchup_capped,(unsigned long)profile.timer_resyncs,
        (unsigned long)profile.input_poll_samples,(unsigned long)profile.input_poll_skips,
        (unsigned long)profile.gap_max_us,(unsigned long)profile.gaps_over_20ms,
        (unsigned long)profile.gaps_over_100ms);
    printf("[SRP-COST] input=%lums tick=%lums compose=%lums convert=%lums flip=%lums retrace=%lums maxPump=%luus retraceCalls=%lu\n",
        (unsigned long)(profile.input_us/1000u),
        (unsigned long)(profile.tick_us/1000u),
        (unsigned long)(profile.compose_us/1000u),
        (unsigned long)(profile.convert_us/1000u),
        (unsigned long)(profile.flip_us/1000u),
        (unsigned long)(profile.retrace_us/1000u),
        (unsigned long)profile.max_pump_us,(unsigned long)profile.retrace_calls);
    srp_audio_stats_t audio;
    srp_audio_snapshot(&audio);
    printf("[SRP-AUDIO] writes=%lu drops=%lu emptyChecks=%lu pendingMin=%lu pendingMax=%lu preroll=%lu\n",
        (unsigned long)audio.writes,(unsigned long)audio.dropped_buffers,
        (unsigned long)audio.empty_checks,(unsigned long)audio.minimum_pending_pairs,
        (unsigned long)audio.maximum_pending_pairs,(unsigned long)audio.initial_lead_pairs);
    printf("[SRP-SOUND] pit2=%lu port61=%lu gateStarts=%lu dataEnables=%lu firstGateMs=%lu firstDataMs=%lu firstFade=%lu\n",
        (unsigned long)audio.pit2_writes,(unsigned long)audio.port61_writes,
        (unsigned long)audio.gate_starts,(unsigned long)audio.data_enables,
        (unsigned long)audio.first_gate_ms,(unsigned long)audio.first_data_ms,
        (unsigned long)audio.startup_fades);
    printf("[SRP-CURSOR] recoveredLongPolls=%lu mouseX=%d mouseY=%d\n",
        (unsigned long)cursor.recovered_stall_polls,mouse_x,mouse_y);
    cursor.recovered_stall_polls=0;
    printf("[SRP-WAITS] calls=%lu inTick=%lu requestedMs=%lu maxRequestUs=%lu directTicks=%lu\n",
        (unsigned long)delay_api_calls,(unsigned long)delay_api_inside_tick,
        (unsigned long)(delay_api_requested_us/1000ull),
        (unsigned long)delay_api_max_us,(unsigned long)tick_direct_calls);
    printf("[SRP-STARTUP] rebases=%lu largestInitGap=%luus\n",
        (unsigned long)startup_rebases,(unsigned long)startup_rebase_gap_us);
    printf("[SRP-BLOCK] outsideMax=%luus outside100=%lu hostMax=%luus pit2Max=%luus port61Max=%luus\n",
        (unsigned long)outside_max_us,(unsigned long)outside_count_100ms,
        (unsigned long)host_max_us,(unsigned long)pit2_max_us,
        (unsigned long)port61_max_us);
    outside_max_us=host_max_us=pit2_max_us=port61_max_us=outside_count_100ms=0;
    printf("[SRP-GAP] count=%lu maxUs=%lu from=%s:%u to=%s:%u lastAPI=%s@%s:%u apiAgeUs=%lu uiStage=%lu uiAgeUs=%lu uiActive=%lu uiExitLine=%lu sinceExitUs=%lu outsideUiUs=%lu\n",
        (unsigned long)gap_trace_count,(unsigned long)gap_trace_max_us,
        gap_trace_prev_file?gap_trace_prev_file:"host",gap_trace_prev_line,
        gap_trace_next_file?gap_trace_next_file:"host",gap_trace_next_line,
        gap_trace_api_kind?gap_trace_api_kind:"none",
        gap_trace_api_file?gap_trace_api_file:"host",gap_trace_api_line,
        (unsigned long)gap_api_age_us,(unsigned long)ui_gap_phase,(unsigned long)ui_gap_phase_age_us,
        (unsigned long)ui_gap_active,(unsigned long)ui_gap_exit_line,
        (unsigned long)ui_gap_since_exit_us,(unsigned long)ui_gap_outside_ui_us);
    printf("[SRP-UI-SCOPE] enters=%lu exits=%lu active=%lu lastExitLine=%lu\n",
        (unsigned long)ui_scope_entries,(unsigned long)ui_scope_exits,
        (unsigned long)ui_scope_active,(unsigned long)ui_last_exit_line);
    ui_scope_entries=ui_scope_exits=0;
    printf("[SRP-UI-PHASE] beforeKeyMax=%luus beforeKeySamples=%lu afterKeyMax=%luus afterKeySamples=%lu\n",
        (unsigned long)ui_phase_max_us[1],(unsigned long)ui_phase_count[1],
        (unsigned long)ui_phase_max_us[2],(unsigned long)ui_phase_count[2]);
    ui_phase_max_us[1]=ui_phase_max_us[2]=ui_phase_count[1]=ui_phase_count[2]=0;
    printf("[SRP-EGA] blitCalls=%lu blitMs=%lu blitNonHostMs=%lu blitMaxUs=%lu blitMaxNonHostUs=%lu blindsCalls=%lu blindsMs=%lu blindsNonHostMs=%lu blindsMaxUs=%lu blindsMaxNonHostUs=%lu\n",
        (unsigned long)engine_counter[SRP_OP_EGA_BLIT].count,
        (unsigned long)(engine_counter[SRP_OP_EGA_BLIT].elapsed_us/1000ull),
        (unsigned long)(engine_counter[SRP_OP_EGA_BLIT].nonhost_us/1000ull),
        (unsigned long)engine_counter[SRP_OP_EGA_BLIT].max_us,
        (unsigned long)engine_counter[SRP_OP_EGA_BLIT].max_nonhost_us,
        (unsigned long)engine_counter[SRP_OP_EGA_BLINDS].count,
        (unsigned long)(engine_counter[SRP_OP_EGA_BLINDS].elapsed_us/1000ull),
        (unsigned long)(engine_counter[SRP_OP_EGA_BLINDS].nonhost_us/1000ull),
        (unsigned long)engine_counter[SRP_OP_EGA_BLINDS].max_us,
        (unsigned long)engine_counter[SRP_OP_EGA_BLINDS].max_nonhost_us);
    printf("[SRP-GARAGE] bgCalls=%lu bgMs=%lu bgMaxUs=%lu carCalls=%lu carMs=%lu carMaxUs=%lu\n",
        (unsigned long)engine_counter[SRP_OP_GARAGE_BG].count,
        (unsigned long)(engine_counter[SRP_OP_GARAGE_BG].nonhost_us/1000ull),
        (unsigned long)engine_counter[SRP_OP_GARAGE_BG].max_nonhost_us,
        (unsigned long)engine_counter[SRP_OP_GARAGE_CAR].count,
        (unsigned long)(engine_counter[SRP_OP_GARAGE_CAR].nonhost_us/1000ull),
        (unsigned long)engine_counter[SRP_OP_GARAGE_CAR].max_nonhost_us);
    /* Stage-2 garage drilldown. Decode is nested within composition; totals overlap. */
    printf("[SRP-CARPHASE] composeCalls=%lu composeMs=%lu composeMaxUs=%lu animCalls=%lu animMs=%lu animMaxUs=%lu picDecodeCalls=%lu picDecodeMs=%lu picDecodeMaxUs=%lu\n",
        (unsigned long)engine_counter[SRP_OP_CAR_COMPOSE].count,
        (unsigned long)(engine_counter[SRP_OP_CAR_COMPOSE].nonhost_us/1000ull),
        (unsigned long)engine_counter[SRP_OP_CAR_COMPOSE].max_nonhost_us,
        (unsigned long)engine_counter[SRP_OP_CAR_ANIM].count,
        (unsigned long)(engine_counter[SRP_OP_CAR_ANIM].nonhost_us/1000ull),
        (unsigned long)engine_counter[SRP_OP_CAR_ANIM].max_nonhost_us,
        (unsigned long)engine_counter[SRP_OP_PIC_DECODE].count,
        (unsigned long)(engine_counter[SRP_OP_PIC_DECODE].nonhost_us/1000ull),
        (unsigned long)engine_counter[SRP_OP_PIC_DECODE].max_nonhost_us);

    /* Reset after ALL engine metrics have been printed, not after EGA alone. */
    memset(engine_counter,0,sizeof(engine_counter));
    printf("[SRP-RETRACE] pumpMs=%lu idleMs=%lu overshootMs=%lu overruns=%lu\n",
        (unsigned long)(retrace_pump_total_us/1000ull),
        (unsigned long)(retrace_idle_total_us/1000ull),
        (unsigned long)(retrace_overrun_total_us/1000ull),
        (unsigned long)retrace_overshoots);
    retrace_pump_total_us=retrace_idle_total_us=retrace_overrun_total_us=0;
    retrace_overshoots=0;
    printf("[SRP-LOGGER] previousEmitUs=%lu maxEmitUs=%lu intervalMs=%lu\n",
        (unsigned long)profile_last_emit_us,(unsigned long)profile_max_emit_us,
        (unsigned long)span_ms);
    gap_trace_max_us=gap_trace_count=gap_api_age_us=0;
    gap_trace_prev_file=gap_trace_next_file=gap_trace_api_file=NULL;
    gap_trace_api_kind=NULL;
     

    while(boot_audio_printed<boot_audio_count){
        const srp_boot_audio_event_t *e=&boot_audio_events[boot_audio_printed++];
        printf("[SRP-BOOT-AUDIO] ms=%lu op=%s value=%lu\n",
            (unsigned long)e->at_ms,e->kind==1?"pit2":"port61",
            (unsigned long)e->value);
    }
    delay_api_calls=delay_api_inside_tick=delay_api_max_us=tick_direct_calls=0;
    delay_api_requested_us=0;
    printf("[SRP-IO] idleCalls=%lu audioPumps=%lu advancedMs=%lu\n",
        (unsigned long)io_idle_calls,(unsigned long)io_audio_pumps,
        (unsigned long)(io_audio_advanced_clocks*1000ull/PIT_HZ));
    io_idle_calls=io_audio_pumps=0;
    io_audio_advanced_clocks=0;
    memset(&profile,0,sizeof(profile));
    scanout_max_us=0;
    profile_epoch_us=now;
    return true;
}

static int initialized;

static uint32_t micros(void) { return of_time_us(); }
static int clampi(int x,int lo,int hi){return x<lo?lo:x>hi?hi:x;}
static void audio_during_file_io(void){
    if(!initialized || inside_pump || inside_tick || inside_io_audio_hook)return;
    io_idle_calls++;
    uint32_t now=micros();
     

    if((uint32_t)(now-io_audio_last_service_us)<5000u)return;
    inside_io_audio_hook=true;
    io_audio_last_service_us=now;
    uint64_t wall_clock=io_audio_anchor_clock+
        (uint64_t)(uint32_t)(now-io_audio_anchor_us)*PIT_HZ/1000000ull;
    if(wall_clock>audio_now_clock){
        uint64_t old=audio_now_clock;
        audio_now_clock=wall_clock;
        srp_audio_advance(wall_clock);
        srp_audio_flush();
        io_audio_advanced_clocks+=wall_clock-old;
        io_audio_pumps++;
    }
    inside_io_audio_hook=false;
}

static void install_fixed_palette(void){
    memset(rgb565_to_ega,0,sizeof(rgb565_to_ega));
    for(unsigned i=0;i<16;i++){
        rgb565_to_ega[srp_rgb565_key(ega_palette[i])]=(u8)i;
        of_video_palette(i,ega_palette[i]);
    }
}

static void present(void){
    uint32_t compose_start=micros();

    bool changed=compose_indexed?compose_indexed(indexed_frame):
        (compose && compose(rgb_frame));
    uint32_t scanout_us=(uint32_t)(micros()-compose_start);
    profile.compose_us+=scanout_us;
    if(scanout_us>scanout_max_us)scanout_max_us=scanout_us;
    profile.composes++;
     

    if(!changed && !keyboard_dirty)return;
    uint32_t convert_start=micros();
    if(changed && !compose_indexed){

        srp_row_stats_t rows=srp_reindex_changed_rows(
            rgb_frame,previous_rgb_frame,indexed_frame,rgb565_to_ega,
            previous_rgb_valid);
        previous_rgb_valid=true;
        profile.dirty_scanlines+=rows.dirty_rows;
        profile.reused_scanlines+=rows.reused_rows;
        if(rows.dirty_rows==0){
            profile.zero_pixel_change_frames++;
            if(!keyboard_dirty){
                profile.convert_us+=(uint32_t)(micros()-convert_start);
                return;
            }
        }
    }
    u8 *fb=of_video_surface();
    if(!fb)return;

    bool partial_copy=compose_indexed && !keyboard_open && !keyboard_dirty;
    srp_output_copy_stats_t rows_copied=srp_output_cache_copy(
        &output_rows,fb,indexed_frame,partial_copy);
    output_rows_copied+=rows_copied.copied_rows;
    output_rows_reused+=rows_copied.reused_rows;
    output_batched_runs+=rows_copied.batched_runs;
    output_source_rows_visited+=rows_copied.visited_source_rows;
    output_copy_calls++;
    output_new_surfaces+=rows_copied.new_surface;
    output_replaced_surfaces+=rows_copied.replaced_surface;
    if(!partial_copy)srp_output_cache_invalidate(&output_rows);
    if(keyboard_open)srp_draw_keyboard_mode(fb,(int)keyboard_col,(int)keyboard_row,keyboard_text,keyboard_at_top,keyboard_numeric);
    keyboard_dirty=false;
    profile.convert_us+=(uint32_t)(micros()-convert_start);
    /* No palette updates: the same sixteen indices persist between flips. */
    uint32_t flip_start=micros();
    of_video_flip();
    profile.flip_us+=(uint32_t)(micros()-flip_start);
    profile.frame_flips++;
}
static void send_key(bool pressed,u8 sc){if(on_key)on_key((u8)(pressed?sc:(sc|0x80u)));}
static void translate_key(bool down,bool *previous,u8 sc){
    if(down!=*previous){send_key(down,sc);*previous=down;}
}

static void vk_send(char c){
    u8 sc=srp_vk_scancode(c);
    if(!sc)return;
    /* The original DOS line editor accepts uppercase A-Z only. */
    bool shift=c>='A' && c<='Z';
    if(shift)send_key(true,0x2a);
    send_key(true,sc);send_key(false,sc);
    if(shift)send_key(false,0x2a);
}
 

static void vk_freeze_hand(void){
    mouse_buttons=0;
    motion_dx=motion_dy=mickey_dx=mickey_dy=0;
    int unusedx,unusedy;
    srp_cursor_step(&cursor,micros(),0,0,false,&unusedx,&unusedy);
}
static void vk_close(void){
    keyboard_open=false;keyboard_dirty=true;
    keyboard_suppress_until_release=true;
    srp_vk_nav_reset(&vk_nav);vk_nav_wait_neutral=true;
    vk_freeze_hand();
    /* Make sure no held regular keyboard key leaks from the overlay. */
    translate_key(false,&key_start,0x1c);
    translate_key(false,&key_x,0x39);
    translate_key(false,&key_y,0x01);
}

void host_text_entry_begin(const char *buf,int maxlen,int x,int y,int auto_open,int numeric){
    keyboard_text=buf;
    keyboard_numeric=(numeric!=0);
    keyboard_entry_active=true;
    keyboard_wait_field_click=(auto_open==0);
    keyboard_field_armed=!of_btn(OF_BTN_A);
    /* The DOS save-name field sits below the image halfway point. */
    keyboard_at_top=keyboard_numeric || (y>=130); /* Offer keypad docks above price dialog. */
    keyboard_field_x=x;
    keyboard_field_y=y;
    keyboard_field_w=maxlen*6;
    keyboard_open=(auto_open!=0);
    keyboard_dirty=true;
    srp_vk_nav_reset(&vk_nav);
    vk_nav_wait_neutral=of_btn(OF_BTN_UP)||of_btn(OF_BTN_DOWN)||
        of_btn(OF_BTN_LEFT)||of_btn(OF_BTN_RIGHT);
    if(keyboard_open)vk_freeze_hand();
    keyboard_col=keyboard_row=0;
    /* The A press which clicked the name field must NOT type an extra A. */
    vk_prev_a=of_btn(OF_BTN_A);
    vk_prev_b=of_btn(OF_BTN_B);
    vk_prev_x=of_btn(OF_BTN_X);
    vk_prev_y=of_btn(OF_BTN_Y);
    vk_prev_start=of_btn(OF_BTN_START);
    vk_prev_chord=of_btn(OF_BTN_SELECT)&&of_btn(OF_BTN_START);
}
void host_text_entry_end(void){
    keyboard_text=NULL; /* editor buffer can be a stack allocation */
    keyboard_entry_active=false;
    keyboard_wait_field_click=false;
    keyboard_numeric=false;
    keyboard_field_armed=false;
    vk_close();
}
static void vk_poll(bool select,bool start,bool up,bool down,bool left,bool right){
    bool a=of_btn(OF_BTN_A),b=of_btn(OF_BTN_B);
    bool x=of_btn(OF_BTN_X),y=of_btn(OF_BTN_Y);
    bool chord=select&&start;
    bool opened_by_click=false;

    if(!a && keyboard_entry_active)keyboard_field_armed=true;

    int left_edge=keyboard_field_x-(keyboard_numeric?36:48);
    int right_edge=keyboard_field_x+
        (keyboard_numeric && keyboard_field_w>60?60:keyboard_field_w)+
        (keyboard_numeric?24:55);
    int top_edge=keyboard_field_y-(keyboard_numeric?14:22);
    int bottom_edge=keyboard_field_y+(keyboard_numeric?18:29);
    if(left_edge<0)left_edge=0;
    if(right_edge>320)right_edge=320;
    if(top_edge<0)top_edge=0;
    if(bottom_edge>200)bottom_edge=200;

    s16 visual_x=(s16)mouse_x, visual_y=(s16)mouse_y;
    if(pointer_query){
        s16 gx=visual_x,gy=visual_y;
        if(pointer_query(&gx,&gy)){
            visual_x=gx;
            visual_y=gy;
        }
    }
    if(keyboard_entry_active && keyboard_wait_field_click && !keyboard_open &&
       a && keyboard_field_armed &&
       visual_x>=left_edge && visual_x<right_edge &&
       visual_y>=top_edge && visual_y<bottom_edge){
        keyboard_open=true;
        keyboard_dirty=true;
        keyboard_col=keyboard_row=0;
        srp_vk_nav_reset(&vk_nav);
        vk_nav_wait_neutral=up||down||left||right;
        vk_freeze_hand();
        keyboard_field_armed=false;
        opened_by_click=true;
    }
    if(chord && !vk_prev_chord){
        if(keyboard_open)vk_close();
        else {
            keyboard_open=true;keyboard_dirty=true;keyboard_col=keyboard_row=0;
            srp_vk_nav_reset(&vk_nav);
            vk_nav_wait_neutral=up||down||left||right;
            vk_freeze_hand();
        }
    }
    vk_prev_chord=chord;
    if(keyboard_open && !chord && !opened_by_click){

        if(vk_nav_wait_neutral){
            if(!left&&!right&&!up&&!down)vk_nav_wait_neutral=false;
            srp_vk_nav_reset(&vk_nav);
        }else if(srp_vk_nav_step_grid(&vk_nav,micros(),up,down,left,right,
                                  &keyboard_col,&keyboard_row,keyboard_numeric?3u:10u,4u))keyboard_dirty=true;
        if(a&&!vk_prev_a){
            char ch=keyboard_numeric?srp_vk_numeric_grid[keyboard_row][keyboard_col]:srp_vk_grid[keyboard_row][keyboard_col];
            vk_send(ch);
            if(ch=='>')vk_close();
            keyboard_dirty=true;
        }
        if(x&&!vk_prev_x){vk_send('<');keyboard_dirty=true;}
        if(start&&!vk_prev_start){vk_send('>');vk_close();}
        if(b&&!vk_prev_b){
            if(keyboard_numeric){send_key(true,0x01);send_key(false,0x01);}
            vk_close();
        }
    }
    vk_prev_start=start;vk_prev_left=left;vk_prev_right=right;
    vk_prev_up=up;vk_prev_down=down;
    vk_prev_a=a;vk_prev_b=b;vk_prev_x=x;vk_prev_y=y;
}
static void poll_buttons(void){
    of_input_poll();
    bool select=of_btn(OF_BTN_SELECT);
    bool start=of_btn(OF_BTN_START);
    bool input_chord=select && of_btn(OF_BTN_L1);
    if(input_chord && !input_chord_previous){
        input_poll_throttled=!input_poll_throttled;
        input_poll_valid=false; /* sample immediately after changing modes */
        printf("Input sampling: %s\n",input_poll_throttled?"250 Hz":"unthrottled (test)");
    }
    input_chord_previous=input_chord;
    bool profile_chord=select && of_btn(OF_BTN_R1);
    if(profile_chord && !profile_chord_previous){
        if(profile_reporting_enabled){
             

            uint32_t report_start=micros();
            (void)profile_emit(report_start);
            uint32_t report_us=(uint32_t)(micros()-report_start);
            profile_reporting_enabled=false;
            printf("[SRP-FULL] reportUs=%lu (after capture, not a gameplay gap)\n",
                (unsigned long)report_us);
            printf("[SRP-PROFILE] reports=OFF\n");
        }else{
         

        srp_audio_stats_t ab_audio;
        srp_audio_snapshot(&ab_audio);
        printf("[SRP-AB] phase=%s spanMs=%lu drops=%lu writes=%lu gaps100=%lu gapMaxUs=%lu blitMs=%lu retraceMs=%lu garageBgMs=%lu garageCarMs=%lu\n",
            profile_reporting_enabled?"logging":"quiet",
            (unsigned long)((uint32_t)(micros()-profile_epoch_us)/1000u),
            (unsigned long)ab_audio.dropped_buffers,(unsigned long)ab_audio.writes,
            (unsigned long)profile.gaps_over_100ms,(unsigned long)profile.gap_max_us,
            (unsigned long)(engine_counter[SRP_OP_EGA_BLIT].nonhost_us/1000ull),
            (unsigned long)(profile.retrace_us/1000ull),
            (unsigned long)(engine_counter[SRP_OP_GARAGE_BG].nonhost_us/1000ull),
            (unsigned long)(engine_counter[SRP_OP_GARAGE_CAR].nonhost_us/1000ull));
        printf("[SRP-AB-CAR] composeCalls=%lu composeMs=%lu composeMaxUs=%lu animCalls=%lu animMs=%lu animMaxUs=%lu picDecodeCalls=%lu picDecodeMs=%lu picDecodeMaxUs=%lu scanoutMs=%lu retraceCalls=%lu retraceOverruns=%lu\n",
            (unsigned long)engine_counter[SRP_OP_CAR_COMPOSE].count,
            (unsigned long)(engine_counter[SRP_OP_CAR_COMPOSE].nonhost_us/1000ull),
            (unsigned long)engine_counter[SRP_OP_CAR_COMPOSE].max_nonhost_us,
            (unsigned long)engine_counter[SRP_OP_CAR_ANIM].count,
            (unsigned long)(engine_counter[SRP_OP_CAR_ANIM].nonhost_us/1000ull),
            (unsigned long)engine_counter[SRP_OP_CAR_ANIM].max_nonhost_us,
            (unsigned long)engine_counter[SRP_OP_PIC_DECODE].count,
            (unsigned long)(engine_counter[SRP_OP_PIC_DECODE].nonhost_us/1000ull),
            (unsigned long)engine_counter[SRP_OP_PIC_DECODE].max_nonhost_us,
            (unsigned long)(profile.compose_us/1000ull),
            (unsigned long)profile.retrace_calls,
            (unsigned long)retrace_overshoots);
        printf("[SRP-AB-VIDEO] scans=%lu flips=%lu maxScanUs=%lu dirtyRows=%lu reusedRows=%lu retracePumpMs=%lu retraceIdleMs=%lu retraceOvershootMs=%lu decodedGroups=%lu reusedGroups=%lu\n",
            (unsigned long)profile.composes,(unsigned long)profile.frame_flips,
            (unsigned long)scanout_max_us,
            (unsigned long)profile.dirty_scanlines,(unsigned long)profile.reused_scanlines,
            (unsigned long)(retrace_pump_total_us/1000ull),
            (unsigned long)(retrace_idle_total_us/1000ull),
            (unsigned long)(retrace_overrun_total_us/1000ull),
            (unsigned long)profile.ega_groups_decoded,
            (unsigned long)profile.ega_groups_reused);
        printf("[SRP-AB-RETRACE] boundaryScans=%lu deferredFrames=%lu waits=%lu overruns=%lu\n",
            (unsigned long)retrace_boundary_scans,
            (unsigned long)retrace_deferred_frames,
            (unsigned long)profile.retrace_calls,
            (unsigned long)retrace_overshoots);
        printf("[SRP-AB-PACE] retracePresented=%lu retraceSkipped=%lu pumpSuppressed=%lu frameBudgetUs=%lu\n",
            (unsigned long)retrace_boundary_scans,
            (unsigned long)retrace_pace_skips,
            (unsigned long)pump_pace_suppressed,
            (unsigned long)PRESENT_INTERVAL_US);
        printf("[SRP-AB-VGA] caughtUp=%lu reanchors=%lu lateMs=%llu maxLateUs=%lu\n",
            (unsigned long)retrace_catchups,
            (unsigned long)retrace_reanchors,
            (unsigned long long)(retrace_catchup_late_us/1000ull),
            (unsigned long)retrace_max_late_us);
        printf("[SRP-AB-COPY] rowsCopied=%llu rowsReused=%llu rowsTotal=%llu bufferCount=%lu surfaceMisses=%lu surfaceEvictions=%lu sourceRowsVisited=%llu sourceRowsPotential=%llu batchedRuns=%llu\n",
            (unsigned long long)output_rows_copied,
            (unsigned long long)output_rows_reused,
            (unsigned long long)(output_rows_copied+output_rows_reused),
            (unsigned long)srp_output_cache_surface_count(&output_rows),
            (unsigned long)output_new_surfaces,(unsigned long)output_replaced_surfaces,
            (unsigned long long)output_source_rows_visited,
            (unsigned long long)(output_copy_calls*SRP_OUTPUT_SOURCE_ROWS),
            (unsigned long long)output_batched_runs);
         

        output_rows_copied=output_rows_reused=output_batched_runs=0;
        output_source_rows_visited=output_copy_calls=0;
        output_new_surfaces=output_replaced_surfaces=0;
        profile_reporting_enabled=!profile_reporting_enabled;
        /* Start a fresh, comparable capture interval on every enable. */
        memset(&profile,0,sizeof(profile));
        scanout_max_us=0;
        memset(engine_counter,0,sizeof(engine_counter));
        retrace_pump_total_us=retrace_idle_total_us=retrace_overrun_total_us=0;
        retrace_overshoots=0;
        retrace_boundary_scans=retrace_deferred_frames=0;
        retrace_pace_skips=pump_pace_suppressed=0;
        retrace_catchups=retrace_reanchors=retrace_max_late_us=0;
        retrace_catchup_late_us=0;
        profile_epoch_us=micros();profile_seconds=0;
        profile_last_emit_us=profile_max_emit_us=0;
        profile_live_last_us=profile_epoch_us;
        gap_trace_count=gap_trace_max_us=0;
        printf("[SRP-PROFILE] reports=%s\n",profile_reporting_enabled?"ON":"OFF");
        }
    }
    profile_chord_previous=profile_chord;
    bool l=of_btn(OF_BTN_LEFT),r=of_btn(OF_BTN_RIGHT);
    bool u=of_btn(OF_BTN_UP),d=of_btn(OF_BTN_DOWN);

    bool overlay_consumed=keyboard_open || (select&&start);
    vk_poll(select,start,u,d,l,r);
     

    if(keyboard_suppress_until_release){
        if(of_btn(OF_BTN_A)||of_btn(OF_BTN_B)||of_btn(OF_BTN_Y)||start||u||d||l||r){
            vk_freeze_hand();
            translate_key(false,&key_start,0x1c);
            translate_key(false,&key_x,0x39);
            translate_key(false,&key_y,0x01);
            return;
        }
        keyboard_suppress_until_release=false;
    }
    if(overlay_consumed || keyboard_open){
        vk_freeze_hand();
        translate_key(false,&key_start,0x1c);
        translate_key(false,&key_x,0x39);
        translate_key(false,&key_y,0x01);
        translate_key(false,&key_left,0x4b);
        translate_key(false,&key_right,0x4d);
        translate_key(false,&key_up,0x48);
        translate_key(false,&key_down,0x50);
        return;
    }
     

    bool audio_debug_chord=select && of_btn(OF_BTN_Y);
    bool retrace_chord=select && of_btn(OF_BTN_X);
    if(retrace_chord && !retrace_chord_previous){
        retrace_bypass_diagnostic=!retrace_bypass_diagnostic;
        printf("VGA retrace wait: %s\n",retrace_bypass_diagnostic?"BYPASSED (test only)":"NORMAL");
    }
    retrace_chord_previous=retrace_chord;
    if(audio_debug_chord && !audio_debug_chord_previous){
        audio_debug_enabled=!audio_debug_enabled;
        srp_audio_set_enabled(audio_debug_enabled);
        printf("PC speaker CPU synthesis: %s\n",audio_debug_enabled?"ON":"OFF (diagnostic)");
    }
    audio_debug_chord_previous=audio_debug_chord;
     

    int dx=0,dy=0;
    srp_cursor_step(&cursor,micros(),(int)r-(int)l,(int)d-(int)u,
                    select && !start && !of_btn(OF_BTN_Y) && !of_btn(OF_BTN_X),&dx,&dy);
    int nx=clampi(mouse_x+dx,0,319),ny=clampi(mouse_y+dy,0,199);
    dx=nx-mouse_x;dy=ny-mouse_y;
    mouse_x=nx;mouse_y=ny;
    profile.mouse_pixels+=(uint32_t)(dx<0?-dx:dx)+(uint32_t)(dy<0?-dy:dy);
    motion_dx+=dx;motion_dy+=dy;
    mickey_dx+=dx*2;mickey_dy+=dy*2;
    mouse_buttons=(of_btn(OF_BTN_A)?1u:0u)|(of_btn(OF_BTN_Y)&&!select?2u:0u);
    translate_key(start && !select,&key_start,0x1c); /* Enter, except overlay shortcut */
    translate_key(of_btn(OF_BTN_B) && !select,&key_y,0x01); /* B = Esc (Y = right-click) */
    translate_key(of_btn(OF_BTN_X) && !select,&key_x,0x39);  
    translate_key(relative_mouse && l,&key_left,0x4b);
    translate_key(relative_mouse && r,&key_right,0x4d);
    translate_key(relative_mouse && u,&key_up,0x48);
    translate_key(relative_mouse && d,&key_down,0x50);
}
bool host_init(const char *game_dir,int window_scale,bool fullscreen){
    (void)game_dir;(void)window_scale;(void)fullscreen;
    of_video_init();
    install_fixed_palette();
    previous_rgb_valid=false;
    compose_indexed=NULL;
    srp_output_cache_reset(&output_rows);
    output_rows_copied=output_rows_reused=output_batched_runs=0;
    output_source_rows_visited=output_copy_calls=0;
    output_new_surfaces=output_replaced_surfaces=0;
    memset(indexed_frame,0,sizeof(indexed_frame));
    srp_audio_init();
    initialized=1;
    memset(&cursor,0,sizeof(cursor));
    keyboard_open=keyboard_dirty=false;
    keyboard_entry_active=keyboard_wait_field_click=keyboard_at_top=keyboard_numeric=false;
    keyboard_field_armed=false;
    keyboard_field_x=keyboard_field_y=keyboard_field_w=0;
    keyboard_text=NULL;keyboard_suppress_until_release=false;
    keyboard_col=keyboard_row=0;
    srp_vk_nav_reset(&vk_nav);vk_nav_wait_neutral=false;
    vk_prev_start=vk_prev_up=vk_prev_down=vk_prev_left=vk_prev_right=false;
    vk_prev_a=vk_prev_b=vk_prev_x=vk_prev_y=vk_prev_chord=false;
    retrace_bypass_diagnostic=false;
    retrace_chord_previous=false;
    audio_debug_enabled=true;
    audio_debug_chord_previous=false;
    profile_reporting_enabled=false;profile_chord_previous=false;
    input_poll_throttled=true;input_chord_previous=false;
    input_poll_valid=false;input_poll_anchor=0;
    now_us=tick_anchor_us=frame_anchor_us=vga_epoch_us=micros();
    audio_tick_clock=audio_now_clock=0;
    memset(&profile,0,sizeof(profile));
    scanout_max_us=0;
    inside_retrace_wait=false;
    retrace_boundary_scans=retrace_deferred_frames=0;
    retrace_seen=false;
    last_retrace_entry_us=0;
    srp_vga_deadline_reset(&vga_deadline);
    retrace_catchups=retrace_reanchors=retrace_max_late_us=0;
    retrace_catchup_late_us=0;
    retrace_pace_skips=pump_pace_suppressed=0;
    profile_epoch_us=micros();profile_seconds=0;
    profile_last_emit_us=profile_max_emit_us=0;
    engine_host_total_us=0;
    memset(engine_counter,0,sizeof(engine_counter));
    retrace_pump_total_us=retrace_idle_total_us=retrace_overrun_total_us=0;
    retrace_overshoots=0;
    previous_pump_valid=false;last_pump_start_us=0;
    startup_stall_rebased=false;startup_rebase_gap_us=startup_rebases=0;
    inside_io_audio_hook=false;
    io_audio_anchor_us=io_audio_last_service_us=micros();
    io_audio_anchor_clock=audio_now_clock;
    io_idle_calls=io_audio_pumps=0;io_audio_advanced_clocks=0;
    delay_api_calls=delay_api_inside_tick=delay_api_max_us=tick_direct_calls=0;
    delay_api_requested_us=0;
    engine_pump_file=engine_retrace_file=last_pump_file=last_api_file=NULL;
    gap_trace_prev_file=gap_trace_next_file=gap_trace_api_file=NULL;
    last_api_kind=gap_trace_api_kind=NULL;
    engine_pump_line=engine_retrace_line=last_pump_line=last_api_line=0;
    ui_phase=ui_gap_phase=0;
    ui_phase_start_us=ui_gap_phase_age_us=0;
    ui_scope_active=ui_gap_active=false;
    ui_last_exit_us=ui_gap_since_exit_us=ui_gap_outside_ui_us=0;
    ui_last_exit_line=ui_gap_exit_line=0;
    ui_scope_entries=ui_scope_exits=0;
    memset(ui_phase_max_us,0,sizeof(ui_phase_max_us));
    memset(ui_phase_count,0,sizeof(ui_phase_count));
    gap_trace_max_us=gap_trace_count=gap_api_age_us=0;
    last_pump_end_us=outside_max_us=host_max_us=pit2_max_us=port61_max_us=0;
    outside_count_100ms=0;
    gap_trace_prev_line=gap_trace_next_line=gap_trace_api_line=0;
    last_api_at_us=0;
    boot_audio_count=boot_audio_printed=0;
    of_file_set_idle_hook(audio_during_file_io);
    printf("Street Rod upstream engine host: RetroDriven Pocket v0.12.39\n");
    return true;
}
void host_shutdown(void){
    of_file_set_idle_hook(NULL);
    initialized=0;
    if(on_focus_lost)on_focus_lost();
}
void host_set_tick_handler(void(*handler)(void)){on_tick=handler;}
void host_set_kbd_handler(void(*handler)(u8)){on_key=handler;}
void host_set_focus_lost_handler(void(*handler)(void)){on_focus_lost=handler;}
void host_set_frame_source(bool(*fn)(u32 *),int w,int h){
    if(w!=320||h!=200)printf("Unexpected host frame dimensions: %d x %d\n",w,h);
    compose=fn;
    compose_indexed=NULL;
    previous_rgb_valid=false;
    srp_output_cache_reset(&output_rows);
    frame_anchor_us=micros()-PRESENT_INTERVAL_US;
}
/* Pocket-only indexed frame entry point (not part of upstream host.h). */
void host_set_indexed_frame_source(bool (*fn)(u8 *),int w,int h){
    if(w!=SCREEN_W||h!=SCREEN_H)
        printf("Unexpected indexed source: %d x %d\n",w,h);
    compose=NULL;
    compose_indexed=fn;
    previous_rgb_valid=false;
    srp_output_cache_reset(&output_rows);
    frame_anchor_us=micros()-PRESENT_INTERVAL_US;
}
 

void host_begin_indexed_frame(void){
    srp_output_cache_next_frame(&output_rows);
}
void host_mark_indexed_row(unsigned y){
    srp_output_cache_mark_row(&output_rows,y);
}
/* Called from the EGA scanout to retain the existing COM13 profiler fields. */
void host_record_indexed_groups(unsigned decoded,unsigned reused){
    profile.ega_groups_decoded+=decoded;
    profile.ega_groups_reused+=reused;
}
void host_record_indexed_rows(unsigned dirty,unsigned reused){
    profile.dirty_scanlines+=dirty;
    profile.reused_scanlines+=reused;
    if(dirty==0)profile.zero_pixel_change_frames++;
}
void host_set_pit_divisor(u16 divisor){
    pit_div=divisor?divisor:65536u;
    tick_anchor_us=micros();ticks_run=0;
    audio_tick_clock=audio_now_clock;
}
void host_pump(void){
    if(!initialized || inside_pump)return;
    inside_pump=true;
    uint32_t pump_start=micros();

    const char *this_file=engine_pump_file?engine_pump_file:engine_retrace_file;
    unsigned this_line=engine_pump_file?engine_pump_line:engine_retrace_line;
    if(previous_pump_valid && !startup_stall_rebased && profile_seconds==0 &&
       (uint32_t)(pump_start-last_pump_start_us)>=SRP_STARTUP_STALL_US){

        startup_stall_rebased=true;
        startup_rebases++;
        startup_rebase_gap_us=(uint32_t)(pump_start-last_pump_start_us);
        tick_anchor_us=pump_start;
        ticks_run=0;
        frame_anchor_us=pump_start-PRESENT_INTERVAL_US;
        vga_epoch_us=pump_start;
        srp_vga_deadline_reset(&vga_deadline);
    }
    if(previous_pump_valid){

        uint32_t outside=(uint32_t)(pump_start-last_pump_end_us);
        if(outside>outside_max_us)outside_max_us=outside;
        if(outside>=100000u)outside_count_100ms++;
        uint32_t gap=(uint32_t)(pump_start-last_pump_start_us);
        if(gap>=100000u){
            gap_trace_count++;
            if(gap>gap_trace_max_us){
                gap_trace_max_us=gap;
                ui_gap_phase=ui_phase;
                ui_gap_phase_age_us=(ui_scope_active && ui_phase)?(uint32_t)(pump_start-ui_phase_start_us):0;
                ui_gap_active=ui_scope_active;
                ui_gap_exit_line=ui_last_exit_line;
                ui_gap_since_exit_us=ui_last_exit_us?(uint32_t)(pump_start-ui_last_exit_us):0;
                /* Nonzero only if a ui_wait return happened after the prior
 * pump ended and before this one began. */
                ui_gap_outside_ui_us=(ui_last_exit_us && last_pump_end_us &&
                    (uint32_t)(ui_last_exit_us-last_pump_end_us)<=outside)
                    ?(uint32_t)(pump_start-ui_last_exit_us):0;
                gap_trace_prev_file=last_pump_file;
                gap_trace_prev_line=last_pump_line;
                gap_trace_next_file=this_file;
                gap_trace_next_line=this_line;
                gap_trace_api_file=last_api_file;
                gap_trace_api_line=last_api_line;
                gap_trace_api_kind=last_api_kind;
                gap_api_age_us=last_api_file?(uint32_t)(pump_start-last_api_at_us):0;
            }
        }
        if(gap>profile.gap_max_us)profile.gap_max_us=gap;
        if(gap>=20000u)profile.gaps_over_20ms++;
        if(gap>=100000u)profile.gaps_over_100ms++;
    }
    last_pump_start_us=pump_start;
    last_pump_file=this_file;
    last_pump_line=this_line;
    previous_pump_valid=true;
    profile.pumps++;

    if(!input_poll_throttled || !input_poll_valid ||
       (uint32_t)(pump_start-input_poll_anchor)>=INPUT_POLL_INTERVAL_US){
        input_poll_anchor=pump_start;
        input_poll_valid=true;
        poll_buttons();
        profile.input_poll_samples++;
    }else{
        profile.input_poll_skips++;
    }
    uint32_t after_input=micros();
    profile.input_us+=(uint32_t)(after_input-pump_start);
    now_us=after_input;
    /* uint32 subtraction handles 71-minute wrap of SDK's us counter. */
    uint64_t due=(uint64_t)(u32)(now_us-tick_anchor_us)*PIT_HZ/
                  ((uint64_t)pit_div*1000000ULL);
    unsigned catchup=0;
    while(ticks_run<due && catchup++<16) {
        ticks_run++;
        uint64_t boundary=audio_tick_clock+(uint64_t)pit_div;
        if(boundary<audio_now_clock)boundary=audio_now_clock;
        audio_tick_clock=audio_now_clock=boundary;
        srp_audio_advance(boundary);
        srp_audio_flush();
        inside_tick=true;
        uint32_t tick_start=micros();
        if(on_tick)on_tick();
        profile.tick_us+=(uint32_t)(micros()-tick_start);
        profile.ticks++;
        inside_tick=false;
    }
    /* Detect game-clock backlogs without changing the existing recovery. */
    uint64_t backlog=due>ticks_run?due-ticks_run:0;
    if(backlog>profile.max_tick_backlog)
        profile.max_tick_backlog=(uint32_t)(backlog>UINT32_MAX?UINT32_MAX:backlog);
    if(catchup>16 && ticks_run<due)profile.catchup_capped++;
    /* Avoid unbounded catchup after SD stalls / debug breakpoints. */
    if(due>ticks_run+200){tick_anchor_us=now_us;ticks_run=0;profile.timer_resyncs++;}
    if((u32)(now_us-frame_anchor_us)>=PRESENT_INTERVAL_US){
        if(inside_retrace_wait){
            /* VGA retrace owns presentation; continue PIT and audio only. */
            retrace_deferred_frames++;
        }else if(retrace_seen &&
                 (u32)(now_us-last_retrace_entry_us)<RETRACE_ACTIVE_GRACE_US){

            pump_pace_suppressed++;
        }else{
            frame_anchor_us=now_us;  
            present();
        }
    }
    uint32_t pump_us=(uint32_t)(micros()-pump_start);
    if(pump_us>profile.max_pump_us)profile.max_pump_us=pump_us;
     

    io_audio_anchor_us=micros();
    io_audio_anchor_clock=audio_now_clock;
    io_audio_last_service_us=io_audio_anchor_us;
    inside_pump=false;
    uint32_t finished=micros();
    uint32_t spent=(uint32_t)(finished-pump_start);
    if(spent>host_max_us)host_max_us=spent;
     

    engine_host_total_us+=(uint32_t)(finished-pump_start);
    uint32_t emit_begin=micros();
    bool emitted=profile_live_emit(finished);
    if(emitted){
        profile_last_emit_us=(uint32_t)(micros()-emit_begin);
        if(profile_last_emit_us>profile_max_emit_us)
            profile_max_emit_us=profile_last_emit_us;
    }
     
    last_pump_end_us=micros();
}
 

void host_wait_vretrace(void){
    uint32_t wait_start=micros();
    profile.retrace_calls++;
    if(retrace_bypass_diagnostic){host_pump();profile.retrace_us+=(uint32_t)(micros()-wait_start);return;}
    retrace_seen=true;
    last_retrace_entry_us=wait_start;
    srp_vga_deadline_result_t slot=srp_vga_deadline_begin(
        &vga_deadline,wait_start,vga_epoch_us,VIDEO_INTERVAL_US);
    u32 remaining=slot.wait_us;
    if(slot.reanchored)retrace_reanchors++;
    if(slot.caught_up){
        retrace_catchups++;
        retrace_catchup_late_us+=slot.late_us;
        if(slot.late_us>retrace_max_late_us)retrace_max_late_us=slot.late_us;
    }
    u32 begin=micros();
    uint64_t total_pump_time=0;
    inside_retrace_wait=true;
    /* Even if the deadline has passed, give PIT, input and audio a pump. */
    if(remaining==0u){
        uint32_t pump_begin=micros();
        host_pump();
        total_pump_time+=(uint32_t)(micros()-pump_begin);
    }
    while((u32)(micros()-begin)<remaining){
        uint32_t pump_begin=micros();
        host_pump();
        total_pump_time+=(uint32_t)(micros()-pump_begin);
        /* Avoid sleep overshoot in the last few hundred microseconds. */
        if((u32)(micros()-begin)+350u<remaining)usleep(100);
    }
    uint32_t elapsed=(uint32_t)(micros()-wait_start);
    uint32_t interval_elapsed=(uint32_t)(micros()-begin);
    profile.retrace_us+=elapsed;
    retrace_pump_total_us+=total_pump_time;
    if(elapsed>total_pump_time)retrace_idle_total_us+=elapsed-total_pump_time;
    if(interval_elapsed>remaining){
        retrace_overshoots++;
        retrace_overrun_total_us+=interval_elapsed-remaining;
    }
    inside_retrace_wait=false;

    if(compose_indexed || compose){
        uint32_t render_start=micros();
        if((uint32_t)(render_start-frame_anchor_us)>=PRESENT_INTERVAL_US){

            frame_anchor_us=render_start; /* start-to-start, no extra 33ms delay */
            present();
            retrace_boundary_scans++;
        }else{
            retrace_pace_skips++;
        }
    }
}
bool host_in_vretrace(void){
    return ((u32)(micros()-vga_epoch_us)%VIDEO_INTERVAL_US)<1500u;
}
void host_mouse_read(s16 *x,s16 *y,u8 *buttons){
    if(x)*x=(s16)mouse_x;
    if(y)*y=(s16)mouse_y;
    if(buttons)*buttons=mouse_buttons;
}
void host_mouse_motion(s16 *dx,s16 *dy){
    if(dx)*dx=(s16)motion_dx;
    if(dy)*dy=(s16)motion_dy;
    motion_dx=motion_dy=0;
}
void host_mouse_mickeys(s16 *mx,s16 *my){
    if(mx)*mx=(s16)mickey_dx;
    if(my)*my=(s16)mickey_dy;
    mickey_dx=mickey_dy=0;
}
void host_mouse_set_relative(bool on){relative_mouse=on;}
bool host_joy_read(s16 *x,s16 *y,u8 *buttons){
    if(keyboard_open || keyboard_suppress_until_release){
        if(x)*x=0;
        if(y)*y=0;
        if(buttons)*buttons=0;
        return true;
    }
    if(x)*x=(s16)((of_btn(OF_BTN_RIGHT)?32767:0)-(of_btn(OF_BTN_LEFT)?32767:0));
    if(y)*y=(s16)((of_btn(OF_BTN_DOWN)?32767:0)-(of_btn(OF_BTN_UP)?32767:0));
    if(buttons)*buttons=of_btn(OF_BTN_A)?1:0;
    return true;
}
void host_pit2_divisor(u16 div){
    trace_audio_event(1,div?div:65536u);
    speaker_div=div?div:65536u;
    srp_audio_pit2(audio_now_clock,div);
}
void host_port61(u8 value){
    trace_audio_event(2,value);
    speaker_port=value;
    srp_audio_port61(audio_now_clock,value);
}
u8 host_port61_get(void){return speaker_port;}
void host_speaker(u16 divisor,bool on){
    host_pit2_divisor(divisor);
    host_port61(on?(u8)(speaker_port|3u):(u8)(speaker_port&~3u));
}
void host_busy_wait_us(u32 us){
    delay_api_calls++;
    delay_api_requested_us+=us;
    if(us>delay_api_max_us)delay_api_max_us=us;
    if(inside_tick)delay_api_inside_tick++;
    uint64_t clock_delta=((uint64_t)us*PIT_HZ)/1000000ull;
    if(inside_tick || !realtime_mode){
        audio_now_clock+=clock_delta;
        return;
    }
    u32 start=micros();
    uint64_t baseline=audio_now_clock;
    while((u32)(micros()-start)<us){
        host_pump();
        usleep(100);
    }
    uint64_t elapsed=((uint64_t)(u32)(micros()-start)*PIT_HZ)/1000000ull;
    if(audio_now_clock<baseline+elapsed)audio_now_clock=baseline+elapsed;
}
void host_busy_wait_ns(uint64_t ns){
    if(ns>1000000000ULL)ns=1000000000ULL;
    host_busy_wait_us((u32)((ns+999u)/1000u));
}
void host_set_realtime(bool on){realtime_mode=on;}
void host_run_tick_now(void){
    tick_direct_calls++;
    uint64_t next=audio_tick_clock+(uint64_t)pit_div;
    if(next<audio_now_clock)next=audio_now_clock;
    audio_tick_clock=audio_now_clock=next;
    srp_audio_advance(next);srp_audio_flush();
    ticks_run++;
    inside_tick=true;
    uint32_t t=micros();
    if(on_tick)on_tick();
    profile.tick_us+=(uint32_t)(micros()-t);
    profile.ticks++;
    inside_tick=false;
}
uint64_t host_audio_clock(void){return audio_now_clock;}
bool host_audio_dump_begin(const char *path){(void)path;return false;}
void host_audio_dump_end(void){}
void host_set_pointer_query(bool(*query)(s16*,s16*)){pointer_query=query;(void)pointer_query;}
char *host_game_path(const char *name,bool create){
    if(!name||!*name)return NULL;
    const char *base=name;
    for(const char *p=name;*p;p++)if(*p=='/'||*p=='\\'||*p==':')base=p+1;
    /* Pocket's registered data slots use the DOS filenames exactly. */
    static const char *const original[]={"SR.EXE","LIB1","LIB2","HOT_DATA"};
    for(unsigned j=0;j<4;j++){
        const char *a=base,*b=original[j];
        while(*a&&*b && ((*a|32)==(*b|32)||*a==*b)){a++;b++;}
        if(!*a&&!*b){
            char *s=malloc(strlen(original[j])+1u);
            if(s)strcpy(s,original[j]);
            return s;
        }
    }
    /* For saves preserve the requested filename, no unsafe path traversal. */
    if(create && srp_save_logical_id(base)>=0){
        static unsigned save_create_logs=0;
        if(save_create_logs++<24u)printf("[SRP-SAVE] path-create %s\n",base);
    }

    if(!create){
        if(srp_save_logical_id(base)>=0){
            int present=srp_save_has_data(base);
            static unsigned save_lookup_logs=0;
            if(save_lookup_logs++<36u)printf("[SRP-SAVE] lookup %s present=%d\n",base,present);
            if(!present)return NULL;
        }else{
            FILE *f=fopen(base,"rb");if(!f)return NULL;fclose(f);
        }
    }
    char *s=malloc(strlen(base)+1u);
    if(s)strcpy(s,base);
    return s;
}
void host_free(void *p){free(p);}
_Noreturn void host_fatal(const char *fmt,...){
    va_list ap;va_start(ap,fmt);
    printf("STREET ROD FATAL: ");vprintf(fmt,ap);printf("\n");va_end(ap);
    /* Leave a visible red screen rather than instantly silently rebooting. */
    if(initialized){
        u8 *fb=of_video_surface();
        if(fb){memset(fb,0,320u*240u);of_video_palette(0,0x720000);of_video_flip();}
    }
    for(;;){usleep(200000);}
}
