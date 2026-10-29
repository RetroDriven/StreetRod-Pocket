 

#ifndef RETRODRIVEN_POCKET_SDL3_COMPAT_H
#define RETRODRIVEN_POCKET_SDL3_COMPAT_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#define SDL_malloc malloc
#define SDL_calloc calloc
#define SDL_realloc realloc
#define SDL_free free
#define SDL_memcpy memcpy
#define SDL_memset memset
#define SDL_memcmp memcmp
#define SDL_strlen strlen
#define SDL_strcmp strcmp
#define SDL_strcasecmp strcasecmp
#define SDL_snprintf snprintf
#define SDL_arraysize(x) (sizeof(x)/sizeof((x)[0]))
#define SDL_min(a,b) ((a)<(b)?(a):(b))
#define SDL_max(a,b) ((a)>(b)?(a):(b))
#define SDL_clamp(v,lo,hi) ((v)<(lo)?(lo):(v)>(hi)?(hi):(v))
typedef uint64_t Uint64;
typedef uint32_t Uint32;
typedef uint16_t Uint16;
typedef uint8_t Uint8;
typedef int16_t Sint16;
static inline char *SDL_strdup(const char *s) {
    if(!s)return NULL;
    size_t n=strlen(s)+1;char *p=malloc(n);
    if(p)memcpy(p,s,n);
    return p;
}
static inline const char *SDL_GetError(void) { return strerror(errno); }
static inline void *SDL_LoadFile(const char *filename,size_t *datasize) {
    if(datasize)*datasize=0;
    FILE *f=fopen(filename,"rb");
    if(!f)return NULL;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return NULL;}
    long n=ftell(f);
    if(n<0||fseek(f,0,SEEK_SET)!=0){fclose(f);return NULL;}
    unsigned char *p=(unsigned char*)malloc((size_t)n+1u);
    if(!p){fclose(f);return NULL;}
    if(fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}
    fclose(f);
    p[n]=0;
    if(datasize)*datasize=(size_t)n;
    return p;
}
#endif
