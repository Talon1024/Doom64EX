/*
  PLAYWAVE:  A test application for the SDL mixer library.
  Copyright (C) 1997-2017 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/* $Id$ */

#include "SDL_error.h"
#include "SDL_events.h"
#include "SDL_keycode.h"
#include "SDL_pixels.h"
#include "SDL_render.h"
#include "SDL_rwops.h"
#include "SDL_stdinc.h"
#include "SDL_surface.h"
#include "SDL_video.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef unix
#include <unistd.h>
#endif

#include "SDL.h"
#include "SDL_mixer.h"
#include "linkedlist.h"
// #include "bitcrush.h"

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

static int audio_open = 0;
// NOTE: wave[0] will be unused due to the first file being used for "background music".
static Mix_Chunk** wave = NULL;
static Mix_Music* bgmus = NULL;
static Uint16 audio_count = 0;
static Uint8 channels_in_use = 0;
// I hate global variables, but this sacrifice must be made...
static SDL_Window* window;
static SDL_Renderer* renderer;

static volatile Uint16 channel_is_done = 0;
static void SDLCALL channel_complete_callback (int chan)
{
    SDL_Log("We were just alerted that Mixer channel #%d is done.\n", chan);
    channel_is_done |= (1 << chan);
    channels_in_use--;
}


/* rcg06192001 abstract this out for testing purposes. */
static int still_playing(void)
{
    return !(channel_is_done & 1);
}

static void CleanUp(int exitcode)
{
    // Mix_Pause(0);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (bgmus) {
        Mix_FreeMusic(bgmus);
        for (Uint8 cur_wave = 0; cur_wave < audio_count - 1; cur_wave++) {
            if (wave[cur_wave] != NULL){
                Mix_FreeChunk(wave[cur_wave]);
            }
        }
    }
    free(wave);
    if (audio_open) {
        Mix_CloseAudio();
        audio_open = 0;
    }
    SDL_Quit();

    exit(exitcode);
}

static void Usage(char *argv0)
{
    SDL_Log("Usage: %s [-8] [-f32] [-r rate] [-c channels] [-f] [-F] [-l] [-m] [-sf soundfont_file] <wavefile>\n\nNOTE: You need to load at least one audio file!\n", argv0);
}

static SDL_Texture* makeChanNumsTexture() {
    int channumgfx[] = {0x10000000,0x24301818,0x08081024,0x10040810,0x3C3C2410,0x00000018,0x24000000,0x20243C3C,0x3C3C3C20,0x04240404,0x3C042404,0x0000003C,0x3C000000,0x24043C3C,0x3C3C0824,0x10042408,0x3C100424,0x00000004};
    SDL_Surface* channumsurf;
    if(!(channumsurf = SDL_CreateRGBSurfaceWithFormatFrom(channumgfx, 24, 24, 1, 3, SDL_PIXELFORMAT_INDEX1LSB))) {
        SDL_Log("Could not load channums image: %s\n", SDL_GetError());
        CleanUp(255);
    }
    // Set up palette
    SDL_Palette* pal = SDL_AllocPalette(2);
    pal->colors[0].r = 0;
    pal->colors[0].g = 0;
    pal->colors[0].b = 0;
    pal->colors[0].a = 0;
    pal->colors[1].r = 100;
    pal->colors[1].g = 140;
    pal->colors[1].b = 255;
    pal->colors[1].a = 255;
    SDL_SetSurfacePalette(channumsurf, pal);
    // Upload
    SDL_Texture* channumtex = SDL_CreateTextureFromSurface(renderer, channumsurf);
    SDL_FreePalette(pal);
    SDL_FreeSurface(channumsurf);
    return channumtex;
}
/*
 * rcg06182001 This is sick, but cool.
 *
 *  Actually, it's meant to be an example of how to manipulate a voice
 *  without having to use the mixer effects API. This is more processing
 *  up front, but no extra during the mixing process. Also, in a case like
 *  this, when you need to touch the whole sample at once, it's the only
 *  option you've got. And, with the effects API, you are altering a copy of
 *  the original sample for each playback, and thus, your changes aren't
 *  permanent; here, you've got a reversed sample, and that's that until
 *  you either reverse it again, or reload it.
 */
