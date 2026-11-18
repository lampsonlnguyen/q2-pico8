/* SPDX-License-Identifier: GPL-3.0-or-later
 * Q2 Pod native MIPS framebuffer/input backend; no SDL or libc dependency.
 * Input datagrams follow DiamondBond/q2-pod's card-app protocol.
 */
#include "q2_platform.h"
long sc(long n,long a,long b,long c,long d,long e,long f);
/* Clang may lower fixed-size struct initialization to memcpy. */
void *memcpy(void *dest,const void *src,__SIZE_TYPE__ n) {
    unsigned char *d=dest;const unsigned char *s=src;
    while(n--)*d++=*s++;return dest;
}
__asm__(".text\n.set noreorder\n.globl sc\nsc:\n"
        "move $2, $4\nmove $4, $5\nmove $5, $6\nmove $6, $7\n"
        "lw $7, 16($29)\nlw $8, 20($29)\nlw $9, 24($29)\n"
        "addiu $29, $29, -32\nsw $8, 16($29)\nsw $9, 20($29)\n"
        "syscall\naddiu $29, $29, 32\n"
        "beqz $7, 1f\nnop\nsubu $2, $0, $2\n1: jr $31\nnop\n"
#ifndef Q2_GLIBC
        ".globl __start\n__start:\n"
        "lw $4, 0($29)\naddiu $5, $29, 4\nli $8, -8\n"
        "and $29, $29, $8\naddiu $29, $29, -32\n"
        "jal q2_main\nnop\nmove $4, $2\nli $2, 4246\nsyscall\n"
        "1: b 1b\nnop\n"
#endif
        ".set reorder\n");

enum { SYS_CLOSE=4006, SYS_OPEN=4005, SYS_UNLINK=4010, SYS_IOCTL=4054,
       SYS_MMAP=4090, SYS_MUNMAP=4091, SYS_BIND=4169, SYS_RECV=4175,
       SYS_SOCKET=4183, SYS_CLOCK_GETTIME=4263,
       FB_VSCREEN=0x4600, FB_FSCREEN=0x4602, FB_PAN=0x4606,
       O_RDWR=2, O_CLOEXEC=0x80000, MSG_DONTWAIT=0x40 };
#define SOCKET_PATH "/tmp/q2-pico8.input.sock"
static int fb=-1, sock=-1;
static unsigned var[40],fix[17],stride,front,visible,rows,can_pan;
static q2_pixel *memory;
static q2_input input_state;
static unsigned back_releases;

unsigned q2_millis(void) {
    long ts[2]={0};
    sc(SYS_CLOCK_GETTIME,1,(long)ts,0,0,0,0);
    return (unsigned)ts[0]*1000u+(unsigned)ts[1]/1000000u;
}

int q2_open(void) {
    fb=(int)sc(SYS_OPEN,(long)"/dev/fb0",O_RDWR|O_CLOEXEC,0,0,0,0);
    if(fb<0) return -1;
    if(sc(SYS_IOCTL,fb,FB_VSCREEN,(long)var,0,0,0) ||
       sc(SYS_IOCTL,fb,FB_FSCREEN,(long)fix,0,0,0)) goto error;
    if(var[0]!=Q2_HEIGHT||var[1]!=Q2_WIDTH||var[4]||var[6]!=32||
       var[8]!=16||fix[11]%sizeof(q2_pixel)) goto error;
    stride=fix[11]/sizeof(q2_pixel);
    if(stride<Q2_HEIGHT) goto error;
    rows=fix[5]/fix[11];
    if(rows>var[3]) rows=var[3];
    front=var[5];visible=front;
    if(rows<Q2_WIDTH||front>rows-Q2_WIDTH) goto error;
    long address=sc(SYS_MMAP,0,(long)fix[5],3,1,fb,0);
    if(address<0&&address>=-4095) goto error;
    memory=(q2_pixel*)address;
    can_pan=rows>=2*Q2_WIDTH&&(front==0||front==Q2_WIDTH);
    sock=(int)sc(SYS_SOCKET,1,1,0,0,0,0);
    if(sock<0) goto error;
    struct {unsigned short family;char path[108];}addr={1,SOCKET_PATH};
    sc(SYS_UNLINK,(long)SOCKET_PATH,0,0,0,0,0);
    if(sc(SYS_BIND,sock,(long)&addr,sizeof addr,0,0,0)) goto error;
    return 0;
error:
    q2_close();
    return -1;
}

static int number(const char *str) {
    int value=0,neg=0;
    if(*str=='-') {neg=1;++str;}
    if(*str<'0'||*str>'9') return 0;
    while(*str>='0'&&*str<='9') {
        if(value>100000)break;
        value=value*10+*str++-'0';
    }
    return neg?-value:value;
}

