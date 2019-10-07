xsystem4
========

xsystem4 is an implementation of AliceSoft's System 4 game engine for unix-like
operating systems.

NOTE: This is a work in progress. Very little is implemented so far.

Building
--------

xsystem4 uses the meson build system.

    mkdir build
    meson build
    ninja -C build

Running
-------

You will need to create an AIN file using the System 4 SDK compiler. Then simply
pass it to the system4 executable.

    build/system4 ???.ain
