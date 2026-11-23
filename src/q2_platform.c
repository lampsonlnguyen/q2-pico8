/* SPDX-License-Identifier: GPL-3.0-or-later
 * Q2 Pod native MIPS framebuffer/input backend; no SDL or libc dependency.
 * Input datagrams follow DiamondBond/q2-pod's card-app protocol.
 * Audio (Q2_GLIBC builds only) goes through ALSA, resolved at runtime
 * with dlopen like q2-atari2600's platform layer, so the binary needs
 * no link-time libasound. If ALSA cannot be opened, the game runs silent.
 * NOTE: no libc headers are included here on purpose. CI syntax-checks
 * this file with clang --target=mipsel-linux-gnu and no sysroot, where
 * host headers such as <dlfcn.h>/<stdio.h> do not resolve (they pull in
 * bits/wordsize.h). dlopen/dlsym/dlclose are declared manually below
 * and logging goes through the raw write syscall, as the rest of this
 * file already does for everything else.
 */
#include "q2_platform.h"
#include "q2_input.h"
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
#ifdef Q2_GLIBC
static unsigned gameview;
static int keys=-1;   /* md-gpio-keys, grabbed when the firmware lacks gameview */
#endif
static uint32_t canvas[Q2_WIDTH*Q2_HEIGHT];

/* --- ALSA audio (Q2_GLIBC only) ---------------------------------------- */
/* Only the entry points the player needs. The numeric constants are the
 * stable ALSA ABI values (SND_PCM_STREAM_PLAYBACK 0, SND_PCM_FORMAT_S16_LE
 * 2, SND_PCM_ACCESS_RW_INTERLEAVED 3, SND_PCM_NONBLOCK 1). */
#ifdef Q2_GLIBC
/* Manual declarations: see the note at the top of this file. The values
 * are the stable glibc ABI ones (RTLD_NOW 2, RTLD_LOCAL 0). */
extern void *dlopen(const char *filename, int flags);
extern void *dlsym(void *handle, const char *symbol);
extern int dlclose(void *handle);
#define RTLD_NOW 2
#define RTLD_LOCAL 0

/* Log to stderr (fd 2) through the raw write syscall (MIPS o32: 4004),
 * so audio failures stay visible without any libc header or call. */
static void q2_log(const char *msg) {
    unsigned n = 0;
    while (msg[n]) ++n;
    sc(4004, 2, (long)msg, (long)n, 0, 0, 0);
}

typedef void snd_pcm_t;
static void *alsa_handle;
static int (*alsa_open_fn)(snd_pcm_t **, const char *, int, int);
static int (*alsa_set_params_fn)(snd_pcm_t *, int, int, unsigned, unsigned, int, unsigned);
static long (*alsa_writei_fn)(snd_pcm_t *, const void *, unsigned long);
static int (*alsa_close_fn)(snd_pcm_t *);
static int (*alsa_drop_fn)(snd_pcm_t *);
static int (*alsa_recover_fn)(snd_pcm_t *, int, int);
static snd_pcm_t *audio_pcm;

static const char *arg_device, *arg_volume;
static int audio_jack, soft_volume = 100, audio_mono, audio_swap;
static volatile int term_requested;

static int text_equal(const char *a, const char *b) {
    while (*a && *a == *b) ++a, ++b;
    return *a == *b;
}
static void sleep_ms(unsigned ms) {
    long ts[2] = {(long)(ms / 1000u), (long)(ms % 1000u) * 1000000L};
    sc(4166 /* nanosleep */, (long)ts, 0, 0, 0, 0, 0);
}
/* Reads a small file whole (raw syscalls); returns its length, or -1. */
static long read_small(const char *path, char *buf, unsigned cap) {
    long fd = sc(SYS_OPEN, (long)path, O_CLOEXEC, 0, 0, 0, 0), n = 0, r;
    if (fd < 0) return -1;
    while (n < (long)cap - 1 && (r = sc(4003 /* read */, fd, (long)buf + n, cap - 1 - n, 0, 0, 0)) > 0) n += r;
    sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
    buf[n] = 0;
    return n;
}
static int file_flag(const char *path) { /* /tmp/q2mono, /tmp/q2swap: an int, 1 when on */
    char b[16];
    return read_small(path, b, sizeof b) > 0 && b[0] == '1';
}
/* The Q2's DAC: after the open on the jack its mute and PCM mode are cleared
 * (hciplayer mutes it as it lets go of the PCM), and the mute set again at close. */
