AUTOMAKE_OPTIONS = subdir-objects

noinst_PROGRAMS = gattaca

AM_CXXFLAGS = -I$(srcdir) -I$(srcdir)/../include -I$(srcdir)/../src -Wall

gattaca_SOURCES = \
	../src/http_parser.cpp \
	../src/http_client.cpp \
	../src/cpu_monitor.cpp \
	../src/boost_utils.cpp \
	../src/dnadb.cpp \
	../src/control.cpp \
	../src/sha.cpp \
	../src/utils.cpp \
	../src/brpc_client.cpp \
	../src/parsed_url.cpp \
	../src/connection.cpp \
	../src/connection_manager.cpp \
	../src/reply.cpp \
	../src/request.cpp \
	../src/request_handler.cpp \
	gattaca_handler.cpp \
	../src/request_parser.cpp \
	../src/server.cpp \
	../src/libtorrent/entry.cpp \
	../src/libtorrent/escape_string.cpp \
	../src/boost-system/error_code.cpp \
	main.cpp
gattaca_LDADD = -l@BOOST_DATE_TIME_LIB@ -l@BOOST_THREAD_LIB@ -l@BOOST_PROGRAM_OPTIONS_LIB@ @PTHREAD_LIBS@

EXTRA_DIST = \
	gattaca_handler.hpp

MAINTAINERCLEANFILES = \
	$(srcdir)/Makefile.in
