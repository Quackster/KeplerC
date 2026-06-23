find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_LIBUV QUIET libuv)
endif()

FIND_PATH(LIBUV_INCLUDE_DIR
        NAMES uv.h
        HINTS ${PC_LIBUV_INCLUDE_DIRS})

# Try to find the library
FIND_LIBRARY(LIBUV_LIBRARY
        NAMES uv libuv
        HINTS ${PC_LIBUV_LIBRARY_DIRS})

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Libuv
        REQUIRED_VARS
        LIBUV_LIBRARY
        LIBUV_INCLUDE_DIR)

# Hide internal variables
MARK_AS_ADVANCED(LIBUV_INCLUDE_DIR LIBUV_LIBRARY)

# Set standard variables
IF(Libuv_FOUND)
    SET(LIBUV_FOUND TRUE)
    SET(LIBUV_INCLUDE_DIRS "${LIBUV_INCLUDE_DIR}")
    SET(LIBUV_LIBRARIES "${LIBUV_LIBRARY}")
ENDIF()