static void dac(unsigned long request, int value) {
    long fd = sc(SYS_OPEN, (long)"/dev/shanling_dac", O_RDWR | O_CLOEXEC, 0, 0, 0, 0);
    if (fd < 0) return;
    sc(SYS_IOCTL, fd, (long)request, (long)&value, 0, 0, 0);
    sc(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
}
/* "usb": the card /proc/asound/cards lists as USB-Audio, as plughw:N,0. */
static void usb_device(char *out) {
    char cards[2048];
    const char *dev = "plughw:1,0";
    for (unsigned i = 0; i < 11; i++) out[i] = dev[i];
    if (read_small("/proc/asound/cards", cards, sizeof cards) <= 0) return;
    for (char *line = cards; *line;) {
        char *end = line;
        while (*end && *end != '\n') ++end;
        int usb = 0;
        for (char *c = line; c + 9 <= end; ++c)
            if (c[0]=='U'&&c[1]=='S'&&c[2]=='B'&&c[3]=='-'&&c[4]=='A'&&c[5]=='u'&&c[6]=='d'&&c[7]=='i'&&c[8]=='o') usb = 1;
        const char *d = line;
        while (*d == ' ') ++d;
        if (usb && *d >= '0' && *d <= '9') {
            out[7] = *d; /* plughw:N,0 for a single-digit card */
            return;
        }
        line = *end ? end + 1 : end;
    }
}
static int alsa_try_open(const char *device) {
    if (alsa_open_fn(&audio_pcm, device, 0, 1) != 0) return -1;
    if (alsa_set_params_fn(audio_pcm, 2, 3, 2, Q2_AUDIO_RATE, 1, 68000) != 0) {
        alsa_close_fn(audio_pcm);
        audio_pcm = 0;
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
            alsa_handle = 0;
            return -1;
        }
    }
    /* The firmware's output (argv[1]); none (older firmware, or run by hand) is the jack. */
    char usb[16];
    const char *device = arg_device && *arg_device ? arg_device : "plughw:0,0";
    if (text_equal(device, "usb")) usb_device(usb), device = usb;
    audio_jack = text_equal(device, "plughw:0,0");
    /* argv[2] off the jack: NN is a soft volume the game applies, hNN the DAC's (unity here). */
    soft_volume = 100;
    if (arg_volume && *arg_volume >= '0' && *arg_volume <= '9') {
        int v = 0;
        for (const char *c = arg_volume; *c >= '0' && *c <= '9'; ++c) v = v * 10 + (*c - '0');
        soft_volume = v > 100 ? 100 : v;
    }
    audio_mono = file_flag("/tmp/q2mono");
    audio_swap = file_flag("/tmp/q2swap");
    /* hciplayer lets go of the PCM a moment after Q2 Pod stops it, and bluealsa
     * claims its PCM late: retry for 2 s on the jack, 5 s on Bluetooth and USB. */
    for (int i = 0; i < (audio_jack ? 20 : 50) && !term_requested; ++i) {
        if (alsa_try_open(device) == 0) {
            if (audio_jack) { dac(0xc0044d1bu, 0); dac(0xc0044d1fu, 0); } /* PCM mode, unmute */
            return 0;
        }
        sleep_ms(100);
    }
    q2_log("q2-pico8: ALSA open failed, running silent\n");
    return -1;
}

