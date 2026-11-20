cmake_minimum_required(VERSION 3.22)
project(MdkCpp VERSION 1.2.6 LANGUAGES C CXX)

# C++ port of mdk-sdl (C# / SDL3). Layers (each talks only to the one below):
#
#   Mdk.App / Android head ──► Mdk.Game ──► Mdk.Engine ──► SDL3
#                                  │             │
#                                  └──► Mdk.Formats ──► Mdk.Core (.NET semantics, numerics)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(MDK_BUILD_TESTS "Build the unit tests" ON)

if(NOT MDK_PLATFORM)
  if(ANDROID)
    # The prebuilt SDL's folders are named by .NET runtime identifier.
    if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
      set(MDK_PLATFORM android-arm64)
    elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
      set(MDK_PLATFORM android-x64)
    else()
      set(MDK_PLATFORM android-${CMAKE_ANDROID_ARCH_ABI})
    endif()
  else()
    set(MDK_PLATFORM linux-x64)
  endif()
endif()

# C# semantics: wrapping int overflow, no FMA contraction (RyuJIT doesn't fuse), no
# strict-aliasing surprises from the ported byte reinterpretation.
set(MDK_FLAGS -fwrapv -ffp-contract=off -fno-strict-aliasing)
if(WIN32)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
set(MDK_WARNINGS -Wall -Wno-unused-variable -Wno-unused-function -Wno-unused-private-field -Wno-sign-compare
    -Wno-unused-but-set-variable -Wno-missing-braces -Wno-reorder-ctor)

# Release binaries ship without debug info and symbols (zig links its libc++ with them: 108 MB).
if(NOT ANDROID)
  add_compile_options($<$<CONFIG:Release>:-g0>)
  if(APPLE)
    add_link_options($<$<CONFIG:Release>:-Wl,-S> $<$<CONFIG:Release>:-Wl,-dead_strip>)
  else()
    add_link_options($<$<CONFIG:Release>:-s>)
  endif()
endif()

# ---- Third party ----

set(TP ${CMAKE_SOURCE_DIR}/third_party)

add_library(miniz STATIC ${TP}/miniz/miniz.c)
target_include_directories(miniz PUBLIC ${TP}/miniz)
target_compile_options(miniz PRIVATE -w)

add_library(headers_tp INTERFACE)
target_include_directories(headers_tp INTERFACE ${TP}/nlohmann ${TP}/picosha2 ${TP}/doctest)

# SDL3: the prebuilt library of ppy.SDL3-CS 2026.722.0 (SDL 3.5.0 a8591d9) and its headers.
set(SDL_BIN ${TP}/sdl-bin/${MDK_PLATFORM})
add_library(SDL3 SHARED IMPORTED GLOBAL)
target_include_directories(SDL3 INTERFACE ${TP}/SDL/include)
if(WIN32)
  set_target_properties(SDL3 PROPERTIES IMPORTED_LOCATION ${SDL_BIN}/SDL3.dll IMPORTED_IMPLIB ${SDL_BIN}/SDL3.dll)
  set(SDL_RUNTIME ${SDL_BIN}/SDL3.dll)
elseif(APPLE)
  set_target_properties(SDL3 PROPERTIES IMPORTED_LOCATION ${SDL_BIN}/libSDL3.dylib)
  set(SDL_RUNTIME ${SDL_BIN}/libSDL3.dylib)
else()
  # Its soname is libSDL3.so.0: the loader looks for that name next to the program.
  set_target_properties(SDL3 PROPERTIES IMPORTED_LOCATION ${SDL_BIN}/libSDL3.so)
  set(SDL_RUNTIME ${SDL_BIN}/libSDL3.so)
  set(SDL_RUNTIME_ALIAS libSDL3.so.0)
endif()

# ---- Shaders: HLSL -> DXIL, SPIR-V (dxc), MSL (spirv-cross), embedded ----

