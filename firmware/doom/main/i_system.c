/*
 * Game Five DOOM — system layer (timing + WAD access) for ESP32-S3 / ESP-IDF v6
 *
 * Derived from esp32-doom's prboom-esp32-compat/i_system.c (GPL-2.0):
 * PrBoom: Copyright (C) 1999-2006 id Software, Lee Killough, Colin Phipps et al.
 * ESP32 code: Copyright 2016-2017 Espressif Systems, Apache-2.0.
 *
 * S3 rewrite: the whole WAD partition is memory-mapped ONCE (the S3 MMU has a
 * 32MB data window, so the classic ESP32 128-entry mmap-handle cache is gone);
 * I_Mmap/I_Munmap collapse to pointer arithmetic into the mapping.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "config.h"
#include "m_argv.h"
#include "lprintf.h"
#include "doomtype.h"
#include "doomdef.h"
#include "m_fixed.h"
#include "r_fps.h"
#include "i_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_partition.h"
#include "esp_timer.h"

#define WAD_PART_TYPE    ((esp_partition_type_t)0x42)
#define WAD_PART_SUBTYPE ((esp_partition_subtype_t)0x06)

int realtime = 0;

void I_uSleep(unsigned long usecs)
{
    usleep(usecs);
}

static unsigned long getMsTicks(void)
{
    return (unsigned long)(esp_timer_get_time() / 1000);
}

int I_GetTime_RealTime(void)
{
    return (int)((esp_timer_get_time() * TICRATE) / 1000000);
}

const int displaytime = 0;

fixed_t I_GetTimeFrac(void)
{
    unsigned long now = getMsTicks();

    if (tic_vars.step == 0)
        return FRACUNIT;

    fixed_t frac = (fixed_t)((now - tic_vars.start + displaytime) * FRACUNIT
                             / tic_vars.step);
    if (frac < 0)
        frac = 0;
    if (frac > FRACUNIT)
        frac = FRACUNIT;
    return frac;
}

void I_GetTime_SaveMS(void)
{
    if (!movement_smooth)
        return;

    tic_vars.start = getMsTicks();
    tic_vars.next = (unsigned int)((tic_vars.start * tic_vars.msec + 1.0f)
                                   / tic_vars.msec);
    tic_vars.step = tic_vars.next - tic_vars.start;
}

unsigned long I_GetRandomTimeSeed(void)
{
    return 4; /* per https://xkcd.com/221/ (kept from the original port) */
}

const char *I_GetVersionString(char *buf, size_t sz)
{
    snprintf(buf, sz, "%s v%s (Game Five build)", PACKAGE, VERSION);
    return buf;
}

const char *I_SigString(char *buf, size_t sz, int signum)
{
    return buf;
}

/*
 * WAD access: prboom's w_wad.c uses these instead of open/read/lseek.
 * The only "file" is the wad flash partition, exposed as DOOM1.WAD.
 */

typedef struct {
    const esp_partition_t *part;
    int offset;
    int size;
} FileDesc;

#define NUM_FDS 8
static FileDesc fds[NUM_FDS];

static const uint8_t *wad_base;
static esp_partition_mmap_handle_t wad_mmap;

int I_Open(const char *wad, int flags)
{
    if (strcmp(wad, "DOOM1.WAD") != 0) {
        lprintf(LO_INFO, "I_Open: open %s failed\n", wad);
        return -1;
    }

    int x = 3;
    while (x < NUM_FDS && fds[x].part != NULL)
        x++;
    if (x == NUM_FDS) {
        lprintf(LO_ERROR, "I_Open: out of fds\n");
        return -1;
    }

    const esp_partition_t *part =
        esp_partition_find_first(WAD_PART_TYPE, WAD_PART_SUBTYPE, NULL);
    if (part == NULL) {
        lprintf(LO_ERROR, "I_Open: wad partition (type 0x42 sub 0x06) not found\n");
        return -1;
    }

    if (wad_base == NULL) {
        esp_err_t err = esp_partition_mmap(part, 0, part->size,
                                           ESP_PARTITION_MMAP_DATA,
                                           (const void **)&wad_base, &wad_mmap);
        if (err != ESP_OK) {
            lprintf(LO_ERROR, "I_Open: esp_partition_mmap failed: 0x%x\n", err);
            wad_base = NULL;
            return -1;
        }
        lprintf(LO_INFO, "I_Open: wad partition %u bytes mapped at %p\n",
                (unsigned)part->size, wad_base);
    }

    fds[x].part = part;
    fds[x].offset = 0;
    fds[x].size = part->size;
    return x;
}

int I_Lseek(int ifd, off_t offset, int whence)
{
    if (whence == SEEK_SET) {
        fds[ifd].offset = offset;
    } else if (whence == SEEK_CUR) {
        fds[ifd].offset += offset;
    } else if (whence == SEEK_END) {
        lprintf(LO_INFO, "I_Lseek: SEEK_END unimplemented\n");
    }
    return fds[ifd].offset;
}

int I_Filelength(int ifd)
{
    return fds[ifd].size;
}

void I_Close(int fd)
{
    fds[fd].part = NULL;
}

void *I_Mmap(void *addr, size_t length, int prot, int flags, int ifd,
             off_t offset)
{
    if (wad_base == NULL || offset + (off_t)length > fds[ifd].size) {
        lprintf(LO_ERROR, "I_Mmap: bad request off=%d len=%d\n",
                (int)offset, (int)length);
        return NULL;
    }
    return (void *)(wad_base + offset);
}

int I_Munmap(void *addr, size_t length)
{
    return 0;
}

void I_Read(int ifd, void *vbuf, size_t sz)
{
    memcpy(vbuf, wad_base + fds[ifd].offset, sz);
    fds[ifd].offset += sz;
}

const char *I_DoomExeDir(void)
{
    return "";
}

char *I_FindFile(const char *wfname, const char *ext)
{
    return NULL; /* no filesystem: aux wads/cfg are never found */
}

void I_SetAffinityMask(void)
{
}

/* Note: the original port overrode libc access() to always fail; on IDF v6
 * the VFS provides access() (multiple-definition otherwise) and returns -1
 * for any unmounted path, which is the same "not found" answer. */
