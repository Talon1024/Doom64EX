// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 2007-2012 Samuel Villarreal
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
// 02111-1307, USA.
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION: Low-level audio API. Incorporates a sequencer system to
//              handle all sounds and music. All code related to the sequencer
//              is kept in it's own module and seperated from the rest of the
//              game code.
//
//-----------------------------------------------------------------------------


#include <algorithm>
#include <iterator>
#include <map>
#include <utility>

#include "SDL.h"

#include "SDL_audio.h"
#include "SDL_error.h"
#include "SDL_log.h"
#include "SDL_rwops.h"
#include "doomtype.h"
#include "doomdef.h"
#include "con_console.h"    // for cvars
#include <imp/Wad>
#include <imp/App>

#include "SDL_mixer.h"
#include "i_audio.h"
#include "imp/Property"

// 20120203 villsa - cvar for soundfont location
StringProperty s_soundfont("s_soundfont", "Soundfont location", "doomsnd.sf2"_sv);
IntProperty s_rate("s_rate", "Audio sample rate", 22050);
IntProperty s_format("s_format", "Audio format", AUDIO_S16LSB);
IntProperty s_channels("s_channels", "Channels", 2);

// 20210117 rewrite - Talon1024

static size_t channels_in_use = 0;

static std::map<String, size_t> snd_entries;
static std::vector<Mix_Chunk*> audio_chunks;
static std::vector<Mix_Music*> musics;
static std::vector<size_t> channels;
static std::vector<sndsrc_t*> sources;

size_t Seq_SoundLookup(String name) {
    auto found = snd_entries.find(name);
    if (found == snd_entries.end()) {
        // Not found
        return 0;
    }
    return found->second;
}

static bool Audio_LoadTable() {
    // Parse the sound table lump and populate snd_names 
    std::vector< std::vector<StringView> > audio_lump_names{};
    char* snddata;
    {
        auto sndtable_lump = wad::find("SNDTABLE");
        if (!sndtable_lump) {
            I_Printf("Cannot find sound table!\n");
            return false;
        }
        String sndtable = sndtable_lump->as_bytes();
        uint32_t end = sndtable.size();
        snddata = new char[end+1];
        sndtable.copy(snddata, end);
        snddata[end] = 0;
        uint32_t pos = 0;
        uint32_t str_length = 0;
        bool keep_parsing = true;
        std::vector<StringView> alternatives;
        while (pos < end) {
            if (isspace(snddata[pos])) {
                // Add the lump name to the list of alternatives
                if (str_length > 0) {
                    StringView sndname(snddata + pos - str_length, str_length);
                    alternatives.push_back(sndname);
                }
                if (snddata[pos] == '\n') {
                    // Line break - add a new entry to the audio lump list
                    if (alternatives.size()) {
                        audio_lump_names.push_back(alternatives);
                        alternatives = std::vector<StringView>{};
                    }
                    keep_parsing = true;
                }
                str_length = 0;
            } else if (snddata[pos] == '/' && snddata[pos+1] == '/') {
                // line comment
                keep_parsing = false;
            } else if (keep_parsing) {
                str_length++;
            }
            pos++;
        }
    }
    size_t entry_index = 0;
    for (auto entry : audio_lump_names) {
        size_t loaded = 0;
        for (auto name : entry) {
            auto lump = wad::find(name);
            if (lump && (lump->section() == wad::Section::sounds || lump->section() == wad::Section::music)) {
                String data = lump->as_bytes();
                SDL_RWops* reader = SDL_RWFromConstMem(data.data(), data.size());
                Mix_Chunk* chunk = Mix_LoadWAV_RW(reader, 1);
                audio_chunks.push_back(chunk);
                snd_entries.insert(std::make_pair(lump->lump_name(), entry_index));
                loaded += 1;
            }
        }
        if (!loaded) {
            I_Printf("Failed to register entry %u!\n", entry_index);
        }
        entry_index++;
    }
    I_Printf("Found %u sounds.\n", snd_entries.size());
    delete [] snddata;
    return true;
}

static void I_ChannelFinished(int channel);

