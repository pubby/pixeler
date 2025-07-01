# NES Pixeler

A small tool to quantize images into the NES color palette.

## License

- NES Pixeler is licensed under GPL 3.0 (see `COPYING`).

## Tutorial

See: https://www.youtube.com/watch?v=mVBX6pp75is

## Building

**Requirements:**
- [wxWidgets](https://www.wxwidgets.org/)
- [Make](https://www.gnu.org/software/make/)

NES Pixeler can be built in either debug mode, or release mode.

To build in debug mode, run:

    make debug

To build in release mode, run:

    make release

You may need to pull the submodules first:

    git submodule init
