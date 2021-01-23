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
#include "SDL_rwops.h"
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
#include "imp/util/Types"

// 20120203 villsa - cvar for soundfont location
StringProperty s_soundfont("s_soundfont", "Soundfont location", "doomsnd.sf2"_sv);
IntProperty s_rate("s_rate", "Audio sample rate", 44100);
IntProperty s_channels("s_channels", "Channels", 2);

// 20210117 rewrite - Talon1024

// static SDL_sem* audio_access;
const int MAX_CHANNELS = 256;
static std::bitset<MAX_CHANNELS> active_channels{};
const int GROUP_GAMESOUNDS = 5;

enum audio_file_format {
    FORMAT_UNKNOWN,
    FORMAT_OTHER,
    FORMAT_MIDI
};

struct sndentry_t {
    size_t chunk_index;
    audio_file_format format;
};

typedef std::variant<String, size_t> audioref;

static std::unordered_map<audioref, sndentry_t> snd_entries; // Lump name/id to chunk index
static std::vector<Mix_Chunk*> audio_chunks; // Chunks - these store raw audio data

// Music names to MIDI/music lumps
static std::unordered_map<String, Mix_Music*> musics;

// Sources to channels, and channels to sources
// A source may play multiple sounds...
static std::unordered_multimap<sndsrc_t*, int> sources;
// But there is only one sound per mixer channel, and each sound may or may not have a source.
static std::unordered_map<int, sndsrc_t*> channels;

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
            // Get lump data
            size_t entry_index = entry->second;
            String data = iter->as_bytes();
            // Determine file format and load it
            audio_file_format fmt = FORMAT_OTHER;
            SDL_RWops* reader = SDL_RWFromConstMem(data.data(), data.size());
            size_t audio_pos = SDL_RWtell(reader);
            // Look at the header
            char format_id[4];
            SDL_RWread(reader, format_id, 1, 4);
            if (dstrncmp(format_id, "MThd", 4) == 0) {
                fmt = FORMAT_MIDI;
            }
            // Now load the data
            SDL_RWseek(reader, audio_pos, RW_SEEK_SET);
            Mix_Chunk* chunk = Mix_LoadWAV_RW(reader, 1);
            size_t chunk_index = audio_chunks.size();
            snd_entries.insert_or_assign(entry_index, sndentry_t{chunk_index, fmt});
            snd_entries.insert_or_assign(iter->lump_name().to_string(), sndentry_t{chunk_index, fmt});
            audio_chunks.push_back(chunk);
            // loaded += 1;
            // I_Printf("Entry %d: %s\n", entry_index, iter->lump_name().to_string().c_str());
        } else {
            // Assume it's music
            String data = iter->as_bytes();
            SDL_RWops* reader = SDL_RWFromConstMem(data.data(), data.size());
            Mix_Music* music = Mix_LoadMUS_RW(reader, 1);
            musics.insert_or_assign(iter->lump_name().to_string(), music);
            music_count += 1;
            // loaded += 1;
        }
    }
    if (wad::section_size(wad::Section::music)) {
        for (wad::LumpIterator iter = wad::section(wad::Section::music); iter.has_next(); iter.next()) {
            String data = iter->as_bytes();
            SDL_RWops* reader = SDL_RWFromConstMem(data.data(), data.size());
            Mix_Music* music = Mix_LoadMUS_RW(reader, 1);
            musics.insert_or_assign(iter->lump_name().to_string(), music);
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
    Uint16 audio_format = AUDIO_S16LSB;
    int audio_channels = s_channels;
    // Set up mixer
    if (Mix_OpenAudio(audio_rate, audio_format, audio_channels, 2048) < 0) {
        I_Printf("Could not open audio device! %s\n", SDL_GetError());
    } else {
        Mix_QuerySpec(&audio_rate, &audio_format, &audio_channels);
        I_Printf("Opened audio at %d Hz %d bit%s %s\n", audio_rate,
            (audio_format&0xFF),
            (SDL_AUDIO_ISFLOAT(audio_format) ? " (float)" : ""),
            (audio_channels > 2) ? "surround" :
            (audio_channels > 1) ? "stereo" : "mono");
    }
    Mix_AllocateChannels(MAX_CHANNELS);
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
        I_Printf("Soundfont not found!\n");
    }
    Audio_LoadTable();
    // audio_access = SDL_CreateSemaphore(1);
    return;
}

// Channel finished callback
static void I_ChannelFinished(int channel) {
    // I_Printf("Channel %d finished\n", channel);
    active_channels.set(channel, false);
    auto channel_element = channels.find(channel);
    if (channel_element != channels.end()) {
        // I_Printf("I_ChannelFinished: info refcount %d\n", info.use_count());
        sndsrc_t* origin = channel_element->second;
        for (auto source_element = sources.cbegin(); source_element != sources.cend(); source_element++) {
            if (source_element->first == origin && source_element->second == channel) {
                sources.erase(source_element);
            }
        }
    }
    channels.erase(channel);
}

//
// I_GetMaxChannels
//

