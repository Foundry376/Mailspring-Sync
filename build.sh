#!/bin/bash

if [[ "$OSTYPE" == "darwin"* ]]; then
  xcodebuild -scheme MailSync -configuration Release;

elif [[ "$OSTYPE" == "linux-gnu" ]]; then
  export MAILSYNC_DIR=$( cd $(dirname $0) ; pwd -P );
  cd "$MAILSYNC_DIR";

  # install libetpan from source
  mkdir -p temp
  cd ./temp
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