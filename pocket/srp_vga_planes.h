#ifndef SRP_VGA_PLANES_H
#define SRP_VGA_PLANES_H

#include <stdint.h>
#include <string.h>
#define SRP_VGA_W 320u
#define SRP_VGA_H 200u
#define SRP_VGA_STRIDE 40u
#define SRP_VGA_PLANE_BYTES 65536u

typedef struct {
    uint8_t plane[4][SRP_VGA_PLANE_BYTES];
    uint16_t start_addr;
    uint16_t line_compare;
    uint8_t attr[17];
    uint8_t dirty;
} SrpVgaPlanes;

static void srp_vga_reset(SrpVgaPlanes *v) {
    static const uint8_t regs[17]={0,1,2,3,4,5,6,7,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0};
    memset(v,0,sizeof(*v));
    memcpy(v->attr,regs,sizeof(regs));
    v->line_compare=200;
    v->dirty=1;
}
/* Pack original DOS decoded indexed pixels (0-15) into four real VGA bitplanes. */
static void srp_vga_upload_indices(SrpVgaPlanes *v, const uint8_t *src, unsigned pixels) {
    if (!src || pixels != SRP_VGA_W*SRP_VGA_H) return;
    for (unsigned y=0;y<SRP_VGA_H;y++)
        for (unsigned xbyte=0;xbyte<SRP_VGA_STRIDE;xbyte++) {
            uint8_t bits[4]={0,0,0,0};
            for (unsigned b=0;b<8;b++) {
                uint8_t pixel=(uint8_t)(src[y*SRP_VGA_W+xbyte*8+b] & 15u);
                for (unsigned p=0;p<4;p++) bits[p]=(uint8_t)(bits[p] | ((pixel>>p & 1u)<<(7-b)));
            }
            for (unsigned p=0;p<4;p++)v->plane[p][y*SRP_VGA_STRIDE+xbyte]=bits[p];
        }
    v->dirty=1;
}
/* Attribute registers use 200-line VGA interpretation, preserving EGA color mapping. */
static uint8_t srp_vga_color_from_reg(uint8_t reg) {
    return (uint8_t)((reg & 7u) | ((reg >> 1) & 8u));
}
/* Output palette indices rather than RGB so openfpgaOS owns the palette DAC. */
static void srp_vga_scanout(const SrpVgaPlanes *v, uint8_t *dst, unsigned pixels) {
    if(!dst || pixels != SRP_VGA_W*SRP_VGA_H) return;
    for (unsigned y=0;y<SRP_VGA_H;y++) {
        unsigned row=(y<v->line_compare)?(v->start_addr+y*SRP_VGA_STRIDE):((y-v->line_compare)*SRP_VGA_STRIDE);
        for (unsigned b=0;b<SRP_VGA_STRIDE;b++) {
            unsigned o=(row+b)&0xffffu;
            uint8_t p0=v->plane[0][o],p1=v->plane[1][o],p2=v->plane[2][o],p3=v->plane[3][o];
            for (unsigned bit=0;bit<8;bit++) {
                unsigned k=7u-bit;
                unsigned index=((p0>>k)&1u)|(((p1>>k)&1u)<<1)|(((p2>>k)&1u)<<2)|(((p3>>k)&1u)<<3);
                dst[y*SRP_VGA_W+b*8+bit]=(uint8_t)index;
            }
        }
    }
}
static void srp_vga_set_start(SrpVgaPlanes *v, uint16_t start) {v->start_addr=start;v->dirty=1;}
static void srp_vga_set_split(SrpVgaPlanes *v, unsigned first_from_zero) {
    if(first_from_zero>200)first_from_zero=200;
    v->line_compare=(uint16_t)first_from_zero;v->dirty=1;
}
static void srp_vga_set_attr(SrpVgaPlanes *v, unsigned reg, uint8_t color) {
    if(reg<17){v->attr[reg]=color;v->dirty=1;}
}
#endif
