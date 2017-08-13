#!/bin/bash

if [[ "$OSTYPE" == "darwin"* ]]; then
  xcodebuild -scheme MailSync -configuration Release;

elif [[ "$OSTYPE" == "linux-gnu" ]]; then
  export MAILSYNC_DIR=$( cd $(dirname $0) ; pwd -P );
  export DEP_BUILDS_DIR=/tmp/mailsync-build-deps

  # we cache this directory between builds to make CI faster.
  # if it exists, just run make install again, otherwise pull
  # the libraries down and build from source.
  if [ -d "$DEP_BUILDS_DIR/libetpan" ]; then
    cd "$DEP_BUILDS_DIR/curl-7.50.2"
    sudo make install prefix=/usr >/dev/null
    cd "$DEP_BUILDS_DIR/libetpan"
    sudo make install prefix=/usr >/dev/null

  else
    mkdir "$DEP_BUILDS_DIR"

    # Install curl from source because the Ubuntu trusty version is too old
    cd "$DEP_BUILDS_DIR"
    sudo apt-get build-dep curl
    wget http://curl.haxx.se/download/curl-7.50.2.tar.bz2
    tar -xjf curl-7.50.2.tar.bz2
    cd curl-7.50.2
    ./configure
    make >/dev/null
    sudo make install prefix=/usr >/dev/null
    sudo ldconfig

    # install libetpan from source
    cd "$DEP_BUILDS_DIR"
    git clone --depth=1 https://github.com/dinhviethoa/libetpan
    cd ./libetpan
    ./autogen.sh
    make >/dev/null
    sudo make install prefix=/usr >/dev/null
  fi

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