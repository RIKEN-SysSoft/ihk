file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/ikc)

kmod(ihk
	C_FLAGS
		-I${CMAKE_CURRENT_SOURCE_DIR}
		-I${PROJECT_SOURCE_DIR}/linux/include
		-I${PROJECT_SOURCE_DIR}/linux/include/ihk/arch/${ARCH}
		-I${PROJECT_SOURCE_DIR}/ikc/include
		-I${PROJECT_BINARY_DIR}
	SOURCES
		host_driver.c mem_alloc.c mm.c mikc.c
		../../ikc/linux.c ../../ikc/master.c ../../ikc/queue.c
	INSTALL_DEST
		kmod
)
