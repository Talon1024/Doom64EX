// -*- mode: c++ -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 2016 Zohar Malamant
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

#include "doomdef.h"
#include "imp/Pixel"
#include "imp/util/Types"
#include <cstring>
#include <imp/Image>
#include <imp/util/Endian>
#include <istream>
#include <zlib.h>
#include <png.h>

namespace {
  constexpr const char magic[] = "\x89PNG\r\n\x1a\n";

  struct PngChunk {
      size_t length;
      char id[4];
      byte* data {nullptr};
      size_t crc;
      bool verify_crc() {
          return crc32(crc, (Bytef*)id, length + 4) == crc;
      }
      void read(std::istream &s) {
          size_t num;
          char buffer[4];
          // Read data
          data = new byte[length];
          s.read((char*)data, length);
          // Read CRC
          s.read(buffer, 4);
          num = *((size_t*)buffer);
          crc = big_endian(num);
      }
      void skip(std::istream &s) {
          s.seekg((size_t)s.tellg() + length + 4);
      }
  };

  PngChunk* read_chunk_head(std::istream &s) {
      PngChunk* chunk = new PngChunk;
      size_t num;
      char buffer[4];
      // Read length
      s.read(buffer, 4);
      num = *((size_t*)buffer);
      chunk->length = big_endian(num);
      // Read ID
      s.read(buffer, 4);
      dmemcpy(chunk->id, buffer, 4);
      return chunk;
  }

  PngChunk* read_whole_chunk(std::istream &s) {
      PngChunk* chunk = read_chunk_head(s);
      chunk->read(s);
      // Verify CRC
      if (!chunk->verify_crc()) {
          std::fprintf(stderr, "WARNING: CRC invalid for chunk %.4s!\n", chunk->id);
      }
      return chunk;
  }

  void free_chunk(PngChunk* chunk) {
      delete[] chunk->data;
      delete chunk;
  }

  struct PngImage : ImageFormatIO {
      StringView mimetype() const override;
      bool is_format(std::istream &) const override;
      Image load(std::istream &) const override;
      void save(std::ostream &, const Image &) const override;
  };

  StringView PngImage::mimetype() const
  { return "png"; }

  bool PngImage::is_format(std::istream &stream) const
  {
      char buf[sizeof magic] = {};

      stream.read(buf, sizeof magic);
      return std::memcmp(magic, buf, sizeof magic) == 0;
  }

