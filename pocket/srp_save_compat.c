 


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include "srp_save_compat.h"
#undef fopen
#undef fread
#undef fwrite
#undef fseek
#undef ftell
#undef fclose
#undef feof
#undef fgetc
#undef fputc
#undef remove

#define SRP_REGION_SIZE 131072UL
#define SRP_PHYSICAL_SIZE (2UL*SRP_REGION_SIZE)
#define SRP_HEADER_SIZE 16UL
#define SRP_MAX_FILE (SRP_REGION_SIZE-SRP_HEADER_SIZE)
#define SRP_OPEN_LIMIT 12
#define SRP_MAGIC "SRPSV126" /* Stable on-disk save header. */

typedef struct {
    FILE *f;
    unsigned logical;
    uint32_t length;
    uint32_t position;
    int writable;
    int changed;
    uint8_t *slot_buf; /* writable handles only; full 256 KiB physical slot */
} SrpOpenFile;
int srp_save_instance_mode=SRP_GAME_UNRESOLVED;
void srp_save_set_instance(int mode){
    srp_save_instance_mode=(mode==SRP_GAME_ORIGINAL||mode==SRP_GAME_SE)
                           ?mode:SRP_GAME_UNRESOLVED;
    printf("[SRP-INSTANCE] save namespace=%s\n",
        srp_save_instance_mode==SRP_GAME_ORIGINAL?"SR":
        srp_save_instance_mode==SRP_GAME_SE?"SE":"UNRESOLVED (saves disabled)");
}
static SrpOpenFile open_files[SRP_OPEN_LIMIT];
static SrpOpenFile *lookup(FILE *f){
    for(unsigned i=0;i<SRP_OPEN_LIMIT;i++)if(f&&open_files[i].f==f)return &open_files[i];
    return NULL;
}
static uint32_t le32(const uint8_t *s){
    return (uint32_t)s[0]|(uint32_t)s[1]<<8|(uint32_t)s[2]<<16|(uint32_t)s[3]<<24;
}
static void put32(uint8_t *s,uint32_t n){
    for(unsigned i=0;i<4;i++)s[i]=(uint8_t)(n>>(i*8));
}
static size_t region_offset(unsigned id){return (id&1u)*SRP_REGION_SIZE;}
static int slot_name(unsigned id, char *dest, size_t max){
     

    return srp_save_io_name(id/2u,dest,max);
}
static int valid_header(const uint8_t *h){
    if(memcmp(h,SRP_MAGIC,8)!=0)return 0;
    uint32_t n=le32(h+8);
    return n<=SRP_MAX_FILE&&le32(h+12)==~n;
}
static void set_header(uint8_t *h,uint32_t n){
    memcpy(h,SRP_MAGIC,8);put32(h+8,n);put32(h+12,~n);
}
static void trace_save(const char *op,unsigned id,uint32_t len,int ok){
    /* Save events are rare. Console logs help distinguish path I/O failures. */
    printf("[SRP-SAVE] %s logical=%u physical=%u bytes=%lu %s\n",
        op,id+1u,id/2u,(unsigned long)len,ok?"OK":"FAILED");
}
FILE *srp_file_open(const char *path,const char *mode){
    int id=srp_save_logical_id(path);
    if(id<0)return fopen(path,mode);
    if(!mode||!mode[0]){errno=EINVAL;return NULL;}
    const int writing=(mode[0]=='w'||mode[0]=='a'||strchr(mode,'+')!=NULL);
    const int trunc=(mode[0]=='w');
    const int append=(mode[0]=='a');
    if(mode[0]!='r'&&mode[0]!='a'&&mode[0]!='w'){errno=EINVAL;return NULL;}
    SrpOpenFile *ctx=NULL;
    for(unsigned i=0;i<SRP_OPEN_LIMIT;i++)if(!open_files[i].f){ctx=&open_files[i];break;}
    if(!ctx){errno=EMFILE;trace_save("open",(unsigned)id,0,0);return NULL;}
    char slot[32];
    if(!slot_name((unsigned)id,slot,sizeof slot)){
        errno=ENODEV;trace_save("unresolved-instance",(unsigned)id,0,0);
        return NULL;
    }
    FILE *f=fopen(slot,"rb");
    uint8_t header[SRP_HEADER_SIZE]={0};
    uint8_t *buf=NULL;
    int valid=0;
    if(writing){
        buf=(uint8_t*)calloc(1,SRP_PHYSICAL_SIZE);
        if(!buf){if(f)fclose(f);errno=ENOMEM;trace_save("alloc",(unsigned)id,0,0);return NULL;}
        if(f){
            /* Read any prior saves, preserving the *other* logical half. */
            if(fseek(f,0,SEEK_SET)==0) (void)fread(buf,1,SRP_PHYSICAL_SIZE,f);
        }
        memcpy(header,buf+region_offset((unsigned)id),SRP_HEADER_SIZE);
    }else if(f){
        if(fseek(f,(long)region_offset((unsigned)id),SEEK_SET)==0){
            (void)fread(header,1,sizeof header,f);
        }
    }
    valid=valid_header(header);
    if(!valid&&!trunc&&mode[0]=='r'){
        if(f)fclose(f);
        free(buf);
        errno=ENOENT;
        static unsigned missing_save_logs=0;
        if(missing_save_logs++ < 20u)
            trace_save("missing",(unsigned)id,0,0);
        return NULL;
    }
    if(!f&&writing){
        /* An uninitialized save slot may have no readable file yet. */
        f=fopen(slot,"wb");
    }
    if(!f){
        free(buf);trace_save("open",(unsigned)id,0,0);
        return NULL;
    }
    *ctx=(SrpOpenFile){f,(unsigned)id,valid?le32(header+8):0,
                      append&&valid?le32(header+8):0,writing,
                      trunc||(!valid&&writing),buf};
    if(trunc){
        /* Prevent stale data from masquerading as a valid new save. */
        memset(buf+region_offset((unsigned)id),0,SRP_REGION_SIZE);
        ctx->length=ctx->position=0;
    }
    return f;
}
size_t srp_file_read(void *ptr,size_t size,size_t count,FILE *f){
    SrpOpenFile *r=lookup(f);
    if(!r)return fread(ptr,size,count,f);
    if(!size||!count)return 0;
    if(size>SIZE_MAX/count){errno=EOVERFLOW;return 0;}
    size_t available=r->length>r->position?(size_t)(r->length-r->position):0;
    size_t items=available/size;
    if(count>items)count=items;
    if(!count)return 0;
    if(r->slot_buf){
        memcpy(ptr,r->slot_buf+region_offset(r->logical)+SRP_HEADER_SIZE+r->position,count*size);
        r->position+=(uint32_t)(count*size);
        return count;
    }
    if(fseek(f,(long)(region_offset(r->logical)+SRP_HEADER_SIZE+r->position),SEEK_SET)!=0)return 0;
    size_t got=fread(ptr,size,count,f);
    r->position+=(uint32_t)(got*size);
    return got;
}
size_t srp_file_write(const void *ptr,size_t size,size_t count,FILE *f){
    SrpOpenFile *r=lookup(f);
    if(!r)return fwrite(ptr,size,count,f);
    if(!r->writable||!r->slot_buf){errno=EBADF;return 0;}
    if(!size||!count)return 0;
    if(size>SIZE_MAX/count){errno=EOVERFLOW;return 0;}
    size_t room=r->position<SRP_MAX_FILE?SRP_MAX_FILE-r->position:0;
    size_t items=room/size;
    if(count>items){count=items;errno=ENOSPC;}
    if(!count)return 0;
    memcpy(r->slot_buf+region_offset(r->logical)+SRP_HEADER_SIZE+r->position,ptr,count*size);
    r->position+=(uint32_t)(count*size);
    if(r->position>r->length)r->length=r->position;
    r->changed=1;
    return count;
}
int srp_file_seek(FILE *f,long offset,int whence){
    SrpOpenFile *r=lookup(f);
    if(!r)return fseek(f,offset,whence);
    int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?(int64_t)r->position:whence==SEEK_END?(int64_t)r->length:-1;
    if(base<0){errno=EINVAL;return -1;}
    int64_t pos=base+(int64_t)offset;
    if(pos<0||(uint64_t)pos>(uint64_t)SRP_MAX_FILE){errno=EINVAL;return -1;}
    r->position=(uint32_t)pos;return 0;
}
long srp_file_tell(FILE *f){SrpOpenFile *r=lookup(f);return r?(long)r->position:ftell(f);}
int srp_file_eof(FILE *f){SrpOpenFile *r=lookup(f);return r?r->position>=r->length:feof(f);}
int srp_file_getc(FILE *f){uint8_t x;return srp_file_read(&x,1,1,f)==1?x:EOF;}
int srp_file_putc(int ch,FILE *f){uint8_t x=(uint8_t)ch;return srp_file_write(&x,1,1,f)==1?ch:EOF;}
int srp_file_close(FILE *f){
    SrpOpenFile *r=lookup(f);
    if(!r)return fclose(f);
    SrpOpenFile local=*r;
    memset(r,0,sizeof *r);
    int rc=fclose(f);
    if(local.changed && local.slot_buf){
        char slot[32];
        if(!slot_name(local.logical,slot,sizeof slot)){
            free(local.slot_buf);
            errno=ENODEV;return EOF;
        }
        if(local.length==UINT32_MAX)memset(local.slot_buf+region_offset(local.logical),0,SRP_HEADER_SIZE);
        else set_header(local.slot_buf+region_offset(local.logical),local.length);
        FILE *out=fopen(slot,"wb");
        if(!out){rc=EOF;}
        else{
            size_t wrote=fwrite(local.slot_buf,1,SRP_PHYSICAL_SIZE,out);
            if(wrote!=SRP_PHYSICAL_SIZE)rc=EOF;
            if(fflush(out)!=0)rc=EOF;
            if(fclose(out)!=0)rc=EOF;
        }
        trace_save("commit",local.logical,local.length,rc==0);
        if(rc==0){

            FILE *check=fopen(slot,"rb");
            unsigned char header[SRP_HEADER_SIZE]={0};
            int verified=0;
            if(check){
                if(fseek(check,(long)region_offset(local.logical),SEEK_SET)==0 &&
                   fread(header,1,sizeof header,check)==sizeof header &&
                   valid_header(header) && le32(header+8)==local.length)
                    verified=1;
                if(fclose(check)!=0)verified=0;
            }
            trace_save("readback",local.logical,local.length,verified);
        }
    }
    free(local.slot_buf);
    return rc;
}
int srp_file_remove(const char *name){
    int id=srp_save_logical_id(name);
    if(id<0)return remove(name);
    /* Preserve other logical half while clearing this file's header. */
    FILE *f=srp_file_open(name,"wb");
    if(!f)return -1;
    SrpOpenFile *r=lookup(f);
    if(!r||!r->slot_buf){srp_file_close(f);return -1;}
    memset(r->slot_buf+region_offset((unsigned)id),0,SRP_HEADER_SIZE);
    /* zero magic instead of making a valid empty save on close */
    r->changed=1;
    /* separate delete marker, so close doesn't restore SRPSV126 */
    r->length=UINT32_MAX;
    return srp_file_close(f);
}
