#include "srp_snapshot.h"
#include <string.h>

static const uint8_t snapshot_magic[8] = {'S','R','P','S','T','A','T','E'};

static void put32(uint8_t *p, uint32_t n) {
    p[0]=(uint8_t)n;
    p[1]=(uint8_t)(n>>8);
    p[2]=(uint8_t)(n>>16);
    p[3]=(uint8_t)(n>>24);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

static uint32_t checksum(const uint8_t *p, size_t size) {
    uint32_t crc=0xFFFFFFFFu;
    for (size_t i=0; i<size; ++i) {
        crc ^= p[i];
        for (unsigned j=0; j<8; ++j)
            crc = (crc>>1) ^ (0xEDB88320u & (0u-(crc&1u)));
    }
    return ~crc;
}

static int valid_edition(unsigned edition) {
    return edition==SRP_SNAPSHOT_ORIGINAL || edition==SRP_SNAPSHOT_SE;
}

size_t srp_snapshot_required_size(size_t memory_size, size_t video_size) {
    if (!memory_size || memory_size>SRP_SNAPSHOT_MEMORY_LIMIT ||
        video_size>SRP_SNAPSHOT_VIDEO_LIMIT) return 0;
    return SRP_SNAPSHOT_HEADER_SIZE+memory_size+video_size;
}

srp_snapshot_result_t srp_snapshot_encode(
    uint8_t *output, size_t capacity, size_t *written,
    unsigned edition, const void *memory, size_t memory_size,
    const void *video, size_t video_size)
{
    if (written) *written=0;
    if (!output || !written || !valid_edition(edition) || !memory ||
        (video_size && !video)) return SRP_SNAPSHOT_BAD_ARGUMENT;
    size_t required=srp_snapshot_required_size(memory_size, video_size);
    if (!required) return SRP_SNAPSHOT_TOO_LARGE;
    if (capacity<required) return SRP_SNAPSHOT_TRUNCATED;
    memcpy(output,snapshot_magic,sizeof(snapshot_magic));
    put32(output+8,SRP_SNAPSHOT_VERSION);
    put32(output+12,(uint32_t)edition);
    put32(output+16,(uint32_t)memory_size);
    put32(output+20,(uint32_t)video_size);
    put32(output+24,checksum((const uint8_t *)memory,memory_size));
    put32(output+28,video_size?checksum((const uint8_t *)video,video_size):0u);
    put32(output+32,(uint32_t)(memory_size+video_size));
    put32(output+36,0);
    memcpy(output+SRP_SNAPSHOT_HEADER_SIZE,memory,memory_size);
    if(video_size) memcpy(output+SRP_SNAPSHOT_HEADER_SIZE+memory_size,video,video_size);
    *written=required;
    return SRP_SNAPSHOT_OK;
}

srp_snapshot_result_t srp_snapshot_decode(
    const uint8_t *input, size_t input_size, unsigned expected_edition,
    void *memory, size_t memory_capacity, size_t *memory_written,
    void *video, size_t video_capacity, size_t *video_written)
{
    if(memory_written) *memory_written=0;
    if(video_written) *video_written=0;
    if(!input || !valid_edition(expected_edition) || !memory ||
       !memory_written || !video_written) return SRP_SNAPSHOT_BAD_ARGUMENT;
    if(input_size<SRP_SNAPSHOT_HEADER_SIZE) return SRP_SNAPSHOT_TRUNCATED;
    if(memcmp(input,snapshot_magic,sizeof(snapshot_magic))!=0 ||
       get32(input+8)!=SRP_SNAPSHOT_VERSION || get32(input+36)!=0)
        return SRP_SNAPSHOT_BAD_HEADER;
    if(get32(input+12)!=expected_edition) return SRP_SNAPSHOT_WRONG_EDITION;
    size_t memory_size=get32(input+16), video_size=get32(input+20);
    size_t required=srp_snapshot_required_size(memory_size,video_size);
    if(!required || get32(input+32)!=(uint32_t)(memory_size+video_size))
        return SRP_SNAPSHOT_BAD_HEADER;
    if(input_size!=required) return SRP_SNAPSHOT_TRUNCATED;
    if(memory_capacity<memory_size || video_capacity<video_size || (video_size&&!video))
        return SRP_SNAPSHOT_TOO_LARGE;
    const uint8_t *payload=input+SRP_SNAPSHOT_HEADER_SIZE;
    if(checksum(payload,memory_size)!=get32(input+24) ||
       (video_size?checksum(payload+memory_size,video_size):0u)!=get32(input+28))
        return SRP_SNAPSHOT_CORRUPT;
    /* Validate every region before modifying the caller's buffers. */
    memcpy(memory,payload,memory_size);
    if(video_size) memcpy(video,payload+memory_size,video_size);
    *memory_written=memory_size;
    *video_written=video_size;
    return SRP_SNAPSHOT_OK;
}
