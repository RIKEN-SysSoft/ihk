cmake_minimum_required(VERSION 3.0)

# Options: -DWITH_MCK=<McKernel install directory>
add_definitions(-DWITH_MCK=${WITH_MCK})

execute_process(COMMAND readlink -f ../../../../../
                OUTPUT_VARIABLE mckernel_root)

string(REGEX REPLACE "\n$" "" mckernel_root "${mckernel_root}")

set (patch_cmd patch -l)
set (patch_arg ${mckernel_root}/ihk/linux/user/CMakeLists.txt -i ${mckernel_root}/ihk/test/ihklib/whitebox/patch/mck_ihk_linux_user.patch)
execute_process(COMMAND ${patch_cmd} ${patch_arg})

set (patch_arg ${mckernel_root}/ihk/linux/driver/smp/CMakeLists.txt -i ${mckernel_root}/ihk/test/ihklib/whitebox/patch/mck_ihk_linux_driver_smp.patch)
execute_process(COMMAND ${patch_cmd} ${patch_arg})

set (patch_arg ${mckernel_root}/ihk/linux/core/CMakeLists.txt -i ${mckernel_root}/ihk/test/ihklib/whitebox/patch/mck_ihk_linux_core.patch)
execute_process(COMMAND ${patch_cmd} ${patch_arg})

set (patch_arg ${mckernel_root}/ihk/CMakeLists.txt -i ${mckernel_root}/ihk/test/ihklib/whitebox/patch/mck_ihk.patch)
execute_process(COMMAND ${patch_cmd} ${patch_arg})

set (patch_arg ${mckernel_root}/CMakeLists.txt -i ${mckernel_root}/ihk/test/ihklib/whitebox/patch/mck.patch)
execute_process(COMMAND ${patch_cmd} ${patch_arg})

set (patch_arg ${mckernel_root}/executer/kernel/mcctrl/CMakeLists.txt -i ${mckernel_root}/ihk/test/ihklib/whitebox/patch/mck_exec_kernel_mcctrl.patch)
execute_process(COMMAND ${patch_cmd} ${patch_arg})

set (cmake_cmd cmake)
set (cmake_arg -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=${WITH_MCK} -DUNAME_R=4.14.0-115.13.1.1chaos.el7a.aarch64 ${mckernel_root})
execute_process(COMMAND ${cmake_cmd} ${cmake_arg})
execute_process(COMMAND make -j install)


set (patch_cmd patch -R)
set (patch_arg ${mckernel_root}/ihk/linux/user/CMakeLists.txt -i ${mckernel_root}/ihk/test/ihklib/whitebox/patch/mck_ihk_linux_user.patch)
execute_process(COMMAND ${patch_cmd} ${patch_arg})

set (patch_arg ${mckernel_root}/ihk/linux/driver/smp/CMakeLists.txt -i ${mckernel_root}/ihk/test/ihklib/whitebox/patch/mck_ihk_linux_driver_smp.patch)
execute_process(COMMAND ${patch_cmd} ${patch_arg})

set (patch_arg ${mckernel_root}/ihk/linux/core/CMakeLists.txt -i ${mckernel_root}/ihk/test/ihklib/whitebox/patch/mck_ihk_linux_core.patch)
execute_process(COMMAND ${patch_cmd} ${patch_arg})

set (patch_arg ${mckernel_root}/ihk/CMakeLists.txt -i ${mckernel_root}/ihk/test/ihklib/whitebox/patch/mck_ihk.patch)
execute_process(COMMAND ${patch_cmd} ${patch_arg})

set (patch_arg ${mckernel_root}/CMakeLists.txt -i ${mckernel_root}/ihk/test/ihklib/whitebox/patch/mck.patch)
execute_process(COMMAND ${patch_cmd} ${patch_arg})

set (patch_arg ${mckernel_root}/executer/kernel/mcctrl/CMakeLists.txt -i ${mckernel_root}/ihk/test/ihklib/whitebox/patch/mck_exec_kernel_mcctrl.patch)
execute_process(COMMAND ${patch_cmd} ${patch_arg})



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
  add_definitions(-D__a64fx__)
endif()

cmake_policy(SET CMP0005 NEW)

# Options: -DWITH_MCK=<McKernel install directory>
add_definitions(-DWITH_MCK=${WITH_MCK})

