xsystem4
========

xsystem4 is an implementation of AliceSoft's System 4 game engine for unix-like
operating systems.

NOTE: This is a work in progress. Some simple ADV games run more or less okay,
      but most games are not fully working or have game-breaking bug when run
      with xsystem4.

Building
--------

First install the dependencies (corresponding Debian package in parentheses):

* chibi-scheme [optional, for debugger]
* bison (bison)
* flex (flex)
* meson (meson)
* libffi (libffi-dev)
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

Running
-------

You can run a game by passing the path to its "System40.ini" to the xsystem4
executable.

    build/src/xsystem4 /path/to/System40.ini

For now, xsystem4 needs to be run in the top-level project directory (i.e. the
directory that contains this README file). This will change in the future.
