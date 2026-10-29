/* Bounded MP3 metadata/frame scanning. GPL-2.0-or-later. */
#ifndef FNX_MEDIA_H
#define FNX_MEDIA_H
#include <stdio.h>
#include <stdint.h>
#define FNX_PATH 384
#define FNX_TRACKS 128
struct fnx_media {
    char title[80],artist[64],format[16];
    long size,start,end;
    uint32_t duration,rate,bitrate;
    int mp3;
};
int fnx_media_info(FILE *f,const char *path,struct fnx_media *m);
long fnx_seek(FILE *f,const struct fnx_media *m,unsigned permille);
int fnx_m3u_path(const char *playlist,const char *entry,char out[FNX_PATH]);
#endif
