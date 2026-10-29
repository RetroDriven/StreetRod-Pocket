#ifndef SR_ENGINE_MEMORY_H
#define SR_ENGINE_MEMORY_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SR_ENGINE_LOAD_SEG 0x1000u
#define SR_ENGINE_DGROUP 0x4E96u
#define SR_ENGINE_IMAGE_SIZE 0x45590u
#define SR_ENGINE_RAM_SIZE 0x100000u
#define SR_ENGINE_MEM_OFFSET(seg,off) ((uint32_t)(seg)*16u+(uint32_t)(off))

typedef struct {
    uint8_t *ram;
    uint32_t image_size;
    uint32_t file_size;
    unsigned relocation_count;
    uint8_t exepack;
    uint8_t loaded;
    char error[96];
} SrEngineMemory;

static uint16_t sr_em16(const uint8_t *p) {return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static void sr_em_put16(uint8_t *p,uint16_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void sr_engine_free(SrEngineMemory *e) {
    free(e->ram); e->ram=NULL;e->loaded=0;
}

static int sr_engine_unpack(uint8_t *buf,size_t src,size_t expanded) {
    if(!src||!expanded)return 0;
    size_t dst=expanded;
    while(src && buf[src-1]==0xFF)src--;
    size_t max_ops=expanded+src+1;
    for(size_t op=0;op<max_ops;op++) {
        if(src<3)return 0;
        uint8_t cmd=buf[--src];
        uint16_t n=(uint16_t)((uint16_t)buf[src-1]<<8|buf[src-2]);
        src-=2;
        if(!n || (size_t)n>dst)return 0;
        if((cmd&0xFE)==0xB0) {
            if(src<1)return 0;
            uint8_t v=buf[--src];
            dst-=n;memset(buf+dst,v,n);
        }else if((cmd&0xFE)==0xB2) {
            if(src<n)return 0;
            src-=n;dst-=n;memmove(buf+dst,buf+src,n);
        }else return 0;
        if(cmd&1)return 1; /* Packed input can retain a prefix unchanged. */
    }
    return 0;
}
static int sr_engine_fail(SrEngineMemory *e,const char *why) {
    snprintf(e->error,sizeof(e->error),"%s",why);e->loaded=0;return 0;
}
static int sr_engine_load(SrEngineMemory *e,const char *name) {
    sr_engine_free(e);
    e->image_size=0;e->file_size=0;e->relocation_count=0;e->exepack=0;
    e->error[0]=0;
    FILE *f=fopen(name,"rb");
    if(!f)return sr_engine_fail(e,"SR.EXE NOT FOUND ON SD CARD");
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return sr_engine_fail(e,"SR.EXE SEEK FAILED");}
    long length=ftell(f);
    if(length<28 || length>1024*1024 || fseek(f,0,SEEK_SET)!=0){fclose(f);return sr_engine_fail(e,"SR.EXE BAD FILE SIZE");}
    uint8_t *file=(uint8_t *)malloc((size_t)length);
    if(!file){fclose(f);return sr_engine_fail(e,"NO RAM FOR DOS EXECUTABLE");}
    int success=(fread(file,1,(size_t)length,f)==(size_t)length);
    fclose(f);
    if(!success){free(file);return sr_engine_fail(e,"SR.EXE READ FAILED");}
    e->file_size=(uint32_t)length;
    if(sr_em16(file)!=0x5A4D){free(file);return sr_engine_fail(e,"NOT A DOS MZ EXECUTABLE");}
    unsigned cblp=sr_em16(file+2),cp=sr_em16(file+4);
    size_t mz_len=(size_t)cp*512u-(cblp?512u-cblp:0u);
    if(!cp || !mz_len || mz_len>(size_t)length){free(file);return sr_engine_fail(e,"INVALID MZ PAGE COUNT");}
    size_t header=(size_t)sr_em16(file+8)*16u;
    unsigned reloc_count=sr_em16(file+6),reloc_off=sr_em16(file+0x18);
    if(header<28 || header>=mz_len) {
        free(file);return sr_engine_fail(e,"INVALID MZ HEADER");
    }
    const uint8_t *body=file+header;
    size_t bodylen=mz_len-header;
    uint16_t cs=sr_em16(file+0x16);
    size_t ep=(size_t)cs*16u;
    size_t sig=0;
    if(ep<=bodylen && ep+18u<=bodylen && memcmp(body+ep+16,"RB",2)==0)sig=ep+16;
    else if(ep<=bodylen && ep+16u<=bodylen && memcmp(body+ep+14,"RB",2)==0)sig=ep+14;
    e->ram=(uint8_t *)calloc(1,SR_ENGINE_RAM_SIZE);
    if(!e->ram){free(file);return sr_engine_fail(e,"NO RAM FOR DOS MEMORY MODEL");}
    uint8_t *unpacked=NULL;
    uint16_t *patch_seg=NULL,*patch_off=NULL;
    unsigned patches=0;
    size_t image_len=0;
    const char *err="INVALID EXEPACK DATA";
    if(sig) {
        e->exepack=1;
        unsigned packed_stub_len=sr_em16(body+ep+6);
        size_t expanded=(size_t)sr_em16(body+ep+12)*16u;
        unsigned skip=(sig==ep+16)?sr_em16(body+ep+14):1;
        if(!skip || !expanded || expanded>SR_ENGINE_IMAGE_SIZE ||
           ep<(size_t)(skip-1)*16u)goto fail;
        if((size_t)packed_stub_len>bodylen-ep)packed_stub_len=(unsigned)(bodylen-ep);
        /* EXEPACK relocation list follows the standard error text in the stub. */
        const char msg[]="Packed file is corrupt";
        size_t start=0,limit=packed_stub_len;
        for(size_t j=0;j+sizeof(msg)-1u<=limit;j++) {
            if(memcmp(body+ep+j,msg,sizeof(msg)-1u)==0){start=j+sizeof(msg)-1u;break;}
        }
        if(!start)goto fail;
        /* First count patches, validate every offset and segment group. */
        size_t at=start;
        for(unsigned group=0;group<16;group++) {
            if(at+2>limit)goto fail;
            unsigned n=sr_em16(body+ep+at);at+=2;
            if(n>65535u-patches || (size_t)n>(limit-at)/2u)goto fail;
            patches+=n;at+=(size_t)n*2u;
        }
        patch_seg=(uint16_t *)malloc((patches+1u)*sizeof(*patch_seg));
        patch_off=(uint16_t *)malloc((patches+1u)*sizeof(*patch_off));
        if(!patch_seg||!patch_off){err="EXEPACK RELOC ALLOCATION";goto fail;}
        at=start;unsigned index=0;
        for(unsigned group=0;group<16;group++) {
            unsigned n=sr_em16(body+ep+at);at+=2;
            for(unsigned k=0;k<n;k++){
                patch_seg[index]=(uint16_t)(group*0x1000u);
                patch_off[index++]=sr_em16(body+ep+at);at+=2;
            }
        }
        size_t packed=ep-(size_t)(skip-1)*16u;
        unpacked=(uint8_t *)calloc(1,packed>expanded?packed:expanded);
        if(!unpacked){err="EXEPACK OUT OF MEMORY";goto fail;}
        memcpy(unpacked,body,packed);
        if(!sr_engine_unpack(unpacked,packed,expanded))goto fail;
        image_len=expanded;
    }else{
        if((size_t)reloc_off+4u*reloc_count>header){err="INVALID MZ RELOC TABLE";goto fail;}
        image_len=bodylen;
        unpacked=(uint8_t *)malloc(image_len);
        if(!unpacked){err="EXE UNPACKED ALLOCATION";goto fail;}
        memcpy(unpacked,body,image_len);
        patches=reloc_count;
        patch_seg=(uint16_t *)malloc((patches+1u)*sizeof(*patch_seg));
        patch_off=(uint16_t *)malloc((patches+1u)*sizeof(*patch_off));
        if(!patch_seg||!patch_off){err="EXE RELOC ALLOCATION";goto fail;}
        for(unsigned i=0;i<patches;i++) {
            patch_off[i]=sr_em16(file+reloc_off+4u*i);
            patch_seg[i]=sr_em16(file+reloc_off+4u*i+2u);
        }
    }
    if(image_len!=SR_ENGINE_IMAGE_SIZE){err="UNEXPECTED STREET ROD IMAGE SIZE";goto fail;}
    if(SR_ENGINE_MEM_OFFSET(SR_ENGINE_LOAD_SEG,0)+image_len>SR_ENGINE_RAM_SIZE){err="EXE MEMORY LIMIT";goto fail;}
    memcpy(e->ram+SR_ENGINE_MEM_OFFSET(SR_ENGINE_LOAD_SEG,0),unpacked,image_len);
    for(unsigned i=0;i<patches;i++) {
        uint32_t at=SR_ENGINE_MEM_OFFSET(SR_ENGINE_LOAD_SEG,0)+SR_ENGINE_MEM_OFFSET(patch_seg[i],patch_off[i]);
        if(at+2u>SR_ENGINE_MEM_OFFSET(SR_ENGINE_LOAD_SEG,0)+image_len)continue;
        uint16_t v=sr_em16(e->ram+at);
        sr_em_put16(e->ram+at,(uint16_t)(v+SR_ENGINE_LOAD_SEG));
    }
    /* These fixed addresses are identifiers used by the original C port. */
    if(memcmp(e->ram+SR_ENGINE_MEM_OFFSET(SR_ENGINE_DGROUP,0x0266),"nomouse",7)!=0 ||
       memcmp(e->ram+SR_ENGINE_MEM_OFFSET(SR_ENGINE_DGROUP,0x4E0A),"hot_data",8)!=0){
        err="SR.EXE BUILD NOT RECOGNIZED";goto fail;
    }
    e->image_size=(uint32_t)image_len;
    e->relocation_count=patches;
    e->loaded=1;
    free(unpacked);free(patch_seg);free(patch_off);free(file);
    return 1;
fail:
    free(unpacked);free(patch_seg);free(patch_off);free(file);
    sr_engine_free(e);
    return sr_engine_fail(e,err);
}
/* 1989 game VGA/EGA mode 0Dh: original register has a 4-bit EGA color. */
static const uint32_t sr_engine_ega_rgb[16]={
    0x000000,0x0000AA,0x00AA00,0x00AAAA,
    0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,
    0x555555,0x5555FF,0x55FF55,0x55FFFF,
    0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF
};
static int sr_engine_get_palette(const SrEngineMemory *e,unsigned offset,uint32_t out[16]){
    if(!e->loaded)return 0;
    uint32_t base=SR_ENGINE_MEM_OFFSET(SR_ENGINE_DGROUP,offset);
    if(base+16u>=SR_ENGINE_RAM_SIZE)return 0;
    for(unsigned i=0;i<16;i++){
        uint8_t reg=e->ram[base+i];
        if(reg>31)return 0;
        out[i]=sr_engine_ega_rgb[reg&15u];
    }
    return 1;
}
#endif
