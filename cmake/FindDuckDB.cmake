# - Try to find DuckDB
# Once done this will define
#  DuckDB_FOUND - System has DuckDB
#  DuckDB_INCLUDE_DIRS - The DuckDB include directories
#  DuckDB_LIBRARIES - The libraries needed to use DuckDB

find_path(DuckDB_INCLUDE_DIR duckdb.hpp
    HINTS
    ${DuckDB_ROOT_DIR}/include
    ${CMAKE_PREFIX_PATH}/duckdb/include
    ${CMAKE_INSTALL_PREFIX}/duckdb/include
    PATH_SUFFIXES duckdb
)

find_library(DuckDB_LIBRARY
    NAMES duckdb_static libduckdb_static duckdb libduckdb
    HINTS
    ${DuckDB_ROOT_DIR}/lib
    ${CMAKE_PREFIX_PATH}/duckdb/lib
    ${CMAKE_INSTALL_PREFIX}/duckdb/lib
    PATH_SUFFIXES lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(DuckDB DEFAULT_MSG
    DuckDB_LIBRARY DuckDB_INCLUDE_DIR)

if(DuckDB_FOUND)
    set(DuckDB_LIBRARIES ${DuckDB_LIBRARY})
    set(DuckDB_INCLUDE_DIRS ${DuckDB_INCLUDE_DIR})
endif()

mark_as_advanced(DuckDB_INCLUDE_DIR DuckDB_LIBRARY)