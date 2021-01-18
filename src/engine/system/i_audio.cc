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


#include <map>
#include <utility>

#include "SDL.h"

#include "doomtype.h"
#include "doomdef.h"
#include "con_console.h"    // for cvars
#include <imp/Wad>
#include <imp/App>

#include "SDL_mixer.h"
#include "i_audio.h"

// 20120203 villsa - cvar for soundfont location
StringProperty s_soundfont("s_soundfont", "doomsnd.sf2 location", "doomsnd.sf2"_sv);

// 20210117 rewrite - Talon1024

static size_t channels_in_use = 0;

static std::map<String, size_t> snd_entries;
static std::map<sndsrc_t*, size_t> source_indices;
static std::vector<sndsrc_t*> sources;

size_t Seq_SoundLookup(String name) {
    auto found = snd_entries.find(name);
    if (found == snd_entries.end()) {
        // Not found
        return 0;
    }
    return found->second;
}

bool Audio_LoadTable() {
    // Parse the sound table lump and populate snd_names 
    std::vector< std::vector<StringView> > audio_lumps{};
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
                        audio_lumps.push_back(alternatives);
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
    for (auto entry : audio_lumps) {
        size_t loaded = 0;
        for (auto name : entry) {
            auto lump = wad::find(name);
            if (lump && lump->section() == wad::Section::sounds) {
                snd_entries.insert(std::make_pair(lump->lump_name(), snd_entries.size() + 1));
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

// I_InitSequencer
void I_InitSequencer() {
    return;
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
}

//
// I_ShutdownSound
//

void I_ShutdownSound(void) {
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
    return;
}

//
// I_StartSound
//

void I_StartSound(int sfx_id, sndsrc_t* origin, int volume, int pan, int reverb) {
    return;
}

void I_RemoveSoundSource(int c) {
    return;
}