// I_InitSequencer
void I_InitSequencer() {
    // Set mixer params
    int audio_rate = s_rate;
    Uint16 audio_format = s_format;
    int audio_channels = s_channels;
    // Set up mixer
    if (Mix_OpenAudio(audio_rate, audio_format, audio_channels, 4096) < 0) {
        SDL_Log("Could not open audio device! %s", SDL_GetError());
    } else {
        Mix_QuerySpec(&audio_rate, &audio_format, &audio_channels);
        SDL_Log("Opened audio at %d Hz %d bit%s %s", audio_rate,
            (audio_format&0xFF),
            (SDL_AUDIO_ISFLOAT(audio_format) ? " (float)" : ""),
            (audio_channels > 2) ? "surround" :
            (audio_channels > 1) ? "stereo" : "mono");
    }
    // Channel finished callback
    Mix_ChannelFinished(I_ChannelFinished);
    // Set up soundfont
    bool sffound = false;
    Optional<String> sfpath = s_soundfont->c_str();
    if (!s_soundfont->empty() && app::file_exists(sfpath.value())) {
        sffound = true;
        // SDL_Log("Soundfont %s doesn't exist!", s_soundfont->data());
    }
    // Search for soundfonts
    if (!sffound && (sfpath = app::find_data_file("doomsnd.sf2"))) {
        sffound = true;
    }
    if (!sffound && (sfpath = app::find_data_file("DOOMSND.DLS"))) {
        sffound = true;
    }
    if (sffound) {
        if(!Mix_SetSoundFonts(sfpath->data())) {
            SDL_Log("Could not set soundfont path to %s\n", s_soundfont->data());
        }
    } else {
        SDL_Log("Soundfont not found!");
    }
    Audio_LoadTable();
    return;
}

static void I_ChannelFinished(int channel) {
    channels_in_use--;
}

//
// I_GetMaxChannels
//

int I_GetMaxChannels(void) {
    return channels_in_use;
}

//
// I_GetVoiceCount
//

int I_GetVoiceCount() {
    return channels_in_use;
}

//
// I_GetSoundSource
//
sndsrc_t* I_GetSoundSource(int c) {
    if (c < sources.size()) {
        return sources[c];
    }
    return nullptr;
}

//
// I_UpdateChannel
//

void I_UpdateChannel(int c, int volume, int pan) {
    /*
    chan            = &playlist[c];
    chan->basevol   = (float)volume;
    chan->pan       = (byte)(pan >> 1);
    */
    Uint8 leftPan = pan;
    Uint8 rightPan = 255 - pan;
    Mix_SetPanning(c, leftPan, rightPan);
}

//
// I_ShutdownSound
//

void I_ShutdownSound(void) {
    std::for_each(audio_chunks.begin(), audio_chunks.end(), Mix_FreeChunk);
    Mix_CloseAudio();
}

//
// I_SetMusicVolume
//

void I_SetMusicVolume(float volume) {
    int mixVolume = (int) volume * 128;
    Mix_VolumeMusic(mixVolume);
}

//
// I_SetSoundVolume
//

void I_SetSoundVolume(float volume) {
    int mixVolume = (int) volume * 128;
    for (size_t curChannel = 0; curChannel < channels_in_use; curChannel++) {
        Mix_Volume(curChannel, mixVolume);
    }
}

//
// I_ResetSound
//

void I_ResetSound(void) {
    return;
}

//
// I_PauseSound
//

void I_PauseSound(void) {
    for (size_t curChannel = 0; curChannel < channels_in_use; curChannel++) {
        Mix_Pause(curChannel);
    }
}

//
// I_ResumeSound
//

void I_ResumeSound(void) {
    for (size_t curChannel = 0; curChannel < channels_in_use; curChannel++) {
        Mix_Resume(curChannel);
    }
}

//
// I_SetGain
//

void I_SetGain(float db) {
    return;
}

//
// I_StartMusic
//

void I_StartMusic(int mus_id) {
    return;
}

//
// I_StopSound
//

void I_StopSound(sndsrc_t* origin, int sfx_id) {
    size_t channels_stopped = 0;
    std::vector<size_t> to_erase{};
    for (size_t channel = 0; channel < channels_in_use; channel++) {
        Mix_Chunk* chunk = Mix_GetChunk(channel);
        if (chunk == audio_chunks[sfx_id]) {
            Mix_HaltChannel(channel);
            channels_stopped++;
        }
    }
    for (size_t index = 0; index < sources.size(); index++) {
        sndsrc_t* source = sources[index];
        if (source == origin) {
            Mix_HaltChannel(channels[index]);
            to_erase.push_back(index);
            channels_stopped++;
        }
    }
    std::for_each(to_erase.rbegin(), to_erase.rend(), [](size_t i) {
        sources.erase(sources.begin() + i);
        channels.erase(channels.begin() + 1);
    });
    channels_in_use -= channels_stopped;
}

//
// I_StartSound
//

void I_StartSound(int sfx_id, sndsrc_t* origin, int volume, int pan, int reverb) {
    Mix_Chunk* chunk = audio_chunks[sfx_id];
    size_t curChannel = ++channels_in_use;
    Uint8 leftPan = pan;
    Uint8 rightPan = 255 - pan;
    Mix_Volume(curChannel, volume);
    Mix_SetPanning(curChannel, leftPan, rightPan);
    Mix_PlayChannel(curChannel, chunk, 0);
    if (origin) {
        sources.push_back(origin);
        channels.push_back(curChannel);
    }
}

void I_RemoveSoundSource(int c) {
    Mix_Volume(c, 128);
    Mix_SetPanning(c, 128, 128);
}