 


#ifndef RETRODRIVEN_PC_AUDIO_H
#define RETRODRIVEN_PC_AUDIO_H
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    uint32_t writes, dropped_buffers, empty_checks;
    uint32_t minimum_pending_pairs, maximum_pending_pairs;
    uint32_t initial_lead_pairs;
    uint32_t pit2_writes, port61_writes, gate_starts, data_enables;
    uint32_t first_gate_ms, first_data_ms;
    uint32_t startup_fades;
} srp_audio_stats_t;
void srp_audio_snapshot(srp_audio_stats_t *out);
void srp_audio_init(void);
void srp_audio_set_enabled(bool on);
void srp_audio_advance(uint64_t pit_clocks);
void srp_audio_flush(void);
void srp_audio_pit2(uint64_t at, uint16_t divisor);
void srp_audio_port61(uint64_t at, uint8_t value);
#endif
