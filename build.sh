#!/bin/bash
#
# This script is run by Travis on Mac and Linux to build and package mailsync.
# Windows uses ./build.cmd.
#
export MAILSYNC_DIR=$( cd $(dirname $0) ; pwd -P );
export APP_ROOT_DIR="$MAILSYNC_DIR/../app"
export APP_DIST_DIR="$APP_ROOT_DIR/dist"
export DEP_BUILDS_DIR=/tmp/mailsync-build-deps-v2 # Note: also referenced in CMakeLists

set -e
mkdir -p "$APP_DIST_DIR"

if [[ "$OSTYPE" == "darwin"* ]]; then
  cd "$MAILSYNC_DIR"
  gem install xcpretty;
  set -o pipefail && xcodebuild -scheme mailsync -configuration Release | xcpretty;

  # the xcodebuild copies the build products to the APP_ROOT_DIR and codesigns
  # them for us. We just need to tar them up and move them to the artifacts folder
  cd "$APP_ROOT_DIR"
  if [ -e "mailsync.dSYM.zip" ]; then
    tar -czf "$APP_DIST_DIR/mailsync.tar.gz" mailsync.dSYM.zip mailsync
  else
    tar -czf "$APP_DIST_DIR/mailsync.tar.gz" mailsync
  fi

elif [[ "$OSTYPE" == "linux-gnu" ]]; then
  # we cache this directory between builds to make CI faster.
  # if it exists, just run make install again, otherwise pull
  # the libraries down and build from source.
  if [ ! -d "$DEP_BUILDS_DIR" ]; then
    mkdir "$DEP_BUILDS_DIR"
  fi

  if [ -d "$DEP_BUILDS_DIR/openssl-1.1.0f" ]; then
    echo "Installing openssl-1.1.0f"
    cd "$DEP_BUILDS_DIR/openssl-1.1.0f"
    sudo make install
    sudo ldconfig
  else
    echo "Building and installing openssl-1.1.0f"
    cd "$DEP_BUILDS_DIR"
    wget -q https://ftp.openssl.org/source/old/1.1.0/openssl-1.1.0f.tar.gz
    tar -xzf openssl-1.1.0f.tar.gz
    cd openssl-1.1.0f
    sudo ./config --prefix=/opt/openssl --openssldir=/opt/openssl shared
    sudo make
    sudo make install
    sudo ldconfig
  fi


  if [ -d "$DEP_BUILDS_DIR/curl-7.70.0" ]; then
    echo "Installing curl-7.70.0..."
    cd "$DEP_BUILDS_DIR/curl-7.70.0"
    sudo make install prefix=/usr >/dev/null
  else
    # Install curl from source because the Ubuntu trusty version
    # is too old. We need v7.46 or greater.
    echo "Building and installing curl-7.70.0..."
    cd "$DEP_BUILDS_DIR"
    sudo apt-get -y build-dep curl
    wget -q http://curl.haxx.se/download/curl-7.70.0.tar.bz2
    tar -xjf curl-7.70.0.tar.bz2
    cd curl-7.70.0
    ./configure --quiet --disable-cookies --disable-ldaps --disable-ldap --disable-ftp --disable-ftps --disable-gopher --disable-dict --disable-imap --disable-imaps --disable-pop3 --disable-pop3s --disable-rtsp --disable-smb --disable-smtp --disable-smtps --disable-telnet --disable-tftp --disable-shared --enable-static --enable-ares --without-libidn --without-librtmp --with-ssl=/opt/openssl
    make >/dev/null
    sudo make install prefix=/usr >/dev/null
    sudo ldconfig
  fi
  

  echo "OpenSSL files:"
  ls /opt/openssl/include/openssl
  ls /opt/openssl/lib

  echo "Symbolic linking new libssl libs into /usr/lib to workaround weird libetpan autogen:"
  sudo ln -s /opt/openssl/lib/libssl.so.1.1 /usr/lib/libssl.so.1.1
  sudo ln -s /opt/openssl/lib/libcrypto.so.1.1 /usr/lib/libcrypto.so.1.1

  echo "Building and installing libetpan..."
  cd "$MAILSYNC_DIR/Vendor/libetpan"
  ./autogen.sh --with-openssl=/opt/openssl
  make >/dev/null
  sudo make install prefix=/usr >/dev/null

  # build mailcore2
  echo "Building mailcore2..."
  cd "$MAILSYNC_DIR/Vendor/mailcore2"
  mkdir -p build
  cd build
  cmake ..
  make

  # build mailsync
  echo "Building Mailspring MailSync..."
  cd "$MAILSYNC_DIR"
  cmake .
  make

  # copy build product into the client working directory.
  cp "$MAILSYNC_DIR/mailsync" "$APP_ROOT_DIR/mailsync.bin"

  # copy libsasl2 (and potentially others - just add to grep expression)
  # into the target directory since we don't want to depend on installed version
  ldd "$MAILSYNC_DIR/mailsync" | grep "=> /" | awk '{print $3}' | grep "libsasl2" | xargs -I '{}' cp -v '{}' "$APP_ROOT_DIR"

  # copy libsasl2's modules into the target directory because they're all shipped separately
  # (We set SASL_PATH below so it finds these.)
  cp /usr/lib/x86_64-linux-gnu/sasl2/* "$APP_ROOT_DIR"

  printf "#!/bin/bash\nset -e\nset -o pipefail\nSCRIPTPATH=\"\$( cd \"\$(dirname \"\$0\")\" >/dev/null 2>&1 ; pwd -P )\"\nSASL_PATH=\"\$SCRIPTPATH\" LD_LIBRARY_PATH=\"\$SCRIPTPATH;\$LD_LIBRARY_PATH\" \"\$SCRIPTPATH/mailsync.bin\" \"\$@\"" > "$APP_ROOT_DIR/mailsync"
  chmod +x "$APP_ROOT_DIR/mailsync"

  # Zip this stuff up so we can push it to S3 as a single artifacts
  cd "$APP_ROOT_DIR"
  tar -czf "$APP_DIST_DIR/mailsync.tar.gz" *.so* mailsync mailsync.bin --wildcards
else
  echo "Mailsync does not build on $OSTYPE yet.";
fi
