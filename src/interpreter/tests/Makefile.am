# Souffle - A Datalog Compiler
# Copyright (c) 2020, The Souffle Developers. All rights reserved.
# Licensed under the Universal Permissive License v 1.0 as shown at:
# - https://opensource.org/licenses/UPL
# - <souffle root>/licenses/SOUFFLE-UPL.txt


SUFFIXES = .cpp .h

CXXFLAGS += -I${top_srcdir}/src -I$(top_srcdir)/src/include

########## Unit Tests

AM_COLOR_TESTS=always

# -------------------------

# init check programs
check_PROGRAMS =

# test description
# check_PROGRAMS += testname
# test_testname_SOURCES = cppfile test.h
# if needed
# test_testname_LDADD = $(top_builddir)/src/libsouffle.la
# test_testname_CXXFLAGS = test-specific flags

# interpreter relation test
check_PROGRAMS += interpreter_relation_test
interpreter_relation_test_SOURCES = interpreter_relation_test.cpp
interpreter_relation_test_LDADD = $(top_builddir)/src/libsouffle.la

# arithmetic test
check_PROGRAMS += ram_arithmetic_test
ram_arithmetic_test_SOURCES = ram_arithmetic_test.cpp
ram_arithmetic_test_LDADD = $(top_builddir)/src/libsouffle.la

# relation test
check_PROGRAMS += ram_relation_test
ram_relation_test_SOURCES = ram_relation_test.cpp
ram_relation_test_LDADD = $(top_builddir)/src/libsouffle.la

# make all check-programs tests
TESTS = $(check_PROGRAMS)
