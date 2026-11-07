cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sp1master)
include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
target_sources(app PRIVATE src/main.c ../master/master.c)
target_include_directories(app PRIVATE ../master)
# The mastering engine is float math in the audio thread: build it for speed. The rest stays -Os.
# -ffp-contract=off: on the Cortex-M4 a fused VFMA takes 3 cycles, VMUL + VADD take 2.
set_source_files_properties(../master/master.c PROPERTIES COMPILE_OPTIONS "-O2;-fno-math-errno;-ffp-contract=off;-fno-strict-aliasing")
