"""
BlueZ Agent1 D-Bus pairing implementation - split into its own module so
that importing it is the *only* thing gated on dbus-next being
installed. backend.py imports this lazily, inside a try/except, so a
machine without dbus-next (or on non-Linux) can still use every other
part of MorpheusBackend; only pairing becomes unavailable.

This module intentionally has no PySide6/Qt import - see backend.py's
module docstring for the overall transport-agnostic design.
"""

import asyncio

from dbus_next import BusType, Variant
from dbus_next.aio import MessageBus
from dbus_next.errors import DBusError
from dbus_next.service import ServiceInterface, method

AGENT_PATH = "/com/morpheus/bleclient/agent"
BLUEZ = "org.bluez"
DISCOVERY_TIMEOUT_S = 15.0


class _PairingAgentInterface(ServiceInterface):
    """org.bluez.Agent1 implementation. Methods that need a human answer
    await an asyncio.Future that `driver` resolves via its
    await_passkey_entry()/await_confirmation() callbacks."""

    def __init__(self, driver: "PairingDriver"):
        super().__init__("org.bluez.Agent1")
        self._driver = driver

    @method()
    def Release(self):
        pass

    @method()
    async def RequestPasskey(self, device: "o") -> "u":  # noqa: F821
        return await self._driver.await_passkey_entry(device)

    @method()
    def DisplayPasskey(self, device: "o", passkey: "u", entered: "q"):  # noqa: F821
        self._driver.on_passkey_display(device, passkey)

    @method()
    async def RequestConfirmation(self, device: "o", passkey: "u"):  # noqa: F821
        accepted = await self._driver.await_confirmation(device, passkey)
        if not accepted:
            raise DBusError("org.bluez.Error.Rejected", "Rejected by user")

    @method()
    async def RequestAuthorization(self, device: "o"):  # noqa: F821
        return None

    @method()
    def AuthorizeService(self, device: "o", uuid: "s"):  # noqa: F821
        return None

    @method()
    def Cancel(self):
        pass


class PairingDriver:
    """Callback-based driver for one pairing attempt. `backend.py`
    supplies plain callables; this module owns every dbus-next detail."""

    def __init__(self, loop, on_waiting_passkey, on_waiting_confirmation,
                 on_display_passkey, on_succeeded, on_failed):
        self._loop = loop
        self._on_waiting_passkey = on_waiting_passkey
        self._on_waiting_confirmation = on_waiting_confirmation
        self._on_display_passkey = on_display_passkey
        self._on_succeeded = on_succeeded
        self._on_failed = on_failed
        self._passkey_future = None
        self._confirm_future = None

    def submit_passkey(self, value: int):
        self._resolve(self._passkey_future, value)

    def submit_confirmation(self, accepted: bool):
        self._resolve(self._confirm_future, accepted)

    def _resolve(self, future, value):
        if future is None or future.done():
            return
        self._loop.call_soon_threadsafe(future.set_result, value)

    async def await_passkey_entry(self, device_path: str) -> int:
        self._passkey_future = self._loop.create_future()
        self._on_waiting_passkey(device_path)
        try:
            return await self._passkey_future
        finally:
            self._passkey_future = None

    async def await_confirmation(self, device_path: str, passkey: int) -> bool:
        self._confirm_future = self._loop.create_future()
        self._on_waiting_confirmation(device_path, passkey)
        try:
            return await self._confirm_future
        finally:
            self._confirm_future = None

    def on_passkey_display(self, device_path: str, passkey: int) -> None:
        self._on_display_passkey(device_path, passkey)

    async def run(self, target_name: str):
        bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
        agent = _PairingAgentInterface(self)
        bus.export(AGENT_PATH, agent)

        bluez_intr = await bus.introspect(BLUEZ, "/org/bluez")
        bluez_obj = bus.get_proxy_object(BLUEZ, "/org/bluez", bluez_intr)
        agent_mgr = bluez_obj.get_interface("org.bluez.AgentManager1")
        await agent_mgr.call_register_agent(AGENT_PATH, "KeyboardDisplay")
        await agent_mgr.call_request_default_agent(AGENT_PATH)

        root_intr = await bus.introspect(BLUEZ, "/")
        root_obj = bus.get_proxy_object(BLUEZ, "/", root_intr)
        om = root_obj.get_interface("org.freedesktop.DBus.ObjectManager")
        objects = await om.call_get_managed_objects()

        adapter_path = next((p for p, ifaces in objects.items() if "org.bluez.Adapter1" in ifaces), None)
        if adapter_path is None:
            self._on_failed("PAIRING_FAILED", "No Bluetooth adapter found")
            return

        adapter_intr = await bus.introspect(BLUEZ, adapter_path)
        adapter_obj = bus.get_proxy_object(BLUEZ, adapter_path, adapter_intr)
        adapter = adapter_obj.get_interface("org.bluez.Adapter1")

        found_path = next(
            (p for p, ifaces in objects.items()
             if "org.bluez.Device1" in ifaces and ifaces["org.bluez.Device1"].get("Name")
             and ifaces["org.bluez.Device1"]["Name"].value == target_name),
            None,
        )

        if found_path is None:
            found_event = asyncio.Event()
            found_holder = {}

            def on_interfaces_added(path, interfaces):
                dev = interfaces.get("org.bluez.Device1")
                if dev and dev.get("Name") and dev["Name"].value == target_name:
                    found_holder["path"] = path
                    found_event.set()

            om.on_interfaces_added(on_interfaces_added)
            await adapter.call_start_discovery()
            try:
                await asyncio.wait_for(found_event.wait(), timeout=DISCOVERY_TIMEOUT_S)
                found_path = found_holder.get("path")
            except asyncio.TimeoutError:
                pass
            finally:
                try:
                    await adapter.call_stop_discovery()
                except DBusError:
                    pass
                om.off_interfaces_added(on_interfaces_added)

        if found_path is None:
            self._on_failed("DEVICE_NOT_FOUND", f"{target_name} not found within {DISCOVERY_TIMEOUT_S:.0f}s")
            return

        dev_intr = await bus.introspect(BLUEZ, found_path)
        dev_obj = bus.get_proxy_object(BLUEZ, found_path, dev_intr)
        device_iface = dev_obj.get_interface("org.bluez.Device1")
        props_iface = dev_obj.get_interface("org.freedesktop.DBus.Properties")

        try:
            already_paired = await props_iface.call_get("org.bluez.Device1", "Paired")
            if not already_paired.value:
                await device_iface.call_pair()
            await props_iface.call_set("org.bluez.Device1", "Trusted", Variant("b", True))
        except DBusError as exc:
            self._on_failed("PAIRING_FAILED", str(exc))
            return

        self._on_succeeded(found_path)
