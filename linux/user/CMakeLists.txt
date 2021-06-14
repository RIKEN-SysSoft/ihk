include_directories(
	"${PROJECT_SOURCE_DIR}/linux/include"
	"${PROJECT_SOURCE_DIR}/linux/include/ihk/arch/${ARCH}"
	"${PROJECT_BINARY_DIR}/linux/include"
	"${PROJECT_BINARY_DIR}"
)

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

add_library(ihklib SHARED ihklib.c)
target_compile_definitions(ihklib PRIVATE -DPAGE_SIZE=${PAGE_SIZE})
SET_TARGET_PROPERTIES(ihklib PROPERTIES OUTPUT_NAME ihk)
target_link_libraries(ihklib ${LIBBFD})

add_executable(ihkconfig ihkconfig.c)
set_property(TARGET ihkconfig PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET ihkconfig PROPERTY LINK_FLAGS "-fPIE -pie")
target_link_libraries(ihkconfig ihklib ${LIBBFD})

add_executable(ihkosctl ihkosctl.c)
set_property(TARGET ihkosctl PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET ihkosctl PROPERTY LINK_FLAGS "-fPIE -pie")
target_link_libraries(ihkosctl ihklib ${LIBBFD} ${LIBIBERTY})

add_executable(ihkmond ihkmond.c)
set_property(TARGET ihkmond PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET ihkmond PROPERTY LINK_FLAGS "-fPIE -pie")
target_link_libraries(ihkmond ihklib ${LIBUDEV} pthread)

configure_file(ihkconfig.1in ihkconfig.1 @ONLY)
configure_file(ihkosctl.1in ihkosctl.1 @ONLY)

option(ENABLE_GCOV "Enable GCOV" OFF)
if (ENABLE_GCOV)
	add_compile_options("-fprofile-arcs" "-ftest-coverage")
	add_link_options("-lgcov" "--coverage")
endif(ENABLE_GCOV)

install(TARGETS "ihkconfig" "ihkosctl" "ihkmond" "ihklib"
	RUNTIME DESTINATION "${CMAKE_INSTALL_SBINDIR}"
	LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES
		"${CMAKE_CURRENT_BINARY_DIR}/ihkconfig.1"
		"${CMAKE_CURRENT_BINARY_DIR}/ihkosctl.1"
	DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")
install(FILES "../include/ihk/ihklib.h"
	DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "../include/ihk/affinity.h"
	DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ihk")


