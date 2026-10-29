"""Read-only XInput preview; never sends keys, rumble or focus changes."""
import ctypes
import os
import time


def foreground_owned() -> bool:
    if os.name != 'nt':
        return False
    user32 = ctypes.WinDLL('user32.dll', winmode=0x800)
    user32.GetForegroundWindow.restype = ctypes.c_void_p
    user32.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint32)]
    pid = ctypes.c_uint32()
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(pid))
    return pid.value == os.getpid()


class Gamepad(ctypes.Structure):
    _fields_ = [('buttons', ctypes.c_uint16), ('lt', ctypes.c_uint8), ('rt', ctypes.c_uint8),
                ('lx', ctypes.c_int16), ('ly', ctypes.c_int16), ('rx', ctypes.c_int16), ('ry', ctypes.c_int16)]


class State(ctypes.Structure):
    _fields_ = [('packet', ctypes.c_uint32), ('gamepad', Gamepad)]


class Pads:
    def __init__(self):
        self.api = None
        self.connected = [False] * 4
        self.retry = [0.0] * 4
        if os.name == 'nt':
            try:
                self.dll = ctypes.WinDLL('xinput1_4.dll', winmode=0x800)  # System32 only
                self.api = self.dll.XInputGetState
                self.api.argtypes = [ctypes.c_uint32, ctypes.POINTER(State)]
                self.api.restype = ctypes.c_uint32
            except OSError:
                pass

    def poll(self, active: bool) -> list[int]:
        result = [0] * 4
        if not active or self.api is None:
            return result
        now = time.monotonic()
        for n in range(4):
            if not self.connected[n] and now < self.retry[n]:
                continue
            state = State()
            self.connected[n] = self.api(n, ctypes.byref(state)) == 0
            if self.connected[n]:
                result[n] = state.gamepad.buttons
            else:
                self.retry[n] = now + 1.0
        return result
