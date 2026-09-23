// Native (host g++) test of the ACTUAL firmware decoder - compiles and
// links firmware/MORPHEUS/core_decoder.cpp itself, not a reimplementation.
// tests/test_decoder_logic.py is a parallel Python model of the timing
// logic; a real regression in core_decoder.cpp would not be caught by it.
// This file closes that gap by exercising the real source directly.
//
// Build/run: tests/native/run.sh (invoked from the repo root, or see
// docs/build.md). No external test framework - a handful of CHECK()s
// and an explicit main(), matching this project's existing "no
// unnecessary dependencies" style.

#include "core_decoder.h"
#include "config.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ----------------------------------------------------------------------------
// Fake clock - every test sets this explicitly rather than relying on
// wall-clock time, so timing-boundary assertions are exact and repeatable.
// ----------------------------------------------------------------------------
static unsigned long g_millis = 0;
unsigned long millis() { return g_millis; }

// ----------------------------------------------------------------------------
// core_keyer.h test doubles - core_decoder.cpp only calls these two
// functions from the real (unmodified) core_keyer.h it includes.
// ----------------------------------------------------------------------------
static unsigned long g_ditLengthMs = 80;   // 1200/15 => 15 WPM, matches the old Python test's default
static bool g_txActive = false;
unsigned long core_keyer_getDitLengthMs() { return g_ditLengthMs; }
bool core_keyer_isTxActive() { return g_txActive; }

// ----------------------------------------------------------------------------
// events_onCharacterComplete/events_onWordComplete - declared in
// core_decoder.h, called by the real decoder. Captured here instead of
// wired to display/BLE/stats like MORPHEUS.ino does.
// ----------------------------------------------------------------------------
struct CharEvent { char decoded; std::string pattern; };
struct WordEvent { std::string word; unsigned long now; };
static std::vector<CharEvent> g_chars;
static std::vector<WordEvent> g_words;

void events_onCharacterComplete(char decodedChar, const char *pattern) {
  g_chars.push_back({decodedChar, pattern});
}
void events_onWordComplete(const char *word, unsigned long now) {
  g_words.push_back({std::string(word), now});
}

// ----------------------------------------------------------------------------
// Tiny assert-and-continue harness.
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
  g_ditLengthMs = 80;
  g_txActive = false;
  g_chars.clear();
  g_words.clear();
  core_decoder_setTrainingSink(nullptr);
  core_decoder_setEnabled(true);
  core_decoder_init();
}

static unsigned long charGapMs() { return (unsigned long)(g_ditLengthMs * CHAR_GAP_MULT); }
static unsigned long wordGapMs() { return (unsigned long)(g_ditLengthMs * WORD_GAP_MULT); }

// ----------------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------------

static void test_decodes_single_dit_after_char_gap() {
  resetForTest("decodes_single_dit_after_char_gap");

  core_decoder_addElement(ELEM_DIT, g_millis);
  g_millis += charGapMs();
  core_decoder_service(g_millis);

  CHECK(g_chars.size() == 1);
  CHECK(g_chars[0].decoded == 'E');
  CHECK(g_chars[0].pattern == ".");
  CHECK(strcmp(core_decoder_getWordBuffer(), "E") == 0);
  CHECK(core_decoder_getCharPatternLen() == 0);
}

static void test_decodes_sos_and_finalizes_word_after_word_gap() {
  resetForTest("decodes_sos_and_finalizes_word_after_word_gap");

  const char *pattern = "SOS";   // S=... O=--- S=...
  const char *elements[] = { "...", "---", "..." };
  unsigned long lastCharEndMs = 0;
  for (const char *elem : elements) {
    for (size_t i = 0; elem[i] != '\0'; i++) {
      core_decoder_addElement(elem[i] == '.' ? ELEM_DIT : ELEM_DAH, g_millis);
      g_millis += g_ditLengthMs;
    }
    lastCharEndMs = g_millis - g_ditLengthMs;   // timestamp of the last addElement() call
    g_millis += charGapMs();
    core_decoder_service(g_millis);
  }
  g_millis += wordGapMs();
  core_decoder_service(g_millis);

  CHECK(g_chars.size() == 3);
  if (g_chars.size() == 3) {
    CHECK(g_chars[0].decoded == 'S');
    CHECK(g_chars[1].decoded == 'O');
    CHECK(g_chars[2].decoded == 'S');
  }
  CHECK(g_words.size() == 1);
  if (g_words.size() == 1) {
    CHECK(g_words[0].word == pattern);
    CHECK(g_words[0].now == lastCharEndMs);
  }
  CHECK(strcmp(core_decoder_getWordBuffer(), "") == 0);
}