void q2_audio_close(void) {
    if (audio_pcm) {
        alsa_drop_fn(audio_pcm);
        alsa_close_fn(audio_pcm);
        audio_pcm = 0;
        if (audio_jack) dac(0xc0044d1fu, 1); /* mute again, as Q2 Pod expects */
    }
    if (alsa_handle) {
        dlclose(alsa_handle);
        alsa_handle = 0;
    }
}

int q2_audio_available(void) { return audio_pcm != 0; }

void q2_audio_write(const int16_t *samples, unsigned frames) {
    if (!audio_pcm || !samples || !frames) return;
    static int16_t mixed[2048 * Q2_AUDIO_CHANNELS];
    if (frames > 2048) frames = 2048;
    if (soft_volume != 100 || audio_mono || audio_swap) {
        for (unsigned i = 0; i < frames; i++) {
            int l = samples[2 * i], r = samples[2 * i + 1];
            if (audio_mono) l = r = (l + r) / 2;
            if (audio_swap) { int t = l; l = r; r = t; }
            mixed[2 * i] = (int16_t)(l * soft_volume / 100);
            mixed[2 * i + 1] = (int16_t)(r * soft_volume / 100);
        }
        samples = mixed;
    }
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
            q2_log("q2-pico8: ALSA write failed, running silent\n");
            q2_audio_close();
            return;
        }
        cursor += (unsigned)written * Q2_AUDIO_CHANNELS;
        remaining -= (unsigned)written;
    }
}
/* SIGTERM: Q2 Pod's way to end a game (Return and Centre held with gameview=1,
 * USB storage, an App Store update, power-off). Ending through the normal path
 * closes the PCM (muting the DAC again) and lets FAKE-08 write cart data. */
typedef void (*q2_sighandler)(int);
extern q2_sighandler signal(int, q2_sighandler);
static void on_term(int sig) { (void)sig; term_requested = 1; }
void q2_set_args(int argc, char **argv) {
    arg_device = argc > 1 ? argv[1] : 0;
    arg_volume = argc > 2 ? argv[2] : 0;
    signal(15 /* SIGTERM */, on_term);
    signal(2 /* SIGINT */, on_term);
}
#else /* !Q2_GLIBC: freestanding browser preview has no audio device. */
void q2_set_args(int argc, char **argv) { (void)argc; (void)argv; }
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

#ifdef Q2_GLIBC
/* Pocket Boy's approach on firmware without gameview: grab the Q2's GPIO
 * keys so Return, Play/Pause, Previous, Next and Centre all reach the app.
 * Q2 Pod then no longer sees them (its Return-hold exit included), so the
 * app ends itself on Return + Centre held, as gameview firmware does.
 * Closing the descriptor, or the process ending, releases the grab. */
enum { SYS_READ=4003, O_NONBLOCK_MIPS=0x80,
       EVIOCGNAME_80=0x40504506,  /* _IOC(_IOC_READ,'E',0x06,80) on MIPS */
       EVIOCGRAB_MIPS=(int)0x80044590u /* _IOW('E',0x90,int) on MIPS */ };
static void grab_buttons(void) {
    char path[]="/dev/input/event00";
    for(int i=0;i<32;++i){
        unsigned n=16;
        if(i>=10){path[n++]=(char)('0'+i/10);}
        path[n++]=(char)('0'+i%10);
        path[n]=0;
        long fd=sc(SYS_OPEN,(long)path,O_NONBLOCK_MIPS|O_CLOEXEC,0,0,0,0);
        if(fd<0)continue;
        char name[80];
        for(unsigned j=0;j<sizeof name;++j)name[j]=0;
        if(sc(SYS_IOCTL,fd,EVIOCGNAME_80,(long)name,0,0,0)>=0&&
           text_equal(name,"md-gpio-keys")&&
           sc(SYS_IOCTL,fd,EVIOCGRAB_MIPS,1,0,0,0)==0){
            keys=(int)fd;
            return;
        }
        sc(SYS_CLOSE,fd,0,0,0,0,0);
    }
}
/* md-gpio-keys Linux codes (KEY_UP, KEY_DOWN, KEY_PREVIOUSSONG,
 * KEY_NEXTSONG, KEY_POWER) to Q2 Pod's card-app codes, as in Pocket Boy. */
