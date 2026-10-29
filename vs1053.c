/* Hardware ref rev 1.8 pp98-100; VLSI VS1053b datasheet 1.33 10.5/10.11.
 * No IRQ vectors or global audio mixer settings are changed. GPL-2.0+. */
#include "vs1053.h"
#include <string.h>
static int fail(struct fnx_hw *h,const char *s)
{ strncpy(h->error,s,sizeof(h->error)-1);h->error[sizeof(h->error)-1]=0;return 0; }
static int wait_status(struct fnx_hw *h,uint32_t mask,uint32_t value,uint32_t ms)
{
    uint32_t start=h->io.now();
    do { if((h->io.read(VS_STATUS)&mask)==value) return 1; h->io.yield(); }
    while((uint32_t)(h->io.now()-start)<ms);
    return fail(h,"Decoder did not respond before timeout");
}
int fnx_sci_write(struct fnx_hw *h,unsigned reg,uint16_t value)
{
    if(!h->owned || !wait_status(h,8,0,100)) return 0;
    h->io.write(VS_SCI,((uint32_t)(reg&15)<<16)|value);
    return wait_status(h,4,0,150);
}
int fnx_sci_read(struct fnx_hw *h,unsigned reg,uint16_t *value)
{
    if(!h->owned || !wait_status(h,4,0,150)) return 0;
    h->io.write(VS_SCI,0x01000000UL|((uint32_t)(reg&15)<<16));
    if(!wait_status(h,4,0,150)) return 0;
    *value=h->io.read(VS_SCI)&65535;return 1;
}
static int initialize(struct fnx_hw *h)
{
    uint16_t status,mode,clock;uint32_t stopped;
    h->ready=0;
    h->io.write(VS_CTRL,2); /* Deliberately stop AND flush before 0->1 start. */
    /* Vendor MCP test keeps RUN low for two 60 Hz ticks before restart. */
    stopped=h->io.now();
    do { h->io.yield(); } while((uint32_t)(h->io.now()-stopped)<35);
    h->io.write(VS_CTRL,1); /* Polled operation; LOW_IRQ_EN remains off. */
    if(!wait_status(h,1,1,750) || !fnx_sci_read(h,1,&status) ||
       !fnx_sci_read(h,0,&mode) || !fnx_sci_read(h,3,&clock)) return 0;
    if(((status>>4)&15)!=4 || (mode&0x4800)!=0x4800 || clock!=0x8800)
        return fail(h,"VS1053b not detected (requires FPGA build 70+)");
    h->io.write(VS_STATUS,0x30);h->ready=1;return 1;
}
int fnx_start(struct fnx_hw *h)
{
    if(h->owned) return h->ready ? 1 : initialize(h);
    h->saved=h->io.read(VS_CTRL);
    if(h->saved&1) return fail(h,"Decoder link is already running in another player");
    h->owned=1;
    if(!initialize(h)) { h->io.write(VS_CTRL,h->saved|2);h->owned=0;return 0; }
    return 1;
}
size_t fnx_feed(struct fnx_hw *h,const unsigned char *p,size_t count)
{
    size_t n=0;
    uint32_t room;
    if(!h->ready) return 0;
    room=h->io.read(VS_ROOM)&2047;
    if(room>1024) { fail(h,"Invalid FPGA FIFO room counter");h->ready=0;return 0; }
    while(room && count-n>=4) {
        uint32_t v=((uint32_t)p[n]<<24)|((uint32_t)p[n+1]<<16)|((uint32_t)p[n+2]<<8)|p[n+3];
        h->io.write(VS_DATA,v);n+=4;--room;
    }
    while(room && n<count) { h->io.byte(p[n++]);--room; }
    return n;
}
static int empty(struct fnx_hw *h)
{ return wait_status(h,0x1fff0000UL,0,1500); }
static int send_bytes(struct fnx_hw *h,const unsigned char *p,size_t n)
{
    uint32_t start=h->io.now();
    while(n) {
        size_t sent=fnx_feed(h,p,n);p+=sent;n-=sent;
        if(!h->ready || (uint32_t)(h->io.now()-start)>1500) return fail(h,"Decoder data FIFO stalled");
        if(n) h->io.yield();
    }
    return 1;
}
static int fill(struct fnx_hw *h,unsigned char byte,unsigned count)
{
    unsigned char data[32];memset(data,byte,sizeof(data));
    while(count) { unsigned n=count>32?32:count; if(!send_bytes(h,data,n)) return 0;count-=n; }
    return empty(h);
}
static int end_byte(struct fnx_hw *h,uint16_t *value)
{ return fnx_sci_write(h,7,0x1e06) && fnx_sci_read(h,6,value); }
int fnx_finish(struct fnx_hw *h,int eof,size_t (*more)(void *,unsigned char *,size_t),void *context)
{
    uint16_t mode,byte=0,a,b;
    uint32_t start;
    unsigned sent=0;
    unsigned char data[32];
    if(!h->owned || !h->ready) return 1;
    if(!end_byte(h,&byte)) goto reset;
    if(eof && !fill(h,byte,2052)) goto reset;
    if(!fnx_sci_read(h,0,&mode) || !fnx_sci_write(h,0,mode|8) || !empty(h)) goto reset;
    start=h->io.now();
    do {
        size_t n=0;
        /* On early stop keep sending the remaining encoded file, not zeros. */
        if(!eof && more) n=more(context,data,sizeof(data));
        if(n>sizeof(data)) n=sizeof(data);
        memset(data+n,(unsigned char)byte,sizeof(data)-n);
        if(!send_bytes(h,data,sizeof(data)) || !empty(h) || !fnx_sci_read(h,0,&mode)) goto reset;
        sent+=sizeof(data);
        if(!(mode&8)) break;
    } while(sent<2048 && (uint32_t)(h->io.now()-start)<1000);
    if(mode&8) goto reset;
    if(!eof && (!end_byte(h,&byte) || !fill(h,byte,2052))) goto reset;
    start=h->io.now();
    do {
        if(!fnx_sci_read(h,8,&a) || !fnx_sci_read(h,9,&b)) goto reset;
        if(!a && !b) { h->io.write(VS_CTRL,3);return 1; } /* FLUSH + RUN */
        h->io.yield();
    } while((uint32_t)(h->io.now()-start)<1000);
reset:
    /* Datasheet recovery: software reset via FPGA startup, bounded throughout. */
    if(initialize(h)) { fail(h,"Decoder reset after cancellation timeout");return 2; }
    return 0;
}
int fnx_volume(struct fnx_hw *h,int volume,int balance)
{
    unsigned l,r;
    if(volume<0) volume=0;if(volume>100) volume=100;
    if(balance < -100) balance=-100;if(balance>100) balance=100;
    l=r=volume ? (unsigned)(100-volume)*96/100 : 254;
    if(balance>0) l+=(unsigned)balance*96/100;
    if(balance<0) r+=(unsigned)(-balance)*96/100;
    if(l>254) l=254;if(r>254) r=254;
    return fnx_sci_write(h,11,(l<<8)|r);
}
int fnx_tone(struct fnx_hw *h,int bass,int treble)
{
    if(bass<0) bass=0;if(bass>15) bass=15;
    if(treble < -8) treble=-8;if(treble>7) treble=7;
    return fnx_sci_write(h,2,((treble&15)<<12)|0x0a00|(bass<<4)|6);
}
void fnx_close(struct fnx_hw *h)
{
    if(h->owned) {
        if(h->ready) (void)fnx_sci_write(h,11,0xfefe);
        h->io.write(VS_CTRL,h->saved|2); /* Old RUN was zero; drop pending data. */
        h->owned=h->ready=0;
    }
}
#ifndef FNX_HOST_TEST
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <sys/time.h>
#include <unistd.h>
static uint32_t rd(unsigned r) { return *(volatile uint32_t *)(0xfec22200UL+r); }
static void wr(unsigned r,uint32_t v) { *(volatile uint32_t *)(0xfec22200UL+r)=v; }
static void byte(unsigned char v) { *(volatile unsigned char *)0xfec22210UL=v; }
static uint32_t now(void) { struct timeval t;gettimeofday(&t,0);return (uint32_t)t.tv_sec*1000+t.tv_usec/1000; }
static void yield(void) { usleep(1000); }
static long board_super(void)
{
    const uint32_t *p=*(const uint32_t **)0x5a0;
    if(p) for(;p[0];p+=2) if(p[0]==0x5f4d4348UL) return p[1]==0xf258;
    return 0;
}
int fnx_board(void) { return Supexec(board_super)!=0; }
void fnx_target_io(struct fnx_hw *h)
{ memset(h,0,sizeof(*h));h->io.read=rd;h->io.write=wr;h->io.byte=byte;h->io.now=now;h->io.yield=yield; }
#endif
