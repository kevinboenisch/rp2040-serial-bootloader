# Derived from the Pico SDK, which carries the following
# LICENSE.txt:
# Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd.
#
# Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
# following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
#    disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
#    disclaimer in the documentation and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
# INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cmake_minimum_required(VERSION 3.13)

# Set the board to jpo_brain, in jpo_software repo
### Prerequisites
if (NOT DEFINED ENV{JPO_PATH})
	message(FATAL_ERROR "Environment variable JPO_PATH not set.")
endif()
# Fix backslashes in Windows
file(TO_CMAKE_PATH "$ENV{JPO_PATH}" JPO_PATH)
message("JPO_PATH (from environment) is ${JPO_PATH}")

# resources
file(TO_CMAKE_PATH "$ENV{JPO_PATH}/resources" JPO_RES_PATH)
message("----JPO_PATH/resources is: ${JPO_RES_PATH}")
if (NOT EXISTS "${JPO_RES_PATH}")
	message(FATAL_ERROR "Directory JPO_RES_PATH '${JPO_RES_PATH}' does not exist. ${TO_FIX}")
endif()

# Set board
set(JPO_BOARD_DIR "${JPO_PATH}/resources/build_config")
if (NOT EXISTS "${JPO_BOARD_DIR}")
	message(FATAL_ERROR "Directory '${JPO_BOARD_DIR}' does not exist.")
endif()
set(PICO_BOARD_HEADER_DIRS "${JPO_BOARD_DIR}")
set(PICO_BOARD "jpo_brain")

# Intialize the Pico SDK
include(pico_sdk_import.cmake)

