execute_process(COMMAND bash -c "gawk '/CPU implementer/ { print \$4; exit; }' /proc/cpuinfo"
  OUTPUT_VARIABLE CPU_IMPLEMENTER OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND bash -c "gawk '/CPU architecture/ { print \$3; exit; }' /proc/cpuinfo"
  OUTPUT_VARIABLE CPU_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND bash -c "gawk '/CPU variant/ { print \$4; exit; }' /proc/cpuinfo"
  OUTPUT_VARIABLE CPU_VARIANT OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND bash -c "gawk '/CPU part/ { print \$4; exit; }' /proc/cpuinfo"
  OUTPUT_VARIABLE CPU_PART OUTPUT_STRIP_TRAILING_WHITESPACE)

if(CPU_IMPLEMENTER STREQUAL "0x46" AND CPU_ARCH STREQUAL "8" AND
    CPU_VARIANT STREQUAL "0x1" AND CPU_PART STREQUAL "0x001")
  message("A64FX detected")
  set(CPU_MODEL "a64fx")
endif()

cmake_policy(SET CMP0005 NEW)

# Options: -DWITH_MCK=<McKernel install directory>
add_definitions(-DWITH_MCK=${WITH_MCK})

# Options: -DWITH_MCK_SRC=<McKernel source directory>
add_definitions(-DWITH_MCK_SRC=${WITH_MCK_SRC})

cmake_minimum_required(VERSION 3.0)

project(ihk_rmif_bbtest C)

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

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  add_definitions(-DBUILD_TARGET=smp-x86)
  add_definitions(-DKMOD_POSTFIX=smp_x86)
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
  add_definitions(-DBUILD_TARGET=smp-arm64)
  add_definitions(-DKMOD_POSTFIX=smp_arm64)
endif()

add_definitions(-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX})

# CFLAGS
set(CFLAGS_WARNING "-Wall" "-Wextra" "-Wno-unused-parameter" "-Wno-sign-compare" "-Wno-unused-function" ${EXTRA_WARNINGS} CACHE STRING "Warning flags")
add_compile_options(
  -O2
  -g
  ${CFLAGS_WARNING}
  )


# -L, this must be done before adding dependants
link_directories("${WITH_MCK}/lib64")

# -Wl,--rpath=, this must be done before adding dependants
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_INSTALL_RPATH "${WITH_MCK}/lib64")

# Add libraries
foreach(target IN ITEMS
    cpu
    mem
    os
    )
  add_library(${target} STATIC src/${target}.c)
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${WITH_MCK}/include"
    )
  target_link_libraries(${target}
    PUBLIC ihk
    )
endforeach()

foreach(target IN ITEMS
    params
    linux
    user
    )
  add_library(${target} STATIC src/${target}.c)
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${WITH_MCK}/include"
    )
endforeach()

# library used by program running on McKernel
foreach(target IN ITEMS
    mckernel
    )
  add_library(${target} STATIC src/${target}.c)
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    )
endforeach()

