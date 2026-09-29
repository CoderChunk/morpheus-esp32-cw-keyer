// Native (host g++) test of the ACTUAL firmware keyer - compiles and
// links firmware/MORPHEUS/core_keyer.cpp itself, not a reimplementation.
// Covers straight-key DIT/DAH classification, WPM/weight/volume/sidetone
// clamping, mode-change state reset, and iambic paddle behavior
// (including the actual A vs B distinction: whether a squeeze released
// mid-element still sends one extra alternating element after the gap).
//
// Build/run: tests/native/run.sh. No external test framework - matches
// test_core_decoder.cpp's CHECK()-and-continue style.

#include "core_keyer.h"
#include "config.h"

#include <cstdio>
#include <vector>

// ----------------------------------------------------------------------------
// Fake clock and fake GPIO - every test sets these explicitly rather than
// relying on wall-clock time or real pins, so timing/state assertions are
// exact and repeatable.
// ----------------------------------------------------------------------------
static unsigned long g_millis = 0;
unsigned long millis() { return g_millis; }

static int g_tipState = HIGH;   // INPUT_PULLUP: HIGH = released, LOW = pressed
static int g_ringState = HIGH;
int digitalRead(uint8_t pin) {
  if (pin == PIN_JACK_TIP) return g_tipState;
  if (pin == PIN_JACK_RING) return g_ringState;
  return HIGH;
}

// ----------------------------------------------------------------------------
// events_onKeyDown/onKeyUp - declared in core_keyer.h, called directly by
// core_keyer.cpp (normally defined in MORPHEUS.ino). Captured here instead.
// ----------------------------------------------------------------------------
struct KeyUpEvent { ElementType type; unsigned long durMs; unsigned long thresholdMs; };
static int g_keyDownCount = 0;
static std::vector<KeyUpEvent> g_keyUps;

void events_onKeyDown(unsigned long) { g_keyDownCount++; }
void events_onKeyUp(ElementType type, unsigned long durMs, unsigned long thresholdMs, unsigned long, bool) {
  g_keyUps.push_back({type, durMs, thresholdMs});
}

// ----------------------------------------------------------------------------
// Tiny assert-and-continue harness (matches test_core_decoder.cpp).
// ----------------------------------------------------------------------------
static int g_failures = 0;
static const char *g_currentTest = "";

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      g_failures++; \
      std::fprintf(stderr, "FAIL %s: %s (line %d)\n", g_currentTest, #cond, __LINE__); \
    } \
  } while (0)

static void resetForTest(const char *name) {
  g_currentTest = name;
  g_millis = 0;
  g_tipState = HIGH;
  g_ringState = HIGH;
  g_keyDownCount = 0;
  g_keyUps.clear();
  core_keyer_init();
  // core_keyer_init() deliberately does NOT reset mode/paddleReversed/
  // iambicMode - on a real reboot those are restored from persisted
  // settings, not defaulted. Tests need explicit isolation instead.
  core_keyer_setMode(MODE_STRAIGHT);
  core_keyer_setPaddleReversed(false);
  core_keyer_setIambicMode(IAMBIC_MODE_B);
  core_keyer_setWpm(DEFAULT_WPM);
  core_keyer_setWeightPercent(50);
}

// Advances time past DEBOUNCE_MS and services once, so a just-changed pin
// state latches as the debouncer's stable reading.
static void settleDebounce() {
  g_millis += DEBOUNCE_MS + 1;
  core_keyer_service(g_millis);
}

// ----------------------------------------------------------------------------
// Straight key
// ----------------------------------------------------------------------------

static void test_straight_key_short_press_classifies_dit() {
  resetForTest("straight_key_short_press_classifies_dit");

  g_tipState = LOW;   // press
  core_keyer_service(g_millis);
  settleDebounce();
  CHECK(core_keyer_isTxActive());
  CHECK(g_keyDownCount == 1);

  g_millis += 20;   // well under the ~132ms dit/dah threshold at 18 WPM
  g_tipState = HIGH;  // release
  core_keyer_service(g_millis);
  settleDebounce();

  CHECK(!core_keyer_isTxActive());
  CHECK(g_keyUps.size() == 1);
  if (g_keyUps.size() == 1) CHECK(g_keyUps[0].type == ELEM_DIT);
}

