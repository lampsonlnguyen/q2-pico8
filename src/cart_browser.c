/* SPDX-License-Identifier: GPL-3.0-or-later
 * Lightweight cartridge index and 128x128 browser with no libc dependencies.
 * The runtime can call q2_cart_path() and pass the selected filename to p8_load().
 */
#include "cart_browser.h"
long sc(long n,long a,long b,long c,long d,long e,long f);
enum { SYS_OPEN=4005, SYS_CLOSE=4006, SYS_GETDENTS64=4219, O_DIRECTORY=0200000 };
static char entries[2048];
static unsigned slen(const char *s,unsigned cap) {
    unsigned n=0;while(n<cap&&s[n])++n;return n;
}
static int ends(const char *s,unsigned n,const char *tail) {
    unsigned t=slen(tail,16);
    if(n<t)return 0;
    for(unsigned j=0;j<t;++j)if(s[n-t+j]!=tail[j])return 0;
    return 1;
}
unsigned q2_find_carts(q2_cart_list *list) {
    if(!list)return 0;
    list->count=0;list->selected=0;
    int fd=(int)sc(SYS_OPEN,(long)Q2_CART_DIR,O_DIRECTORY,0,0,0,0);
    if(fd<0)return 0;
    long n;
    while(list->count<Q2_CART_MAX &&
          (n=sc(SYS_GETDENTS64,fd,(long)entries,sizeof entries,0,0,0))>0) {
        unsigned off=0;
        while(off+20<=(unsigned)n && list->count<Q2_CART_MAX) {
            /* linux_dirent64: ino64(0), off64(8), reclen(16), type(18), name(19) */
            unsigned reclen=(unsigned)(unsigned char)entries[off+16]|
                            ((unsigned)(unsigned char)entries[off+17]<<8);
            if(reclen<20||off+reclen>(unsigned)n)break;
            const char *name=entries+off+19;
            unsigned len=slen(name,reclen-19);
            if(len>0&&len<Q2_CART_NAME_MAX&&len<reclen-19&&name[0]!='.'&&
               (ends(name,len,".p8")||ends(name,len,".p8.png"))) {
                unsigned slot=list->count++;
                for(unsigned i=0;i<=len;++i)list->names[slot][i]=name[i];
            }
            off+=reclen;
        }
    }
    sc(SYS_CLOSE,fd,0,0,0,0,0);
    return list->count;
}
int q2_cart_path(const q2_cart_list *list,char *out,unsigned cap) {
    if(!list||!out||list->selected>=list->count)return 0;
    unsigned prefix=slen(Q2_CART_DIR,256),name=slen(list->names[list->selected],Q2_CART_NAME_MAX);
    if(prefix+1+name+1>cap)return 0;
    for(unsigned i=0;i<prefix;++i)out[i]=Q2_CART_DIR[i];
    out[prefix]='/';
    for(unsigned i=0;i<=name;++i)out[prefix+1+i]=list->names[list->selected][i];
    return 1;
}
static void dot(uint8_t *fb,unsigned x,unsigned y,unsigned color) {
    if(x>=128||y>=128)return;
    unsigned idx=y*64+x/2;
    if(x&1)fb[idx]=(uint8_t)((fb[idx]&15)|(color<<4));
    else fb[idx]=(uint8_t)((fb[idx]&240)|color);
}
static void rect(uint8_t *fb,unsigned x,unsigned y,unsigned w,unsigned h,unsigned c){
    for(unsigned yy=y;yy<y+h&&yy<128;++yy)
        for(unsigned xx=x;xx<x+w&&xx<128;++xx)dot(fb,xx,yy,c);
}
/* Compact 3x5 font: uppercase A-Z, digits, punctuation. */
static const uint8_t font[40][5]={
 {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},
 {7,4,6,4,7},{7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},
 {7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},
 {5,7,7,5,5},{5,7,7,7,5},{2,5,5,5,2},{6,5,6,4,4},
 {2,5,5,7,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
 {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
 {5,5,2,2,2},{7,1,2,4,7},
 {7,5,5,5,7},{2,6,2,2,7},{6,1,7,4,7},{6,1,3,1,6},
 {5,5,7,1,1},{7,4,6,1,6},{3,4,7,5,7},{7,1,1,1,1},
 {7,5,7,5,7},{7,5,7,1,6},
 {0,0,0,0,2},{0,2,0,2,0},{0,0,7,0,0},{0,0,0,0,0}
};
static unsigned glyph(char c){
    if(c>='a'&&c<='z')c=(char)(c-'a'+'A');
    if(c>='A'&&c<='Z')return (unsigned)(c-'A');
    if(c>='0'&&c<='9')return (unsigned)(26+c-'0');
    if(c=='.')return 36;
    if(c==':')return 37;
    if(c=='-')return 38;
    return 39;
}
static void label(uint8_t *fb,unsigned x,unsigned y,const char *s,unsigned col,unsigned max){
    for(unsigned k=0;s[k]&&k<max;++k,x+=4){
        unsigned g=glyph(s[k]);
        for(unsigned yy=0;yy<5;++yy)for(unsigned xx=0;xx<3;++xx)
            if(font[g][yy]&(4u>>xx))dot(fb,x+xx,y+yy,col);
    }
}
void q2_draw_cart_error(uint8_t *screen,const char *name,const char *error){
    unsigned i;
    for(i=0;i<8192;++i)screen[i]=0x11; /* dark navy */
    rect(screen,0,0,128,14,13);
    label(screen,7,5,"LOAD ERROR",7,30);
    if(name)label(screen,7,20,name,7,29);
    /* Wrap the error at 29 chars/line (the 3x5 font is 4 px/char).
     * The font only renders A-Z, 0-9, '.', ':' and '-'; anything else
     * (quotes, parens, apostrophes) renders blank, but the words,
     * line number and "syntax error" stay readable. */
    {
        const char *p=error&&error[0]?error:"SYNTAX ERROR";
        unsigned y=32;
        while(*p&&y+5<104){
            unsigned len=0;
            while(p[len]&&len<29)++len;
            if(p[len]&&p[len]!=' '){
                unsigned cut=len;
                while(cut>8&&p[cut]!=' ')--cut;
                if(cut>8)len=cut;
            }
            {
                char line[30];
                unsigned k;
                for(k=0;k<len;++k)line[k]=p[k];
                line[len]=0;
                label(screen,7,y,line,8,29);
            }
            y+=8;
            p+=len;
            while(*p==' ')++p;
        }
    }
    rect(screen,0,110,128,18,0);
    label(screen,5,113,"CENTER: EXIT",7,30);
    label(screen,5,121,"BACK: EXIT",6,30);
}
void q2_draw_cart_browser(uint8_t *screen,const q2_cart_list *list,int pending){
    for(unsigned i=0;i<8192;++i)screen[i]=0x11; /* dark navy */
    rect(screen,0,0,128,14,13);
    label(screen,7,5,"PICO-8 CARTS",7,30);
    if(!list->count){
        label(screen,7,32,"NO CARTS FOUND",7,28);
        label(screen,7,45,"COPY .P8 FILES",6,28);
        label(screen,7,55,"TO .PICO8/CARTS",6,28);
    }else{
        unsigned first=(list->selected/9)*9;
        for(unsigned n=0;n<9&&first+n<list->count;++n){
            unsigned at=first+n,y=18+n*10;
            if(at==list->selected)rect(screen,3,y-2,122,9,5);
            label(screen,6,y,list->names[at],at==list->selected?7:6,29);
        }
    }
    rect(screen,0,110,128,18,0);
    label(screen,5,113,pending?"RUNTIME NOT READY":"WHEEL: SELECT",7,30);
    label(screen,5,121,"CENTER: OPEN BACK: EXIT",6,30);
}
