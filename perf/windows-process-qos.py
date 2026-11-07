"""Read or explicitly select QoS for one exact Windows game process.

Use read mode outside timed windows to bind policy to a benchmark. High mode
can apply the same policy to an immutable reference that predates the game's
BUZZ_WINDOWS_HIGH_QOS option. No priority, affinity or power-plan changes.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import sys


class Power(c.Structure):
    _fields_ = [('Version', w.ULONG), ('ControlMask', w.ULONG), ('StateMask', w.ULONG)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pid', type=int)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--expected-sha256', required=True)
    parser.add_argument('--mode', choices=('read', 'high', 'system'), default='read')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != 'win32':
        parser.error('This diagnostic requires Windows')
    if args.pid <= 0 or args.output.exists():
        parser.error('Require a positive PID and a new output path')
    digest = hashlib.sha256(args.exe.read_bytes()).hexdigest()
    if digest != args.expected_sha256.lower():
        parser.error('EXE hash differs from the required frozen identity')

    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
    kernel.GetProcessInformation.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD]
    kernel.SetProcessInformation.argtypes = [w.HANDLE, c.c_int, c.c_void_p, w.DWORD]
    kernel.GetPriorityClass.argtypes = [w.HANDLE]
    kernel.GetPriorityClass.restype = w.DWORD
    kernel.GetProcessAffinityMask.argtypes = [w.HANDLE, c.POINTER(c.c_size_t), c.POINTER(c.c_size_t)]
    handle = kernel.OpenProcess(0x1400 | (0x200 if args.mode != 'read' else 0), False, args.pid)
    if not handle:
        raise c.WinError(c.get_last_error())
    try:
        name = c.create_unicode_buffer(32768)
        size = w.DWORD(len(name))
        if not kernel.QueryFullProcessImageNameW(handle, 0, name, c.byref(size)):
            raise c.WinError(c.get_last_error())
        if Path(name.value).resolve() != args.exe.resolve():
            raise ValueError('PID belongs to a different executable')

        def query():
            state = Power(1, 0, 0)
            # ProcessPowerThrottling = 4, PROCESS_POWER_THROTTLING_STATE v1.
            if not kernel.GetProcessInformation(handle, 4, c.byref(state), c.sizeof(state)):
                raise c.WinError(c.get_last_error())
            return {field: getattr(state, field) for field, _ in state._fields_}

        def scheduling():
            affinity, system = c.c_size_t(), c.c_size_t()
            if not kernel.GetProcessAffinityMask(handle, c.byref(affinity), c.byref(system)):
                raise c.WinError(c.get_last_error())
            priority = kernel.GetPriorityClass(handle)
            if not priority:
                raise c.WinError(c.get_last_error())
            return dict(priority_class=priority, process_affinity=hex(affinity.value),
                        system_affinity=hex(system.value))

        before, schedule = query(), scheduling()
        if args.mode != 'read':
            mask = 1 if args.mode == 'high' else 0  # Execution-speed control only.
            policy = Power(1, mask, 0)
            if not kernel.SetProcessInformation(handle, 4, c.byref(policy), c.sizeof(policy)):
                raise c.WinError(c.get_last_error())
        after = query()
        if args.mode != 'read' and (after['ControlMask'] != mask or after['StateMask'] != 0):
            raise ValueError('Requested process power policy was not applied')
        if schedule != scheduling():
            raise ValueError('Priority or affinity changed during the diagnostic')
        result = dict(pid=args.pid, exe=str(args.exe.resolve()), exe_sha256=digest,
                      mode=args.mode, before=before, after=after, **schedule,
                      windows=str(sys.getwindowsversion()),
                      scope='One exact process; diagnostic policy change, not a native-code gain')
        with args.output.open('x') as output:
            json.dump(result, output, indent=2)
            output.write('\n')
        print(json.dumps(result))
    finally:
        kernel.CloseHandle(handle)


if __name__ == '__main__':
    main()
