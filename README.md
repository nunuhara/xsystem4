xsystem4
========

xsystem4 is an implementation of AliceSoft's System 4 game engine for unix-like
operating systems.

NOTE: This is a work in progress. Some simple ADV games run more or less okay,
      but most games are not fully working or have game-breaking bug when run
      with xsystem4.

Building
--------

xsystem4 uses the meson build system.

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
