file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/arch/${ARCH})

kmod(ihk-smp-${ARCH}
	C_FLAGS
		-I${PROJECT_BINARY_DIR}
		-I${PROJECT_SOURCE_DIR}/cokernel/smp/${ARCH}
		-I${PROJECT_SOURCE_DIR}/linux/include
		-I${PROJECT_SOURCE_DIR}/linux/include/ihk/arch/${ARCH}
		-I${PROJECT_SOURCE_DIR}/ikc/include
		-I${CMAKE_CURRENT_SOURCE_DIR}
		-I${CMAKE_CURRENT_SOURCE_DIR}/arch/${ARCH}
		-I${CMAKE_CURRENT_BINARY_DIR}/arch/${ARCH}
		-I${PROJECT_SOURCE_DIR}/linux/core
	SOURCES
		arch/${ARCH}/smp-${ARCH}-startup.c
		arch/${ARCH}/smp-${ARCH}-trampoline.c
		arch/${ARCH}/smp-arch-driver.c
		smp-driver.c
	EXTRA_SYMBOLS
		${PROJECT_BINARY_DIR}/linux/core/Module.symvers
	DEPENDS
		ihk_ko
	INSTALL_DEST
		${KMODDIR}
)

option(ENABLE_GCOV "Enable GCOV" OFF)
if (ENABLE_GCOV)
	add_compile_options("-fprofile-arcs" "-ftest-coverage")
	add_link_options("-lgcov" "--coverage")
endif(ENABLE_GCOV)

