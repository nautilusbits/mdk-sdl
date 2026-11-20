# Linux x86_64 with zig (bundled libc++, glibc 2.34 baseline).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER ${CMAKE_CURRENT_LIST_DIR}/../bin/zig-cc)
set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/../bin/zig-c++)
set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/../bin/zig-ar CACHE FILEPATH "")
set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/../bin/zig-ranlib CACHE FILEPATH "")
set(MDK_PLATFORM linux-x64)
