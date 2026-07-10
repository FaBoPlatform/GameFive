/*
 * Game Five DOOM — sound layer: 8-channel SFX mixer -> MAX98357A over I2S
 *
 * PrBoom: Copyright (C) 1999-2006 id Software, Lee Killough, Colin Phipps
 * et al., GPL-2.0. Game Five I2S implementation 2026.
 *
 * SFX only (mus_card=0 — no music synth). DMX-format lumps are read
 * zero-copy from the memory-mapped WAD partition and mixed at 11025 Hz
 * mono by a dedicated task on core 1; the MAX98357A (SD_MODE pulled high =
 * (L+R)/2) gets identical L/R slots.
 */

#include "config.h"
#include <string.h>
#include <stdio.h>
#include "doomdef.h"
#include "doomstat.h"
#include "doomtype.h"
#include "sounds.h"
#include "s_sound.h"
#include "i_sound.h"
#include "w_wad.h"
#include "lprintf.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"

#include "doom_config.h"

int snd_card = 1;        /* sfx enabled */
int mus_card = 0;        /* no music */
int snd_samplerate = 11025;

#define MIX_RATE     11025
#define MIX_FRAMES   256          /* ~23 ms per block */
#define NUM_CHANNELS 16
#define MASTER_SHIFT 2            /* s(-128..127)*vol(0..127)>>2 ≈ -12dBFS/ch */

typedef struct {
    const uint8_t *data;   /* unsigned 8-bit samples (mmapped flash) */
    uint32_t len;          /* sample count */
    uint32_t pos;          /* 16.16 fixed */
    uint32_t step;         /* 16.16 fixed: lump rate / MIX_RATE */
    int vol;               /* 0..127 */
    volatile int active;
} mix_chan_t;

static mix_chan_t chans[NUM_CHANNELS];
static portMUX_TYPE snd_mux = portMUX_INITIALIZER_UNLOCKED;
static i2s_chan_handle_t s_tx;
static int snd_inited;

static void sound_task(void *arg)
{
    static int16_t buf[MIX_FRAMES * 2]; /* L+R interleaved */

    for (;;) {
        for (int i = 0; i < MIX_FRAMES; i++) {
            int32_t mix = 0;
            taskENTER_CRITICAL(&snd_mux);
            for (int c = 0; c < NUM_CHANNELS; c++) {
                mix_chan_t *ch = &chans[c];
                if (!ch->active)
                    continue;
                uint32_t idx = ch->pos >> 16;
                if (idx >= ch->len) {
                    ch->active = 0;
                    continue;
                }
                mix += ((int)ch->data[idx] - 128) * ch->vol;
                ch->pos += ch->step;
            }
            taskEXIT_CRITICAL(&snd_mux);
            mix >>= MASTER_SHIFT;
            if (mix > 32767) mix = 32767;
            if (mix < -32768) mix = -32768;
            buf[i * 2] = (int16_t)mix;
            buf[i * 2 + 1] = (int16_t)mix;
        }
        size_t written;
        esp_err_t err =
            i2s_channel_write(s_tx, buf, sizeof(buf), &written, portMAX_DELAY);
        static int dbg_state; /* 0=init, 1=reported nonzero, <0=reported err */
        if (err != ESP_OK && dbg_state >= 0) {
            dbg_state = -1;
            lprintf(LO_WARN, "sound_task: i2s write err 0x%x\n", err);
        } else if (dbg_state == 0 && buf[0] != 0) {
            dbg_state = 1;
            lprintf(LO_INFO, "sound_task: first nonzero audio block\n");
        }
    }
}

void I_InitSound(void)
{
    if (snd_inited)
        return;

    i2s_chan_config_t ccfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    if (i2s_new_channel(&ccfg, &s_tx, NULL) != ESP_OK) {
        lprintf(LO_WARN, "I_InitSound: i2s channel alloc failed\n");
        snd_card = 0;
        return;
    }
    i2s_std_config_t scfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIX_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = 6,   /* GF_PIN_I2S_BCLK  (XIAO D5) */
            .ws   = 43,  /* GF_PIN_I2S_LRCLK (XIAO D6) */
            .dout = 44,  /* GF_PIN_I2S_DIN   (XIAO D7) */
            .din  = I2S_GPIO_UNUSED,
        },
    };
    if (i2s_channel_init_std_mode(s_tx, &scfg) != ESP_OK ||
        i2s_channel_enable(s_tx) != ESP_OK) {
        lprintf(LO_WARN, "I_InitSound: i2s init failed\n");
        snd_card = 0;
        return;
    }

    xTaskCreatePinnedToCore(sound_task, "sound", 4096, NULL, 6, NULL, 1);
    snd_inited = 1;
    lprintf(LO_INFO, "I_InitSound: I2S %d Hz, %d mix channels\n",
            MIX_RATE, NUM_CHANNELS);
}

