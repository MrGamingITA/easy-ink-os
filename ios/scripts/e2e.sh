#!/bin/bash
# Servizio vero in locale per le prove dell'app (nessuna scheda): dati e config in una cartella temporanea,
# porte 18765/18766/18767, solo 127.0.0.1 (--loopback), stato finto, niente mDNS. Apre una finestra di
# abbinamento e lancia il comando con CALAMAIO_E2E_LINK (e TEST_RUNNER_CALAMAIO_E2E_LINK per xcodebuild).
#   ios/scripts/e2e.sh swift test --package-path ios/CalamaioKit --filter E2E
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/calamaio-e2e.XXXXXX")
PID=""
# La pulizia parte subito: anche se la compilazione fallisce la cartella temporanea sparisce.
cleanup() {
  if [ -n "$PID" ]; then kill "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true; fi
  rm -rf "$TMP"
}
trap cleanup EXIT
fail() { echo "e2e: $1" >&2; [ -f "$TMP/serve.log" ] && { echo "e2e: registro del servizio:" >&2; tail -n 20 "$TMP/serve.log" >&2; }; exit 1; }
export CALAMAIO_CONFIG_DIR="$TMP/config" CALAMAIO_DATA_DIR="$TMP/data"
export CALAMAIO_API_PORT=18765 CALAMAIO_WS_PORT=18766 CALAMAIO_APP_PORT=18767
(cd "$ROOT/service" && go build -o "$TMP/calamaio" ./cmd/calamaio)
"$TMP/calamaio" serve --fake --no-mdns --loopback >"$TMP/serve.log" 2>&1 &
PID=$!
ready=0
for _ in $(seq 1 100); do
  kill -0 "$PID" 2>/dev/null || fail "il servizio è uscito subito (porta già occupata?)"
  if curl -fsS "http://127.0.0.1:18765/api/status" >/dev/null 2>&1; then ready=1; break; fi
  sleep 0.1
done
[ "$ready" = 1 ] || fail "il servizio non risponde su 127.0.0.1:18765 dopo 10 s"
# Chi risponde deve essere il nostro processo: se la porta era di un altro, ci si ferma.
owned() { # porta → vero se in ascolto c'è solo il nostro processo
  local owner
  owner=$(lsof -nP -iTCP:"$1" -sTCP:LISTEN -t 2>/dev/null | sort -u | tr '\n' ' ' || true)
  [ "${owner% }" = "$PID" ] || fail "la porta $1 è di un altro processo (${owner:-nessuno}), non del servizio di prova ($PID)"
}
owned 18765
kill -0 "$PID" 2>/dev/null || fail "il servizio è uscito"
LINK=$(curl -fsS -X POST "http://127.0.0.1:18765/api/phones/pair" -H 'Content-Type: application/json' \
  -H 'Origin: http://127.0.0.1:18765' -H 'Sec-Fetch-Site: same-origin' -d '{}' |
  python3 -I -c 'import json,sys; print(json.load(sys.stdin)["pairing"]["link"])') || fail "finestra di abbinamento non aperta"
owned 18767 # la porta dei telefoni si apre con la finestra di abbinamento
echo "e2e: servizio su 127.0.0.1:18765, finestra aperta (${LINK:0:40}…)" >&2
export CALAMAIO_E2E_LINK="$LINK" TEST_RUNNER_CALAMAIO_E2E_LINK="$LINK"
set +e
"$@"
STATUS=$?
set -e
if [ "$STATUS" -ne 0 ]; then echo "e2e: registro del servizio:" >&2; tail -n 20 "$TMP/serve.log" >&2; fi
exit "$STATUS"