project(jpo_bootloader C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Build the jpo_bootloader as a standalone thing

add_executable(jpo_bootloader main.c core1.c)

###### JPO additions
# Enable usb output, disable uart output
pico_enable_stdio_usb(jpo_bootloader 1)
# pico_enable_stdio_uart(jpo_bootloader 0)

### Prerequisites
set(TO_FIX "To fix, (re)install the JPO tools.")
if (NOT DEFINED ENV{JPO_PATH})
	message(FATAL_ERROR "Environment variable JPO_PATH not set. ${TO_FIX}")
endif()
# Fix backslashes in Windows
file(TO_CMAKE_PATH "$ENV{JPO_PATH}" JPO_PATH)
message("JPO_PATH (from environment) is ${JPO_PATH}")

### Includes
target_include_directories(${PROJECT_NAME}
	# PRIVATE "${JPO_RES_PATH}/include/hal" # not needed for bootloader
	PRIVATE "${JPO_RES_PATH}/include/jcomp"
	PRIVATE "${JPO_RES_PATH}/include/jcomp_protocol" # protocol
)

# CMake docs recommend using find_library instead, but couldn't get it to work
target_link_directories(${PROJECT_NAME}
	PRIVATE "${JPO_RES_PATH}/lib"
)

###### end JPO additions

function(target_cl_options option)
    target_compile_options(jpo_bootloader PRIVATE ${option})
    target_link_options(jpo_bootloader PRIVATE ${option})
endfunction()

target_cl_options("-Os")
target_cl_options("-ffunction-sections")
target_cl_options("-fdata-sections")
target_link_options(jpo_bootloader PRIVATE "LINKER:--gc-sections")

pico_add_extra_outputs(jpo_bootloader)
pico_set_binary_type(jpo_bootloader copy_to_ram)

set_target_properties(jpo_bootloader PROPERTIES COMPILE_FLAGS "-Wall")


pico_set_linker_script(jpo_bootloader ${CMAKE_CURRENT_SOURCE_DIR}/bootloader.ld)

target_link_libraries(jpo_bootloader
                      pico_stdlib
					  pico_multicore
                      hardware_dma
                      hardware_flash
                      hardware_structs
                      hardware_resets
                      cmsis_core
					  jpo_jcomp
					  jpo_jcomp_protocol
					  )

set(BOOTLOADER_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "")

# Build a library to embed into applications
function(bootloader_define_library)
	set(NAME jpo_bootloader)
	set(ORIGINAL_BIN ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.bin)
	set(BIN_ASM ${CMAKE_CURRENT_BINARY_DIR}/${NAME}_bin.S)

	add_custom_target(${NAME}_bin DEPENDS ${ORIGINAL_BIN})
	add_custom_command(OUTPUT ${ORIGINAL_BIN} DEPENDS ${NAME} COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:${NAME}> ${ORIGINAL_BIN})

	find_package (Python3 REQUIRED COMPONENTS Interpreter)
	add_custom_target(${NAME}_bin_asm DEPENDS ${BIN_ASM})
	add_custom_command(OUTPUT ${BIN_ASM} DEPENDS ${ORIGINAL_BIN}
		COMMAND ${Python3_EXECUTABLE} ${BOOTLOADER_DIR}/mkasm.py ${ORIGINAL_BIN} ${BIN_ASM}
	)

	add_library(${NAME}_library INTERFACE)
	add_dependencies(${NAME}_library ${NAME}_bin_asm)
	# not strictly (or indeed actually) a link library, but this avoids dependency cycle
	target_link_libraries(${NAME}_library INTERFACE ${BIN_ASM})
endfunction()

bootloader_define_library()

# Provide a helper to build a combined target

function(bootloader_build_combined NAME)
	set(APP ${NAME}_app)
	set(APP_BIN ${CMAKE_CURRENT_BINARY_DIR}/${APP}.bin)
	set(APP_HDR ${CMAKE_CURRENT_BINARY_DIR}/${APP}_hdr.bin)

	set(COMBINED ${NAME}_combined)

	target_link_libraries(${NAME} bootloader_library)

	pico_set_linker_script(${NAME} ${BOOTLOADER_DIR}/combined.ld)

	pico_add_bin_output(${NAME})

	# TODO: The hard-coded 16k here is a bit nasty
	add_custom_target(${APP}_bin DEPENDS ${APP_BIN})
	add_custom_command(OUTPUT ${APP_BIN} DEPENDS ${NAME}.bin
		COMMAND dd ibs=1k iseek=16 if=${NAME}.bin of=${APP_BIN}
	)

	# TODO: The hard-coded address here is a bit nasty
	add_custom_target(${APP}_hdr DEPENDS ${APP}_bin)
	add_custom_command(OUTPUT ${APP_HDR} DEPENDS ${APP_BIN}
		COMMAND ${BOOTLOADER_DIR}/gen_imghdr.py -a 0x10004000 ${APP_BIN} ${APP_HDR}
	)

	add_custom_target(${COMBINED} ALL DEPENDS ${APP_HDR})
	add_custom_command(TARGET ${COMBINED} DEPENDS ${APP_HDR}
		COMMAND ${CMAKE_OBJCOPY} --update-section .app_hdr=${APP_HDR} ${NAME}.elf ${COMBINED}.elf
	)
	add_custom_command(TARGET ${COMBINED} POST_BUILD
		COMMAND ${CMAKE_OBJCOPY} -Obinary ${COMBINED}.elf ${COMBINED}.bin
	)
endfunction()

# Provide a helper to build a standalone target

function(bootloader_build_standalone NAME)
	pico_set_linker_script(${NAME} ${BOOTLOADER_DIR}/standalone.ld)
	pico_add_bin_output(${NAME})
endfunction()

# Copy to JPO_PATH/resources/bin
add_custom_command(
	TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy
			"${CMAKE_CURRENT_BINARY_DIR}/jpo_bootloader.uf2"
			"${JPO_PATH}/resources/bin/jpo_bootloader.uf2")

# Generate version files with arguments
set(H_PATH "${CMAKE_CURRENT_SOURCE_DIR}/version.h")
set(TXT_PATH "${JPO_PATH}/resources/bin/jpo_bootloader_version.txt")
add_custom_target(generate_version ALL
    COMMAND ${CMAKE_COMMAND} -DH_PATH=${H_PATH} -DTXT_PATH=${TXT_PATH} -P ${JPO_PATH}/resources/build_config/jpo_version.cmake
)
add_dependencies(${PROJECT_NAME} generate_version)

