EXTRA_DIST=vcards/vcard.vcf vcards/vcards.vcf vcards/unfoldtest.vcf vcards/foldtest.vcf

if BUILD_TESTS

noinst_PROGRAMS=belcard-tester
belcard_tester_SOURCES=belcard-tester.cpp belcard-tester.hpp \
			belcard-general-tester.cpp \
			belcard-identification-tester.cpp \
			belcard-addressing-tester.cpp \
			belcard-communication-tester.cpp \
			belcard-geographical-tester.cpp \
			belcard-organizational-tester.cpp \
			belcard-explanatory-tester.cpp \
			belcard-security-tester.cpp \
			belcard-calendar-tester.cpp \
			belcard-rfc6474-tester.cpp \
			belcard-vcard-tester.cpp
			
belcard_tester_LDADD=$(top_builddir)/src/libbelcard.la $(BELR_LIBS) -lbcunit $(BCTOOLBOX_LIBS) $(BCTOOLBOXTESTER_LIBS)

belcard_tester_CFLAGS=\
	-DBC_CONFIG_FILE=\"config.h\" \
	$(BCUNIT_CFLAGS)
	
belcard_tester_CXXFLAGS=$(BELR_CFLAGS)

belcard_tester_AM_CPPFLAGS=-I$(top_srcdir)/include \
			-I$(top_srcdir)/tester \
			-I$(top_srcdir)/tester/common

endif # BUILD_TESTS

AM_CPPFLAGS=-I$(top_srcdir)/include