foreach(target IN ITEMS
    ihk_reserve_cpu01
    ihk_reserve_cpu02
    ihk_reserve_cpu03
    ihk_reserve_cpu04
    ihk_reserve_cpu05
    ihk_reserve_cpu06
    ihk_get_num_reserved_cpus01
    ihk_get_num_reserved_cpus02
    ihk_get_num_reserved_cpus03
    ihk_get_num_reserved_cpus05
    ihk_get_num_reserved_cpus06
    ihk_query_cpu01
    ihk_query_cpu02
    ihk_query_cpu03
    ihk_query_cpu04
    ihk_query_cpu05
    ihk_query_cpu06
    ihk_release_cpu01
    ihk_release_cpu02
    ihk_release_cpu03
    ihk_release_cpu04
    ihk_release_cpu05
    ihk_release_cpu06
    ihk_release_mem01
    ihk_release_mem02
    ihk_release_mem03
    ihk_release_mem04
    ihk_release_mem05
    ihk_release_mem06
    ihk_release_mem07
    ihk_reserve_mem01
    ihk_reserve_mem02
    ihk_reserve_mem07
    ihk_reserve_mem08
    ihk_os_assign_cpu01
    ihk_os_assign_cpu02
    ihk_os_assign_cpu03
    ihk_os_assign_cpu04
    ihk_os_assign_cpu05
    ihk_os_assign_cpu06
    ihk_os_assign_cpu07
    ihk_os_assign_mem01
    ihk_os_assign_mem02
    ihk_os_assign_mem03
    ihk_os_assign_mem04
    ihk_os_shutdown07
    ihk_os_set_ikc_map07
    #  ihk_destroy_os07
    ihk_destroy_os03
    #ihk_destroy_os08
    ihk_destroy_os05
    ihk_destroy_os06
    ihk_os_shutdown08
    ihk_os_shutdown01
    ihk_os_release_mem01
    ihk_os_release_mem02
    ihk_os_release_mem03
    ihk_os_release_mem04
    ihk_os_release_mem05
    ihk_os_release_mem06
    ihk_os_release_mem07
    ihk_os_release_mem08
    ihk_os_release_cpu01
    ihk_os_release_cpu02
    ihk_os_release_cpu03
    ihk_os_release_cpu04
    ihk_os_release_cpu05
    ihk_os_release_cpu06
    ihk_os_release_cpu07
    ihk_os_shutdown03
    ihk_os_shutdown09
    ihk_os_shutdown05
    ihk_os_shutdown06
    ihk_destroy_os09
    ihk_destroy_os10
    ihk_destroy_os01
    ihk_os_get_num_assigned_cpus01
    ihk_os_get_num_assigned_cpus02
    ihk_os_get_num_assigned_cpus03
    ihk_os_get_num_assigned_cpus04
    ihk_os_get_eventfd01
    ihk_os_get_eventfd02
    ihk_os_get_eventfd03
    ihk_os_get_eventfd05
    ihk_os_get_eventfd06
    ihk_reserve_mem09
    ihk_os_get_eventfd07
    ihk_os_query_cpu01
    ihk_os_query_cpu02
    ihk_os_query_cpu03
    ihk_os_query_cpu04
    ihk_os_query_cpu07
    ihk_get_num_reserved_mem_chunks01
    ihk_get_num_reserved_mem_chunks02
    ihk_get_num_reserved_mem_chunks03
    ihk_get_num_reserved_mem_chunks05
    ihk_get_num_reserved_mem_chunks06
    ihk_query_mem01
    ihk_query_mem02
    ihk_reserve_mem10
    ihk_reserve_mem11
    ihk_query_mem03
    ihk_query_mem04
    ihk_query_mem05
    ihk_query_mem06
    ihk_query_mem07
    ihk_create_os01
    ihk_create_os02
    ihk_create_os05
    ihk_create_os06
    ihk_get_num_os_instances01
    ihk_get_num_os_instances02
    ihk_get_num_os_instances03
    ihk_get_num_os_instances05
    ihk_get_num_os_instances06
    ihk_get_os_instances01
    ihk_get_os_instances02
    ihk_get_os_instances03
    ihk_get_os_instances04
    ihk_get_os_instances05
    ihk_get_os_instances06
    ihk_os_assign_mem05
    ihk_os_assign_mem06
    ihk_os_assign_mem07
    ihk_os_assign_mem08
    ihk_os_assign_mem09
    ihk_os_get_num_assigned_mem_chunks01
    ihk_os_get_num_assigned_mem_chunks02
    ihk_os_get_num_assigned_mem_chunks03
    ihk_os_get_num_assigned_mem_chunks04
    ihk_os_query_mem01
    ihk_os_query_mem02
    ihk_os_query_mem03
    ihk_os_query_mem04
    ihk_os_query_mem05
    ihk_os_load01
    ihk_os_load02
    ihk_os_load03
    ihk_reserve_mem12
    ihk_os_load04
    ihk_os_load05
    ihk_os_load06
    ihk_os_kargs01
    ihk_os_kargs02
    ihk_os_kargs03
    ihk_os_kargs04
    ihk_os_kargs05
    ihk_os_kargs06
    ihk_os_boot01
    ihk_os_boot02
    ihk_os_boot03
    ihk_os_boot05
    ihk_os_boot06
    ihk_reserve_mem03
    ihk_reserve_mem04
    ihk_reserve_mem05
    ihk_reserve_mem06
    ihk_os_get_status01
    ihk_os_get_status02
    ihk_os_get_kmsg_size01
    ihk_os_get_kmsg_size02
    ihk_reserve_mem13
    ihk_os_kmsg01
    ihk_os_kmsg02
    ihk_os_kmsg03
    ihk_os_kmsg04
    ihk_os_kmsg05
    ihk_os_clear_kmsg01
    ihk_os_clear_kmsg02
    ihk_os_clear_kmsg03
    ihk_os_get_eventfd08
    ihk_os_setperfevent07
    ihk_os_setperfevent08
    ihk_os_clear_kmsg04
    ihk_os_get_num_numa_nodes01
    ihk_os_get_num_numa_nodes02
    ihk_os_get_num_pagesizes01
    ihk_os_get_num_pagesizes02
    ihk_os_get_pagesizes01
    ihk_os_get_pagesizes02
    ihk_os_get_pagesizes03
    ihk_os_get_pagesizes04
    ihk_os_set_ikc_map01
    ihk_os_set_ikc_map02
    ihk_os_set_ikc_map03
    ihk_os_set_ikc_map04
    ihk_os_set_ikc_map05
    ihk_os_set_ikc_map06
    ihk_os_set_ikc_map08
    ihk_os_set_ikc_map09
    ihk_os_set_ikc_map10
    ihk_os_get_ikc_map01
    ihk_os_get_ikc_map02
    ihk_os_get_ikc_map03
    ihk_os_get_ikc_map04
    ihk_os_get_ikc_map05
    ihk_os_get_ikc_map06
    ihk_os_freeze08
    ihk_os_setperfevent01
    ihk_os_setperfevent02
    ihk_os_setperfevent03
    ihk_os_setperfevent04
    ihk_os_setperfevent05
    ihk_os_setperfevent06
    ihk_os_perfctl01
    ihk_os_perfctl02
    ihk_os_perfctl03
#    ihk_os_perfctl04
    ihk_os_perfctl05
    ihk_os_perfctl06
    ihk_os_perfctl07
    ihk_os_perfctl08
    ihk_os_getperfevent01
    ihk_os_getperfevent02
    ihk_os_getperfevent03
    ihk_os_getperfevent04
    ihk_os_getperfevent05
    ihk_os_getperfevent06
    ihk_os_getperfevent07
    ihk_os_getperfevent08
    ihk_os_freeze01
    ihk_os_freeze02
    ihk_os_freeze03
    ihk_os_freeze05
    ihk_os_freeze06
    ihk_os_thaw01
    ihk_os_thaw02
    ihk_os_thaw03
    ihk_os_thaw05
    ihk_os_thaw06
    ihk_os_makedumpfile01
    ihk_os_makedumpfile02
    ihk_os_makedumpfile03
    ihk_os_makedumpfile04
    ihk_os_makedumpfile05
    ihk_os_makedumpfile06
    ihk_os_get_status08
    ihk_os_thaw08
    ihk_reserve_mem_conf03
    ihk_reserve_mem_conf04
    ihk_os_getrusage01
    ihk_os_getrusage02
    ihk_os_getrusage03
    ihk_os_getrusage04
    ihk_os_getrusage05
    ihk_os_getrusage06
    ihk_os_getrusage07
    ihk_os_getrusage08
    ihk_os_getrusage09
    ihk_os_read_cpu_register07
    ihk_os_getrusage10
    ihk_os_getrusage11
    ihk_os_getrusage12
    ihk_os_getrusage13
    ihk_os_getrusage14
    ihk_os_setperfevent09
    ihk_os_load07
    ihk_os_load08
    ihk_os_set_ikc_map11
    ihk_os_set_ikc_map12
    ihk_os_set_ikc_map13
    ihk_os_assign_cpu08
    ihk_os_release_cpu08
    ihk_os_read_cpu_register08
    ihk_os_read_cpu_register02
    ihk_os_read_cpu_register01
    ihk_os_release_cpu09
    ihk_os_release_cpu10
    ihk_reserve_mem_conf01
    ihk_reserve_mem_conf05
    ihk_reserve_mem_conf06
    ihk_release_cpu07
    ihk_reserve_cpu07
    ihk_os_write_cpu_register02
    ihk_os_write_cpu_register01
    )

  # Add target
  add_executable(${target} src/${target}.c)

  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )

  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )

  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )

  # String replacement
  configure_file(src/${target}.sh.in ${target}.sh @ONLY)

  # Install
  install(TARGETS ${target} DESTINATION bin)
  install(PROGRAMS ${CMAKE_BINARY_DIR}/${target}.sh DESTINATION bin)

