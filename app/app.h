/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* FM-1 Again, by PolarPatch: started from fm1-base's template (Charles Vestal, MPL-2.0).
 * This header is the app's half of the contract with the platform (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "FM-1 AGAIN"
#define APP_SPLASH_RGB RGB(255, 255, 255)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity and pressure from the keys (lib/touch.h) */
#define APP_BTDEV                        /* Bluetooth MIDI devices (lib/btdev.h) */
#define APP_BLE_AT_BOOT (!again_ble_off())   /* Bluetooth at power-on: as the player left it (REC held) */
int again_ble_off(void);

/* the store: the app's objects, the panel calibration (the platform's) and Bluetooth's address and pairing among
 * them, and their flash offsets */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_WIFI, OBJ_NOBJ };   /* OBJ_WIFI: the networks kept, the mode (fm1-base's) */
#if defined PLAT_SMK37 && PLAT_SMK37              /* the SMK-37: the settings at PLAT_GLOB, the rest at the data area's top */
#define APP_STORE_MAP {0xEB000u, 0xE4000u, 0xE6000u, 0xE2000u}
#else
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u, 0xDA000u}
#endif

/* the resource region (fm1.toml resources): the screensaver's film (saver.h), the data area 0x97000 .. 0xD9FFF, up to
 * the store's pairs (the Wi-Fi's from 1.1 at 0xDA000; CHOP's sample is in XDATA1). The film is 53 sectors */
#define APP_RES_BASE 0x97000u
#define APP_RES_SECTORS 67u

/* backup and restore over USB (fm1.toml backup = true; the resource region's transfer is the restore's): the settings */
#define APP_BACKUP_OBJS {OBJ_SETTINGS}
#define APP_BACKUP_MAGIC 0x31424741u         /* "AGB1" */
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "replaces the settings"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

/* the objects a computer may keep as files (fm1.toml companion = true: web/companion, tools/fm1_companion.py): object,
 * kind, what it may do, the file's name. Never OBJ_PANEL (the unit's own) or OBJ_BLE (this FM-1's pairings) */
#define APP_FILE_OBJS {{OBJ_SETTINGS, PLAT_FK_SETTINGS, PLAT_FF_READ | PLAT_FF_WRITE, "settings"}}
int app_file_busy(void);                 /* 1: no file written now (playing) */
#define APP_FILES                        /* and a file of its own: CHOP's sample, "chop" (app.c, tools/again_chop.py) */
int app_file(uint32_t i, plat_file_info_t *f);
int app_file_read(uint32_t i, uint32_t off, uint8_t *dst, uint32_t n);
int app_file_begin(uint32_t i, uint32_t size);
int app_file_write(uint32_t i, uint32_t off, const uint8_t *src, uint32_t n);
int app_file_commit(uint32_t i, uint32_t size, uint32_t crc);
int app_file_delete(uint32_t i);
void app_file_changed(uint32_t obj);     /* the computer wrote obj: load it again */
#define APP_REDRAW                       /* the mirror lost a part: everything drawn again (app.c) */
void app_redraw(void);
#define APP_PARAMS                       /* and every parameter, live (the editor page; app.c app_param) */
int app_param(uint32_t i, plat_param_t *p);
const char *app_param_group(uint32_t g);
int32_t app_param_get(uint32_t i);
int32_t app_param_set(uint32_t i, int32_t v);

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the engine, then the saved settings (or defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);                  /* the first screen, after the splash */
void app_frame(void);                    /* the UI: input and a redraw, about 60 times a second */
void app_input_only(void);               /* between frames: input only */
void app_say(const char *a, const char *b);   /* a short message on the screen and to a screen reader */

/* the host simulator's checks (host/plat_host.c "expect"): octave, wave, note (-1 none), dirty, turnsN */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