static void test_straight_key_long_press_classifies_dah() {
  resetForTest("straight_key_long_press_classifies_dah");

  g_tipState = LOW;
  core_keyer_service(g_millis);
  settleDebounce();

  g_millis += 200;   // well over the ~132ms threshold
  g_tipState = HIGH;
  core_keyer_service(g_millis);
  settleDebounce();

  CHECK(g_keyUps.size() == 1);
  if (g_keyUps.size() == 1) CHECK(g_keyUps[0].type == ELEM_DAH);
}

// ----------------------------------------------------------------------------
// WPM
// ----------------------------------------------------------------------------

static void test_wpm_sets_dit_length_and_clamps_to_range() {
  resetForTest("wpm_sets_dit_length_and_clamps_to_range");

  core_keyer_setWpm(20);
  CHECK(core_keyer_getWpm() == 20);
  CHECK(core_keyer_getDitLengthMs() == 1200UL / 20UL);

  core_keyer_setWpm(WPM_MIN - 1);
  CHECK(core_keyer_getWpm() == WPM_MIN);

  core_keyer_setWpm(WPM_MAX + 1);
  CHECK(core_keyer_getWpm() == WPM_MAX);
}

// ----------------------------------------------------------------------------
// Mode change mid-element - must silently discard, not complete, the
// in-progress element (resetKeyerState(), not elementComplete()).
// ----------------------------------------------------------------------------

static void test_mode_change_resets_inprogress_element() {
  resetForTest("mode_change_resets_inprogress_element");

  g_tipState = LOW;
  core_keyer_service(g_millis);
  settleDebounce();
  CHECK(core_keyer_isTxActive());

  core_keyer_setMode(MODE_PADDLE);   // was already MODE_STRAIGHT's default; force a real change
  CHECK(!core_keyer_isTxActive());
  CHECK(g_keyUps.empty());   // discarded, not completed - no key-up event

  core_keyer_setMode(MODE_STRAIGHT);
}

// ----------------------------------------------------------------------------
// Iambic paddle - Mode B's defining behavior: releasing a squeeze mid-
// element still sends one extra alternating element once the gap elapses.
// Mode A does not (the memory that causes it is only set in Mode B).
// ----------------------------------------------------------------------------

static void squeezeBothThenReleaseMidElement() {
  core_keyer_setMode(MODE_PADDLE);
  g_tipState = LOW;   // dit
  g_ringState = LOW;  // dah
  core_keyer_service(g_millis);
  settleDebounce();   // dit starts first (PHASE_IDLE: ditPressed checked first)

  unsigned long ditLen = core_keyer_getDitLengthMs();
  g_millis += ditLen / 2;   // mid-element, still sending the dit
  core_keyer_service(g_millis);

  g_tipState = HIGH;   // release both before the element completes
  g_ringState = HIGH;
  core_keyer_service(g_millis);
  settleDebounce();

  // Run the clock forward through the element's remaining time, then the
  // full inter-element gap, servicing along the way.
  for (int i = 0; i < 20; i++) {
    g_millis += ditLen / 2;
    core_keyer_service(g_millis);
  }
}

static void test_iambic_mode_b_sends_extra_element_after_midelement_release() {
  resetForTest("iambic_mode_b_sends_extra_element_after_midelement_release");
  core_keyer_setIambicMode(IAMBIC_MODE_B);

  squeezeBothThenReleaseMidElement();

  CHECK(g_keyUps.size() == 2);
  if (g_keyUps.size() == 2) {
    CHECK(g_keyUps[0].type == ELEM_DIT);
    CHECK(g_keyUps[1].type == ELEM_DAH);   // the extra alternating element
  }
  CHECK(!core_keyer_isTxActive());
}

