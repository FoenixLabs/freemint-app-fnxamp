/* Audible original demo + register evidence. GPL-2.0-or-later. */
#include "vs1053.h"
#include "media.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static struct fnx_hw h;
static FILE *logfile,*song;
static unsigned char data[4096];
static size_t used,at;
static int failures;
static void check(int ok,const char *s)
{ fprintf(logfile,"%s: %s\n",ok?"PASS":"FAIL",s);fflush(logfile);if(!ok) failures++; }
static size_t more(void *arg,unsigned char *p,size_t n)
{ (void)arg;return fread(p,1,n,song); }
int main(int argc,char **argv)
{
    struct fnx_media m;uint16_t value,format=0,time=0;uint32_t start,total=0;int decoded=0;
    const char *path=argc>1?argv[1]:"D:\\MUSIC\\DEMO.MP3";
    logfile=fopen("D:\\APPS\\FNXAMP\\FNXTEST.TXT","w");if(!logfile) return 1;
    if(!fnx_board()) { check(0,"A2560Me cookie");fclose(logfile);return 1; }
    fnx_target_io(&h);check(fnx_start(&h),"VS1053b identity/startup");
    if(!h.ready) { fprintf(logfile,"%s\n",h.error);goto done; }
    check(fnx_volume(&h,60,0) && fnx_sci_read(&h,11,&value) && value==0x2626,"volume register");
    check(fnx_tone(&h,3,2) && fnx_sci_read(&h,2,&value) && value==0x2a36,"bass/treble register");
    song=fopen(path,"rb");if(!song) { check(0,"open demo");goto done; }
    check(fnx_media_info(song,path,&m),"media metadata");
    fprintf(logfile,"File %s, format %s, rate %lu, bytes %ld\n",path,m.format,(unsigned long)m.rate,m.size);
    fnx_sci_write(&h,4,0);fnx_sci_write(&h,4,0);start=h.io.now();
    while((uint32_t)(h.io.now()-start)<15000) {
        size_t n;
        if(at==used) { used=fread(data,1,sizeof(data),song);at=0;if(!used) break; }
        n=fnx_feed(&h,data+at,used-at);at+=n;total+=n;
        if((h.io.now()-start)%250<10) {
            fnx_sci_read(&h,4,&time);fnx_sci_read(&h,9,&format);
            if(time>0 && format) decoded=1;
        }
        h.io.yield();
    }
    check(decoded,"decoder time advances and HDAT1 reports an audio format");
    fprintf(logfile,"Sent %lu bytes, decode_time %u, HDAT1 %04x, status %08lx\n",(unsigned long)total,time,format,(unsigned long)h.io.read(VS_STATUS));
    check(!(h.io.read(VS_STATUS)&0x20),"no FPGA data overflow");
    check(fnx_finish(&h,at==used && feof(song),more,0)!=0,"clean cancellation/end of file");
    check(fnx_sci_read(&h,9,&value) && value==0,"decoder idle after cancellation");
    fclose(song);song=NULL;
 done:
    fnx_close(&h);fprintf(logfile,"RESULT: %d failures\n",failures);fclose(logfile);return failures?1:0;
}
