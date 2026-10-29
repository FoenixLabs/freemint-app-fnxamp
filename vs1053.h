/* A2560Me FPGA/VS1053b interface. GPL-2.0-or-later. */
#ifndef FNX_VS1053_H
#define FNX_VS1053_H
#include <stdint.h>
#include <stddef.h>
struct fnx_io {
    uint32_t (*read)(unsigned reg);
    void (*write)(unsigned reg,uint32_t value);
    void (*byte)(unsigned char value);
    uint32_t (*now)(void);
    void (*yield)(void);
};
struct fnx_hw { struct fnx_io io; int owned,ready; uint32_t saved; char error[96]; };
enum { VS_CTRL=0,VS_STATUS=4,VS_SCI=8,VS_ROOM=12,VS_DATA=16 };
int fnx_start(struct fnx_hw *h);
int fnx_sci_read(struct fnx_hw *h,unsigned reg,uint16_t *value);
int fnx_sci_write(struct fnx_hw *h,unsigned reg,uint16_t value);
size_t fnx_feed(struct fnx_hw *h,const unsigned char *bytes,size_t count);
int fnx_finish(struct fnx_hw *h,int eof,size_t (*more)(void *,unsigned char *,size_t),void *context);
int fnx_volume(struct fnx_hw *h,int volume,int balance);
int fnx_tone(struct fnx_hw *h,int bass,int treble);
void fnx_close(struct fnx_hw *h);
#ifndef FNX_HOST_TEST
void fnx_target_io(struct fnx_hw *h);
int fnx_board(void);
#endif
#endif
