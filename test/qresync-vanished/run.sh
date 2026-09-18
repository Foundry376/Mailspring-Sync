#!/bin/bash
#
# Drives a real mailsync process against the scriptable QRESYNC IMAP server in
# imapd.py. See README.md.
#
# Usage: run.sh <scenario.json> <runname> [seconds]
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SCEN="$1"; NAME="$2"; SECS="${3:-40}"
PORT=$((11143 + RANDOM % 1000))

WORK="$HERE/runs/$NAME"
rm -rf "$WORK"; mkdir -p "$WORK"

export IMAPD_LOG="$WORK/imapd.log"
python3 "$HERE/imapd.py" "$SCEN" "$PORT" &
IMAPD=$!
sleep 1

ACCOUNT=$(python3 - "$PORT" <<'PY'
import json,sys
port=int(sys.argv[1])
print(json.dumps({
  "id":"acct-test-1","__cls":"Account","provider":"imap",
  "emailAddress":"me@example.test","name":"Test",
  "settings":{
    "imap_host":"127.0.0.1","imap_port":port,"imap_username":"me",
    "imap_password":"pw","imap_security":"none","imap_allow_insecure_ssl":False,
    "smtp_host":"127.0.0.1","smtp_port":10025,"smtp_username":"me",
    "smtp_password":"pw","smtp_security":"none","smtp_allow_insecure_ssl":False,
    "create_helper_folders":False
  }
}))
PY
)

export CONFIG_DIR_PATH="$WORK/config"
export IDENTITY_SERVER="http://127.0.0.1:9/never"
mkdir -p "$CONFIG_DIR_PATH"

MAILSYNC="${MAILSYNC_BIN:-$HERE/../../mailsync}"
"$MAILSYNC" --mode migrate --identity 'null' --account "$ACCOUNT" >"$WORK/migrate.log" 2>&1
echo "migrate exit: $?"

MAILSYNC="${MAILSYNC_BIN:-$HERE/../../mailsync}"
"$MAILSYNC" --mode sync --orphan --verbose \
    --identity 'null' --account "$ACCOUNT" \
    >"$WORK/deltas.log" 2>"$WORK/stderr.log" &
MS=$!

sleep "$SECS"
kill -9 $MS 2>/dev/null
kill -9 $IMAPD 2>/dev/null
wait 2>/dev/null

echo "--- run '$NAME' finished; artifacts in $WORK"
ls "$CONFIG_DIR_PATH"
