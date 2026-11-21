# Linux x86_64 with zig (bundled libc++, glibc 2.34 baseline).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER ${CMAKE_CURRENT_LIST_DIR}/../bin/zig-cc)
set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/../bin/zig-c++)
set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/../bin/zig-ar CACHE FILEPATH "")
set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/../bin/zig-ranlib CACHE FILEPATH "")
set(MDK_PLATFORM linux-x64)

# SDL (MDK_STATIC_SDL) dlopens X11, Wayland, audio, udev... at run time; at build time it needs
# their headers and sonames: Ubuntu 24.04 packages extracted into sysroot-headers (no glibc there),
# or, with MDK_SYSROOT=/ (CI), the installed packages.
set(MDK_SYSROOT /work/.toolchain/sysroot-headers)
if(DEFINED ENV{MDK_SYSROOT})
  set(MDK_SYSROOT $ENV{MDK_SYSROOT})
endif()
set(CMAKE_FIND_ROOT_PATH ${MDK_SYSROOT})
set(CMAKE_LIBRARY_ARCHITECTURE x86_64-linux-gnu)
set(CMAKE_C_FLAGS_INIT "-idirafter ${MDK_SYSROOT}/usr/include")
set(PKG_CONFIG_EXECUTABLE ${CMAKE_CURRENT_LIST_DIR}/../bin/pkg-config CACHE FILEPATH "")
set(WAYLAND_SCANNER ${MDK_SYSROOT}/usr/bin/wayland-scanner CACHE FILEPATH "")
