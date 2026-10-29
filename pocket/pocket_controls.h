/* RetroDriven Street Rod Pocket: independent time-based cursor + DOS input.
 * SPDX-License-Identifier: MIT; original game assets are user supplied.
 */
#ifndef SRP_CONTROLS_H
#define SRP_CONTROLS_H
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t last_us,held_us,accum_us;
    int8_t dirx,diry;
    bool clock_valid;
    uint32_t recovered_stall_polls;
} srp_cursor_state;

static inline void srp_cursor_step(srp_cursor_state *s,uint32_t now,int vx,int vy,bool fine,int *dx,int *dy){
    *dx=0;*dy=0;
    if(!s->clock_valid){s->clock_valid=true;s->last_us=now;}
    uint32_t dt=(uint32_t)(now-s->last_us);
    s->last_us=now;
    if(!vx&&!vy){s->dirx=s->diry=0;s->held_us=s->accum_us=0;return;}
    if(vx!=s->dirx || vy!=s->diry){
        s->dirx=(int8_t)vx;s->diry=(int8_t)vy;
        s->held_us=s->accum_us=0;
        *dx=vx;*dy=vy;
        return;
    }

    if(dt>150000u){dt=36000u;s->recovered_stall_polls++;}
    if(s->held_us<5000000u)s->held_us+=dt;
    uint32_t interval=fine?65000u:(s->held_us>900000u?5500u:(s->held_us>280000u?9000u:18000u));
    if(fine && s->accum_us>interval)s->accum_us=0;
    s->accum_us+=dt;
    unsigned steps=s->accum_us/interval;
    if(steps>6u){steps=6u;s->accum_us=0;}else s->accum_us-=steps*interval;
    *dx=vx*(int)steps;*dy=vy*(int)steps;
}

static inline uint8_t srp_vk_scancode(char ch){
    static const uint8_t letters[26]={
      0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
      0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c
    };
    if(ch>='a'&&ch<='z')ch=(char)(ch-32);
    if(ch>='A'&&ch<='Z')return letters[ch-'A'];
    if(ch>='1'&&ch<='9')return (uint8_t)(0x02+ch-'1');
    if(ch=='0')return 0x0b;
    if(ch==' ')return 0x39;
    if(ch=='-')return 0x0c;
    if(ch=='<')return 0x0e;  
    if(ch=='>')return 0x1c;  
    return 0;
}
/* 10x4 grid, contains alphabet, numbers, space, delete, Enter, hyphen. */
static const char srp_vk_grid[4][11]={
    "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789 <>-"
};
/* Dedicated 3x4 keypad for original DOS NUMERIC text_input() fields. */
static const char srp_vk_numeric_grid[4][4]={"123","456","789","<0>"};

#define SRP_VK_REPEAT_DELAY_US 280000u
#define SRP_VK_REPEAT_INTERVAL_US 85000u
typedef struct {
    int8_t direction;             /* 0 idle, 1 left, 2 right, 3 up, 4 down */
    uint32_t next_repeat_us;
} srp_vk_nav_state;
static inline void srp_vk_nav_reset(srp_vk_nav_state *s){
    s->direction=0;s->next_repeat_us=0;
}
static inline bool srp_vk_nav_step_grid(srp_vk_nav_state *s,uint32_t now,
    bool up,bool down,bool left,bool right,unsigned *col,unsigned *row,
    unsigned columns,unsigned rows){
    int dir=0;
     

    bool h=left!=right,v=up!=down;
    if(h && v && (s->direction==3 || s->direction==4))
        dir=up?3:4;
    else if(h)dir=left?1:2;
    else if(v)dir=up?3:4;
    if(dir==0){srp_vk_nav_reset(s);return false;}
    if(dir!=s->direction){
        s->direction=(int8_t)dir;
        s->next_repeat_us=now+SRP_VK_REPEAT_DELAY_US;
    }else{
        if((int32_t)(now-s->next_repeat_us)<0)return false;
        s->next_repeat_us=now+SRP_VK_REPEAT_INTERVAL_US;
    }
    switch(dir){
        case 1:*col=(*col+columns-1u)%columns;break;
        case 2:*col=(*col+1u)%columns;break;
        case 3:*row=(*row+rows-1u)%rows;break;
        case 4:*row=(*row+1u)%rows;break;
    }
    return true;
}
static inline bool srp_vk_nav_step(srp_vk_nav_state *s,uint32_t now,
    bool up,bool down,bool left,bool right,unsigned *col,unsigned *row){
    return srp_vk_nav_step_grid(s,now,up,down,left,right,col,row,10u,4u);
}

