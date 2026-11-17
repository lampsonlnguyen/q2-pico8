/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cart_browser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static int read_once;
static unsigned write_entry(char *dst,const char *name){
    unsigned n=(unsigned)strlen(name);
    unsigned r=(19+n+1+7)&~7u;
    memset(dst,0,r);
    dst[16]=(char)r;
    dst[17]=(char)(r>>8);
    memcpy(dst+19,name,n+1);
    return r;
}
long sc(long number,long a,long b,long c,long d,long e,long f){
    (void)a;(void)c;(void)d;(void)e;(void)f;
    if(number==4005)return 3;
    if(number==4006)return 0;
    if(number==4219){
        if(read_once++)return 0;
        char *p=(char*)b;
        unsigned n=0;
        n+=write_entry(p+n,"song.mp3");
        n+=write_entry(p+n,"maze.p8");
        n+=write_entry(p+n,"celeste.p8.png");
        n+=write_entry(p+n,".hidden.p8");
        return n;
    }
    return -1;
}
int main(void){
    q2_cart_list list;
    assert(q2_find_carts(&list)==2);
    assert(strcmp(list.names[0],"maze.p8")==0);
    assert(strcmp(list.names[1],"celeste.p8.png")==0);
    char path[128];
    assert(q2_cart_path(&list,path,sizeof path));
    assert(strcmp(path,"/mnt/mmc/.pico8/carts/maze.p8")==0);
    list.selected=1;
    assert(q2_cart_path(&list,path,sizeof path));
    assert(strcmp(path,"/mnt/mmc/.pico8/carts/celeste.p8.png")==0);
    assert(!q2_cart_path(&list,path,8));
    puts("SD-card cartridge scanning and paths OK");
    return 0;
}
