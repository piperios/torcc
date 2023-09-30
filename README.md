# torcpp, a C++20 port of the TORC runtime library

## Basic build instructions

Make sure you have an MPI installation available on your system, preferably a thread-safe one, as well as a C++20 compliant compiler.

To build:

```bash
mkdir build && cd build

cmake -G YOUR_GENERATOR -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=YOUR_COMPILER ..
```

After the CMake build files have been generated, you can use your selected generator to build the library.

For Ninja:

```bash
ninja build
```

For Makefiles:

```bash
make && make install
```

## Disclaimer

The port is in a **very** rough state and the migration from GNU autotools to CMake hasn't been performed properly (yet). This repo is currently acting as a backup tool. Please do not use this library yet.
