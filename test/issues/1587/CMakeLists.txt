cmake_policy(SET CMP0005 NEW)

execute_process(COMMAND basename ${CMAKE_CURRENT_SOURCE_DIR} OUTPUT_VARIABLE issue_id OUTPUT_STRIP_TRAILING_WHITESPACE)

# Options: -DWITH_MCK=<McKernel install directory>
add_definitions(-DWITH_MCK=${WITH_MCK})

# for autotest
if(NOT DEFINED CMAKE_INSTALL_PREFIX_SCRIPTS)
       set(CMAKE_INSTALL_PREFIX_SCRIPTS ${CMAKE_INSTALL_PREFIX}/scripts)
endif()

cmake_minimum_required(VERSION 3.0)

# for string replacement
project(issue1587 C)
if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(BUILD_TARGET smp-x86)
  set(KMOD_POSTFIX smp_x86)
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
  set(BUILD_TARGET smp-arm64)
  set(KMOD_POSTFIX smp_arm64)
endif()

# CPPFLAGS

set(UNAME_R ${CMAKE_SYSTEM_VERSION} CACHE STRING "Kernel version to build against")
set(KERNEL_DIR "/lib/modules/${UNAME_R}/build" CACHE STRING "kernel build directory")
execute_process(COMMAND awk -F= "$1 == \"CONFIG_ARM64_64K_PAGES\" { print $2; exit; }" "${KERNEL_DIR}/.config"
	OUTPUT_VARIABLE CONFIG_ARM64_64K_PAGES OUTPUT_STRIP_TRAILING_WHITESPACE)
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
	if(CONFIG_ARM64_64K_PAGES STREQUAL "y")
		set(PAGE_SIZE "65536")
	else()
		set(PAGE_SIZE "4096")
	endif()
else()
		set(PAGE_SIZE "4096")
endif()
message("PAGE_SIZE: ${PAGE_SIZE}")

# CFLAGS
set(CFLAGS_WARNING "-Wall" "-Wextra" "-Wno-unused-parameter" "-Wno-sign-compare" "-Wno-unused-function" ${EXTRA_WARNINGS} CACHE STRING "Warning flags")
add_compile_options(
  -g
  ${CFLAGS_WARNING}
  )


# -L, this must be done before adding dependants
link_directories("${WITH_MCK}/lib64")

# -Wl,--rpath=, this must be done before adding dependants
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_INSTALL_RPATH "${WITH_MCK}/lib64")

# programs running on Linux
foreach(target IN ITEMS
    besteffort
    )

  # Add C target
  add_executable(${target} ${target}.c)

  # -I
  target_include_directories(${target}
    PRIVATE "${WITH_MCK}/include"
    )

  # -l
  target_link_libraries(${target} PRIVATE ihk)

  # String replacement and install
  configure_file(${target}.sh.in ${issue_id}-${target} @ONLY)

  # Install
  install(TARGETS ${target} DESTINATION bin)
  install(PROGRAMS ${CMAKE_BINARY_DIR}/${issue_id}-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})

endforeach()
