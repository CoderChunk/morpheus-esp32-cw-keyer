// Native (host g++) test of the ACTUAL firmware trainer - compiles and
// links firmware/MORPHEUS/core_trainer.cpp itself, not a
// reimplementation. Regression coverage for the EXAM-completion bug:
// sessionActive and phase=DRILL_EXAM_DONE flip together in the same
// transition, which meant BLE's buildTrainStateJson() (ble_control.cpp,
// not natively testable here - it pulls in the full NimBLE stack) saw
// !isSessionActive() and never reported the exam result, and
// core_trainer_confirmPressed() (called from BLE's train_confirm) hit
// its `if (!sessionActive) return;` guard and silently no-op'd forever.
// This file exercises the real core_trainer.cpp state machine directly;
// see docs/ or the fix commit for the real-hardware BLE verification of
// the ble_control.cpp half (which can't link natively).
//
// Build/run: tests/native/run.sh (invoked from the repo root, or see
// docs/build.md).

#include "core_trainer.h"
#include "core_decoder.h"      // for TrainingCharSink
#include "core_morseplayer.h"  // for MorsePlayer
#include "config.h"

#include <cstdio>
#include <cstring>

// ----------------------------------------------------------------------------
// Fake clock/RNG - core_trainer.cpp's own test doubles, matching the
// pattern established in test_core_decoder.cpp.
// ----------------------------------------------------------------------------
static unsigned long g_millis = 0;
unsigned long millis() { return g_millis; }

// Deterministic: always returns min, so generateNextTarget()'s choice of
// character is repeatable and the test can read it back via
// core_trainer_getTargetText() rather than needing to predict it.
long random(long min, long max) { (void)max; return min; }
void randomSeed(unsigned long seed) { (void)seed; }

// ----------------------------------------------------------------------------
// core_keyer.h test doubles - core_trainer.cpp only calls these two.
// ----------------------------------------------------------------------------
static unsigned long g_ditLengthMs = 80;
static int g_wpm = 15;
unsigned long core_keyer_getDitLengthMs() { return g_ditLengthMs; }
int core_keyer_getWpm() { return g_wpm; }

// ----------------------------------------------------------------------------
// core_morseplayer.h test doubles - core_trainer.cpp only drives the
// player through these five calls; playback progress itself is
// irrelevant to the exam-completion logic under test, so "active" is
// the only piece of state that needs to behave believably.
// ----------------------------------------------------------------------------
void core_morseplayer_init(MorsePlayer &p) { p.active = false; }
bool core_morseplayer_start(MorsePlayer &p, const char *text,
                             unsigned long elementDitMs, unsigned long gapDitMs) {
  (void)text; (void)elementDitMs; (void)gapDitMs;
  // Pretends playback finishes instantly: this test cares about the
  // exam-completion/confirm state machine, not real Morse playback
  // timing. A subsequent core_trainer_service() call then observes
  // !isActive() and drives the real PLAYING -> LISTENING transition
  // (core_trainer.cpp), exactly as the real service loop would once
  // actual playback completes.
  p.active = false;
  return true;
}
void core_morseplayer_stop(MorsePlayer &p) { p.active = false; }
void core_morseplayer_service(MorsePlayer &p, unsigned long now) { (void)p; (void)now; }
bool core_morseplayer_isActive(const MorsePlayer &p) { return p.active; }

// ----------------------------------------------------------------------------
// core_decoder.h test double - captures the training sink core_trainer
// registers, so the test can simulate "the operator copied this
// character" by invoking it directly, exactly like a real decoded
// keystroke would (core_decoder.cpp is not linked into this binary -
// see test_core_decoder.cpp for that module's own native test).
// ----------------------------------------------------------------------------
static TrainingCharSink g_capturedSink = nullptr;
void core_decoder_setTrainingSink(TrainingCharSink sink) { g_capturedSink = sink; }

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
  g_capturedSink = nullptr;
  core_trainer_stopSession();   // idempotent, clears sessionActive/phase
}

// Drives one EXAM round to completion by feeding `correctCount` correct
// answers followed by (TRAIN_EXAM_LENGTH - correctCount) incorrect
// ones - deterministic since random() always returns the low end, so
// the target is always the same known character each round.
static void driveExamToCompletion(uint8_t correctCount) {
  core_trainer_startSession(TRAIN_MODE_EXAM);
  CHECK(g_capturedSink != nullptr);

  for (uint8_t i = 0; i < TRAIN_EXAM_LENGTH; i++) {
    // Each round starts in DRILL_PLAYING (startPlayback()); the real
    // service loop drives PLAYING -> LISTENING once playback finishes
    // (core_trainer_service() observing !core_morseplayer_isActive()).
    // onTrainingCharDecoded() only accepts answers in DRILL_LISTENING.
    core_trainer_service(g_millis);
    const char *target = core_trainer_getTargetText();
    char answer = (i < correctCount) ? target[0] : (target[0] == 'A' ? 'B' : 'A');
    g_capturedSink(answer, "?");
  }
}

// ----------------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------------

