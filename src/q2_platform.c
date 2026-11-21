/* SPDX-License-Identifier: GPL-3.0-or-later
 * Q2 Pod native MIPS framebuffer/input backend; no SDL or libc dependency.
 * Input datagrams follow DiamondBond/q2-pod's card-app protocol.
 * Audio (Q2_GLIBC builds only) goes through ALSA, resolved at runtime
 * with dlopen like q2-atari2600's platform layer, so the binary needs
 * no link-time libasound. If ALSA cannot be opened, the game runs silent.
 */
#ifdef Q2_GLIBC
#define _GNU_SOURCE 1
#include <dlfcn.h>
#include <stdio.h>
#endif
#include "q2_platform.h"
long sc(long n,long a,long b,long c,long d,long e,long f);
#ifndef Q2_GLIBC
/* Clang may lower fixed-size struct initialization to memcpy. */
void *memcpy(void *dest,const void *src,__SIZE_TYPE__ n) {
    unsigned char *d=dest;const unsigned char *s=src;
    while(n--)*d++=*s++;return dest;
}
#endif
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
#ifdef Q2_GLIBC
static unsigned physical_buttons, touch_button, back_at, back_pending;

enum { CONTROL_SIZE=32, CONTROL_X_LEFT=12, CONTROL_X_RIGHT=331 };
static const struct {
    int x, y;
    unsigned key;
    uint8_t glyph[7];
} controls[] = {
    {CONTROL_X_LEFT, 70, Q2_UP,    {8,28,62,8,8,8,8}},
    {CONTROL_X_LEFT, 120,Q2_LEFT,  {0,8,16,62,16,8,0}},
    {CONTROL_X_LEFT, 170,Q2_RIGHT, {0,8,4,62,4,8,0}},
    {CONTROL_X_LEFT, 220,Q2_DOWN,  {8,8,8,8,62,28,8}},
    {CONTROL_X_RIGHT,120,Q2_O,     {0,28,34,34,34,28,0}},
    {CONTROL_X_RIGHT,170,Q2_X,     {0,34,20,8,20,34,0}},
};
#endif

/* --- ALSA audio (Q2_GLIBC only) ---------------------------------------- */
/* Only the entry points the player needs. The numeric constants are the
 * stable ALSA ABI values (SND_PCM_STREAM_PLAYBACK 0, SND_PCM_FORMAT_S16_LE
 * 2, SND_PCM_ACCESS_RW_INTERLEAVED 3, SND_PCM_NONBLOCK 1). */
#ifdef Q2_GLIBC
typedef void snd_pcm_t;
static void *alsa_handle;
static int (*alsa_open_fn)(snd_pcm_t **, const char *, int, int);
static int (*alsa_set_params_fn)(snd_pcm_t *, int, int, unsigned, unsigned, int, unsigned);
static long (*alsa_writei_fn)(snd_pcm_t *, const void *, unsigned long);
static int (*alsa_close_fn)(snd_pcm_t *);
static int (*alsa_drop_fn)(snd_pcm_t *);
static int (*alsa_recover_fn)(snd_pcm_t *, int, int);
static snd_pcm_t *audio_pcm;

static int alsa_try_open(const char *device) {
    if (alsa_open_fn(&audio_pcm, device, 0, 1) != 0) return -1;
    if (alsa_set_params_fn(audio_pcm, 2, 3, 2, Q2_AUDIO_RATE, 1, 68000) != 0) {
        alsa_close_fn(audio_pcm);
        audio_pcm = NULL;
        return -1;
    }
    return 0;
}

int q2_audio_open(void) {
    if (audio_pcm) return 0;
    if (!alsa_handle) {
        alsa_handle = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
        if (!alsa_handle) return -1;
        alsa_open_fn = dlsym(alsa_handle, "snd_pcm_open");
        alsa_set_params_fn = dlsym(alsa_handle, "snd_pcm_set_params");
        alsa_writei_fn = dlsym(alsa_handle, "snd_pcm_writei");
        alsa_close_fn = dlsym(alsa_handle, "snd_pcm_close");
        alsa_drop_fn = dlsym(alsa_handle, "snd_pcm_drop");
        alsa_recover_fn = dlsym(alsa_handle, "snd_pcm_recover");
        if (!alsa_open_fn || !alsa_set_params_fn || !alsa_writei_fn ||
            !alsa_close_fn || !alsa_drop_fn || !alsa_recover_fn) {
            dlclose(alsa_handle);
            alsa_handle = NULL;
            return -1;
        }
    }
    /* plughw first (rate conversion if the hardware cannot do 22050 Hz),
     * "default" as a fallback; both may fail on a device without ALSA. */
    if (alsa_try_open("plughw:0,0") == 0) return 0;
    if (alsa_try_open("default") == 0) return 0;
    fprintf(stderr, "q2-pico8: ALSA open failed, running silent\n");
    return -1;
}

void q2_audio_close(void) {
    if (audio_pcm) {
        alsa_drop_fn(audio_pcm);
        alsa_close_fn(audio_pcm);
        audio_pcm = NULL;
    }
    if (alsa_handle) {
        dlclose(alsa_handle);
        alsa_handle = NULL;
    }
}