static void test_does_not_finalize_while_tx_active() {
  resetForTest("does_not_finalize_while_tx_active");

  core_decoder_addElement(ELEM_DAH, g_millis);
  g_millis += charGapMs();
  g_txActive = true;
  core_decoder_service(g_millis);

  CHECK(g_chars.empty());
  CHECK(strcmp(core_decoder_getCharPattern(), "-") == 0);
  CHECK(core_decoder_getCharPatternLen() == 1);
}

static void test_pattern_buffer_bounded_to_leave_null_terminator_space() {
  resetForTest("pattern_buffer_bounded_to_leave_null_terminator_space");

  for (int i = 0; i < MAX_PATTERN_LEN + 3; i++) {
    core_decoder_addElement(ELEM_DIT, (unsigned long)i);
  }

  CHECK(core_decoder_getCharPatternLen() == (uint8_t)(MAX_PATTERN_LEN - 1));
}

static void test_unknown_pattern_decodes_as_question_mark() {
  resetForTest("unknown_pattern_decodes_as_question_mark");

  // "----" (4 dahs) has no morseTable entry - MORSE_0 is 5 dahs, not 4.
  for (int i = 0; i < 4; i++) {
    core_decoder_addElement(ELEM_DAH, g_millis);
    g_millis += g_ditLengthMs;
  }
  g_millis += charGapMs();
  core_decoder_service(g_millis);

  CHECK(g_chars.size() == 1);
  if (g_chars.size() == 1) {
    CHECK(g_chars[0].decoded == '?');
    CHECK(g_chars[0].pattern == "----");
  }
}

static void test_reverse_lookup_matches_forward_table_and_is_case_insensitive() {
  resetForTest("reverse_lookup_matches_forward_table_and_is_case_insensitive");

  char buf[8];
  CHECK(core_decoder_lookupPattern('S', buf, sizeof(buf)));
  CHECK(strcmp(buf, "...") == 0);

  CHECK(core_decoder_lookupPattern('s', buf, sizeof(buf)));
  CHECK(strcmp(buf, "...") == 0);

  // Space has no table entry - forward and reverse lookup share one
  // table, so this exercises the same "no entry" path as '?' above.
  CHECK(!core_decoder_lookupPattern(' ', buf, sizeof(buf)));
  CHECK(buf[0] == '\0');
}

static void test_setEnabled_false_clears_in_progress_state() {
  resetForTest("setEnabled_false_clears_in_progress_state");

  core_decoder_addElement(ELEM_DIT, g_millis);
  CHECK(core_decoder_getCharPatternLen() == 1);

  core_decoder_setEnabled(false);
  CHECK(core_decoder_getCharPatternLen() == 0);
  CHECK(strcmp(core_decoder_getWordBuffer(), "") == 0);

  // Disabled: elements are ignored and gap timeouts aren't evaluated.
  core_decoder_addElement(ELEM_DAH, g_millis);
  g_millis += charGapMs() * 10;
  core_decoder_service(g_millis);
  CHECK(core_decoder_getCharPatternLen() == 0);
  CHECK(g_chars.empty());
}

static void test_training_sink_routes_chars_and_bypasses_normal_events() {
  resetForTest("training_sink_routes_chars_and_bypasses_normal_events");

  static std::vector<CharEvent> sinkChars;
  core_decoder_setTrainingSink([](char decoded, const char *pattern) {
    sinkChars.push_back({decoded, pattern});
  });

  core_decoder_addElement(ELEM_DIT, g_millis);
  g_millis += charGapMs();
  core_decoder_service(g_millis);

  CHECK(sinkChars.size() == 1);
  if (sinkChars.size() == 1) {
    CHECK(sinkChars[0].decoded == 'E');
    CHECK(sinkChars[0].pattern == ".");
  }
  // Training active: normal events_onCharacterComplete path and the
  // word buffer are deliberately untouched (core_decoder.cpp comment).
  CHECK(g_chars.empty());
  CHECK(strcmp(core_decoder_getWordBuffer(), "") == 0);

  core_decoder_setTrainingSink(nullptr);
}

int main() {
  test_decodes_single_dit_after_char_gap();
  test_decodes_sos_and_finalizes_word_after_word_gap();
  test_does_not_finalize_while_tx_active();
  test_pattern_buffer_bounded_to_leave_null_terminator_space();
  test_unknown_pattern_decodes_as_question_mark();
  test_reverse_lookup_matches_forward_table_and_is_case_insensitive();
  test_setEnabled_false_clears_in_progress_state();
  test_training_sink_routes_chars_and_bypasses_normal_events();

  if (g_failures == 0) {
    std::printf("OK - all native decoder tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "%d assertion(s) failed\n", g_failures);
  return 1;
}
