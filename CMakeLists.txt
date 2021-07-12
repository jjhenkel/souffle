# Souffle - A Datalog Compiler
# Copyright (c) 2021 The Souffle Developers. All rights reserved
# Licensed under the Universal Permissive License v 1.0 as shown at:
# - https://opensource.org/licenses/UPL
# - <souffle root>/licenses/SOUFFLE-UPL.txt

# TODO
# Add option to run tests from install directory
# Update README with cmake instructions
# bash completion
# clang-format, clang-tidy etc
# Packaging
# substitute changelog

# Future TODO/improvements:
# - Replace all the shell scripts (e.g. souffle-compile and all the testing helpers)
#   with Python so it's cross-platform.  @b-scholz has agreed it's ok to have Python
#   as a dependency
# - See if we can call back into cmake so that we have a better mechanism for building
#   consistenly rather than trying to thread compile flags etc into e.g. souffle-compile
# - Make it easier to include Souffle as a subproject using add_subdirectory
# - Currently, there a few unit tests in the src directory, we should
#   migrate them to test to that they can be excluded if needed

cmake_minimum_required(VERSION 3.15)

find_package(Git REQUIRED)

# PACKAGE_VERSION is the full tag with git hash
execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                RESULT_VARIABLE GIT_RESULT
                OUTPUT_VARIABLE GIT_PACKAGE_VERSION)
                # FIXME: Use in cmake 3.19 or later
                # COMMAND_ERROR_IS_FATAL ANY)

# Figure out the version number, depends on whether building from the git repo
if (NOT GIT_RESULT EQUAL 0)
    # Not building from a git clone
    message(WARNING "Unable to find git repository: version number will be incomplete")
    set(PACKAGE_VERSION "UNKOWN")
    set(SOUFFLE_VERSION "")
else()
    string(REGEX REPLACE "\n$" "" PACKAGE_VERSION "${GIT_PACKAGE_VERSION}")
    message(STATUS "Building souffle version ${PACKAGE_VERSION}")

    # SOUFFLE_VERSION only includes the major/minor triplet
    string(REGEX REPLACE "-.*$" "" SOUFFLE_VERSION "${PACKAGE_VERSION}")

    # If building from a shallow clone where tag is not available.
    if (NOT ${SOUFFLE_VERSION} MATCHES "^[0-9.]+$")
        message(WARNING "Cannot find a valid tag: cmake project version will be incomplete")
        set (SOUFFLE_VERSION "")
    endif()
endif()

project(souffle VERSION "${SOUFFLE_VERSION}"
                DESCRIPTION "A datalog compiler"
                LANGUAGES CXX)
include(CTest)

# Require out-of-source builds
file(TO_CMAKE_PATH "${PROJECT_BINARY_DIR}/CMakeLists.txt" LOC_PATH)
if(EXISTS "${LOC_PATH}")
    message(FATAL_ERROR "You cannot build in a source directory (or any directory with a CMakeLists.txt file). Please make a build subdirectory. Feel free to remove CMakeCache.txt and CMakeFiles.")
endif()


if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING
        "Choose the type of build" FORCE)
endif()
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")

# Tells us whether we're building souffle as a main project
# or a subprobject
if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(SOUFFLE_MASTER_PROJECT On)
else()
    set(SOUFFLE_MASTER_PROJECT Off)
endif()

if (SOUFFLE_MASTER_PROJECT AND BUILD_TESTING)
    set(SOUFFLE_ENABLE_TESTING_DEFAULT ON)
else()
    set(SOUFFLE_ENABLE_TESTING_DEFAULT OFF)
endif()