static int gpio_code(unsigned code) {
    switch(code){
    case 103: return Q2_KEY_RETURN;
    case 108: return Q2_KEY_PLAY;
    case 165: return Q2_KEY_PREVIOUS;
    case 163: return Q2_KEY_NEXT;
    case 116: return Q2_KEY_CENTRE;
    }
    return -1;
}
#endif

/* A missing/unsupported framebuffer previously returned no diagnostic and
 * the launcher could leave a fully black screen. Keep the exact stage in the
 * SD-card pico.log (stderr is redirected there by Logger_Initialize). */
#ifdef Q2_GLIBC
#define Q2_OPEN_ERROR(message) do { q2_log("q2-pico8: " message "\n"); goto error; } while(0)
#else
#define Q2_OPEN_ERROR(message) goto error
#endif
int q2_open(void) {
    q2_input_reset();
#ifdef Q2_GLIBC
    {   /* gameview: firmware that forwards all five buttons and ends the game on
         * Return+Centre held, not on Return held (q2-pod docs/internals.md, Card games). */
        char caps[512];
        gameview=0;
        if(read_small("/tmp/q2-addon.caps",caps,sizeof caps)>0)
            for(char *w=caps;*w;){
                while(*w==' '||*w=='\n')++w;
                char *e=w;while(*e&&*e!=' '&&*e!='\n')++e;
                char c=*e;*e=0;
                if(text_equal(w,"gameview"))gameview=1;
                *e=c;w=e;
            }
    }
#endif
    fb=(int)sc(SYS_OPEN,(long)"/dev/fb0",O_RDWR|O_CLOEXEC,0,0,0,0);
    if(fb<0) Q2_OPEN_ERROR("cannot open /dev/fb0");
    if(sc(SYS_IOCTL,fb,FB_VSCREEN,(long)var,0,0,0) ||
       sc(SYS_IOCTL,fb,FB_FSCREEN,(long)fix,0,0,0))
        Q2_OPEN_ERROR("framebuffer ioctl failed");
    if(var[0]!=Q2_HEIGHT||var[1]!=Q2_WIDTH||var[4]||var[6]!=32||
       var[8]!=16||fix[11]%sizeof(q2_pixel))
        Q2_OPEN_ERROR("unsupported framebuffer geometry or pixel format");
    stride=fix[11]/sizeof(q2_pixel);
    if(stride<Q2_HEIGHT) Q2_OPEN_ERROR("framebuffer stride too short");
    rows=fix[5]/fix[11];
    if(rows>var[3]) rows=var[3];
    front=var[5];visible=front;
    if(rows<Q2_WIDTH||front>rows-Q2_WIDTH)
        Q2_OPEN_ERROR("framebuffer page not addressable");
    long address=sc(SYS_MMAP,0,(long)fix[5],3,1,fb,0);
    if(address<0&&address>=-4095)
        Q2_OPEN_ERROR("framebuffer mmap failed");
    memory=(q2_pixel*)address;
    can_pan=rows>=2*Q2_WIDTH&&(front==0||front==Q2_WIDTH);
    sock=(int)sc(SYS_SOCKET,1,1,0,0,0,0);
    if(sock<0) Q2_OPEN_ERROR("input socket creation failed");
    struct {unsigned short family;char path[108];}addr={1,SOCKET_PATH};
    sc(SYS_UNLINK,(long)SOCKET_PATH,0,0,0,0,0);
    if(sc(SYS_BIND,sock,(long)&addr,sizeof addr,0,0,0))
        Q2_OPEN_ERROR("input socket bind failed");
#ifdef Q2_GLIBC
    if(!gameview&&keys<0)grab_buttons();
    q2_input_set_full(gameview||keys>=0);
    q2_log(gameview?"q2-pico8: buttons from Q2 Pod (gameview)\n":
           keys>=0?"q2-pico8: GPIO buttons grabbed\n":
           "q2-pico8: Q2 Pod 1.8 button fallback\n");
#endif
    return 0;
error:
    q2_close();
    return -1;
}
#undef Q2_OPEN_ERROR

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
static const char *next_field(const char *s) {
    while(*s&&*s!=' ')++s;
    while(*s==' ')++s;
    return s;
}

