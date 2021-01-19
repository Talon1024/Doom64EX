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
#include <memory>
#include <unordered_map>
#include <bitset> // Use for "channels_in_use"
#include <variant>

#include "SDL.h"

#include "SDL_mutex.h"
#include "doomtype.h"
#include "doomdef.h"
#include "con_console.h"    // for cvars
#include <imp/Wad>
#include <imp/App>

#include "SDL_mixer.h"
#include "i_audio.h"
#include "i_system.h"
#include "imp/Property"
#include "imp/util/StringView"

// 20120203 villsa - cvar for soundfont location
StringProperty s_soundfont("s_soundfont", "Soundfont location", "doomsnd.sf2"_sv);
IntProperty s_rate("s_rate", "Audio sample rate", 44100);
IntProperty s_format("s_format", "Audio format", AUDIO_S16LSB);
IntProperty s_channels("s_channels", "Channels", 2);

// 20210117 rewrite - Talon1024

// static SDL_sem* audio_access;
const int MAX_CHANNELS = 256;
static std::bitset<MAX_CHANNELS> active_channels{};
const int GROUP_GAMESOUNDS = 5;

struct sndsrcinfo_t {
    sndsrcinfo_t(sndsrc_t* source, size_t channel) : source(source), channel(channel) {}
    sndsrc_t* source;
    size_t channel;
};

struct musicinfo_t {
    Mix_Music* music;
    size_t id;
    uint8_t refcount;
};

typedef std::variant<sndsrc_t*, size_t> sourceref;
typedef std::variant<String, size_t> audioref;

static std::unordered_map<audioref, size_t> snd_entries; // Lump name/id to chunk index
static std::vector<Mix_Chunk*> audio_chunks; // Chunks
static std::unordered_map<String, musicinfo_t*> musics;
static std::unordered_map<sourceref, std::shared_ptr<sndsrcinfo_t>> sources;

/*
template<size_t size> static void PrintBitset(std::bitset<size>& bitset) {
    for (size_t i = 0; i < size; i++) {
        I_Printf("%s", bitset[i] ? "1" : "0");
    }
    I_Printf("\n");
}

size_t Seq_SoundLookup(String name) {
    auto found = musics.find(name);
    if (found == musics.end()) {
        // Not found
        return -1;
    }
    return found->second->id;
}
*/

static bool Audio_LoadTable() {
    // Parse the sound table lump and populate snd_names 
    // std::vector< std::vector<StringView> > audio_lump_names{};
    std::unordered_map<String, size_t> audio_lump_names;
    char* snddata;
    {
        size_t entry_index = 0;
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
        std::vector<StringView> alternatives{};
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
                        for (StringView name : alternatives) {
                            /*
                            String naem = name.to_string();
                            for(size_t i = 0; i < naem.size(); i++) {
                                if (naem[i] >= 'a') {
                                    naem[i] -= 32;
                                }
                            }
                            */
                            audio_lump_names.insert(std::make_pair(name.to_string(), entry_index));
                        }
                        alternatives = std::vector<StringView>{};
                        entry_index++;
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
    size_t music_count = 0;
    for (wad::LumpIterator iter = wad::section(wad::Section::sounds); iter.has_next(); iter.next()) {
        // size_t loaded = 0;
        auto entry = audio_lump_names.find(iter->lump_name().to_string());
        if (entry != audio_lump_names.end()) {
            size_t entry_index = entry->second;
            String data = iter->as_bytes();
            SDL_RWops* reader = SDL_RWFromConstMem(data.data(), data.size());
            Mix_Chunk* chunk = Mix_LoadWAV_RW(reader, 1);
            snd_entries.insert_or_assign(entry_index, audio_chunks.size());snd_entries.insert_or_assign(iter->lump_name().to_string(), audio_chunks.size());
            audio_chunks.push_back(chunk);
            // loaded += 1;
            // I_Printf("Entry %d: %s\n", entry_index, iter->lump_name().to_string().c_str());
        } else {
            // Assume it's music
            String data = iter->as_bytes();
            SDL_RWops* reader = SDL_RWFromConstMem(data.data(), data.size());
            Mix_Music* music = Mix_LoadMUS_RW(reader, 1);
            musicinfo_t* info = new musicinfo_t {music, music_count, 2};
            musics.insert_or_assign(iter->lump_name().to_string(), info);
            music_count += 1;
            // loaded += 1;
        }
    }
    if (wad::section_size(wad::Section::music)) {
        for (wad::LumpIterator iter = wad::section(wad::Section::music); iter.has_next(); iter.next()) {
            String data = iter->as_bytes();
            SDL_RWops* reader = SDL_RWFromConstMem(data.data(), data.size());
            Mix_Music* music = Mix_LoadMUS_RW(reader, 1);
            musicinfo_t* info = new musicinfo_t {music, music_count, 2};
            musics.insert_or_assign(iter->lump_name().to_string(), info);
            music_count += 1;
        }
    }
    I_Printf("Found %u sounds and %u musics.\n", audio_chunks.size(), musics.size());
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
    if (Mix_OpenAudio(audio_rate, audio_format, audio_channels, 2048) < 0) {
        I_Printf("Could not open audio device! %s", SDL_GetError());
    } else {
        Mix_QuerySpec(&audio_rate, &audio_format, &audio_channels);
        I_Printf("Opened audio at %d Hz %d bit%s %s", audio_rate,
            (audio_format&0xFF),
            (SDL_AUDIO_ISFLOAT(audio_format) ? " (float)" : ""),
            (audio_channels > 2) ? "surround" :
            (audio_channels > 1) ? "stereo" : "mono");
    }
    Mix_AllocateChannels(256);
    // Channel finished callback
    Mix_ChannelFinished(I_ChannelFinished);
    // Set up soundfont
    bool sffound = false;
    Optional<String> sfpath = s_soundfont->c_str();
    if (!s_soundfont->empty() && app::file_exists(sfpath.value())) {
        sffound = true;
        // I_Printf("Soundfont %s doesn't exist!", s_soundfont->data());
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
            I_Printf("Could not set soundfont path to %s\n", s_soundfont->data());
        }
    } else {
        I_Printf("Soundfont not found!");
    }
    Audio_LoadTable();
    // audio_access = SDL_CreateSemaphore(1);
    return;
}

