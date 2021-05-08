xsystem4
========

xsystem4 is an implementation of AliceSoft's System 4 game engine for unix-like
operating systems.

NOTE: This is a work in progress. I am focusing primarily on the Japanese
      version of Sengoku Rance (and by extension, the English fan translation)
      which is currently more or less playable. Some other games from the same
      era may also work, but I have not done any significant testing on them so
      breakage is likely.

Building
--------

First install the dependencies (corresponding Debian package in parentheses):

* chibi-scheme [optional, for debugger]
* bison (bison)
* cglm (libcglm-dev)
* flex (flex)
* meson (meson)
* libffi (libffi-dev)
* libpng (libpng-dev)
* libturbojpeg (libturbojpeg0-dev)
* libwebp (libwebp-dev)
* SDL2 (libsdl2-dev)
* SDL2_mixer (libsdl2-mixer-dev)
* SDL2_ttf (libsdl2-ttf-dev)
* zlib (zlib1g-dev)

Then fetch the git submodules,

    git submodule init
    git submodule update

(Alternatively, pass `--recurse-submodules` when cloning this repository)

Then build the xsystem4 executable with meson,

    mkdir build
    meson build
    ninja -C build
    
Finally install it to your system (optional),

    ninja -C build install

Running
-------

You can run a game by passing the path to its game directory to the xsystem4
executable,

    build/src/xsystem4 /path/to/game_directory

Alternatively, run xsystem4 from within the game directory,

    cd /path/to/game_directory
    xsystem4
