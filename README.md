Doom 64 EX [![Build Status](https://travis-ci.org/svkaiser/Doom64EX.svg?branch=master)](https://travis-ci.org/svkaiser/Doom64EX) [![Build status](https://ci.appveyor.com/api/projects/status/04kswu014uwrljrd/branch/master?svg=true)](https://ci.appveyor.com/project/dotfloat/doom64ex/branch/master)
==========

This is a modified/improved version of Doom 64 EX, with compatibility for the Doom 64 Steam rerelease. It is for:

- Linux gamers who prefer native executables over using WINE/Proton
- People who don't want to resort to piracy to obtain a copy of Doom 64
- People who would rather play their legally purchased copy of Doom 64 from Steam on Doom 64 EX
- People who miss freelook in the Doom 64 Steam rerelease
- People who want to play PWADs made for Doom 64 EX

# Installing

You can download GNU/Linux binary build artifacts by clicking on the checkmark next to a commit, and viewing the build for Ubuntu 18.04 or Ubuntu 20.04. If you are logged in, you will be able to see and download "artifacts" from the build process.

At the moment, there are no binary builds for Windows. You can find older versions [here](https://doom64ex.wordpress.com/downloads/). However, these are not compatible with the Doom 64 rerelease.

# Compiling

It's possible to compile Doom 64 EX yourself. Officially, only Linux is supported. Unfortunately, since the recent addition of SDL_mixer, I've been having a lot of trouble with the Windows build. Patches for alternative operating systems are gladly accepted, however.

## Dependencies

|                                                      | Ubuntu 14.04      | Fedora 24        | Arch Linux / [MSYS2*](http://www.msys2.org/) on Windows | [Homebrew](http://brew.sh/) on macOS        |
|------------------------------------------------------|-------------------|------------------|---------------------------------------------------------|---------------------------------------------|
| C++14 compiler                                       | g++-6             | gcc              | gcc                                                     | [Xcode](https://developer.apple.com/xcode/) |
| [CMake](https://cmake.org/download/)                 | cmake             | cmake            | cmake                                                   | cmake                                       |
| [SDL2](http://libsdl.org/download-2.0.php)           | libsdl2-dev       | SDL2-devel       | sdl2                                                    | sdl2                                        |
| [SDL2_net](https://www.libsdl.org/projects/SDL_net/) | libsdl2-net-dev   | SDL2_net-devel   | sdl2_net                                                | sdl2_net                                    |
| [zlib](http://www.zlib.net/)                         | zlib1g-dev        | zlib-devel       | zlib                                                    | zlib                                        |
| [SDL2_mixer](http://libsdl.org/projects/SDL_mixer)   | libsdl2-mixer-dev | SDL2_mixer-devel | sdl2_mixer                                              | sdl2_mixer                                  |

\* MSYS2 uses a naming convention similar to the one utilised by Arch, except
packages are prefixed with `mingw-w64-i686-` and `mingw-w64-x86_64-` for 32-bit
and 64-bit packages, respectively.

## Compiling on Linux

All of these steps are done using the terminal.

### Prepare the Dependencies

On Ubuntu:

    $ # Install GCC
    $ sudo apt install build-essential gcc-6 g++-6

    $ # Install dependencies
    $ sudo apt install git cmake libsdl2-dev libsdl2-net-dev zlib1g-dev libsdl2-mixer-dev

On Fedora:

    $ # Install development group
    $ sudo dnf groupinstall "Development Tools and Libraries"
    
    $ # Install dependencies
    $ sudo dnf install git cmake sdl2-devel sdl2_net-devel zlib-devel SDL2_mixer-devel
    
On Arch Linux:

    $ # Install dependencies
    $ sudo pacman -S git gcc cmake sdl2 sdl2_net zlib sdl2_mixer

### Clone and Build

Find a suitable place to build the program and navigate there using `cd`.

    $ # Clone this repository (if you haven't done so already)
    $ git clone https://github.com/svkaiser/Doom64EX --recursive
    $ cd Doom64EX

    $ # If you have previously cloned the repository, you'll need to also grab the fluidsynth-lite submodule
    $ git submodule update --init --recursive
    
    $ mkdir build       # Create a build directory within the git repo
    $ cd build          # Change into the new directory
    $ cmake ..          # Generate Makefiles
    $ make              # Build everything
    $ sudo make install # Install Doom64EX to /usr/local
    
You can now launch Doom64EX from the menu or using `doom64ex` from terminal.

**NOTICE** Ubuntu ships with a severely outdated version of CMake, so you'll
need to create the `doom64ex.pk3` file manually.

## Compiling on Windows

Download and install [CMAKE](https://cmake.org/download/). Follow the instructions on
the website and make sure to update the system. Clone the repository in a suitable place to build the program.

Next, download the [Win32 Dependencies](https://github.com/svkaiser/Doom64EX/releases/download/win32dep-2018-04-11/Doom64EX-deps-win32-2018-04-11.zip). Extract the archive into the `extern` directory, and generate the `.lib` and `.dll` files. Place these in `extern\lib` and `extern\bin`, respectively.

Next, generate the MSVC project files.

    $ mkdir build                           # Create a build directory within the git repo
    $ cd build                              # Change into the new directory
    $ cmake .. -G "Visual Studio 15 2017"   # Generate MSVC 2017 files
    
Visual Studio 2017 project files will now be sitting in the `build` directory. 

## Compiling on macOS

Install [Xcode](https://developer.apple.com/xcode/) for its developer tools.
Follow the instructions to install [Homebrew](http://brew.sh/). You can probably
use other package managers, but Doom64EX has only been tested with Homebrew.

Open `Terminal.app` (or a terminal replacement).

    $ # Install dependencies
    $ brew install git cmake sdl2 sdl2_net sdl2_mixer zlib
    
Find a suitable place to build the program and navigate there using terminal.

    $ # Clone this repository (if you haven't done so already)
    $ git clone https://github.com/svkaiser/Doom64EX --recursive

    $ # If you have previously cloned the repository, you'll need to also grab the fluidsynth-lite submodule
    $ git submodule update --init --recursive
    
    $ mkdir build       # Create a build directory within the git repo
    $ cd build          # Change into the new directory
    $ cmake ..          # Generate Makefiles
    $ make              # Build everything
    $ sudo make install # Install Doom64EX.app

You will now find Doom64EX in your Applications directory.

## Creating `doom64ex.pk3`

If for some reason CMake refuses to automatically generate the required
`doom64ex.pk3`, you can easily create it yourself.

## Data Files

Doom 64 EX can generate an sf2 soundfont and IWAD from a Doom 64 ROM, or it can use the Doom 64 IWAD from the [Doom 64 Steam rerelease](https://store.steampowered.com/app/1148590/DOOM_64/).

If you have a Doom 64 ROM, you can run:

    $ ./doom64ex -wadgen PATH_TO_ROM

This will generate an IWAD (doom64.wad), and a soundfont (doomsnd.sf2). You will also need doom64ex.pk3 in the working directory.

To use the IWAD from the Doom 64 Steam rerelease, run Doom 64 EX as such:

    $ ./doom64ex -iwad DOOM64.WAD

If you don't hear any music or background sound, open the console, set "s_soundfont" to DOOMSND.DLS, and restart the game. This will only work if you have fluidsynth 2.0 or greater, and libinstpatch installed.

### On Linux and BSDs

* Current directory
* The directory in which the `doom64ex` executable resides
* `$XDG_DATA_HOME/doom64ex` (usually `~/.local/share/doom64ex`)
* `/usr/local/share/games/doom64ex`
* `/usr/local/share/doom64ex`
* `/usr/local/share/doom`
* `/usr/share/games/doom64ex`
* `/usr/share/doom64ex`
* `/usr/share/doom`
* `/opt/doom64ex`

### On Windows

* Current directory
* The directory in which the `doom64ex` executable resides

### On macOS

* Current directory
* `~/Library/Application Support/doom64ex`

# Community

**[Official Blog](https://doom64ex.wordpress.com/)**

**[Forum](http://z13.invisionfree.com/Doom64EX/index.php)** 

**[Discord](https://discord.gg/AHd8t33)**

You can join the official IRC channel `#doom64ex` on `irc.oftc.net` (OFTC).