void I_ShutdownSound(void)
{
}

void I_SetChannels(void)
{
}

int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];
    snprintf(namebuf, sizeof(namebuf), "ds%.6s", sfx->name);
    return W_CheckNumForName(namebuf);
}

/*
 * DMX lump: u16 format(=3), u16 rate, u32 length, then `length` unsigned
 * 8-bit samples of which the first/last 16 are padding.
 */
int I_StartSound(int id, int channel, int vol, int sep, int pitch,
                 int priority)
{
    if (!snd_inited || channel < 0 || channel >= NUM_CHANNELS)
        return -1;

    sfxinfo_t *sfx = &S_sfx[id];
    if (sfx->lumpnum < 0)
        sfx->lumpnum = I_GetSfxLumpNum(sfx);
    if (sfx->lumpnum < 0)
        return -1;

    const uint8_t *lump = W_CacheLumpNum(sfx->lumpnum);
    int lumplen = W_LumpLength(sfx->lumpnum);
    if (lump == NULL || lumplen < 8 + 32) {
        if (lump) W_UnlockLumpNum(sfx->lumpnum);
        return -1;
    }

    uint16_t fmt = lump[0] | (lump[1] << 8);
    uint16_t rate = lump[2] | (lump[3] << 8);
    uint32_t dlen = lump[4] | (lump[5] << 8) | (lump[6] << 16)
                    | ((uint32_t)lump[7] << 24);
    if (fmt != 3 || rate == 0) {
        W_UnlockLumpNum(sfx->lumpnum);
        return -1;
    }
    if (dlen > (uint32_t)(lumplen - 8))
        dlen = lumplen - 8;
    if (dlen <= 32) {
        W_UnlockLumpNum(sfx->lumpnum);
        return -1;
    }

    taskENTER_CRITICAL(&snd_mux);
    chans[channel].active = 0;
    chans[channel].data = lump + 8 + 16;   /* skip header + lead-in pad */
    chans[channel].len = dlen - 32;        /* drop both pads */
    chans[channel].pos = 0;
    chans[channel].step = ((uint32_t)rate << 16) / MIX_RATE;
    chans[channel].vol = vol < 0 ? 0 : (vol > 127 ? 127 : vol);
    chans[channel].active = 1;
    taskEXIT_CRITICAL(&snd_mux);

    static int dbg_count;
    if (dbg_count < 5) {
        dbg_count++;
        lprintf(LO_INFO, "I_StartSound: ds%.6s ch=%d vol=%d rate=%u len=%u\n",
                sfx->name, channel, vol, rate, (unsigned)(dlen - 32));
    }

    /* whole-partition mmap: the pointer stays valid after unlock */
    W_UnlockLumpNum(sfx->lumpnum);
    return channel;
}

void I_StopSound(int handle)
{
    if (handle < 0 || handle >= NUM_CHANNELS)
        return;
    taskENTER_CRITICAL(&snd_mux);
    chans[handle].active = 0;
    taskEXIT_CRITICAL(&snd_mux);
}

int I_SoundIsPlaying(int handle)
{
    if (handle < 0 || handle >= NUM_CHANNELS)
        return 0;
    return chans[handle].active != 0;
}

int I_AnySoundStillPlaying(void)
{
    for (int c = 0; c < NUM_CHANNELS; c++)
        if (chans[c].active)
            return 1;
    return 0;
}

void I_UpdateSoundParams(int handle, int volume, int seperation, int pitch)
{
    if (handle < 0 || handle >= NUM_CHANNELS)
        return;
    taskENTER_CRITICAL(&snd_mux);
    chans[handle].vol = volume < 0 ? 0 : (volume > 127 ? 127 : volume);
    taskEXIT_CRITICAL(&snd_mux);
}

/* ---- music: not implemented (mus_card = 0) ---- */

void I_ShutdownMusic(void) {}
void I_InitMusic(void) {}
void I_PlaySong(int handle, int looping) {}
void I_PauseSong(int handle) {}
void I_ResumeSong(int handle) {}
void I_StopSong(int handle) {}
void I_UnRegisterSong(int handle) {}
int I_RegisterSong(const void *data, size_t len) { return 0; }
int I_RegisterMusic(const char *filename, musicinfo_t *song) { return 1; }
void I_SetMusicVolume(int volume) {}
