# CVFS

## Overview

CVFS, which stands for Class Virtual File System, is a one header only library. which lets you create a simple node based in-memory-filesystem.

## How to use the library?

You need to add the VFS.hpp to your include paths.

### Example of how to use this library.

A example is provided inside the main.cpp file.

## How to build the example project?

First you need cmake from https://cmake.org/.
After you have installed cmake you need to create a build directory inside this directory.
Open a terminal and navigate to the build directory, alternative you can use the cmake-gui, but I will describe the terminal version. Since cmake supports many build system I used in this example `ninja`, but you can use whatever you want.

```bash
cmake .. -G "Ninja"
ninja -j4
```

Now you can execute the compiled project.

## License

This library is under the [MIT License](LICENSE)