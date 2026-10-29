 


#include "platform/ega.h"
#include "host.h"
#include <string.h>
#include <stdint.h>

/* New Pocket-only host methods, deliberately independent of upstream host.h. */
extern void host_set_indexed_frame_source(bool (*callback)(u8 *),int,int);
extern void host_record_indexed_rows(unsigned dirty,unsigned reused);
extern void host_record_indexed_groups(unsigned decoded,unsigned reused);
extern void host_begin_indexed_frame(void);
extern void host_mark_indexed_row(unsigned y);

static u8 planes[4][EGA_PLANE_SIZE];
#ifdef SRP_EGA_TEST_STATS
unsigned srp_ega_test_decoded_groups;
unsigned srp_ega_test_reused_groups;
#endif

static uint32_t pair_spread[256];
static bool pair_spread_ready;
static void prepare_pair_spread(void){
    if(pair_spread_ready)return;
    for(unsigned a=0;a<16;a++)for(unsigned b=0;b<16;b++){
        uint32_t word=0;
        for(unsigned x=0;x<4;x++){
            unsigned pix=((a>>(3u-x))&1u)|(((b>>(3u-x))&1u)<<1);
            word|=(uint32_t)pix<<(x*8u);
        }
        pair_spread[(a<<4)|b]=word;
    }
    pair_spread_ready=true;
}
 

static inline void decode_eight_pixels(u8 p0,u8 p1,u8 p2,u8 p3,
                                       uint32_t *hi,uint32_t *lo){
    uint32_t a=pair_spread[(unsigned)(p0&0xF0u)|(unsigned)(p1>>4)];
    uint32_t b=pair_spread[(unsigned)(p2&0xF0u)|(unsigned)(p3>>4)];
    uint32_t c=pair_spread[((unsigned)(p0&15u)<<4)|(unsigned)(p1&15u)];
    uint32_t d=pair_spread[((unsigned)(p2&15u)<<4)|(unsigned)(p3&15u)];
    *hi=a|(b<<2);
    *lo=c|(d<<2);
}
static u16 start_addr;
static int line_compare = 200;
static bool dirty = true;
static bool force_all = true;
static bool previous_valid = false;

