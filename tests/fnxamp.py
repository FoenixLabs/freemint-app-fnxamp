#!/usr/bin/env python3
"""Production VS1053 protocol and bounded media parser regression."""
from pathlib import Path
import subprocess,tempfile,struct
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT
TEST=r'''
#include "vs1053.h"
#include "media.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint32_t ctrl,now_ms,busy,words,bytes,sci,mode=0x4800;
static uint16_t registers[16];static unsigned char sent[20000];static size_t length;
static int absent,stall,cancel_stuck;static unsigned resets;
static uint32_t rd(unsigned r) {
 if(r==VS_CTRL) return ctrl;
 if(r==VS_ROOM) return 1024-words;
 if(r==VS_STATUS) return (ctrl&1?1:0)|((ctrl&1 && absent)?0:2)|(busy?4:0)|(bytes<<16);
 assert(r==VS_SCI);return sci;
}
static void wr(unsigned r,uint32_t v) {
 if(r==VS_CTRL) { if(v&2) words=bytes=0;if((v&1) && !(ctrl&1)) { mode=0x4800;resets++; }ctrl=v&~2U;return; }
 if(r==VS_STATUS) return;
 if(r==VS_SCI) {
  unsigned reg=(v>>16)&15;
  if(v&0x1000000) sci=reg==0?mode:reg==1?(absent?0:0x40):reg==3?0x8800:reg==6?0x55:registers[reg];
  else if(reg==0) mode=v&65535;else registers[reg]=v&65535;
  busy=1;return;
 }
 assert(r==VS_DATA && words<1024 && length+4<=sizeof(sent));
 sent[length++]=v>>24;sent[length++]=v>>16;sent[length++]=v>>8;sent[length++]=v;words++;bytes+=4;
 if(mode&8 && !cancel_stuck) { mode&=~8U;registers[8]=registers[9]=0; }
}
static void byte(unsigned char v) { assert(words<1024 && length<sizeof(sent));sent[length++]=v;words++;bytes++; }
static uint32_t clock_ms(void) { return now_ms; }
static void yield(void) { now_ms++;busy=0;if(!stall) words=bytes=0; }
static struct fnx_hw device(void) { struct fnx_hw h;memset(&h,0,sizeof(h));h.io.read=rd;h.io.write=wr;h.io.byte=byte;h.io.now=clock_ms;h.io.yield=yield;return h; }
static size_t more(void *unused,unsigned char *p,size_t n) { (void)unused;memset(p,0xaa,n);return n; }
int main(void) {
 struct fnx_hw h=device();unsigned char p[4099];unsigned i;FILE *f;struct fnx_media m;char path[FNX_PATH];
 ctrl=1;assert(!fnx_start(&h) && !h.owned);ctrl=0;
 absent=1;assert(!fnx_start(&h) && !h.owned && !ctrl);absent=0;
 assert(fnx_start(&h) && h.owned && h.ready && ctrl==1);
 for(i=0;i<sizeof(p);i++) p[i]=i;
 assert(fnx_feed(&h,p,sizeof(p))==4096 && words==1024);assert(!memcmp(p,sent,4096));
 assert(fnx_feed(&h,p,1)==0);yield();assert(fnx_feed(&h,p+4096,3)==3);assert(!memcmp(p,sent,sizeof(p)));
 yield();assert(fnx_volume(&h,0,100) && registers[11]==0xfefe);
 assert(fnx_volume(&h,100,0) && registers[11]==0);
 assert(fnx_volume(&h,75,-100) && registers[11]==0x1878);
 assert(fnx_tone(&h,15,-8) && registers[2]==0x8af6);
 length=0;assert(fnx_finish(&h,0,more,0)==1 && ctrl==1);
 assert(length>=2084);assert(sent[0]==0xaa && sent[length-1]==0x55);
 length=0;assert(fnx_finish(&h,1,more,0)==1);assert(length>=2084 && sent[0]==0x55);
 length=0;cancel_stuck=1;i=resets;assert(fnx_finish(&h,0,more,0)==2 && resets==i+1);cancel_stuck=0;
 length=0;stall=1;words=1024;bytes=4096;assert(fnx_finish(&h,0,more,0)==2);stall=0;
 fnx_close(&h);assert(!ctrl && !h.owned && registers[11]==0xfefe);
 assert(fnx_m3u_path("D:\\MUSIC\\LIST.M3U","song.mp3\r\n",path) && !strcmp(path,"D:\\MUSIC\\song.mp3"));
 assert(!fnx_m3u_path("a.m3u","http://host/a.mp3",path));assert(!fnx_m3u_path("a.m3u","#EXTM3U",path));
 f=fopen("test.mp3","rb");assert(f && fnx_media_info(f,"test.mp3",&m));
 assert(m.mp3 && m.rate==44100 && m.bitrate==128000 && !strcmp(m.title,"Test title"));
 assert(fnx_seek(f,&m,500)>=m.start);assert(fnx_seek(f,&m,1001)<0);fclose(f);
 f=fopen("bad.mp3","rb");assert(f && !fnx_media_info(f,"bad.mp3",&m));fclose(f);
 f=fopen("demo.wav","rb");assert(f && fnx_media_info(f,"demo.wav",&m) && !m.mp3 && m.rate==22050);fclose(f);
 puts("PASS: VS1053 identity, exclusive ownership, FIFO word/byte order, no overrun, volume/tone, end/cancel/reset/timeouts, MP3 tags/frame seek, WAV and safe local M3U paths");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp);(d/'test.c').write_text(TEST)
 title=b'\x00Test title';frame=b'TIT2'+struct.pack('>I',len(title))+b'\0\0'+title
 tag=b'ID3\3\0\0'+bytes([0,0,0,len(frame)])+frame
 (d/'test.mp3').write_bytes(tag+(b'\xff\xfb\x90\0'+bytes(413))*100)
 (d/'bad.mp3').write_bytes(b'ID3\3\0\0\x7f\x7f\x7f\x7f')
 (d/'demo.wav').write_bytes(b'RIFF'+struct.pack('<I',80)+b'WAVEfmt '+struct.pack('<IHHIIHH',16,1,1,22050,44100,2,16)+b'data'+struct.pack('<I',44)+bytes(44))
 subprocess.run(['cc','-DFNX_HOST_TEST','-std=c99','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-I'+str(SRC),str(d/'test.c'),str(SRC/'vs1053.c'),str(SRC/'media.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],cwd=d,check=True)
