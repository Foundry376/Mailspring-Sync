#!/bin/bash

cd "${0%/*}"

if [[ "$OSTYPE" == "darwin"* ]]; then
  xcodebuild -scheme MailSync -configuration Release;

elif [[ "$OSTYPE" == "linux-gnu" ]]; then
  # install libetpan from source
  mkdir ~/libetpan
  cd ~/libetpan
  git clone --depth=1 https://github.com/dinhviethoa/libetpan
  cd libetpan
  ./autogen.sh
  make >/dev/null
  sudo make install prefix=/usr >/dev/null

  # build mailcore2
  cd "${0%/*}/Vendor/mailcore2"
  mkdir -p build
  cd build
  cmake ..
  make

  # build mailsync
  cd "${0%/*}"
  cmake .
  make

  # copy build product into the client working directory
  cp mailsync ../client/app

else
  echo "Mailsync does not build on $OSTYPE yet.";
fi