/* Compact 5x7 column glyphs for the keyboard only, including labels. */
static const uint8_t srp_glyphs[36][5]={
 {0x7e,0x11,0x11,0x11,0x7e},  
 {0x7f,0x49,0x49,0x49,0x36},
 {0x3e,0x41,0x41,0x41,0x22},
 {0x7f,0x41,0x41,0x22,0x1c},
 {0x7f,0x49,0x49,0x49,0x41},
 {0x7f,0x09,0x09,0x09,0x01},
 {0x3e,0x41,0x49,0x49,0x7a},
 {0x7f,0x08,0x08,0x08,0x7f},
 {0x00,0x41,0x7f,0x41,0x00},
 {0x20,0x40,0x41,0x3f,0x01},
 {0x7f,0x08,0x14,0x22,0x41},
 {0x7f,0x40,0x40,0x40,0x40},
 {0x7f,0x02,0x0c,0x02,0x7f},
 {0x7f,0x04,0x08,0x10,0x7f},
 {0x3e,0x41,0x41,0x41,0x3e},
 {0x7f,0x09,0x09,0x09,0x06},
 {0x3e,0x41,0x51,0x21,0x5e},
 {0x7f,0x09,0x19,0x29,0x46},
 {0x46,0x49,0x49,0x49,0x31},
 {0x01,0x01,0x7f,0x01,0x01},
 {0x3f,0x40,0x40,0x40,0x3f},
 {0x1f,0x20,0x40,0x20,0x1f},
 {0x3f,0x40,0x38,0x40,0x3f},
 {0x63,0x14,0x08,0x14,0x63},
 {0x07,0x08,0x70,0x08,0x07},
 {0x61,0x51,0x49,0x45,0x43},  
 {0x3e,0x51,0x49,0x45,0x3e},  
 {0x00,0x42,0x7f,0x40,0x00},
 {0x42,0x61,0x51,0x49,0x46},
 {0x21,0x41,0x45,0x4b,0x31},
 {0x18,0x14,0x12,0x7f,0x10},
 {0x27,0x45,0x45,0x45,0x39},
 {0x3c,0x4a,0x49,0x49,0x30},
 {0x01,0x71,0x09,0x05,0x03},
 {0x36,0x49,0x49,0x49,0x36},
 {0x06,0x49,0x49,0x29,0x1e}
};
static inline void srp_draw_char(uint8_t*fb,int x,int y,char c,uint8_t fg){
    const uint8_t *col=0;
    if(c>='A'&&c<='Z')col=srp_glyphs[c-'A'];
    else if(c>='0'&&c<='9')col=srp_glyphs[26+c-'0'];
    if(col){
        for(int xx=0;xx<5;xx++)for(int yy=0;yy<7;yy++)
            if((col[xx]>>yy)&1u){int px=x+xx,py=y+yy;if(px>=0&&px<320&&py>=0&&py<240)fb[py*320+px]=fg;}
    } else if(c=='-')for(int xx=0;xx<5;xx++)fb[(y+3)*320+x+xx]=fg;
    else if(c=='<')for(int i=0;i<4;i++){fb[(y+3-i)*320+x+1+i]=fg;fb[(y+3+i)*320+x+1+i]=fg;}
    else if(c=='>')for(int i=0;i<4;i++){fb[(y+i)*320+x+1+i]=fg;fb[(y+6-i)*320+x+1+i]=fg;}
}
static inline void srp_draw_text(uint8_t *fb,int x,int y,const char *s,uint8_t color){
    for(;*s;s++,x+=6)srp_draw_char(fb,x,y,*s,color);
}

