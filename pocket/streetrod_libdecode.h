#ifndef STREETROD_LIBDECODE_H
#define STREETROD_LIBDECODE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SR_LIB_MAX_PACKED 65536u
#define SR_LIB_MAX_RAW 64000u
#define SR_LIB_PIXELS (320u*200u)
static const uint8_t sr_run_counts[16] = {1,1,2,3,4,2,3,5,6,6,4,7,7,5,8,8};
static const uint8_t sr_run_values[16] = {0,255,0,0,0,255,255,0,255,0,255,255,0,255,255,0};
typedef struct { FILE *fp; uint16_t count; uint32_t length; } SrLibrary;
typedef struct { uint16_t w,h; uint8_t masked; } SrPicture;
static uint8_t sr_packed[SR_LIB_MAX_PACKED+1];
static uint8_t sr_unpacked[SR_LIB_MAX_RAW];

static uint16_t sr_le16(const uint8_t *p) {return (uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8));}
static uint32_t sr_le32(const uint8_t *p) {return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}

static int sr_lib_open(SrLibrary *l, const char *filename) {
    uint8_t header[2];
    memset(l,0,sizeof(*l));
    l->fp=fopen(filename,"rb");
    if (!l->fp) return 0;
    if (fseek(l->fp,0,SEEK_END) || (long)(l->length=(uint32_t)ftell(l->fp)) < 2 ||
        fseek(l->fp,0,SEEK_SET) || fread(header,1,2,l->fp)!=2) goto fail;
    l->count=sr_le16(header);
    if (!l->count || l->count>512 || 2u+30u*l->count>=l->length) goto fail;
    return 1;
fail:
    fclose(l->fp);
    memset(l,0,sizeof(*l));
    return 0;
}
static void sr_lib_close(SrLibrary *l) {
    if (l->fp) fclose(l->fp);
    memset(l,0,sizeof(*l));
}

static int sr_lib_picture(SrLibrary *l, uint16_t index, uint8_t *dst, SrPicture *out) {
    uint8_t record[30];
    if (!l->fp || index>=l->count) return 0;
    uint32_t dir=2u+30u*(uint32_t)l->count;
    if (fseek(l->fp,(long)(2u+30u*index),SEEK_SET) || fread(record,1,30,l->fp)!=30) return 0;
    unsigned w=sr_le16(record), h=sr_le16(record+2);
    unsigned raw=sr_le16(record+4);
    uint32_t off=sr_le32(record+24), size=sr_le16(record+28);
    if (!w || !h || w>320 || h>200 || w%8 || !size || size>=SR_LIB_MAX_PACKED ||
        dir+off>l->length || size+1u>l->length-(dir+off) || raw>SR_LIB_MAX_RAW) return 0;
    if (raw!=(w*h)/2u) {
        /* Four known LIB2 entries contain only one bit-plane. */
        return -1;
    }
    if (fseek(l->fp,(long)(dir+off),SEEK_SET) || fread(sr_packed,1,size+1u,l->fp)!=size+1u ||
        sr_packed[0]!=0xBC || sr_packed[size]!=0xBC) return 0;
    unsigned written=0;
    for (unsigned p=1;p<size;p++) {
        uint8_t b=sr_packed[p];
        unsigned count=1, value=b;
        if (b==0 || b==255) {
            if (++p>=size) return 0;
            count=(unsigned)sr_packed[p]+1u;
        } else {
            for (int k=15;k>=0;k--) {
                if (record[7+k] && b==record[7+k]) {
                    count=sr_run_counts[k]; value=sr_run_values[k];break;
                }
            }
        }
        if (count>raw-written) return 0;
        memset(sr_unpacked+written,(int)value,count);
        written+=count;
    }
    /* Original unpacker zero-fills truncated images. */
    if (written<raw) memset(sr_unpacked+written,0,raw-written);
    memset(dst,0,SR_LIB_PIXELS);
    const unsigned bpr=w/8, plane_bytes=bpr*h;
    unsigned xoff=(320-w)/2, yoff=(200-h)/2;
    for (unsigned plane=0;plane<4;plane++)
        for (unsigned y=0;y<h;y++)
            for (unsigned xb=0;xb<bpr;xb++) {
                uint8_t bits=sr_unpacked[plane*plane_bytes+y*bpr+xb];
                for (unsigned bit=0;bit<8;bit++)
                    if (bits&(0x80u>>bit))
                        dst[(yoff+y)*320u + xoff +xb*8u+bit] |= (uint8_t)(1u<<plane);
            }
    if (out) {out->w=(uint16_t)w;out->h=(uint16_t)h;out->masked=record[6];}
    return 1;
}
#endif