static void I_ChannelFinished(int channel) {
    // I_Printf("Channel %d finished\n", channel);
    active_channels.set(channel, false);
    auto source = sources.find(channel);
    if (source != sources.end()) {
        std::weak_ptr<sndsrcinfo_t> infop(source->second);
        // I_Printf("I_ChannelFinished: info refcount %d\n", info.use_count());
        sndsrc_t* origin = infop.lock()->source;
        sources.erase(channel);
        sources.erase(origin);
    }
}

//
// I_GetMaxChannels
//

int I_GetMaxChannels(void) {
    int last_bit = 0;
    for (size_t bit = 0; bit < active_channels.size(); bit++) {
        if (active_channels[bit]) { last_bit = bit; }
    }
    return last_bit;
}

//
// I_GetVoiceCount
//

int I_GetVoiceCount() {
    return Mix_Playing(-1);
}

//
// I_GetSoundSource
//
sndsrc_t* I_GetSoundSource(int c) {
    if (!active_channels[c]) return nullptr;
    auto source = sources.find(c);
    if (source != sources.end()) {
        std::weak_ptr<sndsrcinfo_t> infop(source->second);
        if (!infop.expired()) {
            return infop.lock()->source;
        }
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
    // I_Printf("Updating channel %d\n", c);
    if (!active_channels[c]) { return; }
    Uint8 leftPan = 255 - pan;
    Uint8 rightPan = pan;
    Mix_Volume(c, volume);
    Mix_SetPanning(c, leftPan, rightPan);
}

//
// I_ShutdownSound
//

void I_ShutdownSound(void) {
    std::for_each(audio_chunks.begin(), audio_chunks.end(), Mix_FreeChunk);
    std::for_each(musics.begin(), musics.end(), [](std::pair<audioref, musicinfo_t*> ref) {
        Mix_FreeMusic(ref.second->music);
        delete ref.second;
    });
    Mix_CloseAudio();
}

//
// I_SetMusicVolume
//

void I_SetMusicVolume(float volume) {
    // 0 <= volume <= 100
    int mixVolume = (int) (volume * 128 / 100);
    Mix_VolumeMusic(mixVolume);
}

//
// I_SetSoundVolume
//

void I_SetSoundVolume(float volume) {
    // 0 <= volume <= 100
    float factor = volume / 100;
    int mixVolume = (int) (volume * 128 * factor / 100);
    for (size_t curChannel = 0; curChannel < MAX_CHANNELS; curChannel++) {
        Mix_Volume(curChannel, mixVolume);
    }
}

//
// I_ResetSound
//

void I_ResetSound(void) {
    Mix_HaltGroup(GROUP_GAMESOUNDS);
    sources.clear();
    active_channels.reset();
    return;
}

//
// I_PauseSound
//

void I_PauseSound(void) {
    Mix_PauseMusic();
    for (size_t curChannel = 0; curChannel < 256; curChannel++) {
        if (active_channels[curChannel]) {
            Mix_Pause(curChannel);
        }
    }
}

//
// I_ResumeSound
//

void I_ResumeSound(void) {
    Mix_ResumeMusic();
    for (size_t curChannel = 0; curChannel < 256; curChannel++) {
        if (active_channels[curChannel]) {
            Mix_Resume(curChannel);
        }
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

/*
void I_StartMusic(int mus_id) {
    Mix_Music* music = musics[mus_id]->music;
    Mix_PlayMusic(music, -1);
}
*/

void I_StartMusic(String &mus_id) {
    auto found = musics.find(mus_id);
    if (found == musics.end()) {
        return;
    }
    Mix_Music* music = found->second->music;
    Mix_PlayMusic(music, -1);
}

void I_StopMusic() {
    Mix_HaltMusic();
}

//
// I_StopSound
//

void I_StopSound(sndsrc_t* origin, int sfx_id) {
    if (sfx_id > 0) {
        for (size_t curChannel = 0; curChannel < MAX_CHANNELS; curChannel++) {
            if (!active_channels[curChannel]) { continue; }
            Mix_Chunk* chunk = Mix_GetChunk(curChannel);
            if (chunk == audio_chunks[sfx_id]) {
                active_channels.set(curChannel, false);
                Mix_HaltChannel(curChannel);
                // Erased by channel finished callback
                /*
                auto source = sources.find(channel);
                if (source != sources.end()) {
                    std::shared_ptr<sndsrcinfo_t> info = source->second;
                    sndsrc_t* origin = info->source;
                    sources.erase(origin);
                    sources.erase(channel);
                }
                */
            }
        }
    }
    if (origin) {
        auto source = sources.find(origin);
        if (source != sources.end()) {
            std::weak_ptr<sndsrcinfo_t> infop(source->second);
            // I_Printf("I_StopSound: info refcount %d\n", info.use_count());
            size_t channel = infop.lock()->channel;
            active_channels.set(channel, false);
            Mix_HaltChannel(channel);
            /*
            auto source = sources.find(origin);
            if (source != sources.end()) {
                std::shared_ptr<sndsrcinfo_t> info = sources[origin];
                size_t channel = info->channel;
                sources.erase(channel);
                sources.erase(origin);
            }
            */
        }
    }
}

//
// I_StartSound
//

void I_StartSound(int sfx_id, sndsrc_t* origin, int volume, int pan, int reverb) {
    auto chuck = snd_entries.find(sfx_id);
    if (chuck == snd_entries.end()) {
        I_Printf("Sound entry %d not found!\n", sfx_id);
        return;
    }
    size_t chunk_index = chuck->second;
    Mix_Chunk* chunk = audio_chunks[chunk_index];
    Uint8 leftPan = 255 - pan;
    Uint8 rightPan = pan;
    int curChannel = Mix_PlayChannel(-1, chunk, 0);
    Mix_Volume(curChannel, volume);
    Mix_SetPanning(curChannel, leftPan, rightPan);
    Mix_GroupChannel(curChannel, GROUP_GAMESOUNDS);
    active_channels.set(curChannel);
    if (origin) {
        std::shared_ptr<sndsrcinfo_t> info = std::make_shared<sndsrcinfo_t>(origin, curChannel);
        sources.insert_or_assign(curChannel, info);
        sources.insert_or_assign(origin, info);
        // I_Printf("I_StartSound: info refcount %d\n", info.use_count());
        // infos.push_back(info);
    }
}

void I_RemoveSoundSource(int channel) {
    auto source = sources.find(channel);
    if (source != sources.end()) {
        std::weak_ptr<sndsrcinfo_t> infop(source->second);
        // I_Printf("I_RemoveSoundSource: info refcount %d\n", infop.use_count());
        sndsrc_t* origin = infop.lock()->source;
        sources.erase(channel);
        sources.erase(origin);
    }
}

void I_RemoveSoundSource(sndsrc_t* origin) {
    auto source = sources.find(origin);
    if (source != sources.end()) {
        std::weak_ptr<sndsrcinfo_t> infop(source->second);
        // I_Printf("I_RemoveSoundSource: info refcount %d\n", infop.use_count());
        size_t channel = infop.lock()->channel;
        sources.erase(channel);
        sources.erase(origin);
    }
}