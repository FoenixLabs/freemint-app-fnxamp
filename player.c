/* Independent MiNT playback process: no AES/VDI calls, even while GUI blocks. */
#include "player.h"
#include "vs1053.h"
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/ioctl.h>
static struct fnx_hw hw;
static struct fnx_status status;
static char paths[FNX_TRACKS][FNX_PATH];
static FILE *song;
static struct fnx_media media;
static unsigned char buffer[8192];
static size_t used,at;
static int count,current_track=-1,volume=75,balance,bass,treble,repeat,shuffle;
static unsigned char visited[FNX_TRACKS];
static uint32_t last_status,last_decode,origin,progress_time;
static volatile sig_atomic_t quit;
static void signal_quit(int sig) { (void)sig;quit=1; }
static void message(const char *s) { snprintf(status.message,sizeof(status.message),"%s",s); }
static void error(const char *s) { status.state=P_ERROR;message(s); }
static size_t more(void *context,unsigned char *out,size_t size)
{
    size_t n=0;(void)context;
    while(song && n<size) {
        size_t take;
        if(at==used) { used=fread(buffer,1,sizeof(buffer),song);at=0;if(!used) break; }
        take=used-at;if(take>size-n) take=size-n;memcpy(out+n,buffer+at,take);at+=take;n+=take;
    }
    return n;
}
static void stop(int eof)
{
    int ok=1;
    if(song) {
        if(hw.ready) ok=fnx_finish(&hw,eof,more,NULL);
        fclose(song);song=NULL;
    }
    used=at=0;status.state=P_STOPPED;status.fifo=0;
    if(!ok) { error(hw.error);fnx_close(&hw); }
    else if(ok==2) message("Decoder recovered with a software reset");
    else message("Stopped");
}
static void controls(void)
{
    if(hw.ready && (!fnx_volume(&hw,volume,balance) || !fnx_tone(&hw,bass,treble))) error(hw.error);
}
static void play(int which)
{
    if(which<0 || which>=count) return;
    stop(0);
    song=fopen(paths[which],"rb");
    if(!song) { error("Cannot open the selected audio file");return; }
    if(!fnx_media_info(song,paths[which],&media)) { fclose(song);song=NULL;error("Invalid or unreadable audio file");return; }
    if(!fnx_start(&hw)) { fclose(song);song=NULL;error(hw.error);return; }
    controls();
    if(status.state==P_ERROR) { fclose(song);song=NULL;fnx_close(&hw);return; }
    current_track=which;visited[which]=1;origin=0;status.index=current_track;status.duration=media.duration;
    status.rate=media.rate;status.bitrate=media.bitrate;status.elapsed=0;
    status.seekable=media.mp3;status.progress=0;
    strcpy(status.title,media.title);strcpy(status.artist,media.artist);strcpy(status.format,media.format);
    (void)fnx_sci_write(&hw,4,0);(void)fnx_sci_write(&hw,4,0);
    status.state=P_PLAYING;message("Playing through VS1053b");
    last_decode=hw.io.now()-500;progress_time=hw.io.now();
}
static void next_song(int direction,int automatic)
{
    int n;
    if(!count) return;
    if(shuffle && count>1) {
        int available[FNX_TRACKS],total=0,i;
        for(i=0;i<count;i++) if(!visited[i] && i!=current_track) available[total++]=i;
        if(!total) {
            if(automatic && !repeat) { stop(1);return; }
            memset(visited,0,sizeof(visited));
            if(current_track>=0) visited[current_track]=1;
            for(i=0;i<count;i++) if(i!=current_track) available[total++]=i;
        }
        n=available[rand()%total];
    }
    else n=current_track+direction;
    if(n>=count) { if(automatic && !repeat) { stop(1);return; }n=0; }
    if(n<0) n=count-1;
    play(n);
}
static void command(const struct fnx_command *c)
{
    switch(c->command) {
    case C_ADD:
        if(count<FNX_TRACKS && memchr(c->path,0,sizeof(c->path)) && *c->path)
            strcpy(paths[count++],c->path);
        break;
    case C_CLEAR:stop(0);memset(visited,0,sizeof(visited));count=0;current_track=-1;memset(&media,0,sizeof(media));status.title[0]=status.artist[0]=0;status.duration=status.elapsed=status.progress=0;break;
    case C_REMOVE:
        if(c->value>=0 && c->value<count) {
            int n=c->value;memset(visited,0,sizeof(visited));if(n==current_track) stop(0);
            memmove(paths[n],paths[n+1],(count-n-1)*sizeof(paths[0]));--count;
            if(current_track>n) --current_track;else if(current_track==n) current_track=-1;
        }break;
    case C_PLAY:
        if(status.state==P_PAUSED && c->value==current_track) { status.state=P_PLAYING;message("Playing through VS1053b"); }
        else play(c->value);break;
    case C_PAUSE:
        if(status.state==P_PLAYING) { status.state=P_PAUSED;message("Paused (decoder buffer drains briefly)"); }
        else if(status.state==P_PAUSED) { status.state=P_PLAYING;message("Playing through VS1053b"); }
        break;
    case C_STOP:stop(0);status.elapsed=status.progress=0;break;
    case C_NEXT:next_song(1,0);break;
    case C_PREV:next_song(-1,0);break;
    case C_LEVEL:volume=c->value;balance=c->extra;controls();break;
    case C_TONE:bass=c->value;treble=c->extra;controls();break;
    case C_MODES:
        if(shuffle!=(c->extra!=0)) { memset(visited,0,sizeof(visited));if(current_track>=0) visited[current_track]=1; }
        repeat=c->value!=0;shuffle=c->extra!=0;break;
    case C_SEEK:
        if(song && media.mp3 && c->value>=0 && c->value<=1000) {
            long offset;int paused=status.state==P_PAUSED;
            stop(0);song=fopen(paths[current_track],"rb");
            if(!song || (offset=fnx_seek(song,&media,c->value))<0) { if(song) fclose(song);song=NULL;error("No MPEG frame found at the requested position");break; }
            if(!fnx_start(&hw)) { fclose(song);song=NULL;error(hw.error);break; }
            controls();origin=(uint32_t)(media.duration/1000)*c->value+(media.duration%1000)*c->value/1000;
            status.elapsed=origin;status.progress=c->value;
            (void)fnx_sci_write(&hw,4,0);(void)fnx_sci_write(&hw,4,0);
            status.state=paused?P_PAUSED:P_PLAYING;progress_time=hw.io.now();message("MP3 seek (approximate for VBR)");
        }break;
    case C_QUIT:quit=1;break;
    }
    status.index=current_track;status.count=count;status.serial++;
}
void fnx_worker(int commands,int events)
{
    struct fnx_command c;size_t got=0;
    signal(SIGTERM,signal_quit);signal(SIGINT,signal_quit);signal(SIGHUP,signal_quit);signal(SIGPIPE,SIG_IGN);
    fnx_target_io(&hw);memset(&status,0,sizeof(status));status.index=-1;status.magic=FNX_MAGIC;
    message("Ready - open an audio file");srand(hw.io.now());
    while(!quit) {
        unsigned messages=0;uint32_t now=hw.io.now();
        while(messages++<16) {
            ssize_t n=read(commands,(unsigned char *)&c+got,sizeof(c)-got);
            if(n==0) { quit=1;break; }
            if(n<0) { if(errno!=EAGAIN && errno!=EINTR) quit=1;break; }
            got+=n;if(got==sizeof(c)) { command(&c);got=0; }
        }
        if(quit) break;
        now=hw.io.now();
        if(status.state==P_PLAYING && song) {
            size_t sent;
            if(at==used) {
                used=fread(buffer,1,sizeof(buffer),song);at=0;
                if(!used) {
                    if(ferror(song)) { stop(0);error("Disk read failed while playing"); }
                    else { stop(1);if(status.state!=P_ERROR) next_song(1,1); }
                }
            }
            if(song && used>at) {
                sent=fnx_feed(&hw,buffer+at,used-at);at+=sent;
                now=hw.io.now();
                if(sent) progress_time=now;
                if(!hw.ready || hw.io.read(VS_STATUS)&0x20) { stop(0);error("Decoder FIFO error; playback stopped"); }
                else if((uint32_t)(now-progress_time)>5000) { stop(0);error("Decoder has stopped accepting audio"); }
            }
        }
        if(hw.ready) {
            status.fifo=(hw.io.read(VS_STATUS)>>16)&8191;
            if((uint32_t)(now-last_decode)>=500) {
                uint16_t time;
                if(fnx_sci_read(&hw,4,&time)) status.elapsed=origin+time;
                last_decode=now;
            }
            if(song && media.end>media.start) {
                long pos=ftell(song)-(long)(used-at)-media.start;
                if(pos<0) pos=0;
                status.progress=(int32_t)((uint64_t)(unsigned long)pos*1000/(unsigned long)(media.end-media.start));
                if(status.progress>1000) status.progress=1000;
            }
        }
        if((uint32_t)(now-last_status)>=100) {
            long room=0;
            /* MiNT atomic pipe_write may sleep even with O_NONBLOCK. There
             * is one writer: check room before writing, drop stale telemetry. */
            if(ioctl(events,FIONWRITE,&room)<0 || room<0) break;
            if(room>=(long)sizeof(status)) {
                ssize_t n=write(events,&status,sizeof(status));
                if(n!=(ssize_t)sizeof(status)) break;
            }
            last_status=now;
        }
        { struct timeval t={0,5000};fd_set fds;FD_ZERO(&fds);FD_SET(commands,&fds);select(commands+1,&fds,NULL,NULL,&t); }
    }
    stop(0);fnx_close(&hw);close(commands);close(events);_exit(0);
}