q2_input q2_read_input(void) {
    char m[64]; long n;
    unsigned now=q2_millis();
#ifdef Q2_GLIBC
    if(term_requested)q2_input_quit();
    struct { long sec, usec; unsigned short type, code; int value; } ev;
    while(keys>=0&&sc(SYS_READ,keys,(long)&ev,sizeof ev,0,0,0)==(long)sizeof ev){
        int code;
        if(ev.type==1&&ev.value>=0&&ev.value<=1&&(code=gpio_code(ev.code))>=0)
            q2_input_key(code,ev.value,now);
    }
#endif
    while(sock>=0&&(n=sc(SYS_RECV,sock,(long)m,sizeof m-1,MSG_DONTWAIT,0,0))>0) {
        m[n]=0;
        if(m[0]=='w'&&m[1]==' ') {
            /* Q2 Pod's travel rises anticlockwise (200 a turn); turned here so
             * clockwise, the Q2's "down/next", is positive and Right. */
            q2_input_wheel(-number(m+2),now);
        } else if(n>=5&&m[0]=='k'&&m[1]==' '&&m[3]==' ') {
#ifdef Q2_GLIBC
            if(keys>=0)continue; /* grabbed: the GPIO device is the source */
#endif
            if(m[2]=='d'||m[2]=='u')q2_input_key(number(m+4),m[2]=='d',now);
        } else if(n>=3&&m[0]=='t'&&m[1]==' ') {
            const char *x=m[2]&&m[3]==' '?m+4:m+3;
            const char *y=next_field(x);
            q2_input_touch(m[2],number(x),*y?number(y):0,now);
        }
    }
    return q2_input_take(now);
}

