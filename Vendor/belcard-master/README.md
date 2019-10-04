[![pipeline status](https://gitlab.linphone.org/BC/public/belcard/badges/master/pipeline.svg)](https://gitlab.linphone.org/BC/public/belcard/commits/master)

BelCard
=======

Belcard is a C++ library to manipulate VCard standard format

Dependencies
------------
- *bctoolbox[1]* : portability layer.
- *belr[2]*      : used to parse VCard format.


Build instrucitons
------------------

	cmake . -DCMAKE_INSTALL_PREFIX=<install_prefix> -DCMAKE_PREFIX_PATH=<search_prefix>
	
	make
	make install


Options
-------

- `CMAKE_INSTALL_PREFIX=<string>` : installation prefix
- `CMAKE_PREFIX_PATH=<string>`    : prefix where depedencies are installed
- `ENABLE_UNIT_TESTS=NO`          : do not compile non-regression tests
- `ENABLE_SHARED=NO`              : do not build the shared library.
- `ENABLE_STATIC=NO`              : do not build the static library.
- `ENABLE_STRICT=NO`              : do not build with strict complier flags e.g. `-Wall -Werror`


Note for packagers
------------------
Our CMake scripts may automatically add some paths into research paths of generated binaries.
To ensure that the installed binaries are striped of any rpath, use `-DCMAKE_SKIP_INSTALL_RPATH=ON`
while you invoke cmake.

Rpm packaging
belcard can be generated with cmake3 using the following command:
mkdir WORK
cd WORK
cmake3 ../
make package_source
rpmbuild -ta --clean --rmsource --rmspec belcard-<version>-<release>.tar.gz


------------------

- [1] git://git.linphone.org/bctoolbox.git or <http://www.linphone.org/releases/sources/bctoolbox>
- [2] git://git.linphone.org/belr.git or <http://www.linphone.org/releases/sources/belr>
