# Miyoo Mini+ (SigmaStar SSD202D, armv7ve) cross toolchain.
# Matches the flags used by the original Makefile.miyoo.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TC "/opt/miyoomini-toolchain")
set(CMAKE_C_COMPILER   "${TC}/bin/arm-linux-gnueabihf-gcc")
set(CMAKE_CXX_COMPILER "${TC}/bin/arm-linux-gnueabihf-g++")

set(CMAKE_SYSROOT "${TC}/arm-linux-gnueabihf/libc")
set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(HGE_ARM_FLAGS
    "-marm -mtune=cortex-a7 -march=armv7ve+simd -mfpu=neon-vfpv4 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT   "${HGE_ARM_FLAGS} -O2 -g -fPIC")
set(CMAKE_CXX_FLAGS_INIT "${HGE_ARM_FLAGS} -O2 -g -fPIC")