# --------------------------------------------------
# User options available from the command line/cache
# --------------------------------------------------
option(SOUFFLE_DOMAIN_64BIT "Enable/Disable 64-bit number values in Datalog tuples" OFF)
option(SOUFFLE_USE_CURSES "Enable/Disable ncurses-based provenance display" ON)
option(SOUFFLE_SWIG "Enable/Disable all SWIG builds" OFF)
option(SOUFFLE_SWIG_PYTHON "Enable/Disable Python SWIG" OFF)
option(SOUFFLE_SWIG_JAVA "Enable/Disable Java SWIG" OFF)
option(SOUFFLE_USE_ZLIB "Enable/Disable use of libz file compression" ON)
option(SOUFFLE_USE_SQLITE "Enable/Disable use sqlite IO" ON)
option(SOUFFLE_SANITISE_MEMORY "Enable/Disable memory sanitiser" OFF)
option(SOUFFLE_SANITISE_THREAD "Enable/Disable thread sanitiser" OFF)
# This is how souffle is built by default in Debug
option(SOUFFLE_FAST_DEBUG "Enable/Disable ON 'fast debug' mode" ON)
# SOUFFLE_NDEBUG = ON means -DNDEBUG on the compiler command line = no cassert
# Therefor SOUFFLE_NDEBUG = OFF means keep asserts
option(SOUFFLE_NDEBUG "Enable/Disable runtime checks even in release mode" OFF)
# Souffle, by default, uses -O3 for release builds
option(SOUFFLE_FAST_RELEASE "Enable/Disable 'fast release' mode" ON)
# By default, the "test/example" directory is not part of testing
# this flag enables the tests to run
option(SOUFFLE_TEST_EXAMPLES "Enable/Disable testing of additional code examples in tests/examples" OFF)
option(SOUFFLE_ENABLE_TESTING "Enable/Disable testing" ${SOUFFLE_ENABLE_TESTING_DEFAULT})
option(SOUFFLE_GENERATE_DOXYGEN "Generate Doxygen files (html;htmlhelp;man;rtf;xml;latex)" "")

# Add aditional modules to CMake
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

# --------------------------------------------------
# curses libraries for Provenance information
# --------------------------------------------------
if (SOUFFLE_USE_CURSES)
    find_package(Curses REQUIRED)
    if(NOT TARGET Curses::NCurses)
        add_library(Curses::NCurses UNKNOWN IMPORTED)
        set_target_properties(Curses::NCurses PROPERTIES
            IMPORTED_LOCATION "${CURSES_NCURSES_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${CURSES_INCLUDE_DIR}"
        )
    endif()
endif()

# --------------------------------------------------
# swig support
# --------------------------------------------------
if (SOUFFLE_SWIG)
    # Enable both
    set(SOUFFLE_SWIG_PYTHON "ON" CACHE STRING "" FORCE)
    set(SOUFFLE_SWIG_JAVA "ON" CACHE STRING "" FORCE)
endif()

if (SOUFFLE_SWIG_PYTHON OR SOUFFLE_SWIG_JAVA)
    find_package(SWIG REQUIRED)

    if (SOUFFLE_SWIG_PYTHON)
        find_package(Python "3.7" REQUIRED)
    endif()

    if (SOUFFLE_SWIG_JAVA)
        find_package(Java REQUIRED
                     COMPONENTS Development)
        find_package(JNI REQUIRED)
        list(APPEND SOUFFLE_JAVA_INCLUDE_PATH ${JAVA_INCLUDE_PATH})
        list(APPEND SOUFFLE_JAVA_INCLUDE_PATH ${JAVA_INCLUDE_PATH2})
    endif()
endif()

# --------------------------------------------------
# flex and bison
# --------------------------------------------------
find_package(FLEX REQUIRED)
find_package(BISON "3.0.4" REQUIRED)

# --------------------------------------------------
# mcpp
# --------------------------------------------------
find_program(MCPP mcpp)

# --------------------------------------------------
# libz
# --------------------------------------------------
if (SOUFFLE_USE_ZLIB)
    find_package(ZLIB REQUIRED)
    if (SOUFFLE_ENABLE_TESTING)
        find_program(GZIP_BINARY gzip REQUIRED)
    endif()
endif()

