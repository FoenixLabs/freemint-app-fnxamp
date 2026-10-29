#ifndef FNX_PLAYER_H
#define FNX_PLAYER_H
#include "media.h"
#include <stdint.h>
enum { C_ADD=1,C_CLEAR,C_REMOVE,C_PLAY,C_PAUSE,C_STOP,C_NEXT,C_PREV,C_LEVEL,C_TONE,C_MODES,C_SEEK,C_QUIT };
enum { P_STOPPED,P_PLAYING,P_PAUSED,P_ERROR };
struct fnx_command { int32_t command,value,extra;char path[FNX_PATH]; };
#define FNX_MAGIC 0x464e5831UL
struct fnx_status {
    uint32_t magic;
    uint32_t serial,elapsed,duration,rate,bitrate,fifo;
    int32_t state,index,count,seekable,progress;
    char title[80],artist[64],format[16],message[96];
};
void fnx_worker(int commands,int events);
#endif