int q2_audio_available(void) { return audio_pcm != NULL; }

void q2_audio_write(const int16_t *samples, unsigned frames) {
    if (!audio_pcm || !samples || !frames) return;
    const int16_t *cursor = samples;
    unsigned remaining = frames;
    while (remaining) {
        long written = alsa_writei_fn(audio_pcm, cursor, remaining);
        if (written == -32 || written == -77) { /* -EPIPE / -ESTRPIPE */
            if (alsa_recover_fn(audio_pcm, (int)written, 1) == 0)
                written = alsa_writei_fn(audio_pcm, cursor, remaining);
        }
        if (written == -11 || written == -4) return; /* -EAGAIN / -EINTR: drop */
        if (written <= 0) {
            /* Fatal error: drop the device and keep the game running silent. */
            fprintf(stderr, "q2-pico8: ALSA write failed (%ld), running silent\n", written);
            q2_audio_close();
            return;
        }
        cursor += (unsigned)written * Q2_AUDIO_CHANNELS;
        remaining -= (unsigned)written;
    }
}
#else /* !Q2_GLIBC: freestanding browser preview has no audio device. */
int q2_audio_open(void) { return -1; }
void q2_audio_close(void) {}
int q2_audio_available(void) { return 0; }
void q2_audio_write(const int16_t *samples, unsigned frames) {
    (void)samples;
    (void)frames;
}
#endif /* Q2_GLIBC */

unsigned q2_millis(void) {
    long ts[2]={0};
    sc(SYS_CLOCK_GETTIME,1,(long)ts,0,0,0,0);
    return (unsigned)ts[0]*1000u+(unsigned)ts[1]/1000000u;
}

int q2_open(void) {
    input_state=(q2_input){0};
#ifdef Q2_GLIBC
    physical_buttons=touch_button=back_pending=0;
#endif
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
        } else if(n>=5&&m[0]=='k'&&m[1]==' '&&m[3]==' ') {
            int code=number(m+4);
            if(code==170&&m[2]=='u') {
#ifdef Q2_GLIBC
                unsigned now=q2_millis();
                if(back_pending&&now-back_at<=500u)input_state.quit=1;
                else input_state.pressed|=Q2_PAUSE;
                back_pending=1;
                back_at=now;
#else
                input_state.pressed|=Q2_PAUSE;
#endif
            } else {
                unsigned bit=code==218?Q2_O:0;
#ifdef Q2_GLIBC
                if(code==222||code==172)bit=Q2_LEFT;
                if(code==223||code==173)bit=Q2_RIGHT;
                if(code==171)bit=Q2_X;
                unsigned *held=&physical_buttons;
#else
                unsigned *held=&input_state.buttons;
#endif
                if(m[2]=='d'){
                    input_state.pressed|=bit&~*held;
                    *held|=bit;
                }else if(m[2]=='u')*held&=~bit;
            }
        } else if(m[0]=='t'&&m[1]==' ') {
#ifdef Q2_GLIBC
            if(m[2]=='u'){
                touch_button=0;
            }else if(n>=6&&m[2]=='d'&&m[3]==' '){
                int x=number(m+4);
                const char *y=m+4;
                while(*y&&*y!=' ')++y;
                if(!*y)continue;
                int py=number(y+1);
                touch_button=0;
                for(unsigned i=0;i<sizeof controls/sizeof controls[0];++i){
                    if(x>=controls[i].x&&x<controls[i].x+CONTROL_SIZE&&
                       py>=controls[i].y&&py<controls[i].y+CONTROL_SIZE){
                        touch_button=controls[i].key;
                        input_state.pressed|=touch_button;
                        break;
                    }
                }
            }
#else
            if(m[2]=='d')input_state.pressed|=Q2_O;
#endif
        }
    }
#ifdef Q2_GLIBC
    input_state.buttons=physical_buttons|touch_button;
#endif
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
#ifdef Q2_GLIBC
    if(map){
        for(unsigned i=0;i<sizeof controls/sizeof controls[0];++i){
            unsigned active=input_state.buttons&controls[i].key;
            for(int x=0;x<CONTROL_SIZE;++x){
                q2_pixel *col=dest+(controls[i].x+x)*stride;
                for(int y=0;y<CONTROL_SIZE;++y){
                    unsigned ink=x>=9&&x<23&&y>=9&&y<23&&
                        (controls[i].glyph[(y-9)/2]&(1u<<(6-(x-9)/2)));
                    col[Q2_HEIGHT-1-controls[i].y-y]=ink?0xffeeeeeeu:
                        active?0xff326977u:0xff263247u;
                }
            }
        }
    }
#endif
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
    q2_audio_close();
    if(sock>=0) {sc(SYS_CLOSE,sock,0,0,0,0,0);sock=-1;sc(SYS_UNLINK,(long)SOCKET_PATH,0,0,0,0,0);}
    if(memory) {
        if(visible!=front) {var[5]=front;sc(SYS_IOCTL,fb,FB_PAN,(long)var,0,0,0);}
        sc(SYS_MUNMAP,(long)memory,(long)fix[5],0,0,0,0);memory=0;
    }
    if(fb>=0){sc(SYS_CLOSE,fb,0,0,0,0,0);fb=-1;}
}