int I_GetMaxChannels(void) {
    int last_bit = 0;
    for (size_t bit = 0; bit < MAX_CHANNELS; bit++) {
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
    if (!active_channels.test(c)) return nullptr;
    auto element = channels.find(c);
    if (element != channels.end()) {
        return element->second;
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
    std::for_each(musics.begin(), musics.end(), [](std::pair<audioref, Mix_Music*> ref) {
        Mix_FreeMusic(ref.second);
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
    Mix_HaltMusic();
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
    for (size_t curChannel = 0; curChannel < MAX_CHANNELS; curChannel++) {
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
    for (size_t curChannel = 0; curChannel < MAX_CHANNELS; curChannel++) {
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
        I_Printf("I_StartMusic: track %s not found!\n", mus_id.c_str());
        return;
    }
    Mix_Music* music = found->second;
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
        auto chuck = snd_entries.find(sfx_id);
        if (chuck == snd_entries.end()) {
            // Not found
            return;
        }
        size_t chunk_index = chuck->second.chunk_index;
        for (size_t curChannel = 0; curChannel < MAX_CHANNELS; curChannel++) {
            if (!Mix_Playing(curChannel)) { continue; }
            Mix_Chunk* chunk = Mix_GetChunk(curChannel);
            if (chunk == audio_chunks[chunk_index]) {
                I_RemoveSoundSource(curChannel, true);
            }
        }
        // Either stop all instances of the sound effect, or all sounds
        // from the given source
        return;
    }
    if (origin) {
        I_RemoveSoundSource(origin, true);
    }
}

// Boost MIDI sound volume
static void Effect_MIDIGain (int chan, void *stream, int len, void *udata) {
    int16* sample = (int16*) stream;
    // int16 minsample = 0;
    // int16 maxsample = 0;
    int length = len / sizeof(int16);
    for (int i = 0; i < length; i++) {
        sample[i] <<= 3; // sample[i] *= 8;
        // if (sample[i] < minsample) {
        //     minsample = sample[i];
        // }
        // if (sample[i] > maxsample) {
        //     maxsample = sample[i];
        // }
    }
    // I_Printf("minimum %d maximum %d\n", minsample, maxsample);
}

// Causes crashes for some reason...
static void Effect_Reverb (int chan, void *stream, int len, void *udata) {
    int reverb = *(int*)udata;
    I_Printf("Reverb: %d\n", reverb);
}

//
// I_StartSound
//

void I_StartSound(int sfx_id, sndsrc_t* origin, int volume, int pan, int reverb, uint32 flags) {
    // I_Printf("I_StartSound: pan %d, reverb %d\n", pan, reverb);
    auto chuck = snd_entries.find(sfx_id);
    if (chuck == snd_entries.end()) {
        I_Printf("Sound entry %d not found!\n", sfx_id);
        return;
    }
    size_t chunk_index = chuck->second.chunk_index;
    audio_file_format fmt = chuck->second.format;
    Mix_Chunk* chunk = audio_chunks[chunk_index];
    Uint8 leftPan = 255 - pan;
    Uint8 rightPan = pan;
    int loops = (flags & SFX_LOOP) ? -1 : 0;
    int curChannel = Mix_PlayChannel(-1, chunk, loops);
    Mix_Volume(curChannel, volume);
    Mix_SetPanning(curChannel, leftPan, rightPan);
    Mix_GroupChannel(curChannel, GROUP_GAMESOUNDS);
    if (fmt == FORMAT_MIDI) {
        Mix_RegisterEffect(curChannel, Effect_MIDIGain, nullptr, nullptr);
    }
    // Mix_RegisterEffect(curChannel, Effect_Reverb, nullptr, &reverb);
    active_channels.set(curChannel);
    if (origin) {
        sources.insert({origin, curChannel});
        // I_Printf("I_StartSound: info refcount %d\n", info.use_count());
        // infos.push_back(info);
    }
}

void I_RemoveSoundSource(int channel, bool halt) {
    active_channels.set(channel, false);
    if (halt) {
        Mix_HaltChannel(channel);
    }
    auto channel_element = channels.find(channel);
    if (channel_element != channels.end()) {
        sndsrc_t* origin = channel_element->second;
        // if(!channels.erase(channel)) {
        //     I_Printf("I_RemoveSoundSource (channel): Could not remove channel from sources!\n");
        // }
        channels.erase(channel);
        // if(!sources.erase(origin)) {
        //     I_Printf("I_RemoveSoundSource (channel): Could not remove origin from sources!\n");
        // }
        // Unlink all sounds on this channel from the source
        for (auto source_element = sources.cbegin(); source_element != sources.cend(); source_element++) {
            if (source_element->first == origin && source_element->second == channel) {
                sources.erase(source_element);
            }
        }
    }
}

void I_RemoveSoundSource(sndsrc_t* origin, bool halt) {
    size_t source_bucket = sources.bucket(origin);
    size_t source_count = sources.bucket_size(source_bucket);
    if (source_count > 0) {
        for (auto channel_element = sources.begin(source_bucket); channel_element != sources.end(source_bucket); channel_element++) {
            // Account for hash collisions
            if (channel_element->first != origin) {
                continue;
            }
            int channel = channel_element->second;
            active_channels.set(channel, false);
            if (halt) {
                Mix_HaltChannel(channel);
            }
            channels.erase(channel);
            // if (!channels.erase(channel)) {
            //     I_Printf("I_RemoveSoundSource (origin): Could not remove channel from sources!\n");
            // }
        }
        sources.erase(origin);
        // if (!sources.erase(origin)) {
        //     I_Printf("I_RemoveSoundSource (origin): Could not remove origin from sources!\n");
        // }
    }
}