  Image PngImage::load(std::istream &s) const
  {
      s.seekg(sizeof magic);
      size_t width, height;
      byte bitDepth, colorType, compressionMethod, filterMethod, interlaceMethod;
      SpriteOffsets offsets;
      PixelFormat format = PixelFormat::none;
      {
          // Should be IHDR - the PNG specification says IHDR is the very first 
          // chunk in a PNG file
          PngChunk* chunk = read_whole_chunk(s);
          width = big_endian(*((size_t*)chunk->data));
          height = big_endian(*((size_t*)chunk->data + 4));
          bitDepth = *(chunk->data + 8);
          colorType = *(chunk->data + 9);
          compressionMethod = *(chunk->data + 10);
          filterMethod = *(chunk->data + 11);
          interlaceMethod = *(chunk->data + 12);
          free_chunk(chunk);
      }
      if (colorType == 3) {
          format = PixelFormat::index8;
      } else if (colorType == 2) {
          format = PixelFormat::rgb;
      } else if (colorType == 6) {
          format = PixelFormat::rgba;
      }
      byte* paldata = nullptr;
      size_t palsize = 0;
      byte* transparency = nullptr;
      size_t transsize = 0;
      while (1) {
          PngChunk* chunk = read_chunk_head(s);
          if (dstrncmp(chunk->id, "grAb", 4) == 0) {
              // Offsets
              chunk->read(s);
              offsets.x = big_endian(*((int*)chunk->data));
              offsets.y = big_endian(*((int*)chunk->data + 4));
          } else if (dstrncmp(chunk->id, "PLTE", 4) == 0) {
              // Palette
              chunk->read(s);
              palsize = chunk->length / 3;
              paldata = new byte[palsize];
              dmemcpy(paldata, chunk->data, chunk->length);
          } else if (dstrncmp(chunk->id, "tRNS", 4) == 0) {
              // Transparency info
              chunk->read(s);
              transsize = chunk->length;
              transparency = new byte[transsize];
              dmemcpy(transparency, chunk->data, transsize);
          } else if (dstrncmp(chunk->id, "IDAT", 4) == 0) {
              free_chunk(chunk);
              break;
          }
          // I don't care about any other chunk
          chunk->skip(s);
          free_chunk(chunk);
      }

      // An IDAT chunk has been reached!
      s.seekg((size_t)s.tellg() - 8);

      Image retval(format, static_cast<uint16>(width), static_cast<uint16>(height), noinit_tag());

      if (colorType == 3)
      {
          if (transsize > 0)
          {
              Palette palette(PixelFormat::rgba, palsize, nullptr);

              int i = 0;
              byte* palp = paldata;
              for (auto &c : palette.map<Rgba>())
              {
                  c.red   = palp[0];
                  c.green = palp[1];
                  c.blue  = palp[2];
                  c.alpha = i < transsize ? transparency[i] : 0xff;
                  palp += 3;
                  i++;
              }

              retval.set_palette(std::move(palette));
          } else {
              byte* palp = paldata;
              Palette palette(PixelFormat::rgb, palsize, nullptr);

              for (auto &c : palette.map<Rgb>())
              {
                  c.red   = palp[0];
                  c.green = palp[1];
                  c.blue  = palp[2];
                  palp += 3;
              }

              retval.set_palette(std::move(palette));
          }
      }

      retval.set_offsets(offsets);

      auto scanlines = std::make_unique<byte*[]>(height);
      for (size_t i = 0; i < height; i++)
          scanlines[i] = retval.scanline_ptr(i);

      // Decompress, defilter, deinterlace
      if (compressionMethod == 0) {
          //
      }

      return retval;
  }

  void PngImage::save(std::ostream &s, const Image &image) const
  {
      png_structp writep = png_create_write_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
      if (writep == nullptr)
          throw ImageSaveError("Failed getting png_structp");

      png_infop infop = png_create_info_struct(writep);
      if (infop == nullptr)
      {
          png_destroy_write_struct(&writep, nullptr);
          throw ImageSaveError("Failed getting png_infop");
      }

      if (setjmp(png_jmpbuf(writep)))
      {
          png_destroy_write_struct(&writep, &infop);
          throw ImageSaveError("Error occurred in libpng");
      }

      png_set_write_fn(writep, &s,
                       [](png_structp ctx, png_bytep data, png_size_t length) {
                           static_cast<std::ostream*>(png_get_io_ptr(ctx))->write((char*)data, length);
                       },
                       [](png_structp ctx) {
                           static_cast<std::ostream*>(png_get_io_ptr(ctx))->flush();
                       });

      Image copy;
      const Image *im = &image;

      int format;
      switch (image.format()) {
          case PixelFormat::rgb:
              format = PNG_COLOR_TYPE_RGB;
              break;

          case PixelFormat::rgba:
              format = PNG_COLOR_TYPE_RGB_ALPHA;
              break;

          default:
              throw ImageSaveError("Saving image with incompatible pixel format");
      }

      png_set_IHDR(writep, infop, im->width(), im->height(), 8,
                   format, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_DEFAULT);
      png_write_info(writep, infop);

	  auto scanlines = std::make_unique<const uint8_t*[]>(im->height());
      for (int i = 0; i < im->height(); i++)
          scanlines[i] = im->scanline_ptr(i);

      png_write_image(writep, const_cast<png_bytepp>(scanlines.get()));
      png_write_end(writep, infop);
      png_destroy_write_struct(&writep, &infop);
  }
}

std::unique_ptr<ImageFormatIO> __initialize_png()
{
    return std::make_unique<PngImage>();
}
