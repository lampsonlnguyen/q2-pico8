/* Host-only Q2 bridge stubs. With Q2_FAKE08_SMOKE defined,
 * run an original .p8 through the real FAKE-08 interpreter and ensure it
 * produces frames before returning. No simulated hardware is shipped. */
#include "q2_platform.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned frame_count, poll_count;
static unsigned audio_frames_written, audio_peak;
#ifdef Q2_FAKE08_SMOKE
int q2_open(void){frame_count=0;poll_count=0;audio_frames_written=0;audio_peak=0;return 0;}
void q2_close(void){
    fprintf(stderr,"FAKE-08 executed %u rendered frames in native VM\n",frame_count);
    fprintf(stderr,"FAKE-08 wrote %u audio frames, peak sample %u\n",
            audio_frames_written,audio_peak);
    if(frame_count==0)exit(9);
    /* The smoke cart plays a sustained sfx, so the synth must have
     * produced real (non-silent) audio through the host audio path. */
    if(audio_frames_written==0||audio_peak==0)exit(9);
}
q2_input q2_read_input(void){
    q2_input in={0};
    if(++poll_count>=12)in.quit=1;
    return in;
}
unsigned q2_millis(void){static unsigned ms;return ms+=34u;}
#else
int q2_open(void){return -1;}
void q2_close(void){}
q2_input q2_read_input(void){q2_input in={0};in.quit=1;return in;}
unsigned q2_millis(void){return 0;}
#endif
void q2_present(const uint8_t *p,const uint32_t colors[16]){(void)p;(void)colors;}
void q2_present_fake08(const uint8_t *p,const uint8_t map[16],
                       const uint32_t colors[144]){
    (void)p;(void)map;(void)colors;
    ++frame_count;
}
/* No ALSA device on the CI host: record what the host tried to play
 * instead of opening a device, so the smoke test can verify the synth
 * produced non-silent audio through the real host audio path. */
int q2_audio_open(void){return -1;}
void q2_audio_close(void){}
int q2_audio_available(void){return 0;}
void q2_audio_write(const int16_t *samples,unsigned frames){
    if(!samples)return;
    for(unsigned i=0;i<frames*2u;++i){
        unsigned v=samples[i]<0?(unsigned)(-(int32_t)samples[i]):(unsigned)samples[i];
        if(v>audio_peak)audio_peak=v;
    }
    audio_frames_written+=frames;
}
long sc(long a,long b,long c,long d,long e,long f,long g){
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;
    return -1;
}
void q2_set_args(int argc,char **argv){(void)argc;(void)argv;}
void q2_set_game_mode(int on){(void)on;}
