#ifndef SRP_SNAPSHOT_H
#define SRP_SNAPSHOT_H

#include <stddef.h>
#include <stdint.h>

/* Portable snapshot envelope. This is not a complete running-game state. */
#define SRP_SNAPSHOT_HEADER_SIZE 40u
#define SRP_SNAPSHOT_VERSION 1u
#define SRP_SNAPSHOT_MEMORY_LIMIT 0x200000u
#define SRP_SNAPSHOT_VIDEO_LIMIT 0x40000u

enum {
    SRP_SNAPSHOT_ORIGINAL = 1,
    SRP_SNAPSHOT_SE = 2
};

typedef enum {
    SRP_SNAPSHOT_OK = 0,
    SRP_SNAPSHOT_BAD_ARGUMENT,
    SRP_SNAPSHOT_TOO_LARGE,
    SRP_SNAPSHOT_TRUNCATED,
    SRP_SNAPSHOT_WRONG_EDITION,
    SRP_SNAPSHOT_BAD_HEADER,
    SRP_SNAPSHOT_CORRUPT
} srp_snapshot_result_t;

size_t srp_snapshot_required_size(size_t memory_size, size_t video_size);
srp_snapshot_result_t srp_snapshot_encode(
    uint8_t *output, size_t capacity, size_t *written,
    unsigned edition, const void *memory, size_t memory_size,
    const void *video, size_t video_size);
srp_snapshot_result_t srp_snapshot_decode(
    const uint8_t *input, size_t input_size, unsigned expected_edition,
    void *memory, size_t memory_capacity, size_t *memory_written,
    void *video, size_t video_capacity, size_t *video_written);

#endif
