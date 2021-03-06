##------------------------------------------------------------------------------
##
## Doom64EX CMake configuration
##
## LICENSE:
##
## Copyright(C) 2014-2016 Zohar Malamant
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
## 02111-1307, USA.
##

set(SOURCE_ROOT_DIR "${CMAKE_SOURCE_DIR}/src/engine")

if (CMAKE_VERSION VERSION_LESS 3.1.3)
  add_definitions(-std=c++14)
else()
  set(CMAKE_CXX_STANDARD 14)
endif()

##------------------------------------------------------------------------------
## Platform-specific sources
##

if(WIN32)

  set(PLATFORM_SOURCES
    native_ui/WinAPI.cc
    native_ui/WinAPI-windows-is-dumb.c
    system/win32/doom64.rc
    system/i_cpu.cc)

  if(MINGW32)
    set(PLATFORM_LIBRARIES
      mingw32)
  endif()

else()

  set(WADGEN_SOURCES
    wadgen/deflate-N64.cc
    wadgen/deflate-N64.h
    wadgen/gfx.cc
    wadgen/gfx.h
    wadgen/level.cc
    wadgen/level.h
    wadgen/png.cc
    wadgen/rom.cc
    wadgen/rom.h
    wadgen/sndfont.cc
    wadgen/sndfont.h
    wadgen/sound.cc
    wadgen/sound.h
    wadgen/sprite.cc
    wadgen/sprite.h
    wadgen/texture.cc
    wadgen/texture.h
    wadgen/wad.cc
    wadgen/wad.h
    wadgen/wadgen.cc
    wadgen/wadgen.h)

  set(PLATFORM_SOURCES
    ${WADGEN_SOURCES}
    native_ui/Gtk3.cc
    system/i_cpu_posix.cc)

  set(PLATFORM_LIBRARIES
    dl
    m)

endif()

##------------------------------------------------------------------------------
## Sources
##

set(LIBRARIES
  ${PLATFORM_LIBRARIES}
  ${SDL2_LIBRARIES}
  ${SDL2_NET_LIBRARIES}
  ${SDL2_MIXER_LIBRARIES}
  png_static
  ${FLUIDSYNTH_LIBRARIES}
  ${OPENGL_LIBRARIES}
  ${CONAN_LIBS})

set(INCLUDES
  ${PLATFORM_INCLUDES}
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_BINARY_DIR}/config
  ${CMAKE_CURRENT_SOURCE_DIR}

  ${SDL2_INCLUDE_DIRS}
  ${SDL2_MIXER_INCLUDE_DIRS}
  ${SDL2_NET_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/libpng
  ${FLUIDSYNTH_INCLUDE_DIR}
  ${OPENGL_INCLUDE_DIRS}

  automap
  common
  console
  doom_main
  finale
  fmt
  game
  gfx
  intermission
  misc
  net
  opengl
  parser
  playloop
  renderer
  sound
  statusbar
  system
  wadgen
  zone)

set(SOURCES
  ${PLATFORM_SOURCES}

  App.cc
  Globals.cc

  # automap
  automap/am_draw.cc
  automap/am_map.cc

  # common
  common/info.cc
  common/md5.cc
  common/tables.cc
  common/Property.cc

  # console
  console/con_console.cc

  # doom_main
  doom_main/d_devstat.cc
  doom_main/d_main.cc
  doom_main/d_net.cc

  # finale
  finale/f_finale.cc
  finale/in_stuff.cc

  # fmt
  fmt/format.cc
  fmt/ostream.cc

  # game
  game/g_actions.cc
  game/g_demo.cc
  game/g_game.cc
  game/g_settings.cc

  # gfx
  gfx/Image.cc
  gfx/PngImage.cc
  gfx/DoomImage.cc
  gfx/Pixel.cc

  # intermission
  intermission/wi_stuff.cc

  # misc
  misc/m_cheat.cc
  misc/m_fixed.cc
  misc/m_keys.cc
  misc/m_menu.cc
  misc/m_misc.cc
  misc/m_password.cc
  misc/m_random.cc
  misc/m_shift.cc

  # net
  net/net_client.cc
  net/net_common.cc
  net/net_dedicated.cc
  net/net_defs.h
  net/net_io.cc
  net/net_loop.cc
  net/net_packet.cc
  net/net_query.cc
  net/net_sdl.cc
  net/net_server.cc
  net/net_structrw.cc

  # opengl
  opengl/dgl.cc
  opengl/gl_draw.cc
  opengl/gl_main.cc
  opengl/gl_texture.cc
  opengl/glad.cc

  # parser
  parser/sc_main.cc

  # playloop
  playloop/p_ceilng.cc
  playloop/p_doors.cc
  playloop/p_enemy.cc
  playloop/p_floor.cc
  playloop/p_inter.cc
  playloop/p_lights.cc
  playloop/p_macros.cc
  playloop/p_map.cc
  playloop/p_maputl.cc
  playloop/p_mobj.cc
  playloop/p_plats.cc
  playloop/p_pspr.cc
  playloop/p_saveg.cc
  playloop/p_setup.cc
  playloop/p_sight.cc
  playloop/p_spec.cc
  playloop/p_switch.cc
  playloop/p_telept.cc
  playloop/p_tick.cc
  playloop/p_user.cc
  playloop/Map.cc

  # renderer
  renderer/r_bsp.cc
  renderer/r_clipper.cc
  renderer/r_drawlist.cc
  renderer/r_lights.cc
  renderer/r_local.h
  renderer/r_main.cc
  renderer/r_scene.cc
  renderer/r_sky.cc
  renderer/r_things.cc
  renderer/r_wipe.cc

  # sound
  sound/s_sound.cc

  # statusbar
  statusbar/st_stuff.cc

  # system
  system/i_audio.cc
  system/i_main.cc
  system/i_png.cc
  system/i_swap.h
  system/i_system.cc
  system/i_video.cc
  system/SdlVideo.cc

  # wad
  wad/Wad.cc
  wad/DoomWad.cc
  wad/RomWad.cc
  wad/ZipWad.cc

  # zone
  zone/z_zone.cc)

##------------------------------------------------------------------------------
## Build target
##

add_executable(doom64ex MACOSX_BUNDLE WIN32 ${SOURCES})
target_include_directories(doom64ex PRIVATE ${INCLUDES})
target_link_libraries(doom64ex ${LIBRARIES})

if(WIN32)
  set_property(TARGET doom64ex PROPERTY COMPILE_FLAGS "-DFLUIDSYNTH_NOT_A_DLL")
endif(WIN32)

if(WIN32)
  # Copy DLL files over
  add_custom_command(TARGET doom64ex POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/extern/bin/libpng16.dll" "${CMAKE_BINARY_DIR}/libpng16.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/extern/bin/SDL2.dll" "${CMAKE_BINARY_DIR}/SDL2.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/extern/bin/SDL2_net.dll" "${CMAKE_BINARY_DIR}/SDL2_net.dll"
	)
endif(WIN32)

##------------------------------------------------------------------------------
## Install target
##

if (NOT WIN32)
  if (APPLE)
    install(CODE "include(BundleUtilities)"
      "fixup_bundle(\"${CMAKE_CURRENT_BINARY_DIR}/doom64ex.app\" \"\" \"\")")
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/doom64ex.app" DESTINATION /Applications USE_SOURCE_PERMISSIONS)
  else ()
    install(PROGRAMS ${CMAKE_BINARY_DIR}/doom64ex DESTINATION bin)
  endif ()
endif ()
