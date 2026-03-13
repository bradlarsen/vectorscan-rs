#
# sqlite is only used in hsbench, no need to special case its build, depend only on OS installations using pkg-config
#

# first check for sqlite on the system
pkg_check_modules(SQLITE3 sqlite3)

# now do version checks
if (SQLITE3_FOUND)
    list(INSERT CMAKE_REQUIRED_INCLUDES 0 "${SQLITE3_INCLUDE_DIRS}")
    if (SQLITE_VERSION LESS "3.8.10")
        message(FATAL_ERROR "sqlite3 is broken from 3.8.7 to 3.8.10 - please find a working version")
    endif()

    list(INSERT CMAKE_REQUIRED_LIBRARIES 0 ${SQLITE3_LDFLAGS})
    CHECK_SYMBOL_EXISTS(sqlite3_open_v2 sqlite3.h HAVE_SQLITE3_OPEN_V2)
    list(REMOVE_ITEM CMAKE_REQUIRED_INCLUDES "${SQLITE3_INCLUDE_DIRS}")
    list(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES ${SQLITE3_LDFLAGS})
endif()
