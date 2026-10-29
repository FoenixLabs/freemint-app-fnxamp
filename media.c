#include "media.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
static uint32_t be32(const unsigned char *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static uint32_t le32(const unsigned char *p)
{ return (uint32_t)p[3]<<24|(uint32_t)p[2]<<16|(uint32_t)p[1]<<8|p[0]; }
static void text(char *out,size_t cap,const unsigned char *p,size_t n,unsigned enc)
{
    size_t i=0,k=0;int little=0;
    if(enc==1 && n>=2) { little=p[0]==255 && p[1]==254; i=2; }
    while(i<n && k+1<cap) {
        unsigned c=p[i++];
        if(enc==1 || enc==2) {
            if(i>=n) break;
            c=little ? c|((unsigned)p[i++]<<8) : (c<<8)|p[i++];
        }
        if(!c) break;
        out[k++]=c>=32 && c<127 ? (char)c : '?';
    }
    while(k && out[k-1]==' ') --k;
    out[k]=0;
}
static int header(const unsigned char *p,unsigned *bytes,unsigned *rate,unsigned *kbps)
{
    static const unsigned rates[3]={44100,48000,32000};
    static const unsigned br1[16]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
    static const unsigned br2[16]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
    unsigned ver=(p[1]>>3)&3,r=(p[2]>>2)&3,b=(p[2]>>4)&15;
    if(p[0]!=255 || (p[1]&0xe0)!=0xe0 || ver==1 || ((p[1]>>1)&3)!=1 || r==3 || !b || b==15) return 0;
    *rate=rates[r]/(ver==3 ? 1 : ver==2 ? 2 : 4);*kbps=ver==3?br1[b]:br2[b];
    *bytes=(ver==3?144000UL:72000UL)*(*kbps)/(*rate)+((p[2]>>1)&1);
    return *bytes>=24;
}
static long scan(FILE *f,long start,long end,unsigned *rate,unsigned *kbps)
{
    unsigned char *window,next[4];unsigned bytes,nbytes,nrate,nkbps;size_t size,i;long result=-1;
    if(start<0 || start>end-4) return -1;
    size=(unsigned long)(end-start)>65536 ? 65536 : (size_t)(end-start);
    window=malloc(size);if(!window) return -1;
    if(fseek(f,start,SEEK_SET) || fread(window,1,size,f)!=size) { free(window);return -1; }
    for(i=0;i+4<=size;i++) {
        const unsigned char *p=window+i;
        if(!header(p,&bytes,rate,kbps) || start+(long)i+(long)bytes+4>end) continue;
        if(i+bytes+4<=size) memcpy(next,window+i+bytes,4);
        else if(fseek(f,start+i+bytes,SEEK_SET) || fread(next,1,4,f)!=4) break;
        if(header(next,&nbytes,&nrate,&nkbps) && nrate==*rate) { result=start+i;break; }
    }
    free(window);return result;
}

int fnx_media_info(FILE *f,const char *path,struct fnx_media *m)
{
    unsigned char p[256];const char *name=path,*q;unsigned r,b;long first;
    memset(m,0,sizeof(*m));
    for(q=path;*q;q++) if(*q=='/' || *q=='\\') name=q+1;
    snprintf(m->title,sizeof(m->title),"%.79s",name);
    strcpy(m->format,"Audio");
    if(fseek(f,0,SEEK_END) || (m->size=ftell(f))<1) return 0;
    m->end=m->size;
    if(m->size>=128 && !fseek(f,-128,SEEK_END) && fread(p,1,128,f)==128 && !memcmp(p,"TAG",3)) {
        text(m->title,sizeof(m->title),p+3,30,0);text(m->artist,sizeof(m->artist),p+33,30,0);m->end-=128;
    }
    rewind(f);
    if(fread(p,1,10,f)==10 && !memcmp(p,"ID3",3)) {
        uint32_t size;long stop;
        if((p[6]|p[7]|p[8]|p[9])&128 || (p[3]!=3 && p[3]!=4 && p[3]!=2)) return 0;
        size=(uint32_t)p[6]<<21|(uint32_t)p[7]<<14|(uint32_t)p[8]<<7|p[9];
        if(size>(uint32_t)m->end-10) return 0;
        m->start=10+size+(p[3]==4 && (p[5]&16)?10:0);
        if(m->start>m->end) return 0;
        /* Bounded tag reads, no allocation from untrusted tag lengths. Skip
         * extended/unsynchronised tags; basename or ID3v1 remains available. */
        stop=10+size;
        if((p[3]==3 || p[3]==4) && !(p[5]&0xc0)) {
            unsigned ver=p[3];long at=10;
            while(at<=stop-10 && at<262144) {
                uint32_t n;unsigned char data[256];
                if(fseek(f,at,SEEK_SET) || fread(p,1,10,f)!=10 || !p[0]) break;
                if(ver==4) {
                    if((p[4]|p[5]|p[6]|p[7])&128) break;
                    n=(uint32_t)p[4]<<21|(uint32_t)p[5]<<14|(uint32_t)p[6]<<7|p[7];
                } else n=be32(p+4);
                if(!n || n>(uint32_t)(stop-at-10)) break;
                if(!p[8] && !p[9] && (!memcmp(p,"TIT2",4) || !memcmp(p,"TPE1",4))) {
                    size_t take=n>sizeof(data)?sizeof(data):n;
                    if(fread(data,1,take,f)!=take) break;
                    if(!memcmp(p,"TIT2",4)) text(m->title,sizeof(m->title),data+1,take-1,data[0]);
                    else text(m->artist,sizeof(m->artist),data+1,take-1,data[0]);
                }
                at+=10+n;
            }
        }
    }
    if(!fseek(f,0,SEEK_SET) && fread(p,1,44,f)==44 && !memcmp(p,"RIFF",4) && !memcmp(p+8,"WAVE",4)) {
        strcpy(m->format,"WAV");m->rate=le32(p+24);m->start=0;
        if(!memcmp(p+12,"fmt ",4) && le32(p+16)==16 && !memcmp(p+36,"data",4) && le32(p+28))
            m->duration=le32(p+40)/le32(p+28);
    } else if((first=scan(f,m->start,m->end,&r,&b))>=0) {
        m->start=first;m->mp3=1;m->rate=r;m->bitrate=b*1000UL;strcpy(m->format,"MP3");
        m->duration=(uint64_t)(unsigned long)(m->end-m->start)*8/m->bitrate;
    } else {
        const char *dot=strrchr(name,'.');
        if(dot) snprintf(m->format,sizeof(m->format),"%.15s",dot+1);
    }
    if(!*m->title) snprintf(m->title,sizeof(m->title),"%.79s",name);
    return fseek(f,m->start,SEEK_SET)==0;
}
long fnx_seek(FILE *f,const struct fnx_media *m,unsigned permille)
{
    unsigned r,b;long at,found;
    if(!m->mp3 || permille>1000 || m->end<=m->start) return -1;
    /* Quotient/remainder avoids 32-bit overflow for large files. */
    at=m->start+(m->end-m->start)/1000*permille+(m->end-m->start)%1000*permille/1000;
    if(at>m->end-2048) at=m->end-2048;
    if(at<m->start) at=m->start;
    found=scan(f,at,m->end,&r,&b);
    if(found<0 || fseek(f,found,SEEK_SET)) return -1;
    return found;
}
int fnx_m3u_path(const char *playlist,const char *entry,char out[FNX_PATH])
{
    const char *p,*last=NULL;size_t prefix=0,n=strcspn(entry,"\r\n");
    if(!n || entry[0]=='#' || strstr(entry,"://") || n>=FNX_PATH) return 0;
    if(entry[0]!='/' && entry[0]!='\\' && !(n>1 && entry[1]==':')) {
        for(p=playlist;*p;p++) if(*p=='/' || *p=='\\') last=p;
        if(last) prefix=last-playlist+1;
    }
    if(prefix+n>=FNX_PATH) return 0;
    memcpy(out,playlist,prefix);memcpy(out+prefix,entry,n);out[prefix+n]=0;return 1;
}
