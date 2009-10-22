AUTOMAKE_OPTIONS = subdir-objects

noinst_PROGRAMS = helix

AM_CXXFLAGS = -I$(srcdir) -I$(srcdir)/../include -I$(srcdir)/../src -Wall

helix_SOURCES = \
	../src/http_parser.cpp \
	../src/http_client.cpp \
	../src/cpu_monitor.cpp \
	../src/boost_utils.cpp \
	../src/dnadb.cpp \
	../src/sha.cpp \
	../src/utils.cpp \
	../src/authorizer.cpp \
	../src/natcheck.cpp \
	../src/stats.cpp \
	../src/swarm.cpp \
	../src/brpc_client.cpp \
	../src/parsed_url.cpp \
	../src/connection.cpp \
	../src/connection_manager.cpp \
	../src/reply.cpp \
	../src/request.cpp \
	helix_handler.cpp \
	../src/request_handler.cpp \
	../src/request_parser.cpp \
	../src/server.cpp \
	../src/control.cpp \
	../src/libtorrent/entry.cpp \
	../src/libtorrent/escape_string.cpp \
	../src/boost-system/error_code.cpp \
	main.cpp
helix_LDADD = -l@BOOST_DATE_TIME_LIB@ -l@BOOST_THREAD_LIB@ -l@BOOST_PROGRAM_OPTIONS_LIB@ @PTHREAD_LIBS@

EXTRA_DIST = \
	helix_handler.hpp

MAINTAINERCLEANFILES = \
	$(srcdir)/Makefile.in
