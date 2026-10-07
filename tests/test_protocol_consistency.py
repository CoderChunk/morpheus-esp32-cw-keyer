"""Cross-layer consistency (integration): firmware <-> bridge <-> simulator <-> app.

Every layer re-states the same contract (UUIDs, command names, size cap, version).
These tests read the real source of each layer and fail when they drift apart.
The Flutter checks are skipped if morpheus_ui is not checked out alongside.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "firmware" / "MORPHEUS"
BLE = ROOT / "tools" / "ble_client"
UI = ROOT.parent / "morpheus_ui"
DEBUG_ONLY = {"dump_screen_start", "dump_screen_chunk"}  # FEATURE_DEBUG_SERIAL_COMMANDS


def firmware_commands():
    text = (FW / "ble_control.cpp").read_text()
    return set(re.findall(r'strcmp\(cmd,\s*"([a-z_]+)"\)', text))


def command_literals(text):
    """Command-like literals, minus anything the file treats as an incoming event."""
    lits = {s for s in re.findall(r"['\"]([a-z]+_[a-z_]+)['\"]", text)
            if s.split("_")[0] in ("key", "train", "game", "set", "get", "probe", "reset", "dump")}
    events = set(re.findall(r"case '([a-z_]+)'", text)) | set(re.findall(r'evt\s*==\s*"([a-z_]+)"', text))
    events |= set(re.findall(r'"evt"\s*:\s*"([a-z_]+)"', text))
    events |= set(re.findall(r"evt[^\n]{0,24}[=!]=\s*['\"]([a-z_]+)['\"]", text))
    return lits - events


class UuidTests(unittest.TestCase):
    def test_gatt_uuids_match_between_firmware_and_bridge(self):
        cfg = (FW / "config.h").read_text()
        py = (BLE / "protocol.py").read_text()
        for c_name, py_name in (("BLE_SERVICE_UUID", "SERVICE_UUID"), ("BLE_WORD_CHAR_UUID", "WORD_CHAR_UUID"),
                                ("BLE_CONTROL_CMD_UUID", "CONTROL_CMD_UUID"), ("BLE_CONTROL_EVT_UUID", "CONTROL_EVT_UUID"),
                                ("BLE_GAME_MORSE_UUID", "GAME_MORSE_UUID")):
            fw = re.search(rf'{c_name}\[\]\s*=\s*"([^"]+)"', cfg).group(1)
            br = re.search(rf'{py_name}\s*=\s*"([^"]+)"', py).group(1)
            self.assertEqual(fw, br, c_name)

    @unittest.skipUnless((UI / "lib").exists(), "morpheus_ui not checked out")
    def test_flutter_client_uses_the_same_uuid_scheme(self):
        dart = (UI / "lib/services/native_ble_morpheus_client.dart").read_text()
        cfg = (FW / "config.h").read_text()
        svc = re.search(r'BLE_SERVICE_UUID\[\]\s*=\s*"([^"]+)"', cfg).group(1)
        # The Dart client builds 7a48a2b0-000<n>-4ad4-9f1a-1c2d3e4f5a6b for n = 1..5.
        self.assertIn("7a48a2b0-000${id.toRadixString(16)}-4ad4-9f1a-1c2d3e4f5a6b", dart)
        self.assertEqual(svc, "7a48a2b0-0001-4ad4-9f1a-1c2d3e4f5a6b")


class CommandVocabularyTests(unittest.TestCase):
    def test_firmware_vocabulary_is_the_documented_set(self):
        self.assertEqual(firmware_commands() - DEBUG_ONLY, {
            "key_down", "key_up", "train_start", "train_stop", "train_confirm", "train_answer", "game_start",
            "game_stop", "game_pause", "game_confirm", "game_restart", "set_keyer", "probe_keyer",
            "reset_keyer_metrics", "get_device_info"})

    def test_bridge_only_sends_commands_the_firmware_understands(self):
        sent = command_literals((BLE / "backend.py").read_text())
        self.assertFalse(sent - firmware_commands(), f"bridge sends unknown commands: {sent - firmware_commands()}")

    def test_simulator_covers_the_whole_firmware_vocabulary(self):
        handled = command_literals((ROOT / "tests/e2e/sim_bridge.py").read_text())
        missing = firmware_commands() - DEBUG_ONLY - handled
        self.assertFalse(missing, f"simulator lacks: {missing}")

    def test_simulator_enums_match_firmware_parsers(self):
        sim = (ROOT / "tests/e2e/sim_bridge.py").read_text()
        ctl = (FW / "ble_control.cpp").read_text()
        for mode in ("KOCH", "CHARACTERS", "WORDS", "CALLSIGNS", "ADAPTIVE", "EXAM", "LISTENING", "COMBINED"):
            self.assertIn(f'"{mode}"', ctl, mode)
            self.assertIn(f'"{mode}"', sim, mode)
        for game in ("COPY", "MEMORY", "SPEED"):
            self.assertIn(f'"{game}"', ctl, game)

    def test_command_size_cap_agrees(self):
        cap = int(re.search(r"BLE_CONTROL_CMD_CAP\s*=\s*(\d+)", (FW / "config.h").read_text()).group(1))
        self.assertEqual(cap, int(re.search(r"CMD_CAP\s*=\s*(\d+)", (ROOT / "tests/e2e/sim_bridge.py").read_text()).group(1)))

    @unittest.skipUnless((UI / "lib").exists(), "morpheus_ui not checked out")
    def test_flutter_client_only_sends_commands_the_firmware_understands(self):
        sent = command_literals((UI / "lib/services/native_ble_morpheus_client.dart").read_text())
        self.assertTrue(sent, "no commands found in the Dart client")
        self.assertFalse(sent - firmware_commands(), f"app sends unknown commands: {sent - firmware_commands()}")

    @unittest.skipUnless((UI / "lib").exists(), "morpheus_ui not checked out")
    def test_settings_ranges_agree_between_firmware_and_app(self):
        hdr = (FW / "keyer_settings_protocol.h").read_text()
        dart = (UI / "lib/app/morpheus_session.dart").read_text()
        for field, lo, hi in (("wpm", 5, 40), ("tone", 200, 2000), ("volume", 0, 100), ("weight", 30, 70)):
            self.assertRegex(hdr, rf'"{field}"\)\)return value>={lo}&&value<={hi}')
            self.assertRegex(dart, rf"'{field}': \({lo}, {hi}\)")


class VersionTests(unittest.TestCase):
    def test_changelog_documents_the_running_firmware_version(self):
        ver = re.search(r'FIRMWARE_VERSION\[\]\s*=\s*"([^"]+)"', (FW / "config.h").read_text()).group(1)
        log = (ROOT / "CHANGELOG.md").read_text()
        self.assertRegex(log, rf"Source\s*{re.escape(ver)}\b", f"CHANGELOG has no entry for firmware {ver}")

    @unittest.skipUnless((UI / "lib").exists(), "morpheus_ui not checked out")
    def test_app_minimum_firmware_not_newer_than_this_firmware(self):
        ver = tuple(int(x) for x in re.search(r'FIRMWARE_VERSION\[\]\s*=\s*"([\d.]+)', (FW / "config.h").read_text()).group(1).split("."))
        m = re.search(r"minimumSupported = FirmwareVersion\((\d+), (\d+), (\d+)\)", (UI / "lib/models/firmware_version.dart").read_text())
        self.assertLessEqual(tuple(int(x) for x in m.groups()), ver)


if __name__ == "__main__":
    unittest.main()
