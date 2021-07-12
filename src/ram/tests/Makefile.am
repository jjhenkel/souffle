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

check_PROGRAMS += ram_condition_equal_clone_test
ram_condition_equal_clone_test_SOURCES = ram_condition_equal_clone_test.cpp
ram_condition_equal_clone_test_LDADD = $(top_builddir)/src/libsouffle.la

check_PROGRAMS += ram_statement_equal_clone_test
ram_statement_equal_clone_test_SOURCES = ram_statement_equal_clone_test.cpp
ram_statement_equal_clone_test_LDADD = $(top_builddir)/src/libsouffle.la

check_PROGRAMS += ram_expression_equal_clone_test
ram_expression_equal_clone_test_SOURCES = ram_expression_equal_clone_test.cpp
ram_expression_equal_clone_test_LDADD = $(top_builddir)/src/libsouffle.la

check_PROGRAMS += ram_operation_equal_clone_test
ram_operation_equal_clone_test_SOURCES = ram_operation_equal_clone_test.cpp
ram_operation_equal_clone_test_LDADD = $(top_builddir)/src/libsouffle.la

check_PROGRAMS += ram_relation_equal_clone_test
ram_relation_equal_clone_test_SOURCES = ram_relation_equal_clone_test.cpp
ram_relation_equal_clone_test_LDADD = $(top_builddir)/src/libsouffle.la

check_PROGRAMS += ram_type_conversion_test
ram_type_conversion_test_SOURCES = ram_type_conversion_test.cpp
ram_type_conversion_test_LDADD = $(top_builddir)/src/libsouffle.la

# matching test
check_PROGRAMS += matching_test
matching_test_SOURCES = matching_test.cpp
matching_test_LDADD = $(top_builddir)/src/libsouffle.la

# matching test
check_PROGRAMS += max_matching_test
max_matching_test_SOURCES = max_matching_test.cpp
max_matching_test_LDADD = $(top_builddir)/src/libsouffle.la

# make all check-programs tests
TESTS = $(check_PROGRAMS)
