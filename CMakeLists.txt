cmake_minimum_required(VERSION 2.6)

if (NOT CMAKE_BUILD_TYPE)
	set (CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type: Debug Release..." FORCE)
endif (NOT CMAKE_BUILD_TYPE)

# C flags need to be set before enabling language?
set(CMAKE_C_FLAGS_DEBUG "-g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare" CACHE STRING "Debug compiler flags")
set(CMAKE_C_FLAGS_RELEAES "-Wall" CACHE STRING "Release compiler flags")

enable_language(C)

project(ihk C)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules)
include(GNUInstallDirs)
include(CMakeParseArguments)
include(Kbuild)
include(Ksym)

# build options
option(ENABLE_WERROR "Enable -Werror" OFF)
if (ENABLE_WERROR)
	add_compile_options("-Werror")
endif(ENABLE_WERROR)
set(BUILD_TARGET "smp-x86" CACHE STRING "Build target: smp-x86 | smp-arm64")
set_property(CACHE BUILD_TARGET PROPERTY STRINGS smp-x86 smp-arm64)

if (BUILD_TARGET STREQUAL "smp-x86")
	option(ENABLE_PERF "Enable perf support" ON)
	set(ARCH "x86_64")
elseif (BUILD_TARGET STREQUAL "smp-arm64")
	option(ENABLE_PERF "Enable perf support" OFF)
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
	OUTPUT_VARIABLE BUILDID OUTPUT_STRIP_TRAILING_WHITESPACE)

if(BUILD_TARGET STREQUAL "smp-x86")
	ksym(x86_trampoline_base PREFIX IHK_)
	ksym(real_mode_header PREFIX IHK_)
	ksym(per_cpu__vector_irq PREFIX IHK_)
	ksym(vector_irq PREFIX IHK_)
	ksym(lapic_get_maxlvt PREFIX IHK_)
	ksym(init_deasserted PREFIX IHK_)
	ksym(irq_to_desc PREFIX IHK_)
	ksym(alloc_desc PREFIX IHK_)
	ksym(irq_desc_tree PREFIX IHK_)
	ksym(irq_to_desc_alloc_node PREFIX IHK_)
	ksym(dummy_irq_chip PREFIX IHK_)
	ksym(get_uv_system_type PREFIX IHK_)
	ksym(wakeup_secondary_cpu_via_init PREFIX IHK_)
	ksym(__default_send_IPI_dest_field PREFIX IHK_)
	ksym(vmap_area_root PREFIX IHK_)
	ksym(vmap_area_lock PREFIX IHK_)
	ksym(__insert_vmap_area PREFIX IHK_)
	ksym(__free_vmap_area PREFIX IHK_)

elseif(BUILD_TARGET STREQUAL "smp-arm64")
	ksym(gic_data PREFIX IHK_ SOURCE_FILE irq-gic.c SUFFIX _gicv2)
	ksym(gic_data PREFIX IHK_ SOURCE_FILE irq-gic-v3.c SUFFIX _gicv3)
	ksym(gic_raise_softirq PREFIX IHK_ SOURCE_FILE irq-gic.c SUFFIX _gicv2)
	ksym(gic_raise_softirq PREFIX IHK_ SOURCE_FILE irq-gic-v3.c SUFFIX _gicv3)
	ksym(__irq_domain_alloc_irqs PREFIX IHK_)
	ksym(irq_domain_free_irqs PREFIX IHK_)
	ksym(irq_to_desc PREFIX IHK_)
	ksym(alloc_desc PREFIX IHK_)
	ksym(irq_desc_tree PREFIX IHK_)
	ksym(irq_to_desc_alloc_node PREFIX IHK_)
	ksym(psci_ops PREFIX IHK_)
	ksym(__cpu_logical_map PREFIX IHK_)
	ksym(invoke_psci_fn PREFIX IHK_)
	ksym(__invoke_psci_fn_hvc PREFIX IHK_)
	ksym(__invoke_psci_fn_smc PREFIX IHK_)
	ksym(acpi_gic_ver PREFIX IHK_)
	ksym(cpu_pmu PREFIX IHK_)
	ksym(__oprofile_cpu_pmu PREFIX IHK_)
	ksym(__irq_set_affinity PREFIX IHK_)
	ksym(arch_timer_use_virtual PREFIX IHK_)
	ksym(arch_timer_uses_ppi PREFIX IHK_)
	ksym(arch_timer_rate PREFIX IHK_)
endif()



configure_file(config.h.in config.h)


# config report only if standalone
if( CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR )
	message("Build type: ${CMAKE_BUILD_TYPE}")
	message("Build target: ${BUILD_TARGET}")
	message("ENABLE_MEMDUMP: ${ENABLE_MEMDUMP}")
	message("ENABLE_PERF: ${ENABLE_PERF}")
	message("ENABLE_RUSAGE: ${ENABLE_RUSAGE}")
endif()
