[![pipeline status](https://gitlab.linphone.org/BC/public/bctoolbox/badges/master/pipeline.svg)](https://gitlab.linphone.org/BC/public/bctoolbox/commits/master)

BcToolbox
=========

Utilities library used by Belledonne Communications softwares like belle-sip, mediastreamer2 and linphone.


Depends
-------

- **mbedtls[1]**: implementation of TLS interface of BcToolbox. For backward
  compatibility, support of polarssl is also provided.
- **bcunit[2]** for unitary test tools. (optional)


To compile
----------

	cmake . -DCMAKE_INSTALL_PREFIX=<install prefix> -DCMAKE_PREFIX_PATH=<search prefix>
	
	make
	make install


To make an rpm package
----------------------

	cmake . -DCMAKE_INSTALL_PREFIX=/usr -DCPACK_GENERATOR="RPM"
	
	make package 


Options
-------

- `CMAKE_INSTALL_PREFIX=<string>`: install prefix.
- `CMAKE_PREFIX_PATH=<string>`: search path prefix for dependencies e.g. mbedtls.
- `ENABLE_MBEDTLS=NO`: do not look for mbedtls. Then, polarssl will be selected.
- `ENABLE_POLARSSL=NO`: do not look for polarssl. That ensure to use mbedtls.
- `ENABLE_SHARED=NO`: do not build the shared libraries.
- `ENABLE_STATIC=NO`: do not build the static libraries.
- `ENABLE_STRICT=NO`: do not build with strict compilator flags e.g. `-Wall -Werror`.
- `ENABLE_TESTS=NO`: do not build testing binaries.
- `ENABLE_TESTS_COMPONENT=NO`: do not build libbctoolbox-tester.


Notes
-----

For backward compatibility with distributions not having the required 2.8.12 cmake version, an automake/autoconf build system is also available.
It is maintained as a best effort and then should be used only in last resort.


Note for packagers
------------------

Our CMake scripts may automatically add some paths into research paths of generated binaries.
To ensure that the installed binaries are striped of any rpath, use `-DCMAKE_SKIP_INSTALL_RPATH=ON`
while you invoke cmake.

--------------------

- [1] <https://github.com/ARMmbed/mbedtls.git>
- [2] git://git.linphone.org/bctoolbox.git or <http://www.linphone.org/releases/sources/bctoolbox/>