set(DXC /work/.toolchain/dxc/bin/dxc CACHE FILEPATH "dxc (DXIL and SPIR-V)")
set(SPIRV_CROSS /work/.toolchain/vulkansdk/spirv-cross CACHE FILEPATH "spirv-cross (MSL)")
file(GLOB SHADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/*.hlsl)
file(GLOB SHADER_INCLUDES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/shaders/*.hlsli)
set(SHADER_OUT ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})

set(EMBED_NAMES icon.png)
set(EMBED_FILES ${CMAKE_SOURCE_DIR}/assets/icon.png)
set(MDK_SHADER_FORMATS "dxil;spv;msl")
if(ANDROID)
  set(MDK_SHADER_FORMATS "spv")
elseif(WIN32)
  set(MDK_SHADER_FORMATS "dxil;spv")
elseif(APPLE)
  set(MDK_SHADER_FORMATS "msl;spv")
elseif(UNIX)
  set(MDK_SHADER_FORMATS "spv")
endif()

foreach(src ${SHADERS})
  get_filename_component(name ${src} NAME_WE)
  foreach(stage vs ps)
    set(spv ${SHADER_OUT}/${name}.${stage}.spv)
    set(dxil ${SHADER_OUT}/${name}.${stage}.dxil)
    set(msl ${SHADER_OUT}/${name}.${stage}.msl)
    add_custom_command(OUTPUT ${spv}
      COMMAND ${DXC} -nologo -spirv -T ${stage}_6_0 -E ${stage}_main -Fo ${spv} ${src}
      DEPENDS ${src} ${SHADER_INCLUDES} VERBATIM)
    add_custom_command(OUTPUT ${dxil}
      COMMAND ${DXC} -nologo -T ${stage}_6_0 -E ${stage}_main -Fo ${dxil} ${src}
      DEPENDS ${src} ${SHADER_INCLUDES} VERBATIM)
    add_custom_command(OUTPUT ${msl}
      COMMAND ${SPIRV_CROSS} ${spv} --msl --msl-decoration-binding --output ${msl}
      DEPENDS ${spv} VERBATIM)
    foreach(fmt ${MDK_SHADER_FORMATS})
      list(APPEND EMBED_NAMES shaders/${name}.${stage}.${fmt})
      list(APPEND EMBED_FILES ${SHADER_OUT}/${name}.${stage}.${fmt})
    endforeach()
  endforeach()
endforeach()

set(EMBED_CPP ${CMAKE_BINARY_DIR}/Embedded.cpp)
add_custom_command(OUTPUT ${EMBED_CPP}
  COMMAND ${CMAKE_COMMAND} "-DOUT=${EMBED_CPP}" "-DNAMES=${EMBED_NAMES}" "-DFILES=${EMBED_FILES}" -P ${CMAKE_SOURCE_DIR}/cmake/Embed.cmake
  DEPENDS ${EMBED_FILES} ${CMAKE_SOURCE_DIR}/cmake/Embed.cmake VERBATIM)

# ---- Layers ----

function(mdk_layer name)
  file(GLOB_RECURSE srcs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/${name}/*.cpp)
  add_library(${name} STATIC ${srcs} ${ARGN})
  target_include_directories(${name} PUBLIC ${CMAKE_SOURCE_DIR}/src)
  target_compile_options(${name} PRIVATE ${MDK_FLAGS} ${MDK_WARNINGS})
  target_compile_options(${name} INTERFACE ${MDK_FLAGS})
endfunction()

mdk_layer(Mdk.Core ${EMBED_CPP})
target_link_libraries(Mdk.Core PUBLIC headers_tp miniz)
if(UNIX AND NOT APPLE AND NOT ANDROID)
  target_link_libraries(Mdk.Core PUBLIC pthread dl)
endif()

file(GLOB_RECURSE FORMATS_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/Mdk.Formats/*.cpp)
if(FORMATS_SRCS)
  mdk_layer(Mdk.Formats)
  target_link_libraries(Mdk.Formats PUBLIC Mdk.Core miniz)
endif()

file(GLOB_RECURSE ENGINE_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/Mdk.Engine/*.cpp)
if(ENGINE_SRCS)
  mdk_layer(Mdk.Engine)
  target_link_libraries(Mdk.Engine PUBLIC Mdk.Core SDL3)
  if(WIN32)
    target_link_libraries(Mdk.Engine PUBLIC winhttp)
  endif()
endif()

file(GLOB_RECURSE GAME_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/Mdk.Game/*.cpp)
if(GAME_SRCS)
  mdk_layer(Mdk.Game)
  target_link_libraries(Mdk.Game PUBLIC Mdk.Formats Mdk.Engine)
endif()

# ---- Executable ----

function(mdk_runtime target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDL_RUNTIME} $<TARGET_FILE_DIR:${target}>)
  if(SDL_RUNTIME_ALIAS)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDL_RUNTIME} $<TARGET_FILE_DIR:${target}>/${SDL_RUNTIME_ALIAS})
  endif()
  if(APPLE)
    set_target_properties(${target} PROPERTIES BUILD_RPATH "@executable_path" INSTALL_RPATH "@executable_path")
  elseif(UNIX)
    set_target_properties(${target} PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
  endif()
endfunction()

file(GLOB_RECURSE APP_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/Mdk.App/*.cpp)
if(APP_SRCS AND TARGET Mdk.Game AND NOT ANDROID)
  file(GLOB_RECURSE APP_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/Mdk.App/*.cpp)
  if(WIN32)
    list(APPEND APP_SRCS ${CMAKE_SOURCE_DIR}/src/Mdk.App/mdk.rc)
  endif()
  add_executable(mdk ${APP_SRCS})
  target_compile_options(mdk PRIVATE ${MDK_FLAGS} ${MDK_WARNINGS})
  target_link_libraries(mdk PRIVATE Mdk.Game)
  mdk_runtime(mdk)
endif()

if(ANDROID)
  # libmain.so: SDL's Java activity loads it and calls SDL_main.
  file(GLOB_RECURSE ANDROID_SRCS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/Mdk.Android/*.cpp)
  add_library(main SHARED ${ANDROID_SRCS})
  target_compile_options(main PRIVATE ${MDK_FLAGS} ${MDK_WARNINGS})
  target_link_libraries(main PRIVATE Mdk.Game android log)
endif()

# ---- Tests ----

if(MDK_BUILD_TESTS AND NOT ANDROID)
  foreach(suite Mdk.Formats.Tests Mdk.Game.Tests Mdk.Core.Tests)
    file(GLOB_RECURSE probe CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/${suite}/*.cpp)
    if(probe)
      file(GLOB_RECURSE tsrcs CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/${suite}/*.cpp)
      if(NOT suite STREQUAL Mdk.Core.Tests)
        file(GLOB shared CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/Shared/*.cpp)
        list(APPEND tsrcs ${shared})
      endif()
      add_executable(${suite} ${tsrcs})
      target_include_directories(${suite} PRIVATE ${CMAKE_SOURCE_DIR}/tests)
      target_compile_options(${suite} PRIVATE ${MDK_FLAGS} ${MDK_WARNINGS})
      if(suite STREQUAL Mdk.Game.Tests)
        # Until Mdk.Game exists, the Engine-only tests link the Engine (and Formats when built).
        if(TARGET Mdk.Game)
          target_link_libraries(${suite} PRIVATE Mdk.Game)
        else()
          target_link_libraries(${suite} PRIVATE Mdk.Engine $<$<TARGET_EXISTS:Mdk.Formats>:Mdk.Formats>)
        endif()
        mdk_runtime(${suite})
        if(WIN32)
          target_link_libraries(${suite} PRIVATE ws2_32)
        endif()
      elseif(suite STREQUAL Mdk.Formats.Tests)
        target_link_libraries(${suite} PRIVATE Mdk.Formats)
      else()
        target_link_libraries(${suite} PRIVATE Mdk.Core)
      endif()
    endif()
  endforeach()
endif()