q2_input q2_read_input(void) {
    char m[64]; long n;
    input_state.pressed=0;
    input_state.wheel=0;
    while(sock>=0&&(n=sc(SYS_RECV,sock,(long)m,sizeof m-1,MSG_DONTWAIT,0,0))>0) {
        m[n]=0;
        if(m[0]=='w'&&m[1]==' ') {
            int w=number(m+2);
            input_state.wheel+=w;
            /* Wheel is relative steering: synthesize brief D-pad presses. */
            if(w>0)input_state.pressed|=Q2_RIGHT;
            if(w<0)input_state.pressed|=Q2_LEFT;
        } else if(m[0]=='k'&&m[1]==' '&&m[3]==' ') {
            int code=number(m+4);
            if(code==218) {
                if(m[2]=='d') {
                    if(!(input_state.buttons&Q2_O))input_state.pressed|=Q2_O;
                    input_state.buttons|=Q2_O;
                } else if(m[2]=='u')input_state.buttons&=~Q2_O;
            } else if(code==170&&m[2]=='u') {
                /* Back maps to PICO-8 pause/exit; two releases quit player. */
                if(++back_releases>=2)input_state.quit=1;
                else input_state.pressed|=Q2_X;
#ifdef Q2_GLIBC
            } else {
                /* Physical Left/Right and Play controls reach the VM. */
                unsigned bit=(code==222||code==172)?Q2_LEFT:
                             (code==223||code==173)?Q2_RIGHT:
                             code==171?Q2_DOWN:0;
                if(bit&&m[2]=='d'){
                    if(!(input_state.buttons&bit))input_state.pressed|=bit;
                    input_state.buttons|=bit;
                }else if(bit&&m[2]=='u'){
                    if(!(input_state.buttons&bit))input_state.pressed|=bit;
                    input_state.buttons&=~bit;
                }
#endif
            }
        } else if(m[0]=='t'&&m[1]==' '&&m[2]=='d'&&m[3]==' ') {
#ifdef Q2_GLIBC
            /* Touch regions supplement the compact Q2 physical controls:
             * left/right sides steer, top/bottom navigate, center activates. */
            int x=number(m+4);
            const char *y=m+4;
            while(*y&&*y!=' ')++y;
            if(*y==' ')++y;
            int py=number(y);
            if(x>=0&&x<Q2_WIDTH&&py>=0&&py<Q2_HEIGHT){
                unsigned bit=x<95?Q2_LEFT:x>=280?Q2_RIGHT:
                             py<90?Q2_UP:py>=235?Q2_DOWN:Q2_O;
                input_state.pressed|=bit;
            }
#else
            input_state.pressed|=Q2_O;
#endif
        }
    }
    return input_state;
}

static void copy_page(q2_pixel *dst,const q2_pixel *src) {
    for(unsigned x=0;x<Q2_WIDTH;++x)
        for(unsigned y=0;y<Q2_HEIGHT;++y)
            dst[x*stride+y]=src[x*stride+y];
}

/* FAKE-08 passes a 16-entry screen map whose values may address
 * the 128..143 alternate colors. Both video paths retain 2x scaling and
 * the Q2 framebuffer's rotated bottom-up storage. */
static void present_mapped(const uint8_t *pixels,const uint8_t *map,
                           const uint32_t *palette) {
    if(!memory||!pixels||!palette)return;
    unsigned page=can_pan?(visible?0:Q2_WIDTH):visible;
    q2_pixel *dest=memory+page*stride;
    for(unsigned x=0;x<Q2_WIDTH;++x) {
        q2_pixel *col=dest+x*stride;
        int sx=((int)x-(Q2_WIDTH-256)/2)/2;
        for(unsigned y=0;y<Q2_HEIGHT;++y) {
            int sy=((int)y-(Q2_HEIGHT-256)/2)/2;
            uint32_t argb=0xff000000u;
            if(x>=(Q2_WIDTH-256)/2 && x<(Q2_WIDTH-256)/2+256 &&
               y>=(Q2_HEIGHT-256)/2 && y<(Q2_HEIGHT-256)/2+256) {
                unsigned off=(unsigned)sy*64u+(unsigned)sx/2u;
                uint8_t data=pixels[off];
                unsigned color=(sx&1)?(data>>4):(data&15u);
                /* FAKE-08 defines the display palette as 0..15 or 128..143.
                 * Mask with 0x8f exactly as its existing FunKey host does. */
                unsigned mapped=map?(map[color]&0x8fu):color;
                argb=0xff000000u|palette[mapped];
            }
            col[Q2_HEIGHT-1-y]=argb;
        }
    }
    if(can_pan){
        var[5]=page;
        if(!sc(SYS_IOCTL,fb,FB_PAN,(long)var,0,0,0))visible=page;
        else {can_pan=0;copy_page(memory+visible*stride,dest);}
    }
}
void q2_present(const uint8_t *pixels,const uint32_t palette[16]) {
    present_mapped(pixels,0,palette);
}
void q2_present_fake08(const uint8_t *pixels,
                       const uint8_t screen_map[16],
                       const uint32_t colors[144]) {
    if(!screen_map)return;
    present_mapped(pixels,screen_map,colors);
}

void q2_close(void) {
    if(sock>=0) {sc(SYS_CLOSE,sock,0,0,0,0,0);sock=-1;sc(SYS_UNLINK,(long)SOCKET_PATH,0,0,0,0,0);}
    if(memory) {
        if(visible!=front) {var[5]=front;sc(SYS_IOCTL,fb,FB_PAN,(long)var,0,0,0);}
        sc(SYS_MUNMAP,(long)memory,(long)fix[5],0,0,0,0);memory=0;
    }
    if(fb>=0){sc(SYS_CLOSE,fb,0,0,0,0,0);fb=-1;}
}