# --------------------------------------------------
# sqlite
# --------------------------------------------------
if (SOUFFLE_USE_SQLITE)
    find_package(SQLite3 REQUIRED)
    if (SOUFFLE_ENABLE_TESTING)
        find_program(SQLITE3_BINARY sqlite3 REQUIRED)
    endif()
endif()

# --------------------------------------------------
# libffi
# --------------------------------------------------
find_package(LibFFI REQUIRED)

# --------------------------------------------------
# pthreads
# --------------------------------------------------
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# --------------------------------------------------
# OpenMP
# --------------------------------------------------
find_package(OpenMP)
if (OPENMP_FOUND)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
endif()

# --------------------------------------------------
# Memory Sanitiser
# --------------------------------------------------
if (SOUFFLE_SANITISE_MEMORY)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address,leak")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,leak")
endif()

# --------------------------------------------------
# Thread Sanitiser
# --------------------------------------------------
if (SOUFFLE_SANITISE_THREAD)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
endif()

# --------------------------------------------------
# Doxygen
# --------------------------------------------------
if (SOUFFLE_GENERATE_DOXYGEN)
    find_package(Doxygen REQUIRED dot)
    set(DOXYGEN_IN "${PROJECT_SOURCE_DIR}/doxygen.in")
    set(DOXYGEN_CFG "${PROJECT_SOURCE_DIR}/Doxyfile")
    set(DOXYGEN_DIR "${PROJECT_SOURCE_DIR}/doc")

    if ("htmlhelp" IN_LIST SOUFFLE_GENERATE_DOXYGEN)
        set(DOXYGEN_GENERATE_HTMLHELP "YES")
    endif()
    if ("man" IN_LIST SOUFFLE_GENERATE_DOXYGEN)
        set(DOXYGEN_GENERATE_MAN "YES")
    endif()
    if ("rtf" IN_LIST SOUFFLE_GENERATE_DOXYGEN)
        set(DOXYGEN_GENERATE_RTF "YES")
    endif()
    if ("xml" IN_LIST SOUFFLE_GENERATE_DOXYGEN)
        set(DOXYGEN_GENERATE_XML "YES")
    endif()
    if ("latex" IN_LIST SOUFFLE_GENERATE_DOXYGEN)
        set(DOXYGEN_GENERATE_LATEX "YES")
    endif()

    configure_file(${DOXYGEN_IN} ${DOXYGEN_CFG})

    add_custom_target(doxygen
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_CFG}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen")
endif()

# --------------------------------------------------
# Generate the config file
# --------------------------------------------------
configure_file("${PROJECT_SOURCE_DIR}/cmake/config.h.in"
               "${PROJECT_BINARY_DIR}/src/config.h")

# --------------------------------------------------
# Change compile flags
# --------------------------------------------------
# FIXME:  We may need to rethink this when building
# souffle as a subproject since this dirrently manipulates
# CMAKE_CXX_*_FLAGS and that's considered bad in "modern cmake"
if (CMAKE_BUILD_TYPE MATCHES "Debug")
    if (SOUFFLE_FAST_DEBUG)
        string(REGEX REPLACE "-g" "-O0 -g3" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
    endif()
else()
    foreach(FLAG_VAR
            CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL
            CMAKE_CXX_FLAGS_RELWITHDEBINFO)

            # Remove/keep NDEBUG in Release builds
            if (NOT SOUFFLE_NDEBUG)
                string(REGEX REPLACE "-DNDEBUG" "" ${FLAG_VAR} "${${FLAG_VAR}}")
            endif()

            # Change -O2 to -O3
            if (SOUFFLE_FAST_RELEASE)
                string(REGEX REPLACE "-O2" "-O3" ${FLAG_VAR} "${${FLAG_VAR}}")
            endif()

    endforeach()
endif()

# There are a few tests in src, so add it *after*
# including CTest
add_subdirectory(src)

if (SOUFFLE_ENABLE_TESTING)
    add_subdirectory(tests)
endif()
