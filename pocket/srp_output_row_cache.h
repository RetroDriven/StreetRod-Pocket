/* Reuse unchanged framebuffer rows and batch contiguous copies. */
#ifndef SRP_OUTPUT_ROW_CACHE_H
#define SRP_OUTPUT_ROW_CACHE_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

enum { SRP_OUTPUT_SOURCE_ROWS=200, SRP_OUTPUT_DEST_ROWS=240,
       SRP_OUTPUT_ROW_BYTES=320, SRP_OUTPUT_SURFACES=3,
       SRP_OUTPUT_MASK_WORDS=(SRP_OUTPUT_SOURCE_ROWS+31)/32 };
typedef struct {
    uint8_t *surface[SRP_OUTPUT_SURFACES];
    uint32_t pending[SRP_OUTPUT_SURFACES][SRP_OUTPUT_MASK_WORDS];
    unsigned next_replace;
} srp_output_row_cache_t;
typedef struct { uint32_t copied_rows, reused_rows, visited_source_rows;
    uint32_t batched_runs;
    uint8_t new_surface, replaced_surface;
} srp_output_copy_stats_t;

static inline void srp_output_mark_all(uint32_t *mask){
    for(unsigned i=0;i<SRP_OUTPUT_MASK_WORDS;i++)mask[i]=UINT32_MAX;
    /* The last mask includes exactly 200 valid source rows, not 224. */
    const unsigned excess=SRP_OUTPUT_MASK_WORDS*32-SRP_OUTPUT_SOURCE_ROWS;
    if(excess)mask[SRP_OUTPUT_MASK_WORDS-1]>>=excess;
}
static inline void srp_output_cache_reset(srp_output_row_cache_t *cache){
    memset(cache,0,sizeof(*cache));
}
static inline unsigned srp_output_cache_surface_count(const srp_output_row_cache_t *cache){
    unsigned count=0;
    for(unsigned i=0;i<SRP_OUTPUT_SURFACES;i++)
        if(cache->surface[i])count++;
    return count;
}
static inline void srp_output_cache_invalidate(srp_output_row_cache_t *cache){
    for(unsigned i=0;i<SRP_OUTPUT_SURFACES;i++)
        if(cache->surface[i])srp_output_mark_all(cache->pending[i]);
}
 

static inline void srp_output_cache_next_frame(srp_output_row_cache_t *cache){
    (void)cache;
}
static inline void srp_output_cache_mark_row(srp_output_row_cache_t *cache,unsigned y){
    if(y>=SRP_OUTPUT_SOURCE_ROWS)return;
    unsigned word=y>>5,bit=y&31u;
    uint32_t flag=(uint32_t)1u<<bit;
    for(unsigned i=0;i<SRP_OUTPUT_SURFACES;i++)
        if(cache->surface[i])cache->pending[i][word]|=flag;
}
 

static inline unsigned srp_output_copy_row(uint8_t *dst,const uint8_t *src,unsigned sy){
    unsigned dy=sy+(sy+4u)/5u;
    const uint8_t *from=src+sy*SRP_OUTPUT_ROW_BYTES;
    uint8_t *to=dst+dy*SRP_OUTPUT_ROW_BYTES;
    memcpy(to,from,SRP_OUTPUT_ROW_BYTES);
    if(sy%5u==0u){
        memcpy(to+SRP_OUTPUT_ROW_BYTES,from,SRP_OUTPUT_ROW_BYTES);
        return 2u;
    }
    return 1u;
}
static inline srp_output_copy_stats_t srp_output_cache_copy(
    srp_output_row_cache_t *cache,uint8_t *output,const uint8_t *indexed,
    bool partial_copy_enabled)
{
    srp_output_copy_stats_t result={0};
    int slot=-1;
    if(partial_copy_enabled){
        for(int i=0;i<SRP_OUTPUT_SURFACES;i++)
            if(cache->surface[i]==output){slot=i;break;}
        if(slot<0){
            for(int i=0;i<SRP_OUTPUT_SURFACES;i++)
                if(!cache->surface[i]){slot=i;break;}
            if(slot<0){
                slot=(int)(cache->next_replace%SRP_OUTPUT_SURFACES);
                cache->next_replace=(unsigned)(slot+1)%SRP_OUTPUT_SURFACES;
                result.replaced_surface=1u;
            }
            result.new_surface=1u;
            cache->surface[slot]=output;
            srp_output_mark_all(cache->pending[slot]);
        }
    }
    if(slot<0){
        /* Overlay/RGB fallback must reconstruct all 240 destination rows. */
        for(unsigned sy=0;sy<SRP_OUTPUT_SOURCE_ROWS;sy++)
            result.copied_rows+=srp_output_copy_row(output,indexed,sy);
        result.visited_source_rows=SRP_OUTPUT_SOURCE_ROWS;
        return result;
    }
    for(unsigned wi=0;wi<SRP_OUTPUT_MASK_WORDS;wi++){
        uint32_t pending=cache->pending[slot][wi];
        cache->pending[slot][wi]=0;
        while(pending){
#if defined(__GNUC__) || defined(__clang__)
            unsigned bit=(unsigned)__builtin_ctz(pending);
#else
            unsigned bit=0;while(((pending>>bit)&1u)==0u)bit++;
#endif
            unsigned sy=wi*32u+bit;
            if(sy<SRP_OUTPUT_SOURCE_ROWS){

                if(sy%5u==1u && bit<=28u && sy+3u<SRP_OUTPUT_SOURCE_ROWS &&
                   ((pending>>bit)&15u)==15u){
                    unsigned dy=sy+(sy+4u)/5u;
                    memcpy(output+dy*SRP_OUTPUT_ROW_BYTES,
                           indexed+sy*SRP_OUTPUT_ROW_BYTES,
                           4u*SRP_OUTPUT_ROW_BYTES);
                    result.batched_runs++;
                    result.copied_rows+=4u;
                    result.visited_source_rows+=4u;
                    pending&=~((uint32_t)15u<<bit);
                    continue;
                }
                result.copied_rows+=srp_output_copy_row(output,indexed,sy);
                result.visited_source_rows++;
            }
            pending&=pending-1u;
        }
    }
    result.reused_rows=SRP_OUTPUT_DEST_ROWS-result.copied_rows;
    return result;
}
#endif
