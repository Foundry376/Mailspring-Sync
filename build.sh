#!/bin/bash
#
# This script is run by Travis on Mac and Linux to build and package mailsync.
# Windows uses ./build.cmd.
#
export MAILSYNC_DIR=$( cd $(dirname $0) ; pwd -P );
export APP_ROOT_DIR="$MAILSYNC_DIR/../app"
export APP_DIST_DIR="$APP_ROOT_DIR/dist"
export CI="true"


set -e
mkdir -p "$APP_DIST_DIR"

if [[ "$OSTYPE" == "darwin"* ]]; then
  cd "$MAILSYNC_DIR"
  gem install xcpretty;
  # Build universal binary for both arm64 and x86_64
  set -o pipefail && xcodebuild -scheme mailsync -configuration Release -destination 'generic/platform=macOS' ONLY_ACTIVE_ARCH=NO ARCHS="arm64 x86_64" | xcpretty;

  # the xcodebuild copies the build products to the APP_ROOT_DIR and codesigns
  # them for us. We just need to tar them up and move them to the artifacts folder
  cd "$APP_ROOT_DIR"
  if [ -e "mailsync.dSYM.zip" ]; then
    tar -czf "$APP_DIST_DIR/mailsync.tar.gz" mailsync.dSYM.zip mailsync
  else
    tar -czf "$APP_DIST_DIR/mailsync.tar.gz" mailsync
  fi

elif [[ "$OSTYPE" == "linux-gnu" ]]; then
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
  make MailCore  # Only build the library, not tests (tests can't link without mailsync)

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

  printf "#!/bin/bash\nset -e\nset -o pipefail\nSCRIPTPATH=\"\$( cd \"\$(dirname \"\$0\")\" >/dev/null 2>&1 ; pwd -P )\"\nSASL_PATH=\"\$SCRIPTPATH\" LD_LIBRARY_PATH=\"\$SCRIPTPATH:\$LD_LIBRARY_PATH\" \"\$SCRIPTPATH/mailsync.bin\" \"\$@\"" > "$APP_ROOT_DIR/mailsync"
  chmod +x "$APP_ROOT_DIR/mailsync"

  # Zip this stuff up so we can push it to S3 as a single artifacts
  cd "$APP_ROOT_DIR"
  tar -czf "$APP_DIST_DIR/mailsync.tar.gz" --wildcards *.so* mailsync mailsync.bin
else
  echo "Mailsync does not build on $OSTYPE yet.";
fi

echo "Build products compressed to $APP_DIST_DIR";
