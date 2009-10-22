AUTOMAKE_OPTIONS = subdir-objects

noinst_PROGRAMS = qps_tester

AM_CXXFLAGS = -I$(srcdir)/../include -Wall

qps_tester_SOURCES = qps_tester.cpp boost-system/error_code.cpp

EXTRA_DIST = \
	Makefile.bor \
	Makefile.mgw \
	Makefile.msc \
	http_parser.hpp \
	http_client.hpp \
	cpu_monitor.hpp \
	boost_utils.hpp \
	dnadb.hpp \
	sha.h \
	utils.hpp \
	authorizer.hpp \
	natcheck.hpp \
	stats.hpp \
	swarm.hpp \
	brpc_client.hpp \
	parsed_url.hpp \
	connection.hpp \
	connection_manager.hpp \
	header.hpp \
	reply.hpp \
	request.hpp \
	request_handler.hpp \
	request_parser.hpp \
	server.hpp \
	control.hpp \
	templates.h \
	callback_handler.hpp

MAINTAINERCLEANFILES = \
	$(srcdir)/Makefile.in
