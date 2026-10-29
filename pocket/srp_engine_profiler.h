 


#ifndef SRP_ENGINE_PROFILER_H
#define SRP_ENGINE_PROFILER_H
#include <stdint.h>
typedef struct {
    uint32_t start_us;
    uint64_t host_us;
    unsigned id;
} srp_engine_scope_t;
enum { SRP_OP_EGA_BLIT=0, SRP_OP_EGA_BLINDS=1, SRP_OP_GARAGE_BG=2, SRP_OP_GARAGE_CAR=3, SRP_OP_CAR_COMPOSE=4, SRP_OP_CAR_ANIM=5, SRP_OP_PIC_DECODE=6, SRP_OP_COUNT=7 };
srp_engine_scope_t srp_engine_scope_begin(unsigned id);
void srp_engine_scope_end(srp_engine_scope_t *scope);
#if !defined(__GNUC__) && !defined(__clang__)
#error "SRP engine scope instrumentation requires GNU-compatible cleanup"
#endif
#define SRP_ENGINE_SCOPE(id) \
    srp_engine_scope_t __attribute__((cleanup(srp_engine_scope_end))) \
        srp_engine_auto_scope = srp_engine_scope_begin(id)
#endif
