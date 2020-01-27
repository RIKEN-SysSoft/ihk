include_directories(
	"${PROJECT_SOURCE_DIR}/linux/include"
	"${PROJECT_SOURCE_DIR}/linux/include/ihk/arch/${ARCH}"
	"${PROJECT_BINARY_DIR}/linux/include"
	"${PROJECT_BINARY_DIR}"
)

execute_process(COMMAND getconf PAGE_SIZE OUTPUT_VARIABLE PAGE_SIZE OUTPUT_STRIP_TRAILING_WHITESPACE)
add_library(ihklib SHARED ihklib.c)
target_compile_definitions(ihklib PRIVATE -DPAGE_SIZE=${PAGE_SIZE})
SET_TARGET_PROPERTIES(ihklib PROPERTIES OUTPUT_NAME ihk)
target_link_libraries(ihklib ${LIBBFD})

add_executable(ihkconfig ihkconfig.c)
target_link_libraries(ihkconfig ihklib ${LIBBFD})

add_executable(ihkosctl ihkosctl.c)
target_link_libraries(ihkosctl ihklib ${LIBBFD} ${LIBIBERTY})

add_executable(ihkmond ihkmond.c)
target_link_libraries(ihkmond ihklib ${LIBUDEV} pthread)

configure_file(ihkconfig.1in ihkconfig.1 @ONLY)
configure_file(ihkosctl.1in ihkosctl.1 @ONLY)

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