static void flip_sample(Mix_Chunk *wave)
{
    Uint16 format;
    int channels, i, incr;
    Uint8 *start = wave->abuf;
    Uint8 *end = wave->abuf + wave->alen;

    Mix_QuerySpec(NULL, &format, &channels);
    incr = (format & 0xFF) * channels;

    end -= incr;

    switch (incr) {
        case 8:
            for (i = wave->alen / 2; i >= 0; i -= 1) {
                Uint8 tmp = *start;
                *start = *end;
                *end = tmp;
                start++;
                end--;
            }
            break;

        case 16:
            for (i = wave->alen / 2; i >= 0; i -= 2) {
                Uint16 tmp = *start;
                *((Uint16 *) start) = *((Uint16 *) end);
                *((Uint16 *) end) = tmp;
                start += 2;
                end -= 2;
            }
            break;

        case 32:
            for (i = wave->alen / 2; i >= 0; i -= 4) {
                Uint32 tmp = *start;
                *((Uint32 *) start) = *((Uint32 *) end);
                *((Uint32 *) end) = tmp;
                start += 4;
                end -= 4;
            }
            break;

        default:
            SDL_Log("Unhandled format in sample flipping.\n");
            return;
    }
}

int main(int argc, char *argv[])
{
    //printf("Bits per pixel in format SDL_PIXELFORMAT_INDEX1LSB: %d\n", SDL_BITSPERPIXEL(SDL_PIXELFORMAT_INDEX1LSB));
    //printf("SDL_PIXELFORMAT_INDEX1LSB is paletted?: %d\n", SDL_ISPIXELFORMAT_INDEXED(SDL_PIXELFORMAT_INDEX1LSB));
    int audio_rate;
    Uint16 audio_format;
    int audio_channels;
    int loops = 0;
    Uint8 cur_arg;
    int reverse_stereo = 0;
    int reverse_sample = 0;
    Uint8 bgm_paused = 0;

#ifdef HAVE_SETBUF
    setbuf(stdout, NULL);    /* rcg06132001 for debugging purposes. */
    setbuf(stderr, NULL);    /* rcg06192001 for debugging purposes, too. */
#endif

    /* Initialize variables */
    audio_rate = MIX_DEFAULT_FREQUENCY;
    audio_format = MIX_DEFAULT_FORMAT;
    audio_channels = 2;

    filellist_t* filesHead = filellist_new();
    filellist_t* prevfile = filesHead;
    filellist_t* file = filesHead;
    char *soundfont_path = NULL;

    /* Check command line usage */
    for (cur_arg = 1; argv[cur_arg]; ++cur_arg) {
      if (argv[cur_arg][0] != '-') {
        if (audio_count > 1) {
          // More than one sound is loaded - make the first one loop in
          // the background while the user can listen to the other ones
          loops = -1;
        }
        file->fname = argv[cur_arg];
        printf("%s added to the linked list\n", file->fname);
        audio_count++;
        file->next = filellist_new();
        prevfile = file;
        file = file->next;
        continue;
      }
      if ((strcmp(argv[cur_arg], "-r") == 0) && argv[cur_arg + 1]) {
        ++cur_arg;
        audio_rate = atoi(argv[cur_arg]);
      } else if (strcmp(argv[cur_arg], "-m") == 0) {
        audio_channels = 1;
      } else if ((strcmp(argv[cur_arg], "-c") == 0) && argv[cur_arg + 1]) {
        ++cur_arg;
        audio_channels = atoi(argv[cur_arg]);
      } else if (strcmp(argv[cur_arg], "-l") == 0) {
        loops = -1;
      } else if (strcmp(argv[cur_arg], "-8") == 0) {
        audio_format = AUDIO_U8;
      } else if (strcmp(argv[cur_arg], "-f32") == 0) {
        audio_format = AUDIO_F32;
      } else if (strcmp(argv[cur_arg], "-f") ==
                 0) { /* rcg06122001 flip stereo */
        reverse_stereo = 1;
      } else if (strcmp(argv[cur_arg], "-F") ==
                 0) { /* rcg06172001 flip sample */
        reverse_sample = 1;
      } else if ((strcmp(argv[cur_arg], "-sf") == 0) && argv[cur_arg + 1]) {
        soundfont_path = argv[++cur_arg];
      } else {
        Usage(argv[0]);
        filellist_free(filesHead);
        return (1);
      }
    }
    free(prevfile->next);
    prevfile->next = NULL;
    if (!audio_count) {
        SDL_Log("No audio loaded. Exiting...");
        Usage(argv[0]);
        filellist_free(filesHead);
        CleanUp(1);
        return 1;
    }

    /* Initialize the SDL library */
    if (SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO) < 0) {
        SDL_Log("Couldn't initialize SDL: %s\n",SDL_GetError());
        return(255);
    }

    SDL_CreateWindowAndRenderer(100, 100, SDL_WINDOW_OPENGL, &window, &renderer);
    SDL_SetWindowTitle(window, "PlayWave2");
    SDL_Rect channumrects[] = {
        {0, 0, 0, 0}, // Window rectangle
        {0, 0, 8, 8},
        {8, 0, 8, 8},
        {16, 0, 8, 8},
        {0, 8, 8, 8},
        {8, 8, 8, 8},
        {16, 8, 8, 8},
        {0, 16, 8, 8},
        {8, 16, 8, 8},
        {16, 16, 8, 8},
    };
    SDL_Texture* channumtex = makeChanNumsTexture();
    SDL_GetWindowSize(window, &channumrects[0].w, &channumrects[0].h);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderFillRect(renderer, channumrects);