void q2_set_game_mode(int on) {
    q2_input_set_game(on);
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
    if(map){
        unsigned held=q2_input_held();
        for(unsigned i=0;i<q2_control_count();++i){
            const q2_control *c=q2_control_at(i);
            unsigned active=held&c->key;
            for(int x=0;x<Q2_CONTROL_SIZE;++x){
                q2_pixel *col=dest+(c->x+x)*stride;
                for(int y=0;y<Q2_CONTROL_SIZE;++y){
                    unsigned ink=x>=9&&x<23&&y>=9&&y<23&&
                        (c->glyph[(y-9)/2]&(1u<<(6-(x-9)/2)));
                    col[Q2_HEIGHT-1-c->y-y]=ink?0xffeeeeeeu:
                        active?0xff326977u:0xff263247u;
                }
            }
        }
    }
    if(can_pan){
        var[5]=page;
        if(!sc(SYS_IOCTL,fb,FB_PAN,(long)var,0,0,0))visible=page;
        else {can_pan=0;copy_page(memory+visible*stride,dest);}
    }
}
static void flip(unsigned page,q2_pixel *dest) {
    if(can_pan){
        var[5]=page;
        if(!sc(SYS_IOCTL,fb,FB_PAN,(long)var,0,0,0))visible=page;
        else {can_pan=0;copy_page(memory+visible*stride,dest);}
    }
}
uint32_t *q2_canvas(void) { return canvas; }
void q2_present_canvas(void) {
    if(!memory)return;
    unsigned page=can_pan?(visible?0:Q2_WIDTH):visible;
    q2_pixel *dest=memory+page*stride;
    for(unsigned x=0;x<Q2_WIDTH;++x){
        q2_pixel *col=dest+x*stride+(Q2_HEIGHT-1);
        const uint32_t *src=canvas+x;
        for(unsigned y=0;y<Q2_HEIGHT;++y,src+=Q2_WIDTH)col[-(int)y]=0xff000000u|*src;
    }
    flip(page,dest);
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

#ifdef Q2_GLIBC
enum { SYS_WRITE=4004, SYS_FSYNC=4118, O_WRONLY_MIPS=1, O_APPEND_MIPS=0x8, O_CREAT_MIPS=0x100 };
static void band(q2_pixel *base,unsigned page_row,unsigned pitch,unsigned stage,uint32_t rgb) {
    unsigned top=8+20*stage;
    if(top+16>Q2_HEIGHT)return;
    for(unsigned x=0;x<Q2_WIDTH;++x){
        q2_pixel *col=base+(page_row+x)*pitch;
        for(unsigned y=top;y<top+16;++y)col[Q2_HEIGHT-1-y]=0xff000000u|rgb;
    }
}
void q2_debug_stage(unsigned stage,uint32_t rgb,const char *what) {
    long fd=sc(SYS_OPEN,(long)"/mnt/mmc/.pico8/startup.txt",
               O_WRONLY_MIPS|O_APPEND_MIPS|O_CREAT_MIPS|O_CLOEXEC,0644,0,0,0);
    if(fd>=0){
        char line[160];
        unsigned n=0;
        const char *head="stage ";
        while(*head)line[n++]=*head++;
        if(stage>=10)line[n++]=(char)('0'+stage/10);
        line[n++]=(char)('0'+stage%10);
        line[n++]=':';line[n++]=' ';
        while(*what&&n<sizeof line-1)line[n++]=*what++;
        line[n++]='\n';
        sc(SYS_WRITE,fd,(long)line,n,0,0,0);
        sc(SYS_FSYNC,fd,0,0,0,0,0);
        sc(SYS_CLOSE,fd,0,0,0,0,0);
    }
    if(memory){
        /* Mapped by q2_open: draw on the page being shown. */
        band(memory,visible,stride,stage,rgb);
        return;
    }
    unsigned v[40],f[17];
    long dev=sc(SYS_OPEN,(long)"/dev/fb0",O_RDWR|O_CLOEXEC,0,0,0,0);
    if(dev<0)return;
    if(!sc(SYS_IOCTL,dev,FB_VSCREEN,(long)v,0,0,0)&&!sc(SYS_IOCTL,dev,FB_FSCREEN,(long)f,0,0,0)&&
       v[0]==Q2_HEIGHT&&v[1]==Q2_WIDTH&&v[6]==32&&f[11]>=Q2_HEIGHT*4&&
       (unsigned long)(v[5]+Q2_WIDTH)*f[11]<=f[5]){
        long at=sc(SYS_MMAP,0,(long)f[5],3,1,dev,0);
        if(!(at<0&&at>=-4095)){
            band((q2_pixel*)at,v[5],f[11]/4,stage,rgb);
            sc(SYS_MUNMAP,at,(long)f[5],0,0,0,0);
        }
    }
    sc(SYS_CLOSE,dev,0,0,0,0,0);
}
#else
void q2_debug_stage(unsigned stage,uint32_t rgb,const char *what){(void)stage;(void)rgb;(void)what;}
#endif

void q2_close(void) {
    q2_audio_close();
#ifdef Q2_GLIBC
    if(keys>=0){sc(SYS_CLOSE,keys,0,0,0,0,0);keys=-1;} /* releases the grab */
#endif
    if(sock>=0) {sc(SYS_CLOSE,sock,0,0,0,0,0);sock=-1;sc(SYS_UNLINK,(long)SOCKET_PATH,0,0,0,0,0);}
    if(memory) {
        if(visible!=front) {var[5]=front;sc(SYS_IOCTL,fb,FB_PAN,(long)var,0,0,0);}
        sc(SYS_MUNMAP,(long)memory,(long)fix[5],0,0,0,0);memory=0;
    }
    if(fb>=0){sc(SYS_CLOSE,fb,0,0,0,0,0);fb=-1;}
}
