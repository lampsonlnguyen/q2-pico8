/* Host-only Q2 bridge stubs. With Q2_FAKE08_SMOKE defined,
 * run an original .p8 through the real FAKE-08 interpreter and ensure it
 * produces frames before returning. No simulated hardware is shipped. */
#include "q2_platform.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned frame_count, poll_count;
#ifdef Q2_FAKE08_SMOKE
int q2_open(void){frame_count=0;poll_count=0;return 0;}
void q2_close(void){
    fprintf(stderr,"FAKE-08 executed %u rendered frames in native VM\n",frame_count);
    if(frame_count==0)exit(9);
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
long sc(long a,long b,long c,long d,long e,long f,long g){
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;
    return -1;
}
