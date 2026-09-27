cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# C++（应用层用，CubeMX 生成的文件一律保持 C）
# 工具链文件已带 -fno-rtti -fno-exceptions -fno-threadsafe-statics
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME epencephalon)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C CXX ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/App/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/*.c
)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${APP_SOURCES}
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/App
    ${CMAKE_CURRENT_SOURCE_DIR}/App/drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/App/fonts
)

# ---------------------------------------------------------------------------
# can_master: the joint-driver CAN host library
# ---------------------------------------------------------------------------
#
# ../master/can_master is a SYMLINK to the joint_motor repo's working tree.
# There is physically one copy on disk, so the master and the slave cannot
# drift apart -- that is not enforced by a check, it is impossible. Version
# pinning is git's job on that side, not a copy's.
#
# external/joint_motor/{comm,core} are per-FILE symlinks to the two firmware
# headers the library actually needs. The closure was derived with -MMD, not
# by reasoning: exactly comm/can_ids.hpp and core/flags.hpp, nothing else.
# Linking the files rather than the directory is deliberate -- if upstream
# grows a third dependency the build fails loudly instead of silently picking
# it up.
set(JM_CAN_MASTER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../master/can_master)

# Explicit, never globbed. A new file upstream must not enter this build
# without someone editing this list. This is also what keeps
# can_bus_loopback.* out: it comes along with the symlinked directory (it is
# the host-test bus model, useless on the board) and is simply not listed.
set(JM_CAN_MASTER_SOURCES
    ${JM_CAN_MASTER_DIR}/can_master.cpp
    ${JM_CAN_MASTER_DIR}/joint_handle.cpp
    ${JM_CAN_MASTER_DIR}/robot.cpp
    ${JM_CAN_MASTER_DIR}/protocol.cpp
    ${JM_CAN_MASTER_DIR}/can_bus_stm32_bxcan.cpp
)

foreach(f ${JM_CAN_MASTER_SOURCES})
    if(NOT EXISTS ${f})
        message(FATAL_ERROR "can_master source missing: ${f}\n"
                "The symlink ../master/can_master probably points nowhere. "
                "It must resolve into the joint_motor repo working tree.")
    endif()
endforeach()

foreach(h comm/can_ids.hpp core/flags.hpp)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/joint_motor/${h})
        message(FATAL_ERROR "upstream header missing: external/joint_motor/${h}\n"
                "Per-file symlink into the joint_motor repo is broken.")
    endif()
endforeach()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${JM_CAN_MASTER_SOURCES})

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${JM_CAN_MASTER_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/external/joint_motor
)

# Both of these are mandatory and both fail SILENTLY if omitted:
#
#   JM_CAN_MASTER_PORT  - can_master_port.hpp #errors without it, so this one
#                         is loud. It picks the critical section AND binds
#                         JM_CAN_MASTER_MIN_SAFE_IRQ_PRIORITY to the
#                         scheduler's threshold; choosing BAREMETAL here would
#                         make the NVIC check in BxCanBus::start() vacuous.
#
#   JM_BXCAN_HAVE_HAL   - guards the whole bxCAN driver
#                         (can_bus_stm32_bxcan.cpp:13 / .hpp:236). Without it
#                         the file compiles to an object with ZERO symbols and
#                         no diagnostic at all: measured 744 B / 0 symbols vs
#                         6124 B / 37 symbols. Neither README.md nor
#                         docs/CAN协议.md documents this macro; only upstream's
#                         check_bare_metal.sh passes it.
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    JM_CAN_MASTER_PORT=JM_CAN_MASTER_PORT_FREERTOS
    JM_BXCAN_HAVE_HAL
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# ── 以下为手动添加（CubeMX 不会重新生成本文件，改动安全）──────────────
# 编译结束后打印各段体积，并额外产出 .hex / .bin
# CMAKE_SIZE / CMAKE_OBJCOPY 由 cmake/gcc-arm-none-eabi.cmake 定义
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    COMMAND ${CMAKE_OBJCOPY} -O ihex
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/${CMAKE_PROJECT_NAME}.bin
    COMMENT "生成 ${CMAKE_PROJECT_NAME}.hex 和 ${CMAKE_PROJECT_NAME}.bin"
)
