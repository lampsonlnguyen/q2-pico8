/* Host-only bridge stub for linking the real FAKE-08 engine in CI.
 * The device implementation lives in src/q2_platform.c. */
#include "q2_platform.h"
#include <stddef.h>
int q2_open(void){return -1;} /* Exit launcher without an actual framebuffer. */
void q2_close(void){}
q2_input q2_read_input(void){q2_input in={0};in.quit=1;return in;}
void q2_present(const uint8_t *p,const uint32_t colors[16]){(void)p;(void)colors;}
void q2_present_fake08(const uint8_t *p,const uint8_t map[16],
                       const uint32_t colors[144]){
    (void)p;(void)map;(void)colors;
}
unsigned q2_millis(void){return 0;}
long sc(long a,long b,long c,long d,long e,long f,long g){
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;
    return -1;
}
