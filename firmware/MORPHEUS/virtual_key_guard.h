#ifndef MORPHEUS_VIRTUAL_KEY_GUARD_H
#define MORPHEUS_VIRTUAL_KEY_GUARD_H

// A BLE client holds the virtual key with key_down and releases it with key_up. If the
// link drops in between, key_up can never arrive: the key would stay "down" (and every
// set_keyer be refused as "keyer busy") until a later client happened to send key_up.
// Found on real hardware (FND-03, 11/11 runs). The key is orphaned when it is down and
// there is no link left to release it.
inline bool virtualKeyOrphaned(bool keyDown, bool linkUp) { return keyDown && !linkUp; }

#endif