static u8 previous_planes[4][200][EGA_BYTES_PER_LINE];
static u8 palette_reg[17] = {
    0,1,2,3,4,5,6,7,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0
};
static u8 mapped_palette[16];
const u32 ega_palette[16] = {
    0x000000,0x0000AA,0x00AA00,0x00AAAA,0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,
    0x555555,0x5555FF,0x55FF55,0x55FFFF,0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF
};
static bool compose_indexed(u8 *indexed)
{
    if(!dirty)return false;
    dirty=false;
    host_begin_indexed_frame();
    unsigned changed=0, reused=0, decoded_groups=0, reused_groups=0;

    bool decode_all=force_all||!previous_valid;
    for(int i=0;i<16;i++){
        u8 v=palette_reg[i];
        mapped_palette[i]=(u8)((v&7u)|((v>>1)&8u));
    }
    bool palette_identity=true;
    for(unsigned i=0;i<16;i++)if(mapped_palette[i]!=(u8)i)palette_identity=false;
    for(unsigned y=0;y<200;y++){
        u16 row=(y<(unsigned)line_compare)?
            (u16)(start_addr+y*EGA_BYTES_PER_LINE):
            (u16)((y-(unsigned)line_compare)*EGA_BYTES_PER_LINE);
        bool row_changed=force_all||!previous_valid;
        if(!row_changed){
            for(unsigned k=0;k<4;k++){
                /* VGA 64KB plane address wraps; handle last 39 bytes safely. */
                if(row<=EGA_PLANE_SIZE-EGA_BYTES_PER_LINE){
                    if(memcmp(previous_planes[k][y],planes[k]+row,EGA_BYTES_PER_LINE))
                        {row_changed=true;break;}
                } else {
                    for(unsigned b=0;b<EGA_BYTES_PER_LINE;b++)
                        if(previous_planes[k][y][b]!=planes[k][(u16)(row+b)])
                            {row_changed=true;break;}
                    if(row_changed)break;
                }
            }
        }
        if(!row_changed){reused++;continue;}
        changed++;
        host_mark_indexed_row(y);

        bool contiguous=row<=EGA_PLANE_SIZE-EGA_BYTES_PER_LINE;
        u8 *out=indexed+y*320;
        for(unsigned b=0;b<EGA_BYTES_PER_LINE;b++){
            u16 off=(u16)(row+b);
            u8 p0=planes[0][off],p1=planes[1][off],p2=planes[2][off],p3=planes[3][off];

            bool group_changed=decode_all ||
                previous_planes[0][y][b]!=p0 ||
                previous_planes[1][y][b]!=p1 ||
                previous_planes[2][y][b]!=p2 ||
                previous_planes[3][y][b]!=p3;
            if(!group_changed){
                reused_groups++;
                out+=8;
                continue;
            }
            decoded_groups++;
             

            if(!contiguous){
                previous_planes[0][y][b]=p0;
                previous_planes[1][y][b]=p1;
                previous_planes[2][y][b]=p2;
                previous_planes[3][y][b]=p3;
            }
            uint32_t hi,lo;
            decode_eight_pixels(p0,p1,p2,p3,&hi,&lo);
            if(palette_identity){
#if defined(__GNUC__) && defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)

                if((((uintptr_t)out)&3u)==0u){
                    typedef uint32_t srp_word_store __attribute__((__may_alias__));
                    srp_word_store *dst=(srp_word_store *)(void *)out;
                    dst[0]=hi;
                    dst[1]=lo;
                }else{
                    memcpy(out,&hi,4);memcpy(out+4,&lo,4);
                }
#else
                memcpy(out,&hi,4);memcpy(out+4,&lo,4);
#endif
            }else{
                 

                for(unsigned x=0;x<4;x++){
                    out[x]=mapped_palette[(hi>>(8u*x))&15u];
                    out[x+4]=mapped_palette[(lo>>(8u*x))&15u];
                }
            }
            out+=8;
        }
         

        if(contiguous){
            for(unsigned k=0;k<4;k++)
                memcpy(previous_planes[k][y],planes[k]+row,EGA_BYTES_PER_LINE);
        }
    }
    previous_valid=true;
    force_all=false;
    host_record_indexed_rows(changed,reused);
    host_record_indexed_groups(decoded_groups,reused_groups);
#ifdef SRP_EGA_TEST_STATS
    srp_ega_test_decoded_groups=decoded_groups;
    srp_ega_test_reused_groups=reused_groups;
#endif
    return changed!=0;
}
void ega_init(void)
{
    prepare_pair_spread();
    memset(planes,0,sizeof(planes));
    memset(previous_planes,0,sizeof(previous_planes));
    start_addr=0;line_compare=200;dirty=true;
    previous_valid=false;force_all=true;
    static const u8 defaults[17]={
        0,1,2,3,4,5,6,7,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0
    };
    memcpy(palette_reg,defaults,sizeof(palette_reg));
    host_set_indexed_frame_source(compose_indexed,320,200);
}
u8 *ega_plane(int plane){return planes[plane&3];}

void ega_set_start(u16 offset){
    if(offset!=start_addr){start_addr=offset;dirty=true;}
}
void ega_set_line_compare(int line){
    if(line<0)line=0;if(line>200)line=200;
    if(line!=line_compare){line_compare=line;dirty=true;}
}
void ega_touch(void){dirty=true;}
void ega_set_palette_reg(int reg,u8 value){
    if(reg<0||reg>16)return;
    if(palette_reg[reg]!=value){
        palette_reg[reg]=value;
        dirty=true;
        /* Palette registers 0-15 can change the color of every pixel. */
        if(reg<16)force_all=true;
    }
}
u8 ega_get_palette_reg(int reg){return reg>=0&&reg<=16?palette_reg[reg]:0;}
