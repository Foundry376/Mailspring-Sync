
lib_LTLIBRARIES=libbelcard.la

libbelcard_la_SOURCES=belcard_parser.cpp \
			belcard_params.cpp \
			belcard_property.cpp \
			belcard.cpp \
			belcard_general.cpp \
			belcard_identification.cpp \
			belcard_addressing.cpp \
			belcard_communication.cpp \
			belcard_geographical.cpp \
			belcard_organizational.cpp \
			belcard_explanatory.cpp \
			belcard_security.cpp \
			belcard_calendar.cpp \
			belcard_rfc6474.cpp \
			vcard_grammar.cpp \
			belcard_utils.cpp

libbelcard_la_CFLAGS=$(BELR_CFLAGS)
libbelcard_la_CXXFLAGS=$(BELR_CFLAGS)

AM_CPPFLAGS=-I$(top_srcdir)/include

libbelcard_la_LIBADD=$(BELR_LIBS) $(BCTOOLBOX_LIBS)
