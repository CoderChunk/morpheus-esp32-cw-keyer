#!/usr/bin/env bash
# MORPHEUS firmware test suite - one entry point for every level.
#
#   tests/run_all.sh --host                    functional, non-functional (host), regression, consistency
#   tests/run_all.sh --host --build            ... plus ESP32 build, size budget and warnings
#   tests/run_all.sh --device AA:BB:CC:DD:EE:FF [--soak 30] [--stress 20]
#                                              system + integration + reliability on the REAL device
#   tests/run_all.sh --all --device AA:BB:..   everything
#
# Options: --baseline <git-tag>   compare firmware size with a tag (e.g. v2.7.1)
#          --reconnect-min <0..1> required reconnect success rate (default 1.0 = strict)
#          --out <dir>            results directory (default test-results/<timestamp>)
# Device steps need exclusive use of the device (one BLE connection) and never flash it.
# Test IDs per step are listed in docs/TEST_SUITE.md.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT"
HOST=0; BUILD=0; DEVICE=""; SOAK=0; STRESS=0; BASE=""; RMIN="1.0"; OUT=""
while [ $# -gt 0 ]; do case "$1" in
  --host) HOST=1;; --build) BUILD=1;; --all) HOST=1; BUILD=1;;
  --device) DEVICE="$2"; shift;; --soak) SOAK="$2"; shift;; --stress) STRESS="$2"; shift;;
  --baseline) BASE="$2"; shift;; --reconnect-min) RMIN="$2"; shift;; --out) OUT="$2"; shift;;
  -h|--help) sed -n 2,16p "$0"; exit 0;; *) echo "unknown option $1"; exit 2;; esac; shift; done
[ "$HOST$BUILD" = "00" ] && [ -z "$DEVICE" ] && { sed -n 2,16p "$0"; exit 2; }
OUT="${OUT:-$ROOT/test-results/$(date +%Y%m%d-%H%M%S)}"; mkdir -p "$OUT"; TSV="$OUT/steps.tsv"; : > "$TSV"
FAILED=0
step() { # level id "description" command...
  local level=$1 id=$2 desc=$3; shift 3; local log="$OUT/$id.log" t0=$SECONDS
  printf '%-8s %-8s %s ... ' "$level" "$id" "$desc"
  if "$@" > "$log" 2>&1; then st=PASS; else st=FAIL; FAILED=1; fi
  printf '%s (%ss)\n' "$st" "$((SECONDS - t0))"
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$level" "$id" "$desc" "$st" "$((SECONDS - t0))" "$(basename "$log")" >> "$TSV"
}
size_budget() { # parse compile output: flash <= 90 %, RAM <= 50 %
  python3 - "$1" <<'P'
import re,sys
t=open(sys.argv[1]).read()
f=re.search(r"Sketch uses (\d+) bytes \((\d+)%\)",t); r=re.search(r"Global variables use (\d+) bytes \((\d+)%\)",t)
print(f"flash {f.group(1)} B ({f.group(2)}%), RAM {r.group(1)} B ({r.group(2)}%)")
sys.exit(0 if int(f.group(2))<=90 and int(r.group(2))<=50 else 1)
P
}
build_step() {
  arduino-cli compile --clean --fqbn esp32:esp32:esp32 --warnings all firmware/MORPHEUS > "$OUT/build.raw" 2>&1 || { cat "$OUT/build.raw"; return 1; }
  grep -E "Sketch uses|Global variables" "$OUT/build.raw"
  # Warnings in the firmware sources must not grow: compare with tests/firmware_warnings.baseline
  # (line numbers ignored). A cached build prints no warnings, so force a full recompile
  # of the sketch before relying on this (arduino-cli --clean).
  python3 - "$OUT/build.raw" tests/firmware_warnings.baseline <<'P' || return 1
import re,sys
cur=set()
for l in open(sys.argv[1]).read().split("\n"):
    m=re.search(r"/firmware/MORPHEUS/([\w.]+):\d+:\d+: warning: (.*)",l)
    if m: cur.add(f"{m.group(1)}: {re.sub(r'[0-9]+','N',m.group(2))[:110]}")
base=set(open(sys.argv[2]).read().split("\n"))-{""}
new=sorted(cur-base)
print(f"firmware warnings: {len(cur)} (baseline {len(base)}), new: {len(new)}")
for n in new: print("  NEW:",n)
sys.exit(1 if new else 0)
P
  size_budget "$OUT/build.raw"
}
baseline_step() {
  local w; w="$(mktemp -d)/fw"; git worktree add -q --detach "$w" "$BASE" || return 1
  (cd "$w" && arduino-cli compile --fqbn esp32:esp32:esp32 firmware/MORPHEUS) > "$OUT/baseline.raw" 2>&1; local rc=$?
  git worktree remove --force "$w"; git worktree prune; [ $rc -eq 0 ] || return 1
  python3 - "$OUT/baseline.raw" "$OUT/build.raw" "$BASE" <<'P'
import re,sys
def sz(p):
    t=open(p).read(); return int(re.search(r"Sketch uses (\d+)",t).group(1)), int(re.search(r"Global variables use (\d+)",t).group(1))
(bf,br),(nf,nr)=sz(sys.argv[1]),sz(sys.argv[2])
print(f"{sys.argv[3]}: flash {bf} B RAM {br} B  ->  now: flash {nf} B RAM {nr} B  (flash {nf-bf:+d} B = {100*(nf-bf)/bf:+.2f}%, RAM {nr-br:+d} B)")
sys.exit(0 if 100*(nf-bf)/bf <= 5 else 1)   # regression gate: flash growth <= 5 %
P
}
if [ $HOST = 1 ]; then
  step host H1 "Python unit + consistency tests (FT-08..10, IT-11..21, RT-01)" python3 -m unittest discover -s tests -v
  step host H2 "Native firmware suites (FT-01..07, RT-02)" tests/native/run.sh
  step host H3 "Hardened/sanitized native + fuzz + benchmark (NFT-01..03)" tests/native/run_sanitized.sh
