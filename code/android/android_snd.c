/*
 * Android audio code for Quake3
 * Copyright (C) 2010 Roderick Colenbrander
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "../client/snd_local.h"

static int buf_size=0;
static int bytes_per_sample=0;
static int chunkSizeBytes=0;
static int dmapos=0;

int  (*getPos)(void);
void (*initAudio)(void *buffer, int size);
void (*writeAudio)(int offset, int length);

void setAudioCallbacks(void *get_pos, void *write_audio, void *init_audio)
{
    getPos = get_pos;
    writeAudio = write_audio;
    initAudio = init_audio;
}

qboolean SNDDMA_Init(void)
{
    Com_Printf("Initializing Android Sound subsystem\n");

    /* For now hardcode this all :) */
    dma.channels = 2;
    dma.samples = 32768;
    dma.samplebits = 16;

    dma.submission_chunk = 4096; /* This is in single samples, so this would equal 2048 frames (assuming stereo) in Android terminology */
    dma.speed = 44100; /* This is the native sample frequency of the Milestone */

    bytes_per_sample = dma.samplebits/8;
    buf_size = dma.samples * bytes_per_sample;
    dma.buffer = calloc(1, buf_size);

    chunkSizeBytes = dma.submission_chunk * bytes_per_sample;

    initAudio(dma.buffer, buf_size);

    return qtrue;
}


int SNDDMA_GetDMAPos(void)
{
    return dmapos;
}

void SNDDMA_Shutdown(void)
{
    Com_Printf("SNDDMA_ShutDown\n");
}

void SNDDMA_BeginPainting (void)
{
}

void requestAudioData(void)
{
    int offset = (dmapos * bytes_per_sample) & (buf_size - 1);
    writeAudio(offset, chunkSizeBytes);
    dmapos+=dma.submission_chunk;
}

void SNDDMA_Submit(void)
{
}
