#include <fstream>
#include <sstream>
#include "WadFormat.hh"
#include "doomdef.h"
#include "imp/Wad"

namespace {
  template <class T>
  void read_into(std::istream& s, T& x)
  {
      s.read(reinterpret_cast<char*>(&x), sizeof(T));
  }

  struct Header {
      char id[4];
      uint32 numlumps;
      uint32 infotableofs;
  };

  struct Directory {
      uint32 filepos;
      uint32 size;
      char name[8];
  };

  static_assert(sizeof(Header) == 12, "WAD Header must have a sizeof of 12 bytes");
  static_assert(sizeof(Directory) == 16, "WAD Directory must have a sizeof of 16 bytes");

  struct Info {
      size_t filepos;
      size_t size;
      std::istringstream cache;

      Info(size_t filepos, size_t size):
          filepos(filepos),
          size(size) {}
  };

  class DoomLump : public wad::BasicLump {
      std::istringstream &stream_;

  public:
      DoomLump(size_t lump_id, std::istringstream& stream):
          wad::BasicLump(lump_id),
          stream_(stream) {}

      std::istream& stream() override
      { return stream_; }

      String as_bytes() override
      { return stream_.str(); }
  };

  class DoomFormat : public wad::Format {
      std::ifstream stream_;
      Vector<Info> table_;

  public:
      DoomFormat(StringView path):
          stream_(path, std::ios::binary)
      {
          stream_.exceptions(stream_.failbit | stream_.badbit);
      }

      ~DoomFormat() override {}

      Vector<wad::LumpInfo> read_all() override
      {
          Vector<wad::LumpInfo> lumps;
          wad::Section section {};
          Header header;
          read_into(stream_, header);

          stream_.seekg(header.infotableofs);
          size_t numlumps = header.numlumps;

          table_.clear();
          for (size_t i = 0; i < numlumps; ++i) {
              Directory dir;
              read_into(stream_, dir);

              std::size_t size {};
              while (size < 8 && dir.name[size]) ++size;
              String name { dir.name, size };
              wad::Section lumpSection = section;

              if (dir.size == 0) {
                  if (name == "T_START") {
                      section = wad::Section::textures;
                  } else if (name == "G_START") {
                      section = wad::Section::graphics;
                  } else if (name == "S_START") {
                      section = wad::Section::sprites;
                  } else if (name == "DM_START") {
                      section = wad::Section::music;
                  } else if (name == "DS_START") {
                      section = wad::Section::sounds;
                  } else if (name == "T_END") {
                      section = wad::Section::normal;
                  } else if (name == "G_END") {
                      section = wad::Section::normal;
                  } else if (name == "S_END") {
                      section = wad::Section::normal;
                  } else if (name == "DM_END") {
                      section = wad::Section::normal;
                  } else if (name == "DS_END") {
                      section = wad::Section::normal;
                  } else if (name == "ENDOFWAD") {
                      break;
                  } else {
                      println("Unknown WAD directory: {}", name);
                  }
                  continue;
              } else if (section == wad::Section::normal) {
                  if (dstrncmp(dir.name, "PAL", 3) == 0 && dir.size == 768) {
                      // It's a palette
                      lumpSection = wad::Section::normal;
                  } else {
                    // Look at the header
                    size_t prevPos = stream_.tellg();
                    stream_.seekg(dir.filepos);
                    char id[4];
                    stream_.read(id, 4);
                    if (dstrncmp(id, "\x89PNG", 4) == 0) {
                        // PNG files go in the graphics section
                        lumpSection = wad::Section::graphics;
                    } else if (dstrncmp(id, "MThd", 4) == 0) {
                        // MIDI - assume it's music
                        lumpSection = wad::Section::music;
                    } else if (dstrncmp(id, "RIFF", 4) == 0) {
                        // WAV file
                        lumpSection = wad::Section::sounds;
                    }
                    stream_.seekg(prevPos);
                  }
              }

              lumps.emplace_back(name, lumpSection, table_.size());
              table_.emplace_back(dir.filepos, dir.size);
          }

          return lumps;
      }

      UniquePtr<wad::BasicLump> find(size_t lump_id, size_t mount_id) override
      {
          std::istringstream ss;
          auto& info = table_[mount_id];

          if (info.size && !info.cache.str().size()) {
              stream_.seekg(info.filepos);
              String str(info.size, 0);
              stream_.read(&str[0], info.size);
              info.cache.str(std::move(str));
          }

          return std::make_unique<DoomLump>(lump_id, info.cache);
      }
  };
}

UniquePtr<wad::Format> wad::doom_loader(StringView path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) { return nullptr; }
    Header header;
    read_into(file, header);
    if (memcmp(header.id, "IWAD", 4) == 0 || memcmp(header.id, "PWAD", 4) == 0) {
        return std::make_unique<DoomFormat>(path);
    } else {
        return nullptr;
    }
}