endforeach()

# shell scripts with the need for string replacement
foreach(target IN ITEMS
    util
    ikc_map
    )
  configure_file(src/${target}.sh.in ${target}.sh @ONLY)
  install(PROGRAMS ${CMAKE_BINARY_DIR}/util.sh DESTINATION bin)
endforeach()

# utility awk scripts
foreach(target IN ITEMS
    zoneinfo.awk
    check_dump.awk
    )
  install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/src/${target} DESTINATION bin)
endforeach()

# patches
foreach(target IN ITEMS
    status_mckernel status_ihk
    mckernel_mem_alloc
    consume_cpu_time
    delay_with_interrupt_disabled_mckernel
    str
    )
  install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/src/${target}.patch DESTINATION bin)
endforeach()

# programs running on McKernel
foreach(target IN ITEMS
    panic
    hungup
    delay_with_interrupt_disabled
    oom
    count
    mmap
    ikc_map
    read_write_cpu_register
    str
    )

  # Add target
  add_executable(${target} src/${target}.c)

  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )

  # Install
  install(TARGETS ${target} DESTINATION bin)

endforeach()

# programs running on McKernel using mckernel.c
foreach(target IN ITEMS
    nop
    nop_ldr
    consume_cpu_time
    )

  # Add target
  add_executable(${target} src/${target}.c)

  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    )

  # -l
  target_link_libraries(${target}
    PRIVATE mckernel
    )

  # Install
  install(TARGETS ${target} DESTINATION bin)

