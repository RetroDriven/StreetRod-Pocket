#ifndef SRP_SCANLINE_CACHE_H
#define SRP_SCANLINE_CACHE_H
/* Scanline cache for indexed EGA output. */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

enum { SRP_ROW_WIDTH=320, SRP_ROW_COUNT=200 };
typedef struct {
    uint16_t dirty_rows, reused_rows;
} srp_row_stats_t;

static inline unsigned srp_rgb565_key(uint32_t rgb){
    unsigned r=(rgb>>16)&255u,g=(rgb>>8)&255u,b=rgb&255u;
    return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);
}
static inline srp_row_stats_t srp_reindex_changed_rows(
    const uint32_t *current_rgb, uint32_t *previous_rgb,
    uint8_t *indexed, const uint8_t *fixed_rgb565_to_index,
    bool previous_valid)
{
    srp_row_stats_t result={0,0};
    for(unsigned y=0;y<SRP_ROW_COUNT;y++){
        unsigned off=y*SRP_ROW_WIDTH;
        const uint32_t *src=current_rgb+off;
        uint32_t *old=previous_rgb+off;
        if(previous_valid && memcmp(src,old,SRP_ROW_WIDTH*sizeof(uint32_t))==0){
            result.reused_rows++;
            continue;
        }
        /* Keep the loop simple to allow the cross-compiler to optimize. */
        for(unsigned x=0;x<SRP_ROW_WIDTH;x++)
            indexed[off+x]=fixed_rgb565_to_index[srp_rgb565_key(src[x])];
        memcpy(old,src,SRP_ROW_WIDTH*sizeof(uint32_t));
        result.dirty_rows++;
    }
    return result;
}
#endif
