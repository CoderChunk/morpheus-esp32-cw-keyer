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
        self._driver.cancel()


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
        self._task = None
        self._device_iface = None
        self._cancelled = False

    def cancel(self):
        self._cancelled = True
        async def stop():
            if self._device_iface is not None:
                try:
                    await self._device_iface.call_cancel_pairing()
                except DBusError:
                    pass
            if self._task is not None and not self._task.done():
                self._task.cancel()
        self._loop.call_soon_threadsafe(lambda: asyncio.create_task(stop()))

    def submit_passkey(self, value: int):
        self._resolve(self._passkey_future, value)

    def submit_confirmation(self, accepted: bool):
        self._resolve(self._confirm_future, accepted)

    def _resolve(self, future, value):
        if future is None or future.done():
            return
        def resolve():
            if not future.done():
                future.set_result(value)
        self._loop.call_soon_threadsafe(resolve)

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

    async def run(self, target_name: str, target_address=None):
        self._task = asyncio.current_task()
        bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
        agent = _PairingAgentInterface(self)
        agent_mgr = None
        try:
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

            def matches(dev):
                if target_address:
                    address = dev.get("Address")
                    name = dev.get("Name")
                    services = dev.get("UUIDs")
                    morpheus = (name is not None and name.value.upper().startswith("MORPHEUS")) or (services is not None and "7a48a2b0-0001-4ad4-9f1a-1c2d3e4f5a6b" in [u.lower() for u in services.value])
                    return morpheus and address is not None and address.value.upper() == target_address.upper()
                name = dev.get("Name")
                return name is not None and name.value == target_name

            found_path = next(
                (p for p, ifaces in objects.items()
                 if "org.bluez.Device1" in ifaces and matches(ifaces["org.bluez.Device1"])),
                None,
            )

            if found_path is None:
                found_event = asyncio.Event()
                found_holder = {}

                def on_interfaces_added(path, interfaces):
                    dev = interfaces.get("org.bluez.Device1")
                    if dev and matches(dev):
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
            self._device_iface = device_iface
            props_iface = dev_obj.get_interface("org.freedesktop.DBus.Properties")

            try:
                already_paired = await props_iface.call_get("org.bluez.Device1", "Paired")
                if not already_paired.value:
                    await asyncio.wait_for(device_iface.call_pair(), timeout=90)
                await props_iface.call_set("org.bluez.Device1", "Trusted", Variant("b", True))
            except DBusError as exc:
                self._on_failed("PAIRING_FAILED", str(exc))
                return

            self._on_succeeded(found_path)

        finally:
            if agent_mgr is not None:
                try:
                    await agent_mgr.call_unregister_agent(AGENT_PATH)
                except DBusError:
                    pass
            bus.unexport(AGENT_PATH)
            bus.disconnect()
            self._device_iface = None
            self._task = None


async def paired_devices():
    """Read actual host-side BlueZ bonds; no PINs or keys are returned."""
    bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
    try:
        intr = await bus.introspect(BLUEZ, "/")
        objects = await bus.get_proxy_object(BLUEZ, "/", intr).get_interface(
            "org.freedesktop.DBus.ObjectManager").call_get_managed_objects()
        result = []
        for path, interfaces in objects.items():
            dev = interfaces.get("org.bluez.Device1", {})
            name = dev.get("Name") or dev.get("Alias")
            address = dev.get("Address")
            paired = dev.get("Paired")
            uuids = dev.get("UUIDs")
            is_morpheus = ((name and name.value.upper().startswith("MORPHEUS")) or
                           (uuids and "7a48a2b0-0001-4ad4-9f1a-1c2d3e4f5a6b" in uuids.value))
            if is_morpheus and address and paired and paired.value:
                result.append(dict(identifier=address.value, name=name.value if name else "MORPHEUS-CW", paired=True))
        return result
    finally:
        bus.disconnect()


async def remove_pairing(address):
    """Remove one exact BlueZ device/bond via its owning adapter."""
    bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
    try:
        intr = await bus.introspect(BLUEZ, "/")
        objects = await bus.get_proxy_object(BLUEZ, "/", intr).get_interface(
            "org.freedesktop.DBus.ObjectManager").call_get_managed_objects()
        for path, interfaces in objects.items():
            dev = interfaces.get("org.bluez.Device1", {})
            identifier = dev.get("Address")
            if identifier and identifier.value.upper() == address.upper():
                adapter_path = dev["Adapter"].value
                intr = await bus.introspect(BLUEZ, adapter_path)
                adapter = bus.get_proxy_object(BLUEZ, adapter_path, intr).get_interface("org.bluez.Adapter1")
                await adapter.call_remove_device(path)
                return
        # Already absent is a successful, idempotent removal.
    finally:
        bus.disconnect()
