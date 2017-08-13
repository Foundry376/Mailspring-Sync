#!/bin/bash
export MAILSYNC_DIR=$( cd $(dirname $0) ; pwd -P );
export APP_ROOT_DIR="$MAILSYNC_DIR/../app"
export APP_DIST_DIR="$APP_ROOT_DIR/dist"
export DEP_BUILDS_DIR=/tmp/mailsync-build-deps

mkdir -p "$APP_DIST_DIR"

if [[ "$OSTYPE" == "darwin"* ]]; then
  cd "$MAILSYNC_DIR"
  gem install xcpretty;
  set -o pipefail && xcodebuild -scheme mailsync -configuration Release | xcpretty;
  cp "$APP_ROOT_DIR/mailsync" "$APP_DIST_DIR/"

elif [[ "$OSTYPE" == "linux-gnu" ]]; then
  # we cache this directory between builds to make CI faster.
  # if it exists, just run make install again, otherwise pull
  # the libraries down and build from source.
  if [ -d "$DEP_BUILDS_DIR/libetpan" ]; then
    echo "Installing curl-7.50.2..."
    cd "$DEP_BUILDS_DIR/curl-7.50.2"
    sudo make install prefix=/usr >/dev/null
    echo "Installing libetpan..."
    cd "$DEP_BUILDS_DIR/libetpan"
    sudo make install prefix=/usr >/dev/null

  else
    mkdir "$DEP_BUILDS_DIR"

    # Install curl from source because the Ubuntu trusty version
    # is too old. We need v7.46 or greater.
    echo "Building and installing curl-7.50.2..."
    cd "$DEP_BUILDS_DIR"
    sudo apt-get build-dep curl
    wget -q http://curl.haxx.se/download/curl-7.50.2.tar.bz2
    tar -xjf curl-7.50.2.tar.bz2
    cd curl-7.50.2
    ./configure --quiet
    make >/dev/null
    sudo make install prefix=/usr >/dev/null
    sudo ldconfig

    # install libetpan from source
    echo "Building and installing libetpan..."
    cd "$DEP_BUILDS_DIR"
    git clone --depth=1 https://github.com/dinhviethoa/libetpan
    cd ./libetpan
    ./autogen.sh
    make >/dev/null
    sudo make install prefix=/usr >/dev/null
  fi

  # build mailcore2
  echo "Building mailcore2..."
  cd "$MAILSYNC_DIR/Vendor/mailcore2"
  mkdir -p build
  cd build
  cmake ..
  make

  # build mailsync
  echo "Building Merani MailSync..."
  cd "$MAILSYNC_DIR"
  cmake .
  make

  # copy build product into the client working directory
  cp "$MAILSYNC_DIR/mailsync" "$APP_ROOT_DIR/"
  cp "$MAILSYNC_DIR/mailsync" "$APP_DIST_DIR/"

else
  echo "Mailsync does not build on $OSTYPE yet.";
fi
