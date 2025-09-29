cmake_minimum_required(VERSION 3.28)
project(uitest_agent_vnc C)

set(CMAKE_C_STANDARD 11)

set(CMAKE_SKIP_RPATH TRUE)


# libjpeg
set(LIBJPEG_ROOT "${CMAKE_SOURCE_DIR}/third/libjpeg")
set(LIBJPEG_INCLUDE "${LIBJPEG_ROOT}/include")
set(LIBJPEG_LIB "${LIBJPEG_ROOT}/lib/libjpeg.a")
include_directories(${LIBJPEG_INCLUDE})
# libpng
set(LIBPNG_ROOT "${CMAKE_SOURCE_DIR}/third/libpng")
set(LIBPNG_INCLUDE "${LIBPNG_ROOT}/include")
set(LIBPNG_LIB "${LIBPNG_ROOT}/lib/libpng.a")
include_directories(${LIBPNG_INCLUDE})
# Libvncserver
set(LIBVNCSERVER_ROOT "${CMAKE_SOURCE_DIR}/third/libvncserver")
set(LIBVNCSERVER_INCLUDE "${LIBVNCSERVER_ROOT}/include")
set(LIBVNCSERVER_LIB "${LIBVNCSERVER_ROOT}/lib/libvncserver.a")
include_directories(${LIBVNCSERVER_INCLUDE})

add_library(agent SHARED
        agent.c
        uitest.c
)

target_link_libraries(agent PRIVATE
        hilog_ndk.z
        z
        native_display_manager
        ${LIBJPEG_LIB}
        ${LIBPNG_LIB}
        ${LIBVNCSERVER_LIB}
)

target_compile_options(agent PRIVATE -ffunction-sections -fdata-sections)
set_target_properties(agent PROPERTIES LINK_FLAGS "-Wl,--gc-sections")
