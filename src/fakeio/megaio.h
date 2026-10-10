/* Shared control block between the fake I/O board (fakeio.c) and the `megaio` tool. */
#ifndef MEGAIO_H
#define MEGAIO_H
#include <stdint.h>

#define MEGAIO_MAGIC 0x4D494F32u   /* "MIO2" */

struct megaio_ctl {
    uint32_t magic;
    uint8_t coins[8];       /* per-channel pulse counters; the board reports the change each poll */
    uint8_t st[24];         /* status bytes 8..23 as sent to the loader (0..7 are the coin counts):
                               8 = DIP bank DS1 (active low), 9 bit0 = SETUP, bit1 = CALIBRATE */
    uint8_t fob_present;    /* a key is touching the front reader */
    uint8_t fob_id[8];      /* its ROM ID (family 0x02) */
    uint8_t fob_kind;       /* 0 player key ("m8b4uc3d" record), 1 operator key ("y2ger1b0") */
    uint8_t key_id_set;     /* use key_id instead of the default security key ROM ID */
    uint8_t key_id[8];
    /* written by the board */
    uint8_t outputs[6];     /* last output bytes (meters) */
    uint8_t outputs_flags;  /* lockout flags from the poll command */
    uint8_t pad;
    uint16_t heartbeat;
    uint32_t polls;
    uint32_t meters[2];     /* pulses seen on output byte 1 (coin meter) and 2 (TournaMAXX meter) */
    /* books printer (status byte 9 bit 2 = printer plugged in) */
    uint8_t printer_auto;   /* set with the plug-in bit: the board unplugs it when the job ends */
    uint8_t pad2[3];
    uint32_t print_jobs;    /* finished print jobs (written by the board) */
};
#endif
