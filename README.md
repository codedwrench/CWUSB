# CWUSB
This application bridges a PSP to XLink Kai via DDS using the adhocredirector plugin. 
For now only USB is supported.

## How to compile

### Debian Testing and above

This program has only been tested on Debian Testing and above. It requires the following packages to be installed:

-   cmake
-   gcc-10
-   libboost-dev (version 1.71 or above)
-   libboost-thread-dev
-   libboost-program-options-dev
-   libpthread-stubs0-dev

If those packages are installed, you can compile the program using the following command (from the project's root):

```bash
mkdir build && cd build && cmake .. && cmake --build . -- -j`nproc`
```

### Windows
Use Visual Studio 2019. MINGW64 with a GCC version of atleast 10 works as well.

The following programs are needed:

Visual Studio 2019 or MINGW64 with a GCC version of atleast 10
CMake, if using Visual Studio 2019, this is built into it
The following libraries are needed:

- Boost 1.7.1 or higher https://www.boost.org/users/download/ (threads and program_options required)
- LibUSB 1.0.23, I had issues with 1.0.24 but it may work for you. 
After installing these, open the CMakeLists.txt and set the paths to the libraries to the paths you installed these libraries to.
By default it looks in the following paths:

- c:\Program Files\boost\boost_1_71_0\output
- c:\libusb

If that is all in order you should be able to compile the program using Visual Studio 2019 or higher, by opening the project using it and then pressing the compile button.

For MINGW64 you should be able to run the following commands:

```
mkdir build 
cd build 
cmake .. -G "MinGW Makefiles"
mingw32-make
```

After compiling, the program needs the following DLLs to be copied over to the binary directory:

- boost_program_options-(compiler)-(architecture)-(version).dll
- boost_thread-(compiler)-(architecture)-(version).dll
- libusb-1.0.dll (found in: C:\libusb\(compiler)\dll)
After that the program should be able to run.

## Contributing
See [Contributing](CONTRIBUTING.md)
