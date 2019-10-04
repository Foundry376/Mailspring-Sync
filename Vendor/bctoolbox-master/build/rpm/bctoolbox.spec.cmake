# -*- rpm-spec -*-

%define _prefix    @CMAKE_INSTALL_PREFIX@
%define pkg_prefix @BC_PACKAGE_NAME_PREFIX@

# re-define some directories for older RPMBuild versions which don't. This messes up the doc/ dir
# taken from https://fedoraproject.org/wiki/Packaging:RPMMacros?rd=Packaging/RPMMacros
%define _datarootdir       %{_prefix}/share
%define _datadir           %{_datarootdir}
%define _docdir            %{_datadir}/doc

%define build_number @PROJECT_VERSION_BUILD@
%if %{build_number}
%define build_number_ext -%{build_number}
%endif



Name:           @CPACK_PACKAGE_NAME@
Version:        @PROJECT_VERSION@
Release:        %{build_number}%{?dist}
Summary:        Belr is language recognition library for ABNF based protocols.

Group:          Applications/Communications
License:        GPL
URL:            http://www.linphone.org
Source0:        %{name}-%{version}%{?build_number_ext}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-buildroot

Requires:	%{pkg_prefix}bctoolbox

%description

Toolbox package used by Belledonne Communications projects

%package devel
Summary:       Development libraries for bctoolbox
Group:         Development/Libraries
Requires:      %{name} = %{version}-%{release}

%description    devel
Libraries and headers required to develop software with bctoolbox

%if 0%{?rhel} && 0%{?rhel} <= 7
%global cmake_name cmake3
%define ctest_name ctest3
%else
%global cmake_name cmake
%define ctest_name ctest
%endif

# This is for debian builds where debug_package has to be manually specified, whereas in centos it does not
%define custom_debug_package %{!?_enable_debug_packages:%debug_package}%{?_enable_debug_package:%{nil}}
%custom_debug_package

%prep
%setup -n %{name}-%{version}%{?build_number_ext}

%build
%{expand:%%%cmake_name} . -DCMAKE_BUILD_TYPE=@CMAKE_BUILD_TYPE@ -DCMAKE_PREFIX_PATH:PATH=%{_prefix} @RPM_ALL_CMAKE_OPTIONS@
make %{?_smp_mflags}

%install
make install DESTDIR=%{buildroot}

# Dirty workaround to give exec rights for all shared libraries. Debian packaging needs this
# TODO : set CMAKE_INSTALL_SO_NO_EXE for a cleaner workaround
chmod +x `find %{buildroot} *.so.*`

%check
%{ctest_name} -V %{?_smp_mflags}

%clean
rm -rf $RPM_BUILD_ROOT

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig


%files
%defattr(-,root,root)
%doc AUTHORS ChangeLog COPYING NEWS README.md
%{_libdir}/*.so.*

%files devel
%defattr(-,root,root)
%{_includedir}/bctoolbox
%if @ENABLE_STATIC@
%{_libdir}/libbctoolbox.a
%endif
%if @ENABLE_SHARED@
%{_libdir}/libbctoolbox.so
%endif
%{_libdir}/pkgconfig/bctoolbox.pc
%{_datadir}/bctoolbox/cmake/*
%if @ENABLE_TESTS_COMPONENT@
%if @ENABLE_STATIC@
%{_libdir}/libbctoolbox-tester.a
%endif
%if @ENABLE_SHARED@
%{_libdir}/libbctoolbox-tester.so
%endif
%{_libdir}/pkgconfig/bctoolbox-tester.pc
%endif

%changelog

* Tue Nov 27 2018 ronan.abhamon <ronan.abhamon@belledonne-communications.com>
- Do not set CMAKE_INSTALL_LIBDIR and never with _libdir!

* Tue Jan 16 2018 Ghislain MARY <ghislain.mary@belledonne-communications.com>
- Initial RPM release.
