#ifndef SRP_SAVE_ID_H
#define SRP_SAVE_ID_H
#include <string.h>
#include <stdio.h>
#include <stdint.h>
 

enum { SRP_GAME_UNRESOLVED=0, SRP_GAME_ORIGINAL=1, SRP_GAME_SE=2 };
 

extern int srp_save_instance_mode __attribute__((weak));
void srp_save_set_instance(int mode);
static inline int srp_save_physical_name(unsigned slot, char *out, size_t max){
    if (!out || max==0 || slot>=8u) return 0;
    if (&srp_save_instance_mode == NULL) { out[0]=0; return 0; }
    if (srp_save_instance_mode==SRP_GAME_ORIGINAL)
        return snprintf(out,max,"SR/streetrod_%u.sav",slot)>0;
    if (srp_save_instance_mode==SRP_GAME_SE)
        return snprintf(out,max,"SE/streetrod_se_%u.sav",slot)>0;
    out[0]=0;
    return 0;
}

static inline int srp_save_io_name(unsigned slot,char *out,size_t max){
    if(!out||!max||slot>=8u)return 0;
    if(!srp_save_physical_name(slot,out,max))return 0;
#if defined(__riscv)
    /* SDK documented slot:ID syntax bypasses automatic filename lookup. */
    int n=snprintf(out,max,"slot:%u",10u+slot);
    return n>0&&(size_t)n<max;
#else
    return 1;
#endif
}
 

static inline const char *srp_save_basename(const char *s){
    const char *p=s;
    if(!s)return "";
    for(;*s;s++)if(*s=='/'||*s=='\\'||*s==':')p=s+1;
    return p;
}
static inline int srp_save_equal(const char *a,const char *b){
    while(*a&&*b){
        char x=*a++,y=*b++;
        if(x>='A'&&x<='Z')x+=32;
        if(y>='A'&&y<='Z')y+=32;
        if(x!=y)return 0;
    }
    return *a==*b;
}
static inline int srp_save_logical_id(const char *path){
    const char *s=srp_save_basename(path);
    if(srp_save_equal(s,"hall_dat"))return 15;
    if(strlen(s)<11 || strlen(s)>12)return -1;
    const char *prefix="HOTROD";
    for(unsigned i=0;i<6;i++){
        char c=s[i];if(c>='a'&&c<='z')c-=32;
        if(c!=prefix[i])return -1;
    }
    const char *digits=s+6;
    unsigned n=0,d=0;
    while(*digits>='0'&&*digits<='9'&&d<2){n=n*10u+(*digits++-'0');d++;}
    if(!d || n<1 || n>15 || !srp_save_equal(digits,".SAV"))return -1;
    return (int)n-1;
}
 

static inline int srp_save_has_data(const char *path){
    int id=srp_save_logical_id(path);
    if(id<0)return 0;
    char slot[32];
    if(!srp_save_io_name((unsigned)id/2u,slot,sizeof slot))return 0;
    FILE *f=fopen(slot,"rb");
    if(!f)return 0;
    unsigned char h[16]={0};
    int ok=fseek(f,(long)((id&1)*131072u),SEEK_SET)==0 && fread(h,1,16,f)==16 &&
           memcmp(h,"SRPSV126",8)==0;
    if(ok){
        uint32_t n=0,check=0;
        for(unsigned i=0;i<4;i++){n|=(uint32_t)h[8+i]<<(i*8);check|=(uint32_t)h[12+i]<<(i*8);}
        ok=n<=131056u && check==~n;
    }
    fclose(f);
    return ok;
}
#endif