static void test_iambic_mode_a_sends_no_extra_element_after_midelement_release() {
  resetForTest("iambic_mode_a_sends_no_extra_element_after_midelement_release");
  core_keyer_setIambicMode(IAMBIC_MODE_A);

  squeezeBothThenReleaseMidElement();

  CHECK(g_keyUps.size() == 1);
  if (g_keyUps.size() == 1) CHECK(g_keyUps[0].type == ELEM_DIT);
  CHECK(!core_keyer_isTxActive());
}

static void test_paddle_reversed_swaps_dit_and_dah_pins() {
  resetForTest("paddle_reversed_swaps_dit_and_dah_pins");
  core_keyer_setMode(MODE_PADDLE);
  core_keyer_setPaddleReversed(true);

  g_tipState = LOW;   // normally DIT, but reversed -> DAH
  core_keyer_service(g_millis);
  settleDebounce();

  unsigned long dahElementMs = core_keyer_getDitLengthMs() * 3UL;
  g_millis += dahElementMs + DEBOUNCE_MS + 1;
  core_keyer_service(g_millis);   // element completes
  g_tipState = HIGH;
  core_keyer_service(g_millis);
  settleDebounce();   // spacing gap elapses, no further paddle held -> idle

  CHECK(g_keyUps.size() == 1);
  if (g_keyUps.size() == 1) CHECK(g_keyUps[0].type == ELEM_DAH);
}

// ----------------------------------------------------------------------------
// Parameter clamping
// ----------------------------------------------------------------------------

static void test_weight_percent_clamps_to_range() {
  resetForTest("weight_percent_clamps_to_range");

  core_keyer_setWeightPercent(WEIGHT_MIN - 5);
  CHECK(core_keyer_getWeightPercent() == WEIGHT_MIN);

  core_keyer_setWeightPercent(WEIGHT_MAX + 5);
  CHECK(core_keyer_getWeightPercent() == WEIGHT_MAX);
}

static void test_volume_clamps_to_max() {
  resetForTest("volume_clamps_to_max");

  core_keyer_setVolume(VOLUME_MAX + 20);
  CHECK(core_keyer_getVolume() == VOLUME_MAX);
}

static void test_sidetone_freq_clamps_to_range() {
  resetForTest("sidetone_freq_clamps_to_range");

  core_keyer_setSidetoneFreq(SIDETONE_FREQ_MIN_HZ - 50);
  CHECK(core_keyer_getSidetoneFreq() == SIDETONE_FREQ_MIN_HZ);

  core_keyer_setSidetoneFreq(SIDETONE_FREQ_MAX_HZ + 500);
  CHECK(core_keyer_getSidetoneFreq() == SIDETONE_FREQ_MAX_HZ);
}

// ----------------------------------------------------------------------------
// Diagnostic tone - must not fight real keying for the buzzer.
// ----------------------------------------------------------------------------

static void test_diag_tone_refuses_while_real_key_active() {
  resetForTest("diag_tone_refuses_while_real_key_active");

  g_tipState = LOW;
  core_keyer_service(g_millis);
  settleDebounce();
  CHECK(core_keyer_isTxActive());

  CHECK(core_keyer_diagToneStart(600) == false);
  CHECK(!core_keyer_isDiagToneActive());

  g_tipState = HIGH;
  core_keyer_service(g_millis);
  settleDebounce();

  CHECK(core_keyer_diagToneStart(600) == true);
  CHECK(core_keyer_isDiagToneActive());
  core_keyer_diagToneStop();
  CHECK(!core_keyer_isDiagToneActive());
}

int main() {
  test_straight_key_short_press_classifies_dit();
  test_straight_key_long_press_classifies_dah();
  test_wpm_sets_dit_length_and_clamps_to_range();
  test_mode_change_resets_inprogress_element();
  test_iambic_mode_b_sends_extra_element_after_midelement_release();
  test_iambic_mode_a_sends_no_extra_element_after_midelement_release();
  test_paddle_reversed_swaps_dit_and_dah_pins();
  test_weight_percent_clamps_to_range();
  test_volume_clamps_to_max();
  test_sidetone_freq_clamps_to_range();
  test_diag_tone_refuses_while_real_key_active();

  if (g_failures == 0) {
    std::printf("OK - all native keyer tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "%d assertion(s) failed\n", g_failures);
  return 1;
}
