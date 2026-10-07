#!/usr/bin/env bash
# Builds every native test with AddressSanitizer + UndefinedBehaviorSanitizer
# (and -Werror) and runs it. Non-functional robustness gate: memory errors,
# overflows and undefined behaviour in the firmware modules fail the run.
set -euo pipefail
D="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"; R="$(cd "$D/../.." && pwd)"; O="$(mktemp -d)"; trap 'rm -rf "$O"' EXIT
# Prefer ASan+UBSan; if their runtime libraries are not installed (Fedora:
# `sudo dnf install libasan libubsan`) fall back to a hardened build that needs no
# runtime: UBSan in trap mode, fortified libc, glibc heap checks, stack protector.
echo 'int main(){return 0;}' > "$O/probe.cpp"
if g++ -fsanitize=address,undefined "$O/probe.cpp" -o "$O/probe" 2>/dev/null; then
  MODE="ASan+UBSan"; S="-fsanitize=address,undefined -fno-sanitize-recover=all"
else
  MODE="hardened (no sanitizer runtime installed)"
  S="-fsanitize=undefined -fsanitize-undefined-trap-on-error -D_FORTIFY_SOURCE=3 -D_GLIBCXX_ASSERTIONS -fstack-protector-all"
  export MALLOC_CHECK_=3 MALLOC_PERTURB_=165
fi
echo "mode: $MODE"
F="-std=c++17 -g -O2 -Wall -Wextra -Werror -Wno-error=stringop-truncation $S -I $D/arduino_stub -I $R/firmware/MORPHEUS"
run() { local name=$1; shift; g++ $F "$@" -o "$O/$name"; "$O/$name"; }
run decoder   "$D/test_core_decoder.cpp"  "$R/firmware/MORPHEUS/core_decoder.cpp"
run trainer   "$D/test_core_trainer.cpp"  "$R/firmware/MORPHEUS/core_trainer.cpp"
run keyer     "$D/test_core_keyer.cpp"    "$R/firmware/MORPHEUS/core_keyer.cpp"
run gamemorse "$D/test_game_morse_protocol.cpp"
run games     "$D/test_core_games_input.cpp" "$R/firmware/MORPHEUS/core_games.cpp"
run settings  "$D/test_keyer_settings_protocol.cpp"
run metrics   "$D/test_keyer_metrics.cpp" "$R/firmware/MORPHEUS/core_decoder.cpp"
run decneg    "$D/test_decoder_negative.cpp" "$R/firmware/MORPHEUS/core_decoder.cpp"
run keyguard  "$D/test_virtual_key_guard.cpp"
run cmdqueue  "$D/test_control_cmd_queue.cpp"
run fuzz      "$D/test_protocol_fuzz.cpp"
run bench     "$D/bench_decoder.cpp" "$R/firmware/MORPHEUS/core_decoder.cpp"
echo "OK - all native tests clean ($MODE)"
