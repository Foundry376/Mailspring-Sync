#!/bin/bash

if [[ "$OSTYPE" == "darwin"* ]]; then
  xcodebuild -scheme MailSync -configuration Release;

elif [[ "$OSTYPE" == "linux-gnu" ]]; then
  export MAILSYNC_DIR=$( cd $(dirname $0) ; pwd -P );
  cd "$MAILSYNC_DIR";

  mkdir -p temp

  # Install curl from source because the Ubuntu trusty version is too old
  cd "$MAILSYNC_DIR/temp"
  sudo apt-get build-dep curl
  wget http://curl.haxx.se/download/curl-7.50.2.tar.bz2
  tar -xvjf curl-7.50.2.tar.bz2
  cd curl-7.50.2

  ./configure
  make
  sudo make install prefix=/usr >/dev/null
  sudo ldconfig

  # install libetpan from source
  cd "$MAILSYNC_DIR/temp"
  git clone --depth=1 https://github.com/dinhviethoa/libetpan
  cd ./libetpan
  ./autogen.sh
  make >/dev/null
  sudo make install prefix=/usr >/dev/null

  # build mailcore2
  cd "$MAILSYNC_DIR/Vendor/mailcore2"
  mkdir -p build
  cd build
  cmake ..
  make

  # build mailsync
  cd "$MAILSYNC_DIR"
  cmake .
  make

  # copy build product into the client working directory
  cp "$MAILSYNC_DIR/mailsync" "$MAILSYNC_DIR/../app"

else
  echo "Mailsync does not build on $OSTYPE yet.";
fi