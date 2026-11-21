# Linux arm64 with zig, cross-compiled like linux-x64. Needs ZIG_TARGET=aarch64-linux-gnu.2.34 in
# the environment (the zig wrappers read it). SDL takes only headers and sonames from the
# sysroot, the same on both CPUs: linux-x64's x86_64 packages serve.
include(${CMAKE_CURRENT_LIST_DIR}/linux-x64.cmake)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(MDK_PLATFORM linux-arm64)
