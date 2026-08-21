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


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Rtos_Learn)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    User/my_rtos/src/sparrow.c
    User/Lib/Src/ws2812.c
    User/Lib/Src/VOFA.c
    User/my_rtos/src/schedule.c
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    User/my_rtos/Inc
    User/Lib/Inc
    # Add user defined include paths
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

find_program(JLINK_COMMANDER
    NAMES JLink.exe
    HINTS "C:/Program Files/SEGGER/JLink_V798"
)

if(JLINK_COMMANDER)
    add_custom_target(flash_jlink
        COMMAND ${JLINK_COMMANDER}
            -device STM32F405RG
            -if SWD
            -speed 4000
            -autoconnect 1
            -CommanderScript ${CMAKE_SOURCE_DIR}/flash.jlink
        DEPENDS ${CMAKE_PROJECT_NAME}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Flashing ${CMAKE_PROJECT_NAME} with J-Link"
    )
endif()