static inline void srp_vk_fill(uint8_t *fb,int x,int y,int w,int h,uint8_t c){
    if(x<0||y<0||w<0||h<0||x+w>320||y+h>240)return;
    for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)fb[yy*320+xx]=c;
}
static inline void srp_vk_key_width(uint8_t *fb,int x,int y,int width,int selected,const char *label){
    const uint8_t top=selected?15:7;
    const uint8_t face=selected?14:6;
    /* Raised key bevel; bright outline only on the selected key. */
    srp_vk_fill(fb,x,y,width,19,0);
    srp_vk_fill(fb,x,y,width-1,18,top);
    srp_vk_fill(fb,x+1,y+1,width-3,16,face);
    srp_vk_fill(fb,x+2,y+2,width-5,14,face);
    int len=0;while(label[len])len++;
    srp_draw_text(fb,x+(width-len*6)/2,y+6,label,selected?0:15);
}
static inline void srp_vk_key(uint8_t *fb,int x,int y,int selected,const char *label){
    srp_vk_key_width(fb,x,y,29,selected,label);
}

static inline void srp_draw_keyboard_mode(uint8_t *fb,int col,int row,const char *typed,bool at_top,bool numeric){
    const int shift=at_top?-106:0;
    srp_vk_fill(fb,0,106+shift,320,134,0);
    srp_vk_fill(fb,3,109+shift,314,130,1);       
    srp_vk_fill(fb,6,112+shift,308,126,8);       
    srp_vk_fill(fb,6,112+shift,308,2,14);        
    srp_draw_text(fb,11,117+shift,numeric?"STREET ROD  -  NUMBER ENTRY":"STREET ROD  -  NAME ENTRY",14);
    srp_vk_fill(fb,11,127+shift,298,13,0);
    srp_vk_fill(fb,12,128+shift,296,11,6);
    srp_vk_fill(fb,13,129+shift,294,9,0);
    srp_draw_text(fb,17,130+shift,numeric?"ASK":"NAME",14);
    if(typed){
        char preview[21];int n=0;
        while(n<20 && typed[n] && typed[n]!='\n' && typed[n]!='\r'){
            char c=typed[n];
            preview[n]=(c>='a'&&c<='z')?(char)(c-32):c;
            n++;
        }
        preview[n]=0;
        srp_draw_text(fb,53,130+shift,preview,15);
        /* Text caret stays visible even if the name field is behind OSK. */
        int caret=53+6*n;if(caret<302)srp_vk_fill(fb,caret,130+shift,1,7,14);
    }else srp_draw_text(fb,53,130+shift,"SELECT A NAME FIELD",15);
    for(int iy=0;iy<4;iy++)for(int ix=0;ix<(numeric?3:10);ix++){
        int x=numeric?23+ix*93:7+ix*31,y=144+shift+iy*20;
        char c=numeric?srp_vk_numeric_grid[iy][ix]:srp_vk_grid[iy][ix];
        const char *caption;
        char single[2]={c,0};
        if(c==' ')caption="SPC";
        else if(c=='<')caption="DEL";
        else if(c=='>')caption="OK";
        else caption=single;
        srp_vk_key_width(fb,x,y,numeric?89:29,ix==col&&iy==row,caption);
    }
    srp_vk_fill(fb,6,227+shift,308,1,6);
    srp_draw_text(fb,10,230+shift,numeric?
        "A DIGIT X DEL START OK B BACK":"A TYPE X DEL START OK B CLOSE",14);
}
/* Keep the original name-overlay API for existing callers and tests. */
static inline void srp_draw_keyboard(uint8_t *fb,int col,int row,const char *typed,bool at_top){
    srp_draw_keyboard_mode(fb,col,row,typed,at_top,false);
}
#endif