endforeach()

# programs need pthread library
foreach(target IN ITEMS
    spawn_threads
    )

  add_executable(${target} src/${target}.c)

  find_package(Threads)
  target_link_libraries(${target} PRIVATE ${CMAKE_THREAD_LIBS_INIT})

  install(TARGETS ${target} DESTINATION bin)

endforeach()

# programs need libnuma
foreach(target IN ITEMS
    numa_alloc
    )

  add_executable(${target} src/${target}.c)
  target_link_libraries(${target} PRIVATE numa)

  install(TARGETS ${target} DESTINATION bin)
endforeach()

# programs needing fcc and mckernel.h
if(CPU_MODEL STREQUAL "a64fx")
  foreach(target IN ITEMS
      vfp_sve_wfi_mem
      )

    execute_process(COMMAND bash -c "module show lang 2>&1 | awk '/LD_LIBRARY_PATH/ { if (str != \"\") { str=str \":\" } str=str $3 } END { print str }'"
      OUTPUT_VARIABLE FCC_LD_LIBRARY_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)

    message("FCC_LD_LIBRARY_PATH: ${FCC_LD_LIBRARY_PATH}")

    add_custom_target(${target} ALL COMMAND module purge && module load lang && fcc -I${CMAKE_CURRENT_SOURCE_DIR}/include -o ${CMAKE_BINARY_DIR}/${target} ${CMAKE_CURRENT_SOURCE_DIR}/src/${target}.c -lpthread
      )

    install(PROGRAMS ${CMAKE_BINARY_DIR}/${target} DESTINATION bin)

  endforeach()

  foreach(target IN ITEMS
      ihk_os_setperfevent10
      )

    # Add target
    add_executable(${target} src/${target}.c)

    # -D
    target_compile_definitions(${target}
      PRIVATE -DPAGE_SIZE=${PAGE_SIZE} -DFCC_LD_LIBRARY_PATH=${FCC_LD_LIBRARY_PATH}
      )

    # -I
    target_include_directories(${target}
      PRIVATE "${PROJECT_SOURCE_DIR}/include"
      PRIVATE "${WITH_MCK}/include"
      PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
      )

    # -l
    target_link_libraries(${target}
      PRIVATE params cpu mem os user linux numa
      )

    # String replacement
    configure_file(src/${target}.sh.in ${target}.sh @ONLY)

    # Install
    install(TARGETS ${target} DESTINATION bin)
    install(PROGRAMS ${CMAKE_BINARY_DIR}/${target}.sh DESTINATION bin)
  endforeach()
endif()

# kernel module
add_custom_target(test_driver ALL COMMAND cp ${WITH_MCK}/kmod/ihk/linux/core/Module.symvers ./ && make EXTRA_CFLAGS=-I/${WITH_MCK}/include && mv test_driver.ko ${CMAKE_BINARY_DIR} && make clean
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/driver
)
install(PROGRAMS ${CMAKE_BINARY_DIR}/test_driver.ko DESTINATION kmod)

configure_file(driver/test_driver_insmod.sh.in test_driver_insmod.sh)
install(PROGRAMS ${CMAKE_BINARY_DIR}/test_driver_insmod.sh DESTINATION bin)
install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/driver/test_driver_rmmod.sh DESTINATION bin)

