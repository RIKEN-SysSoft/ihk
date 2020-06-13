cmake_minimum_required(VERSION 2.6)

if (NOT CMAKE_BUILD_TYPE)
	set (CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type: Debug Release..." FORCE)
endif (NOT CMAKE_BUILD_TYPE)

enable_language(C)

project(ihk C)
set(IHK_VERSION "1.7.0rc4")

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)
include(GNUInstallDirs)
include(CMakeParseArguments)
include(Kbuild)

if (CMAKE_INSTALL_PREFIX STREQUAL "/usr")
        set(KMODDIR "/lib/modules/${UNAME_R}/extra/mckernel")
else()
        set(KMODDIR "${CMAKE_INSTALL_PREFIX}/kmod")
endif()

# build options
# C flags need to be set before enabling language?
set(CFLAGS_WARNING "-Wall" "-Wextra" "-Wno-unused-parameter" "-Wno-sign-compare" "-Wno-unused-function" CACHE STRING "Warning flags")
add_compile_options(${CFLAGS_WARNING})

option(ENABLE_WERROR "Enable -Werror" OFF)
if (ENABLE_WERROR)
	add_compile_options("-Werror")
endif(ENABLE_WERROR)

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
	set(BUILD_TARGET "smp-x86" CACHE STRING "Build target: smp-x86 | smp-arm64")
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
	set(BUILD_TARGET "smp-arm64" CACHE STRING "Build target: smp-x86 | smp-arm64")
endif()
set_property(CACHE BUILD_TARGET PROPERTY STRINGS smp-x86 smp-arm64)

if (BUILD_TARGET STREQUAL "smp-x86")
	option(ENABLE_PERF "Enable perf support" ON)
	set(ARCH "x86_64")
elseif (BUILD_TARGET STREQUAL "smp-arm64")
	option(ENABLE_PERF "Enable perf support" OFF)
	foreach(i RANGE 1 120)
		add_definitions(-DPOSTK_DEBUG_ARCH_DEP_${i} -DPOSTK_DEBUG_TEMP_FIX_${i})
		set(KBUILD_C_FLAGS "${KBUILD_C_FLAGS} -DPOSTK_DEBUG_ARCH_DEP_${i} -DPOSTK_DEBUG_TEMP_FIX_${i}")
	endforeach()
	set(ARCH "arm64")
endif()

set(ENABLE_MEMDUMP AUTO CACHE STRING "Enable memory dump support")
set_property(CACHE ENABLE_MEMDUMP PROPERTY STRINGS AUTO ON OFF)
if (ENABLE_MEMDUMP STREQUAL AUTO)
	if (BUILD_TARGET MATCHES "smp-.*")
		set(ENABLE_MEMDUMP ON)
	else()
		set(ENABLE_MEMDUMP OFF)
	endif()
endif(ENABLE_MEMDUMP STREQUAL AUTO)
if (ENABLE_MEMDUMP)
	# XXX check libiberty?
endif(ENABLE_MEMDUMP)
# bfd seems always needed, so check always.
find_library(LIBBFD bfd)
find_library(LIBIBERTY iberty)
find_library(LIBUDEV udev)

option(ENABLE_PERF "Enable perf support" ON)
option(ENABLE_RUSAGE "Enable rusage support" ON)

# actual build section - just subdirs
add_subdirectory("linux/core")
add_subdirectory("linux/user")
if(BUILD_TARGET STREQUAL "attached-mic")
	#add_subdirectory("linux/driver/attached/mic")
elseif(BUILD_TARGET MATCHES "builtin.*")
	#add_subdirectory("linux/driver/builtin")
elseif(BUILD_TARGET MATCHES "smp.*")
	add_subdirectory("linux/driver/smp")
else()
	message(FATAL_ERROR "Invalid target ${BUILD_TARGET}")
endif()

# rest of config.h
execute_process(COMMAND git --git-dir=${PROJECT_SOURCE_DIR}/.git rev-parse --short HEAD
	OUTPUT_VARIABLE BUILDID OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(BUILDID STREQUAL "")
	set(BUILDID ${IHK_VERSION})
endif()
# also set BUILDID for mckernel
set(BUILDID ${BUILDID} PARENT_SCOPE)

configure_file(config.h.in config.h)


# config report only if standalone
if( CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR )
	message("Build type: ${CMAKE_BUILD_TYPE}")
	message("Build target: ${BUILD_TARGET}")
	message("ENABLE_MEMDUMP: ${ENABLE_MEMDUMP}")
	message("ENABLE_PERF: ${ENABLE_PERF}")
	message("ENABLE_RUSAGE: ${ENABLE_RUSAGE}")
	message("ENABLE_WERROR: ${ENABLE_WERROR}")
endif()
