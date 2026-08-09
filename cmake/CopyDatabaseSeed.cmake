# =============================================================================
# CopyDatabaseSeed.cmake
#
# Purpose:
#   Seed the runtime SQLite database next to the executable from the source-tree
#   default, without overwriting an existing runtime database.
#
# Why "if missing":
#   The runtime database accumulates the nodes the user adds to the Data Access
#   View. Copying unconditionally on every build would discard that data, so the
#   seed is only copied when no runtime database exists yet.
#
# Invocation (cmake script mode):
#   cmake -D SEED_FILE=<path> -D DEST_DIR=<dir> -P CopyDatabaseSeed.cmake
# =============================================================================

if(NOT DEFINED SEED_FILE OR NOT DEFINED DEST_DIR)
    message(FATAL_ERROR "CopyDatabaseSeed.cmake requires SEED_FILE and DEST_DIR.")
endif()

get_filename_component(_seed_name "${SEED_FILE}" NAME)
set(_dest_file "${DEST_DIR}/${_seed_name}")

if(EXISTS "${_dest_file}")
    message(STATUS "Runtime database already present, keeping: ${_dest_file}")
    return()
endif()

if(NOT EXISTS "${SEED_FILE}")
    message(STATUS "No database seed found at ${SEED_FILE}; runtime DB will be created on first run.")
    return()
endif()

file(MAKE_DIRECTORY "${DEST_DIR}")
file(COPY "${SEED_FILE}" DESTINATION "${DEST_DIR}")
message(STATUS "Seeded runtime database: ${_dest_file}")