fi
if [ $BUILD = 1 ]; then
  step build B1 "ESP32 build, warnings, size budget (NFT-04, NFT-06)" build_step
  [ -n "$BASE" ] && step build B2 "Size regression vs $BASE (NFT-05)" baseline_step
fi
if [ -n "$DEVICE" ]; then
  step device D1 "System tests on the real device (ST-01..16)" python3 tests/hardware/ble_system_test.py "$DEVICE" --json "$OUT/system.json"
  step device D2 "Bridge <-> real device integration (IT-01..10)" python3 tests/hardware/bridge_integration_test.py "$DEVICE" --json "$OUT/bridge.json"
  if [ "$STRESS" -gt 0 ]; then
    rel() { python3 tests/hardware/ble_system_test.py --reconnect-stress "$DEVICE" "$STRESS" 2 | tee /dev/stderr | python3 -c "
import re,sys; t=sys.stdin.read(); m=re.search(r'(\d+) ok, (\d+) failed',t); ok,bad=int(m[1]),int(m[2]); r=ok/(ok+bad); print(f'rate {r:.2f} (required $RMIN)'); sys.exit(0 if r>=float('$RMIN') else 1)"; }
    step device D3 "Reconnect reliability x$STRESS (NFT-11, ST-13)" rel
  fi
  [ "$SOAK" != "0" ] && step device D4 "Soak ${SOAK} min (NFT-10, ST-18)" python3 tests/hardware/ble_system_test.py --soak "$DEVICE" "$SOAK"
fi
python3 - "$TSV" "$OUT" <<'P'
import sys
rows=[l.rstrip("\n").split("\t") for l in open(sys.argv[1])]
md=["# Firmware test run","","| Level | Step | Description | Result | Time | Log |","|---|---|---|---|---|---|"]
md+=[f"| {a} | {b} | {c} | **{d}** | {e}s | {f} |" for a,b,c,d,e,f in rows]
p=sum(r[3]=="PASS" for r in rows); md+=["",f"**{p}/{len(rows)} steps passed.**"]
open(sys.argv[2]+"/summary.md","w").write("\n".join(md)+"\n"); print("\n"+md[-1],"-> "+sys.argv[2]+"/summary.md")
P
exit $FAILED