static void test_exam_completion_marks_result_ready_with_correct_score() {
  resetForTest("exam_completion_marks_result_ready_with_correct_score");

  // 23/25 = 92% - above TRAIN_EXAM_PASS_PCT (90).
  driveExamToCompletion(23);

  CHECK(core_trainer_getPhase() == DRILL_EXAM_DONE);
  CHECK(core_trainer_isSessionActive() == false);   // unchanged existing behavior
  CHECK(core_trainer_isExamResultReady() == true);
  CHECK(core_trainer_getExamTotalCount() == TRAIN_EXAM_LENGTH);
  CHECK(core_trainer_getExamCorrectCount() == 23);
  CHECK(core_trainer_getExamScorePercent() == 92);
  CHECK(core_trainer_getExamPassed() == true);
}

static void test_exam_completion_below_pass_threshold() {
  resetForTest("exam_completion_below_pass_threshold");

  // 20/25 = 80% - below TRAIN_EXAM_PASS_PCT (90).
  driveExamToCompletion(20);

  CHECK(core_trainer_getExamScorePercent() == 80);
  CHECK(core_trainer_getExamPassed() == false);
  CHECK(core_trainer_isExamResultReady() == true);
}

// The actual regression test for the reported bug: before the fix,
// core_trainer_confirmPressed() hit `if (!sessionActive) return;`
// immediately (sessionActive is already false once phase reaches
// DRILL_EXAM_DONE) and silently did nothing - examResultReady stayed
// true and phase stayed DRILL_EXAM_DONE forever, so BLE's train_confirm
// could never dismiss the result.
static void test_confirm_pressed_clears_exam_result_and_resets_to_idle() {
  resetForTest("confirm_pressed_clears_exam_result_and_resets_to_idle");

  driveExamToCompletion(25);
  CHECK(core_trainer_isExamResultReady() == true);
  CHECK(core_trainer_getPhase() == DRILL_EXAM_DONE);

  core_trainer_confirmPressed();

  CHECK(core_trainer_isExamResultReady() == false);
  CHECK(core_trainer_getPhase() == DRILL_IDLE);
}

// Regression test for a second exam-result-not-cleared bug found via
// this test file's own state leaking between tests: stopSession() (the
// train_stop command) didn't clear examResultReady either, so stopping
// instead of confirming while phase==DRILL_EXAM_DONE left
// isExamResultReady() stuck true under a phase now reset to DRILL_IDLE
// - an inconsistent combination buildTrainStateJson()'s fix would have
// reported as "active":true with phase="IDLE", a shape no client expects.
static void test_stop_session_clears_exam_result_too() {
  resetForTest("stop_session_clears_exam_result_too");

  driveExamToCompletion(25);
  CHECK(core_trainer_isExamResultReady() == true);

  core_trainer_stopSession();

  CHECK(core_trainer_isExamResultReady() == false);
  CHECK(core_trainer_getPhase() == DRILL_IDLE);
  CHECK(core_trainer_isSessionActive() == false);
}

static void test_confirm_pressed_is_noop_when_fully_idle() {
  resetForTest("confirm_pressed_is_noop_when_fully_idle");

  CHECK(core_trainer_isSessionActive() == false);
  CHECK(core_trainer_isExamResultReady() == false);

  core_trainer_confirmPressed();   // must not crash or change anything observable

  CHECK(core_trainer_isSessionActive() == false);
  CHECK(core_trainer_isExamResultReady() == false);
  CHECK(core_trainer_getPhase() == DRILL_IDLE);
}

// Proves the new exam-done early-check in confirmPressed() doesn't
// disturb the pre-existing DRILL_FEEDBACK -> advanceRound() transition
// (a non-exam, mid-session confirm) - CHARACTERS mode never reaches
// DRILL_EXAM_DONE, so this exercises the untouched switch() path.
static void test_confirm_pressed_feedback_transition_unaffected_by_exam_check() {
  resetForTest("confirm_pressed_feedback_transition_unaffected_by_exam_check");

  core_trainer_startSession(TRAIN_MODE_CHARACTERS);
  CHECK(g_capturedSink != nullptr);
  core_trainer_service(g_millis);   // drive PLAYING -> LISTENING, see driveExamToCompletion()

  const char *target = core_trainer_getTargetText();
  g_capturedSink(target[0], "?");   // one correct character completes the (1-char) target

  CHECK(core_trainer_getPhase() == DRILL_FEEDBACK);
  CHECK(core_trainer_isExamResultReady() == false);

  core_trainer_confirmPressed();   // FEEDBACK -> advanceRound()

  CHECK(core_trainer_isSessionActive() == true);
  CHECK(core_trainer_getPhase() == DRILL_PLAYING);
}

int main() {
  test_exam_completion_marks_result_ready_with_correct_score();
  test_exam_completion_below_pass_threshold();
  test_confirm_pressed_clears_exam_result_and_resets_to_idle();
  test_stop_session_clears_exam_result_too();
  test_confirm_pressed_is_noop_when_fully_idle();
  test_confirm_pressed_feedback_transition_unaffected_by_exam_check();

  if (g_failures == 0) {
    std::printf("OK - all native trainer tests passed\n");
    return 0;
  }
  std::fprintf(stderr, "%d assertion(s) failed\n", g_failures);
  return 1;
}