# Options: -DWITH_MCK_SRC=<McKernel source directory>
add_definitions(-DWITH_MCK_SRC=${WITH_MCK_SRC})

# for autotest
if(NOT DEFINED CMAKE_INSTALL_PREFIX_SCRIPTS)
       set(CMAKE_INSTALL_PREFIX_SCRIPTS ${CMAKE_INSTALL_PREFIX}/scripts)
endif()

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
  add_library(${target} STATIC ../blackbox/src/${target}.c)
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/../blackbox/include"
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
  add_library(${target} STATIC ../blackbox/src/${target}.c)
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/../blackbox/include"
    PRIVATE "${WITH_MCK}/include"
    )
endforeach()

foreach(target IN ITEMS
    test__ihk_device_get_num_cpus
    test_ihk_get_num_reserved_cpus
    test_smp_ihk_get_num_cpus
    )

  # Add target
  add_executable(${target} src/user/ihk_get_num_reserved_cpus/${target}.c)

  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )

  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )

  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )

  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_device_query_cpu
    test_ihk_query_cpu
    test_smp_ihk_query_cpu
    )

  # Add target
  add_executable(${target} src/user/ihk_query_cpu/${target}.c)

  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )

  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )

  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )

  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_device_release_cpu
    test_ihk_release_cpu
    test_smp_ihk_release_cpu
    )

  # Add target
  add_executable(${target} src/user/ihk_release_cpu/${target}.c)

  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )

  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )

  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )

  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS

    test_cpu_array2str
    test__ihk_device_reserve_cpu
    test_ihklib_device_open
    test_ihklib_device_readable
    test_ihk_reserve_cpu
    test_smp_ihk_reserve_cpu
    test__smp_ihk_write_cpu_sys_file
    test_truncate_snprintf

    )

  # Add target
  add_executable(${target} src/user/ihk_reserve_cpu/${target}.c)

  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )

  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )

  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )

  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_device_release_mem_partially
    test__ihk_device_reserve_mem
    test__ihk_smp_reserve_mem
    test_ihk_reserve_mem
    test__ihk_smp_release_mem_partially
    test__smp_ihk_free_mem_from_rbtree
    test_smp_ihk_release_mem_partially
    test_mem_release
    )

  # Add target
  add_executable(${target} src/user/ihk_reserve_mem/${target}.c)

  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )

  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )

  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )

  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test_ihk_reserve_mem_conf
    )
  # Add target
  add_executable(${target} src/user/ihk_reserve_mem_conf/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test_ihk_get_num_reserved_mem_chunks
    )
  # Add target
  add_executable(${target} src/user/ihk_get_num_reserved_mem_chunks/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test_ihk_query_mem
    test__ihk_device_query_mem
    test_smp_ihk_query_mem
    )
  # Add target
  add_executable(${target} src/user/ihk_query_mem/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_device_release_mem
    test__ihk_smp_release_chunk
    test__ihk_smp_release_mem
    test_ihk_release_mem
    test_smp_ihk_release_mem
    )
  # Add target
  add_executable(${target} src/user/ihk_release_mem/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_device_create_os_init
    test__ihk_device_create_os
    test_delete_kmsg_buf
    test_ihk_create_os
    test_smp_ihk_create_os
    create_os
    destroy_os
    shutdown_os
    )
  # Add target
  add_executable(${target} src/user/ihk_create_os/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test_ihk_get_num_os_instances
    )
  # Add target
  add_executable(${target} src/user/ihk_get_num_os_instances/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test_ihk_get_os_instances
    )
  # Add target
  add_executable(${target} src/user/ihk_get_os_instances/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_device_destroy_os
    test__ihk_os_shutdown
    test__ihk_os_query_status
    test_ihk_destroy_os
    test_ihk_host_device_ioctl
    test_smp_ihk_os_wait_for_status
    test_mcctrl_os_shutdown_notifier
    test_add_free_mem_chunk
    test_delete_procfs_entries
    test_destroy_ikc_channels
    test_find_procfs_entry
    test_free_cpu_topology
    test_free_cpu_topology_one
    test_free_node_topology
    test_free_topology_info
    test_ihk_device_unmap_virtual
    test_ihk_ikc_destroy_channels
    test_ihk_ikc_free_channels
    test_ihk_os_get_special_addr
    test_ihk_os_query_status
    test_ihk_os_send_nmi
    test_ihk_os_shutdown
    test_ihk_smp_cpu_kill
    test_ihk_smp_get_cpu_affinity
    test_ihk_smp_reset_cpu
    test_ikc_master_finalize
    test_mcctrl_wakeup_desc_cleanup
    test_pager_cleanup
    test_procfs_exit
    test_release_kmsg_buf
    test_remove
    test_smp_ihk_os_query_status
    test_smp_ihk_map_virtual
    test_ihk_smp_map_virtual
    test_smp_ihk_os_send_nmi
    test_smp_ihk_os_shutdown
    test_smp_ihk_os_unmap_lwk
    test_sysfsm_cleanup
    )
  # Add target
  add_executable(${target} src/user/ihk_destroy_os/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test_ihk_os_assign_cpu
    test__ihk_os_assign_cpu
    test_smp_ihk_os_assign_cpu
    )
  # Add target
  add_executable(${target} src/user/ihk_os_assign_cpu/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_os_assign_mem
    test__smp_ihk_os_assign_mem
    test_ihk_os_assign_mem
    test_merge_mem_chunks
    test_smp_ihk_os_assign_mem
    )
  # Add target
  add_executable(${target} src/user/ihk_os_assign_mem/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test_ihk_os_get_ikc_map
    test__ihk_os_get_ikc_map
    test_smp_ihk_os_get_ikc_map
    )
  # Add target
  add_executable(${target} src/user/ihk_os_get_ikc_map/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_os_get_num_assigned_cpus
    )
  # Add target
  add_executable(${target} src/user/ihk_os_get_num_assigned_cpus/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test_ihk_os_get_num_assigned_mem_chunks
    )
  # Add target
  add_executable(${target} src/user/ihk_os_get_num_assigned_mem_chunks/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_os_query_mem
    test_ihk_os_query_mem
    test_smp_ihk_os_query_mem
    )
  # Add target
  add_executable(${target} src/user/ihk_os_query_mem/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_os_query_cpu
    test_smp_ihk_os_query_cpu
    )
  # Add target
  add_executable(${target} src/user/ihk_os_query_cpu/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_os_release_cpu
    test_smp_ihk_os_release_cpu
    )
  # Add target
  add_executable(${target} src/user/ihk_os_release_cpu/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_os_release_mem
    test__smp_ihk_os_release_mem
    test_ihk_os_release_mem
    test_smp_ihk_os_release_mem
    )
  # Add target
  add_executable(${target} src/user/ihk_os_release_mem/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_os_set_ikc_map
    test_smp_ihk_os_set_ikc_map
    )
  # Add target
  add_executable(${target} src/user/ihk_os_set_ikc_map/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_os_register_event
    test_ihk_os_get_eventfd
    )
  # Add target
  add_executable(${target} src/user/ihk_os_get_eventfd/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_os_load_file
    test__ihk_os_load_memory
    test_smp_ihk_arch_vmap_area_taken
    test_smp_ihk_os_load_file
    test_smp_ihk_os_load_mem
    )
  # Add target
  add_executable(${target} src/user/ihk_os_load/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_os_set_kargs
    )
  # Add target
  add_executable(${target} src/user/ihk_os_kargs/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_os_issue_interrupt
    test__ihk_device_map_memory
    test__ihk_device_map_virtual
    test__ihk_os_boot
    test_ihk_os_boot
    test__ihk_os_get_cpu_info
    test__ihk_os_get_memory_info
    test__ihk_os_get_special_addr
    test__ihk_os_register_handler
    test_ihk_device_map_memory
    test_ihk_host_ikc_init_first
    test_ihk_ikc_master_init
    test_linux_numa_2_lwk_numa
    test_smp_wakeup_secondary_cpu
    test_add_procfs_entry
    test_get_base_entry
    test_ihk_host_find_os
    test_ihk_ikc_init_desc
    test_ihk_ikc_listen_port
    test_ihk_ikc_send
    test_ihk_ikc_write_queue
    test_ihk_os_register_user_call_handlers
    test_mcctrl_os_boot_notifier
    test_prepare_ikc_channels
    test_smp_ihk_os_boot
    test_smp_ihk_os_issue_interrupt
    test_ihk_host_print_os_kmsg
    test_smp_ihk_setup_trampoline
    )
  # Add target
  add_executable(${target} src/user/ihk_os_boot/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_os_read_kmsg
    test_read_kmsg
    )
  # Add target
  add_executable(${target} src/user/ihk_os_kmsg/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test__ihk_os_clear_kmsg
    )
  # Add target
  add_executable(${target} src/user/ihk_os_clear_kmsg/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

foreach(target IN ITEMS
    test_ihk_os_get_status
    )
  # Add target
  add_executable(${target} src/user/ihk_os_get_status/${target}.c)
  # -D
  target_compile_definitions(${target}
    PRIVATE -DPAGE_SIZE=${PAGE_SIZE}
    )
  # -I
  target_include_directories(${target}
    PRIVATE "${PROJECT_SOURCE_DIR}/include"
    PRIVATE "${PROJECT_SOURCE_DIR}/include/user"
    PRIVATE "${PROJECT_SOURCE_DIR}/.."
    PRIVATE "${WITH_MCK}/include"
    PRIVATE "${WITH_MCK_SRC}/ihk/linux/include"
    )
  # -l
  target_link_libraries(${target}
    PRIVATE params cpu mem os user linux numa
    )
  # Install
  install(TARGETS ${target} DESTINATION bin)
#  install(PROGRAMS ${CMAKE_BINARY_DIR}/ihklib-${target} DESTINATION ${CMAKE_INSTALL_PREFIX_SCRIPTS})
endforeach()

# programs needing fcc and mckernel.h
if(CPU_MODEL STREQUAL "a64fx")
  foreach(target IN ITEMS
      vfp_sve_wfi_mem
      )

    execute_process(COMMAND bash -c "module show lang 2>&1 | awk '/LD_LIBRARY_PATH/ { if (str != \"\") { str=str \":\" } str=str $3 } END { print str }'"
      OUTPUT_VARIABLE FCC_LD_LIBRARY_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)

    message("FCC_LD_LIBRARY_PATH: ${FCC_LD_LIBRARY_PATH}")

    add_custom_target(${target} ALL COMMAND module purge && module load lang && fcc -I${CMAKE_CURRENT_SOURCE_DIR}/include -o ${CMAKE_BINARY_DIR}/${target} ${CMAKE_CURRENT_SOURCE_DIR}/src_app/${target}.c -lpthread
      )

    install(PROGRAMS ${CMAKE_BINARY_DIR}/${target} DESTINATION bin)

  endforeach()

endif()
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
  add_definitions(-D__a64fx__)
endif()

cmake_policy(SET CMP0005 NEW)

# Options: -DWITH_MCK=<McKernel install directory>
add_definitions(-DWITH_MCK=${WITH_MCK})

# Options: -DWITH_MCK_SRC=<McKernel source directory>
add_definitions(-DWITH_MCK_SRC=${WITH_MCK_SRC})

# for autotest
if(NOT DEFINED CMAKE_INSTALL_PREFIX_SCRIPTS)
       set(CMAKE_INSTALL_PREFIX_SCRIPTS ${CMAKE_INSTALL_PREFIX}/scripts)
endif()

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

# programs needing fcc and mckernel.h
if(CPU_MODEL STREQUAL "a64fx")
  foreach(target IN ITEMS
      vfp_sve_wfi_mem
      )

    execute_process(COMMAND bash -c "module show lang 2>&1 | awk '/LD_LIBRARY_PATH/ { if (str != \"\") { str=str \":\" } str=str $3 } END { print str }'"
      OUTPUT_VARIABLE FCC_LD_LIBRARY_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)

    message("FCC_LD_LIBRARY_PATH: ${FCC_LD_LIBRARY_PATH}")

    add_custom_target(${target} ALL COMMAND module purge && module load lang && fcc -I${CMAKE_CURRENT_SOURCE_DIR}/include -o ${CMAKE_BINARY_DIR}/${target} ${CMAKE_CURRENT_SOURCE_DIR}/src_app/${target}.c -lpthread
      )

    install(PROGRAMS ${CMAKE_BINARY_DIR}/${target} DESTINATION bin)

  endforeach()

endif()
