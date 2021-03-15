cmake_minimum_required(VERSION 3.11)

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

execute_process(COMMAND bash -c "ls -ld ls /proc/tofu/ 2>/dev/null | wc -l"
  OUTPUT_VARIABLE PROC_TOFU OUTPUT_STRIP_TRAILING_WHITESPACE)

if(PROC_TOFU STREQUAL "1")
	option(ENABLE_TOFU "Built-in tofu driver support" ON)
else()
	option(ENABLE_TOFU "Built-in tofu driver support" OFF)
endif()

if(ENABLE_TOFU)
	add_definitions(-DENABLE_TOFU)
	set(KBUILD_C_FLAGS "${KBUILD_C_FLAGS} -DENABLE_TOFU")
endif()

execute_process(COMMAND bash -c "grep $(hostname) /etc/opt/FJSVfefs/config/fefs_node1.csv 2>/dev/null | cut -d, -f2 | grep -o CN"
  OUTPUT_VARIABLE FUGAKU_NODE_TYPE OUTPUT_STRIP_TRAILING_WHITESPACE)
if(FUGAKU_NODE_TYPE STREQUAL "CN")
	option(ENABLE_FUGAKU_HACKS "Fugaku hacks" ON)
else()
	option(ENABLE_FUGAKU_HACKS "Fugaku hacks" OFF)
endif()

if(ENABLE_FUGAKU_HACKS)
	add_definitions(-DENABLE_FUGAKU_HACKS)
	set(KBUILD_C_FLAGS "${KBUILD_C_FLAGS} -DENABLE_FUGAKU_HACKS")
endif()

# krm that mandates reserved memory amount >= available at boot time?
execute_process(COMMAND bash -c "rpm -qi FJSVpxkrm-plugin-mckernel | awk '$1 == \"Version\" && $2 == \":\" { print $3 }'"
  OUTPUT_VARIABLE KRM_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
message("KRM_VERSION: ${KRM_VERSION}")

if(NOT "${KRM_VERSION}" STREQUAL "" AND "${KRM_VERSION}" VERSION_LESS_EQUAL 4.0.1)
	option(ENABLE_KRM_WORKAROUND "krm workaround" ON)
else()
	option(ENABLE_KRM_WORKAROUND "krm workaround" OFF)
endif()

if(ENABLE_KRM_WORKAROUND)
	add_definitions(-DENABLE_KRM_WORKAROUND)
	set(KBUILD_C_FLAGS "${KBUILD_C_FLAGS} -DENABLE_KRM_WORKAROUND")
endif()

if(NOT "${KRM_VERSION}" STREQUAL "")
	option(WITH_KRM "krm is used" ON)
else()
	option(WITH_KRM "krm is used" OFF)
endif()

if(WITH_KRM)
	add_definitions(-DWITH_KRM)
	set(KBUILD_C_FLAGS "${KBUILD_C_FLAGS} -DWITH_KRM")
endif()

# SIGSTOP instead of SIGSEGV, additional IHK Linux kmsg
option(ENABLE_FUGAKU_DEBUG "Fugaku debug instrumentation" OFF)

if(ENABLE_FUGAKU_DEBUG)
	add_definitions(-DENABLE_FUGAKU_DEBUG)
	set(KBUILD_C_FLAGS "${KBUILD_C_FLAGS} -DENABLE_FUGAKU_DEBUG")
endif()

# redirect kernel messages to Linux's /dev/kmsg
option(ENABLE_KMSG_REDIRECT "Redirect kernel message to Linux's /dev/kmsg" OFF)

if(ENABLE_KMSG_REDIRECT)
	add_definitions(-DENABLE_KMSG_REDIRECT)
	set(KBUILD_C_FLAGS "${KBUILD_C_FLAGS} -DENABLE_KMSG_REDIRECT")
endif()

# exclude mckernel memory from Linux dump
option(EXCLUDE_MEM_FROM_LINUX_DUMP "Exclude McKernel memory from Linux dump" ON)

if(EXCLUDE_MEM_FROM_LINUX_DUMP)
	add_definitions(-DEXCLUDE_MEM_FROM_LINUX_DUMP)
	set(KBUILD_C_FLAGS "${KBUILD_C_FLAGS} -DEXCLUDE_MEM_FROM_LINUX_DUMP")
endif()

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

	set(POSTK_DEBUG_DEFINES "-DPOSTK_DEBUG_ARCH_DEP_100 -DPOSTK_DEBUG_ARCH_DEP_42 -DPOSTK_DEBUG_TEMP_FIX_29 -DPOSTK_DEBUG_TEMP_FIX_49")
	add_definitions("${POSTK_DEBUG_DEFINES}")
	set(KBUILD_C_FLAGS "${KBUILD_C_FLAGS} ${POSTK_DEBUG_DEFINES}")
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
	message("ENABLE_TOFU: ${ENABLE_TOFU}")
	message("ENABLE_KRM_WORKAROUND: ${ENABLE_KRM_WORKAROUND}")
	message("ENABLE_RUSAGE: ${ENABLE_RUSAGE}")
	message("ENABLE_WERROR: ${ENABLE_WERROR}")
endif()

message("WITH_KRM: ${WITH_KRM}")
message("ENABLE_KMSG_REDIRECT: ${ENABLE_KMSG_REDIRECT}")
message("EXCLUDE_MEM_FROM_LINUX_DUMP: ${EXCLUDE_MEM_FROM_LINUX_DUMP}")
