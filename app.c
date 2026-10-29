/* FnxAmp: classic GEM player controlling a separate MiNT streaming process.
 * Original UI, no Winamp artwork/code. GPL-2.0-or-later. */
#include <gem.h>
#include <mint/osbind.h>
#include "player.h"
#include "vs1053.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#define WIDTH 544
#define HEIGHT 366
#define CONFIG "D:\\APPS\\FNXAMP\\FNXAMP.INF"
static short window=-1,vh,app,planes;
static GRECT work;
static struct fnx_status status;
static char tracks[FNX_TRACKS][FNX_PATH];
static int count,selected,scroll,volume=75,balance,bass,treble,repeat,shuffle,iconified;
static int tx=-1,rx=-1,lockfd=-1;
static pid_t child=-1;
static struct fnx_command pending[256];static unsigned head,tail;
static unsigned char incoming[sizeof(status)];static size_t incoming_at;
static int running=1,dirty=1,mouse_down;
static char notice[96];
static void enqueue(int type,int value,int extra,const char *path)
{
    struct fnx_command *c;
    if(tail-head>=256) { strcpy(notice,"Command queue full; try again");return; }
    c=&pending[tail++%256];memset(c,0,sizeof(*c));c->command=type;c->value=value;c->extra=extra;
    if(path) snprintf(c->path,sizeof(c->path),"%s",path);
    dirty=1;
}
static void io(void)
{
    while(head!=tail) {
        long room=0;ssize_t n;
        if(ioctl(tx,FIONWRITE,&room)<0 || room<0) { strcpy(notice,"Playback worker disconnected");head=tail;break; }
        if(room<(long)sizeof(pending[0])) break;
        n=write(tx,&pending[head%256],sizeof(pending[0]));
        if(n==(ssize_t)sizeof(pending[0])) ++head;
        else { if(n>=0 || (errno!=EAGAIN && errno!=EINTR)) { strcpy(notice,"Playback worker disconnected");status.state=P_ERROR;head=tail; }break; }
    }
    for(;;) {
        ssize_t n=read(rx,incoming+incoming_at,sizeof(status)-incoming_at);
        if(n>0) { incoming_at+=n;if(incoming_at==sizeof(status)) { struct fnx_status next;memcpy(&next,incoming,sizeof(next));incoming_at=0;
                if(next.magic!=FNX_MAGIC || next.state<0 || next.state>P_ERROR || next.fifo>4096 || next.progress<0 || next.progress>1000 || !memchr(next.title,0,sizeof(next.title)) || !memchr(next.message,0,sizeof(next.message))) {
                    strcpy(notice,"Invalid playback status packet");dirty=1;continue;
                }
                if(next.state!=status.state || next.index!=status.index || next.count!=status.count) dirty=1;
                else {
                    if(next.elapsed!=status.elapsed) dirty|=2;
                    if(strcmp(next.title,status.title) || strcmp(next.artist,status.artist) || strcmp(next.message,status.message)) dirty|=16;
                    if(next.progress!=status.progress) dirty|=4;
                    if(next.fifo/128!=status.fifo/128) dirty|=8;
                }
                memcpy(&status,&next,sizeof(status)); } }
        else { if(n==0) { strcpy(notice,"Playback worker exited");status.state=P_ERROR; }break; }
    }
}
static void box(int x,int y,int w,int h,int color)
{
    short r[4]={work.g_x+x,work.g_y+y,work.g_x+x+w-1,work.g_y+y+h-1};
    if(w<=0 || h<=0) return;
    if(planes==1 && color>1) color=color==8?0:1;
    vsf_interior(vh,FIS_SOLID);vsf_color(vh,color);vsf_perimeter(vh,0);v_bar(vh,r);
}
static void text(int x,int y,int color,const char *s)
{ vswr_mode(vh,MD_TRANS);vst_color(vh,color);v_gtext(vh,work.g_x+x,work.g_y+y+13,s);vswr_mode(vh,MD_REPLACE); }
static void bevel(int x,int y,int w,int h,int down)
{
    box(x,y,w,h,8);box(x,y,w,1,down?1:0);box(x,y,1,h,down?1:0);
    box(x,y+h-1,w,1,down?0:1);box(x+w-1,y,1,h,down?0:1);
}
static void button(int x,int y,int w,const char *label,int down)
{ bevel(x,y,w,22,down);text(x+6,y+3,1,label); }
static const char *basename(const char *p)
{ const char *n=p;for(;*p;p++) if(*p=='/' || *p=='\\') n=p+1;return n; }
static void slider(int x,int y,int width,int value,int max,const char *label)
{
    text(x,y-17,1,label);box(x,y,width,6,9);box(x+value*(width-8)/max,y-3,8,12,8);
    box(x+value*(width-8)/max,y-3,8,1,0);
}
static void paint(void)
{
    char line[128];int i,green=planes==1?0:3;
    box(0,0,WIDTH,HEIGHT,8);
    if(iconified) { text(5,5,1,"FnxAmp");return; }
    bevel(6,6,532,24,0);text(15,10,1,"FnxAmp");text(130,10,1,"A2560Me  /  small-read test");
    box(10,36,524,70,1);
    snprintf(line,sizeof(line),"%02lu:%02lu  /  %02lu:%02lu%s",(unsigned long)status.elapsed/60,(unsigned long)status.elapsed%60,(unsigned long)status.duration/60,(unsigned long)status.duration%60,status.seekable?" ~":"");
    text(20,41,green,line);
    snprintf(line,sizeof(line),"%s   %s   %lu Hz",status.state==P_PLAYING?"PLAY":status.state==P_PAUSED?"PAUSE":status.state==P_ERROR?"ERROR":"STOP",*status.format?status.format:"VS1053b",(unsigned long)status.rate);
    text(245,41,green,line);
    snprintf(line,sizeof(line),"%.62s",*status.title?status.title:"Open a file or add tracks to the playlist");text(20,63,green,line);
    snprintf(line,sizeof(line),"%.61s",*notice?notice:status.state==P_ERROR?status.message:*status.artist?status.artist:status.message);text(20,83,green,line);
    box(14,115,516,8,9);box(14,115,516*status.progress/1000,8,planes==1?1:3);
    button(12,134,42,"|<",0);button(60,134,42,">",status.state==P_PLAYING);
    button(108,134,42,"||",status.state==P_PAUSED);button(156,134,42,"[]",0);button(204,134,42,">|",0);
    button(260,134,76,"Open",0);button(342,134,76,"Add",0);button(424,134,106,"Load M3U",0);
    snprintf(line,sizeof(line),"Volume %d",volume);slider(14,180,140,volume,100,line);
    snprintf(line,sizeof(line),"Balance %+d",balance);slider(172,180,112,balance+100,200,line);
    snprintf(line,sizeof(line),"Bass %d dB",bass);slider(302,180,104,bass,15,line);
    snprintf(line,sizeof(line),"Treble %+d",treble);slider(424,180,104,treble+8,15,line);
    bevel(10,202,524,118,1);box(12,204,520,114,1);
    for(i=0;i<6 && scroll+i<count;i++) {
        int n=scroll+i;
        if(n==selected) box(13,205+i*18,510,18,planes==1?0:4);
        snprintf(line,sizeof(line),"%c %3d  %.55s",n==status.index && status.state!=P_STOPPED?'>':' ',n+1,basename(tracks[n]));
        text(16,205+i*18,planes==1 && n==selected?1:green,line);
    }
    button(12,328,64,"Up",0);button(82,328,64,"Down",0);button(152,328,76,"Remove",0);
    button(234,328,64,"Save",0);button(304,328,82,"Repeat",repeat);button(392,328,82,"Shuffle",shuffle);
    snprintf(line,sizeof(line),"%d",count);text(487,332,1,line);
    text(14,347,1,"FIFO");box(64,358,466,4,9);box(64,358,466*(status.fifo>4096?4096:status.fifo)/4096,4,planes==1?1:3);
}
static int intersect(GRECT *a,const GRECT *b)
{
    int x=a->g_x>b->g_x?a->g_x:b->g_x,y=a->g_y>b->g_y?a->g_y:b->g_y;
    int r=a->g_x+a->g_w<b->g_x+b->g_w?a->g_x+a->g_w:b->g_x+b->g_w;
    int d=a->g_y+a->g_h<b->g_y+b->g_h?a->g_y+a->g_h:b->g_y+b->g_h;
    a->g_x=x;a->g_y=y;a->g_w=r-x;a->g_h=d-y;return a->g_w>0 && a->g_h>0;
}
static void redraw(const GRECT *requested)
{
    GRECT r;short clip[4];
    wind_update(BEG_UPDATE);graf_mouse(M_OFF,NULL);
    wind_get(window,WF_WORKXYWH,&work.g_x,&work.g_y,&work.g_w,&work.g_h);
    wind_get(window,WF_FIRSTXYWH,&r.g_x,&r.g_y,&r.g_w,&r.g_h);
    while(r.g_w && r.g_h) {
        if(intersect(&r,requested)) {
            clip[0]=r.g_x;clip[1]=r.g_y;clip[2]=r.g_x+r.g_w-1;clip[3]=r.g_y+r.g_h-1;
            vs_clip(vh,1,clip);vswr_mode(vh,MD_REPLACE);paint();
        }
        wind_get(window,WF_NEXTXYWH,&r.g_x,&r.g_y,&r.g_w,&r.g_h);
    }
    vs_clip(vh,0,clip);graf_mouse(M_ON,NULL);wind_update(END_UPDATE);dirty=0;
}
static int choose(char out[FNX_PATH],const char *filter,const char *title)
{
    char path[FNX_PATH]="D:\\MUSIC\\",name[FNX_PATH]="";short ok=0;char *p;
    strcat(path,filter);
    if(!fsel_exinput(path,name,&ok,title) || !ok) return 0;
    p=strrchr(path,'\\');if(!p) p=strrchr(path,'/');
    if(!p || (size_t)(p-path+1)+strlen(name)>=FNX_PATH) { strcpy(notice,"Path is too long");return 0; }
    strcpy(p+1,name);strcpy(out,path);return 1;
}
static void add(const char *path)
{
    if(count>=FNX_TRACKS || strlen(path)>=FNX_PATH || strchr(path,'\n') || strchr(path,'\r')) { strcpy(notice,"Playlist full or invalid pathname");return; }
    strcpy(tracks[count++],path);enqueue(C_ADD,0,0,path);
}
static void open_file(int append)
{
    char path[FNX_PATH];
    if(!choose(path,"*.MP3",append?"Add audio file (change filter for WAV/OGG)":"Open audio file (change filter for WAV/OGG)")) return;
    if(!append) { count=0;selected=scroll=0;enqueue(C_CLEAR,0,0,NULL); }
    add(path);if(!append) enqueue(C_PLAY,0,0,NULL);notice[0]=0;dirty=1;
}
static void load_playlist(void)
{
    char path[FNX_PATH],line[1024],entry[FNX_PATH];FILE *f;
    if(!choose(path,"*.M3U","Load local M3U playlist")) return;
    f=fopen(path,"rb");if(!f) { strcpy(notice,"Cannot open playlist");return; }
    count=0;selected=scroll=0;enqueue(C_CLEAR,0,0,NULL);
    while(fgets(line,sizeof(line),f) && count<FNX_TRACKS) {
        if(!strchr(line,'\n') && !feof(f)) { int c;while((c=fgetc(f))!=EOF && c!='\n'){}continue; }
        if(fnx_m3u_path(path,line,entry)) add(entry);
    }
    fclose(f);if(count) enqueue(C_PLAY,0,0,NULL);dirty=1;
}
static void save_playlist(void)
{
    char path[FNX_PATH];FILE *f;int i,ok=1;
    if(!choose(path,"*.M3U","Save local M3U playlist")) return;
    f=fopen(path,"rb");if(f) { fclose(f);if(form_alert(2,"[2][Replace existing playlist?][Replace|Cancel]")!=1) return; }
    f=fopen(path,"wb");if(!f) { strcpy(notice,"Cannot save playlist");return; }
    if(fputs("#EXTM3U\r\n",f)==EOF) ok=0;
    for(i=0;i<count;i++) if(fprintf(f,"%s\r\n",tracks[i])<0) ok=0;
    if(fclose(f)) ok=0;strcpy(notice,ok?"Playlist saved":"Playlist write failed");dirty=1;
}
static void remove_selected(void)
{
    if(selected<0 || selected>=count) return;
    enqueue(C_REMOVE,selected,0,NULL);memmove(tracks[selected],tracks[selected+1],(count-selected-1)*sizeof(tracks[0]));--count;
    if(selected>=count) selected=count?count-1:0;if(scroll>selected) scroll=selected;dirty=1;
}
static void click(int x,int y)
{
    x-=work.g_x;y-=work.g_y;
    if(iconified || wind_find(x+work.g_x,y+work.g_y)!=window) return;
    if(y>=115 && y<=125 && x>=14 && x<=530 && status.seekable) enqueue(C_SEEK,(x-14)*1000/516,0,NULL);
    if(y>=134 && y<156) {
        if(x>=12 && x<54) enqueue(C_PREV,0,0,NULL);
        else if(x>=60 && x<102) enqueue(C_PLAY,selected,0,NULL);
        else if(x>=108 && x<150) enqueue(C_PAUSE,0,0,NULL);
        else if(x>=156 && x<198) enqueue(C_STOP,0,0,NULL);
        else if(x>=204 && x<246) enqueue(C_NEXT,0,0,NULL);
        else if(x>=260 && x<336) open_file(0);
        else if(x>=342 && x<418) open_file(1);
        else if(x>=424 && x<530) load_playlist();
    }
    if(y>=174 && y<=190) {
        if(x>=14 && x<=154) { volume=(x-14)*100/140;enqueue(C_LEVEL,volume,balance,NULL); }
        else if(x>=172 && x<=284) { balance=(x-172)*200/112-100;enqueue(C_LEVEL,volume,balance,NULL); }
        else if(x>=302 && x<=406) { bass=(x-302)*15/104;enqueue(C_TONE,bass,treble,NULL); }
        else if(x>=424 && x<=528) { treble=(x-424)*15/104-8;enqueue(C_TONE,bass,treble,NULL); }
    }
    if(y>=205 && y<313 && x>=12 && x<532 && scroll+(y-205)/18<count) selected=scroll+(y-205)/18;
    if(y>=328 && y<350) {
        if(x>=12 && x<76) { if(scroll>0) --scroll; }
        else if(x>=82 && x<146) { if(scroll+6<count) ++scroll; }
        else if(x>=152 && x<228) remove_selected();
        else if(x>=234 && x<298) save_playlist();
        else if(x>=304 && x<386) { repeat=!repeat;enqueue(C_MODES,repeat,shuffle,NULL); }
        else if(x>=392 && x<474) { shuffle=!shuffle;enqueue(C_MODES,repeat,shuffle,NULL); }
    }
    dirty=1;
}
static void settings(int save)
{
    FILE *f;
    if(save) {
        f=fopen(CONFIG ".TMP","wb");if(!f) return;
        if(fprintf(f,"FnxAmp 1\n%d %d %d %d %d %d\n",volume,balance,bass,treble,repeat,shuffle)>0 && !ferror(f)) {
            if(!fclose(f)) { rename(CONFIG ".TMP",CONFIG);return; }
        } else fclose(f);
        remove(CONFIG ".TMP");
    } else {
        char line[32];int v,b,lo,hi,r,s;
        f=fopen(CONFIG,"rb");if(!f) return;
        if(fgets(line,sizeof(line),f) && !strcmp(line,"FnxAmp 1\n") && fscanf(f,"%d %d %d %d %d %d",&v,&b,&lo,&hi,&r,&s)==6 && v>=0 && v<=100 && b>=-100 && b<=100 && lo>=0 && lo<=15 && hi>=-8 && hi<=7) {
            volume=v;balance=b;bass=lo;treble=hi;repeat=!!r;shuffle=!!s;
        }fclose(f);
    }
}
int main(int argc,char **argv)
{
    short input[11],out[57],ext[57],i,mx,my,buttons,keys,key,clicks,msg[8];
    short ox,oy,ow,oh,dx,dy,dw,dh;
    int commands[2],events[2];struct flock lock;
    signal(SIGPIPE,SIG_IGN);app=appl_init();if(app<0) return 1;
    menu_register(app,"  FnxAmp");
    if(!fnx_board()) { form_alert(1,"[1][FnxAmp requires an A2560Me.|The VS1053b link needs FPGA build 70+.][OK]");appl_exit();return 1; }
    lockfd=open("D:\\APPS\\FNXAMP\\FNXAMP.LCK",O_RDWR|O_CREAT,0600);
    memset(&lock,0,sizeof(lock));lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
    if(lockfd<0 || fcntl(lockfd,F_SETLK,&lock)<0) { form_alert(1,"[1][FnxAmp is already running or|its application directory is read-only.][OK]");if(lockfd>=0) close(lockfd);appl_exit();return 1; }
    if(pipe(commands) || pipe(events)) { form_alert(1,"[1][Cannot create playback pipes.][OK]");close(lockfd);appl_exit();return 1; }
    /* Commands and status snapshots fit POSIX's 512-byte minimum PIPE_BUF. */
    if(sizeof(struct fnx_command)>512 || sizeof(status)>512) { appl_exit();return 1; }
    for(i=0;i<2;i++) { fcntl(commands[i],F_SETFL,O_NONBLOCK);fcntl(events[i],F_SETFL,O_NONBLOCK); }
    child=fork();
    if(child==0) { close(commands[1]);close(events[0]);close(lockfd);fnx_worker(commands[0],events[1]);_exit(1); }
    close(commands[0]);close(events[1]);tx=commands[1];rx=events[0];
    if(child<0) { form_alert(1,"[1][Cannot start playback worker.][OK]");close(tx);close(rx);close(lockfd);appl_exit();return 1; }
    settings(0);enqueue(C_LEVEL,volume,balance,NULL);enqueue(C_TONE,bass,treble,NULL);enqueue(C_MODES,repeat,shuffle,NULL);
    vh=graf_handle(&i,&i,&i,&i);for(i=0;i<10;i++) input[i]=1;input[10]=2;v_opnvwk(input,&vh,out);vq_extnd(vh,1,ext);planes=ext[4];
    status.index=-1;strcpy(status.message,"Ready - open an audio file");
    wind_get(0,WF_WORKXYWH,&dx,&dy,&dw,&dh);
    wind_calc(WC_BORDER,NAME|CLOSER|MOVER|SMALLER,0,0,WIDTH,HEIGHT,&ox,&oy,&ow,&oh);
    ox=dx+(dw-ow)/2;oy=dy+(dh-oh)/2;
    window=wind_create(NAME|CLOSER|MOVER|SMALLER,dx,dy,dw,dh);
    if(window<0) running=0;
    else { wind_set_str(window,WF_NAME,"FnxAmp  -  hardware audio player");wind_open(window,ox,oy,ow,oh);wind_get(window,WF_WORKXYWH,&work.g_x,&work.g_y,&work.g_w,&work.g_h); }
    for(i=1;i<argc && count<FNX_TRACKS;i++) add(argv[i]);if(count) enqueue(C_PLAY,0,0,NULL);
    while(running) {
        int event;
        io();if(dirty) {
            int changes=dirty;
            if(changes&1) redraw(&work);
            else {
                GRECT r;
                if(changes&2) { r=work;r.g_x+=10;r.g_y+=36;r.g_w=524;r.g_h=24;redraw(&r); }
                if(changes&16) { r=work;r.g_x+=10;r.g_y+=60;r.g_w=524;r.g_h=46;redraw(&r); }
                if(changes&4) { r=work;r.g_x+=14;r.g_y+=115;r.g_w=516;r.g_h=8;redraw(&r); }
                if(changes&8) { r=work;r.g_x+=64;r.g_y+=358;r.g_w=466;r.g_h=4;redraw(&r); }
            }
        }
        event=evnt_multi(MU_MESAG|MU_BUTTON|MU_KEYBD|MU_TIMER,1,1,mouse_down?0:1,0,0,0,0,0,0,0,0,0,0,msg,100,&mx,&my,&buttons,&keys,&key,&clicks);
        if(event&MU_MESAG) {
            if(msg[0]==AP_TERM) running=0;
            if(msg[3]==window) switch(msg[0]) {
            case WM_CLOSED:running=0;break;
            case WM_TOPPED:wind_set(window,WF_TOP,0,0,0,0);break;
            case WM_BOTTOMED:wind_set(window,WF_BOTTOM,0,0,0,0);break;
            case WM_REDRAW:{ GRECT r={msg[4],msg[5],msg[6],msg[7]};redraw(&r);break; }
            case WM_MOVED:wind_set(window,WF_CURRXYWH,msg[4],msg[5],msg[6],msg[7]);wind_get(window,WF_WORKXYWH,&work.g_x,&work.g_y,&work.g_w,&work.g_h);dirty=1;break;
            case WM_ICONIFY:iconified=1;wind_set(window,WF_ICONIFY,msg[4],msg[5],msg[6],msg[7]);dirty=1;break;
            case WM_UNICONIFY:iconified=0;wind_set(window,WF_UNICONIFY,msg[4],msg[5],msg[6],msg[7]);dirty=1;break;
            }
        }
        if(event&MU_BUTTON) { if(!mouse_down) click(mx,my);mouse_down=!mouse_down; }
        if(event&MU_KEYBD) {
            unsigned ch=key&255,scan=(key>>8)&255;
            if(ch==' ' || ch=='p' || ch=='P') enqueue(C_PAUSE,0,0,NULL);
            else if(ch=='o' || ch=='O') open_file(0);
            else if(ch=='a' || ch=='A') open_file(1);
            else if(ch=='l' || ch=='L') load_playlist();
            else if(ch=='s' || ch=='S') enqueue(C_STOP,0,0,NULL);
            else if(ch==13) enqueue(C_PLAY,selected,0,NULL);
            else if(ch==127) remove_selected();
            else if(ch=='+' || ch=='-') { volume+=ch=='+'?5:-5;if(volume<0) volume=0;if(volume>100) volume=100;enqueue(C_LEVEL,volume,balance,NULL); }
            else if(scan==0x48 && selected>0) { --selected;if(selected<scroll) scroll=selected;dirty=1; }
            else if(scan==0x50 && selected+1<count) { ++selected;if(selected>=scroll+6) scroll=selected-5;dirty=1; }
        }
    }
    if(window>=0) { wind_close(window);wind_delete(window); }
    close(tx); /* EOF makes the worker cancel cleanly, even if GUI queue was full. */
    { int n;for(n=0;n<80;n++) { if(waitpid(child,NULL,WNOHANG)==child) break;usleep(100000); }if(n==80) kill(child,SIGTERM); }
    close(rx);settings(1);v_clsvwk(vh);close(lockfd);appl_exit();return 0;
}
