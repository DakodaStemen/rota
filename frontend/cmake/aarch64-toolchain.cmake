# Cross-compile for the Rotate (aarch64) with aarch64-linux-gnu-g++ (Ubuntu package g++-aarch64-linux-gnu).
#   scripts/fetch-aarch64-sysroot.py /path/to/sysroot            # SDL2, SDL2_ttf, zlib for arm64 (or use your own)
#   cmake -S . -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake \
#         -DROTATE_AARCH64_SYSROOT=/path/to/sysroot -DROTATE_BUILD_TESTS=OFF
# libc and libstdc++ come from the cross toolchain; ROTATE_AARCH64_SYSROOT only has to provide the arm64
# libSDL2, libSDL2_ttf, zlib (and the DT_NEEDED libs of those) plus headers, e.g. an extracted Arch Linux ARM rootfs.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-a75.cortex-a55")

if(NOT DEFINED ROTATE_AARCH64_SYSROOT AND DEFINED ENV{ROTATE_AARCH64_SYSROOT})
  set(ROTATE_AARCH64_SYSROOT $ENV{ROTATE_AARCH64_SYSROOT})
endif()
if(ROTATE_AARCH64_SYSROOT)
  set(_libdir ${ROTATE_AARCH64_SYSROOT}/usr/lib/aarch64-linux-gnu)
  set(CMAKE_FIND_ROOT_PATH ${ROTATE_AARCH64_SYSROOT} /usr/aarch64-linux-gnu)
  set(CMAKE_PREFIX_PATH ${ROTATE_AARCH64_SYSROOT}/usr)
  set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -isystem ${ROTATE_AARCH64_SYSROOT}/usr/include/aarch64-linux-gnu")
  set(CMAKE_EXE_LINKER_FLAGS_INIT "-L${_libdir} -Wl,-rpath-link,${_libdir} -Wl,-rpath-link,${_libdir}/pulseaudio -Wl,-rpath-link,${ROTATE_AARCH64_SYSROOT}/lib/aarch64-linux-gnu")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
