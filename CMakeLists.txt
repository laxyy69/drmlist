cmake_minimum_required(VERSION 3.25)

project("drmlist" LANGUAGES C ASM_NASM)
enable_language(C ASM_NASM)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mavx2 -masm=intel -no-pie")

add_executable(drmlist)

target_sources(drmlist PRIVATE
    src/main.c
    src/drmlist.c
    src/drmlist_draw_box.asm
    src/mydrm/mydrm.c
)

target_include_directories(drmlist PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${LIBDRM_INCLUDE_DIRS}"
)