#ifdef HAVE_SIGNAL_H
    signal(SIGINT, CleanUp);
    signal(SIGTERM, CleanUp);
#endif

    /* Open the audio device */
    if (Mix_OpenAudio(audio_rate, audio_format, audio_channels, 4096) < 0) {
        SDL_Log("Couldn't open audio: %s\n", SDL_GetError());
        CleanUp(2);
    } else {
        Mix_QuerySpec(&audio_rate, &audio_format, &audio_channels);
        SDL_Log("Opened audio at %d Hz %d bit%s %s", audio_rate,
            (audio_format&0xFF),
            (SDL_AUDIO_ISFLOAT(audio_format) ? " (float)" : ""),
            (audio_channels > 2) ? "surround" :
            (audio_channels > 1) ? "stereo" : "mono");
        if (loops) {
          SDL_Log(" (looping)\n");
        } else {
          putchar('\n');
        }
    }
    audio_open = 1;

    // Set up soundfont
    if (soundfont_path) {
        SDL_RWops* file_check = SDL_RWFromFile(soundfont_path, "rb");
        if (!file_check) {
            SDL_Log("Soundfont %s doesn't exist!", soundfont_path);
            CleanUp(1);
        }
        if (!Mix_SetSoundFonts(soundfont_path)) {
            SDL_Log("Could not set soundfont path to %s\n", soundfont_path);
            CleanUp(1);
        }
    }

    /*
    Uint8 bitsToCrush = 4;
    Mix_RegisterEffect(0, bitCrusher, NULL, &bitsToCrush);
    */

    
    file = filesHead;
    // Load the bg music
    if ((bgmus = Mix_LoadMUS(file->fname)) == NULL) {
        SDL_Log("Couldn't load BGM %s: %s", file->fname, SDL_GetError());
        CleanUp(1);
    }
    Uint8 audio_good = 0;
    if (audio_count > 1) {
        if (audio_count > 10) {
            audio_count = 10;
        }
        wave = malloc(sizeof(Mix_Chunk*) * audio_count - 1);
        file = file->next;
        audio_good = 0;
        /* Load the requested wave files */
        while(file) {
          if (audio_good == 9) {
            break;
          }
          wave[audio_good] = Mix_LoadWAV(file->fname);
          if (wave[audio_good] == NULL) {
            SDL_Log("Couldn't load sound %s: %s\n", file->fname,
                    SDL_GetError());
            file = file->next;
            continue;
          } else if (wave[audio_good]->alen == 0) {
            SDL_Log("Sound is empty! Skipping...");
            Mix_FreeChunk(wave[audio_good]);
            file = file->next;
            continue;
          } else {
            Uint32 totalSeconds =
                wave[audio_good]->alen / audio_rate /
                ((audio_format & SDL_AUDIO_MASK_BITSIZE) >> 3) / audio_channels;
            Uint32 seconds = totalSeconds % 60;
            Uint32 minutes = totalSeconds / 60.0;
            SDL_Log("Audio buffer for %s is %u bytes long. (That's %.3f MiB, "
                    "%d:%02d)",
                    file->fname, wave[audio_good]->alen,
                    wave[audio_good]->alen / (double)(1 << 20), minutes, seconds);
          }
            if (reverse_sample) {
              flip_sample(wave[audio_good]);
            }
            file = file->next;
            audio_good++;
        }
    }
    filellist_free(filesHead);

    Mix_ChannelFinished(channel_complete_callback);

    if ((!Mix_SetReverseStereo(MIX_CHANNEL_POST, reverse_stereo)) && (reverse_stereo))
    {
        SDL_Log("Failed to set up reverse stereo effect!\n");
        SDL_Log("Reason: [%s].\n", Mix_GetError());
    }

    Mix_VolumeMusic(128);
    Mix_PlayMusic(bgmus, loops);

    puts("Now playing!\n");
    if (audio_good > 1) {
      printf("To play any of the other sounds, press 1-%d on your keyboard.\n",
             audio_good);
    }
    SDL_Event curEvent;
    while (still_playing()) {
        if (SDL_PollEvent(&curEvent)) {
            if (curEvent.type == SDL_KEYUP) {
                Uint8 chanToPlay = 100;
                switch(curEvent.key.keysym.sym) {
                case SDLK_1:
                    chanToPlay = 0;
                    break;
                case SDLK_2:
                    chanToPlay = 1;
                    break;
                case SDLK_3:
                    chanToPlay = 2;
                    break;
                case SDLK_4:
                    chanToPlay = 3;
                    break;
                case SDLK_5:
                    chanToPlay = 4;
                    break;
                case SDLK_6:
                    chanToPlay = 5;
                    break;
                case SDLK_7:
                    chanToPlay = 6;
                    break;
                case SDLK_8:
                    chanToPlay = 7;
                    break;
                case SDLK_9:
                    chanToPlay = 8;
                    break;
                case SDLK_ESCAPE:
                    channel_is_done |= 1;
                    break;
                case SDLK_PAUSE:
                    if (bgm_paused) {
                        bgm_paused = 0;
                        Mix_ResumeMusic();
                    } else {
                        bgm_paused = 1;
                        Mix_PauseMusic();
                    }
                    break;
                }
                if (chanToPlay < audio_good) {
                  Mix_PlayChannel(++channels_in_use, wave[chanToPlay], 0);
                  // Mix_PlayChannel(chanToPlay, wave[chanToPlay], 0);
                  SDL_RenderCopy(renderer, channumtex,
                                 channumrects + chanToPlay + 1,
                                 channumrects + channels_in_use);
                }
            } else if (curEvent.type == SDL_WINDOWEVENT) {
                if (curEvent.window.event == SDL_WINDOWEVENT_CLOSE) {
                    channel_is_done |= 1;
                }
            }
        }
        if (channel_is_done) {
          for (Uint8 i = 1; i <= audio_good; i++) {
            Uint16 bit = (1 << i);
            if (channel_is_done & bit) {
              SDL_RenderFillRect(renderer, channumrects + i);
              channel_is_done &= ~bit;
            }
          }
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    } /* while still_playing() loop... */

    CleanUp(0);

    /* Not reached, but fixes compiler warnings */
    return 0;
}

/* end of playwave.c ... */

/* vi: set ts=4 sw=4 expandtab: */
