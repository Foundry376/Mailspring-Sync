bin_PROGRAMS=belcard-folder belcard-unfolder belcard-parser

belcard_folder_SOURCES=belcard-folder.cpp
belcard_folder_LDADD=$(top_builddir)/src/libbelcard.la -lbelr

belcard_unfolder_SOURCES=belcard-unfolder.cpp
belcard_unfolder_LDADD=$(top_builddir)/src/libbelcard.la -lbelr

belcard_parser_SOURCES=belcard-parser.cpp
belcard_parser_LDADD=$(top_builddir)/src/libbelcard.la -lbelr

AM_CXXFLAGS=$(BELR_CFLAGS)
AM_CPPFLAGS=-I$(top_srcdir)/include
