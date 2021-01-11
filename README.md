# PSPXLinkBridge
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
## Contributing
See [Contributing](CONTRIBUTING.md)
