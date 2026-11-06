"""Read-only Windows presentation health for one exact EXE/map-bound process.

Samples counters and this process's window state without suspending threads.
If used during FPS runs, use the same observer in every comparison arm.
Absent OS-window presentation is not a guest deadlock.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import importlib.util
import json
from pathlib import Path
import re
import struct
import time

spec=importlib.util.spec_from_file_location('process_work',Path(__file__).with_name('measure-process-work.py'))
work=importlib.util.module_from_spec(spec);spec.loader.exec_module(work)
FIELDS={'g_render_perf_serial':8,'g_render_perf_epoch':8,'g_frames_presented':4,
        's_fifo_getoff':4,'s_flip_request_count':4,'s_fifo_flips_reached':4,
        's_fifo_flips_completed':4,'s_flip_pending':4}

def classify(before,after):
    if after['counters']['g_render_perf_serial']>before['counters']['g_render_perf_serial']:
        return 'presenting'
    windows=after['windows']
    if windows and all(x['iconic'] or x['width']<=0 or x['height']<=0 for x in windows):
        return 'window_unavailable'
    if after['counters']['g_frames_presented']>before['counters']['g_frames_presented']:
        return 'guest_present_calls_without_window_present'
    if after['counters']['s_flip_request_count']>before['counters']['s_flip_request_count']:
        return 'guest_flips_without_present_calls'
    return 'no_progress_observed'

def validate_window(records,start_ms,end_ms,width,height,max_gap_ms=2000):
    """Reject missing/minimized/resized observations in a timed FPS window.

    This validates sampled visibility and coverage, not guest correctness or
    activity between samples. FPS-tail and EXE-binding checks are separate.
    """
    if not 0<=start_ms<end_ms or width<=0 or height<=0 or max_gap_ms<=0:
        raise ValueError('Invalid presentation window')
    samples=[x for x in records if x.get('elapsed_ms') is not None and start_ms<=x['elapsed_ms']<end_ms]
    if not samples:return False
    if samples[0]['elapsed_ms']-start_ms>=max_gap_ms or end_ms-samples[-1]['elapsed_ms']>=max_gap_ms:return False
    if any(not 0<b['elapsed_ms']-a['elapsed_ms']<max_gap_ms for a,b in zip(samples,samples[1:])):return False
    return all(x.get('windows') and all(not w['iconic'] and w['width']==width and w['height']==height for w in x['windows']) for x in samples)

def cpu_load_delta(before,after):
    """Aggregate activity, not attribution to unrelated processes or GPU load."""
    if before['processor_groups']!=1 or after['processor_groups']!=1:return None
    fields=('time_ns','system_total_ns','system_idle_ns','game_cpu_ns','game_cycles')
    delta={key:after[key]-before[key] for key in fields}
    if delta['time_ns']<=0 or any(value<0 for value in delta.values()):raise ValueError('CPU counters reset')
    active=delta['system_total_ns']-delta['system_idle_ns']
    if active<0:raise ValueError('Invalid system CPU counters')
    return {'system_busy_cores':active/delta['time_ns'],
            'game_busy_cores':delta['game_cpu_ns']/delta['time_ns'],
            'other_busy_cores_estimate':max(0,active-delta['game_cpu_ns'])/delta['time_ns'],
            'game_cycles_per_cpu_ns':delta['game_cycles']/delta['game_cpu_ns'] if delta['game_cpu_ns'] else None}

def parse_session_info(info,protocol):
    # Windows x64 SDK ABI, checked with sizeof/offsetof against WtsApi32.h.
    # Retain no user, domain, station name or client address from the buffer.
    if len(info)<232 or len(protocol)<2 or struct.unpack_from('<I',info)[0]!=1:
        raise ValueError('Unsupported WTS session record')
    sid,state,flags=struct.unpack_from('<IIi',info,8)
    connected,disconnected=struct.unpack_from('<qq',info,176)
    return dict(session_id=sid,state=state,flags=flags,protocol=struct.unpack_from('<H',protocol)[0],
                connected_100ns=connected,disconnected_100ns=disconnected)

def session_window_signature(records,start_ms,end_ms,policy='unlocked'):
    """Bind one stable session under an explicitly selected lock policy.

    Locked benchmarking is supported when rendering keeps progressing. Never
    pool locked/unlocked, console/RDP or connected/disconnected signatures.
    Window/progress coverage, frame tails and scene checks remain separate.
    """
    if policy not in ('unlocked','locked'):raise ValueError('Unknown session policy')
    rows=[x for x in records if x.get('elapsed_ms') is not None and start_ms<=x['elapsed_ms']<end_ms]
    if not rows or rows[0]['elapsed_ms']-start_ms>=2000 or end_ms-rows[-1]['elapsed_ms']>=2000:return None
    fields=('session_id','state','flags','protocol','connected_100ns','disconnected_100ns')
    signatures=[]
    for row in rows:
        s=row.get('session',{})
        if any(s.get(key) is None for key in fields) or s['protocol'] not in (0,2):return None
        if policy=='unlocked':
            if s['state']!=0 or s['flags']!=1:return None
        elif s['state'] not in (0,4) or s['flags']!=0:return None
        signatures.append(tuple(s[key] for key in fields))
    return signatures[0] if all(x==signatures[0] for x in signatures) else None

class SessionState:
    def __init__(self,pid):
        self.k=c.WinDLL('kernel32',use_last_error=True)
        self.k.ProcessIdToSessionId.restype=w.BOOL
        self.k.ProcessIdToSessionId.argtypes=[w.DWORD,c.POINTER(w.DWORD)]
        self.sid=w.DWORD()
        if not self.k.ProcessIdToSessionId(pid,c.byref(self.sid)):raise c.WinError(c.get_last_error())
        self.api=c.WinDLL('wtsapi32',use_last_error=True)
        self.api.WTSQuerySessionInformationW.restype=w.BOOL
        self.api.WTSQuerySessionInformationW.argtypes=[w.HANDLE,w.DWORD,c.c_int,c.POINTER(c.c_void_p),c.POINTER(w.DWORD)]
        self.api.WTSFreeMemory.restype=None;self.api.WTSFreeMemory.argtypes=[c.c_void_p]
    def query(self,kind):
        data=c.c_void_p();size=w.DWORD()
        if not self.api.WTSQuerySessionInformationW(None,self.sid.value,kind,c.byref(data),c.byref(size)):
            raise c.WinError(c.get_last_error())
        try:return c.string_at(data,size.value)
        finally:self.api.WTSFreeMemory(data)
    def snapshot(self):
        result=parse_session_info(self.query(25),self.query(16))
        if result['session_id']!=self.sid.value:raise ValueError('WTS session identity mismatch')
        return result

class Observer:
    def __init__(self,pid,exe,link_map,cpu_load=False,session_state=False):
        self.binding=work.make_binding(exe,link_map)
        self.target=work.NativeProcess(pid,exe)
        self.cpu_load=cpu_load
        self.session=SessionState(pid) if session_state else None
        if cpu_load:
            self.target.k.GetSystemTimes.restype=w.BOOL
            self.target.k.GetSystemTimes.argtypes=[c.POINTER(w.FILETIME)]*3
            self.target.k.GetActiveProcessorGroupCount.restype=w.WORD
            self.target.k.GetActiveProcessorGroupCount.argtypes=[]
        self.user=c.WinDLL('user32',use_last_error=True)
        self.callback_type=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
        for name,result,args in [
            ('EnumWindows',w.BOOL,[self.callback_type,w.LPARAM]),
            ('GetWindowThreadProcessId',w.DWORD,[w.HWND,c.POINTER(w.DWORD)]),
            ('GetClientRect',w.BOOL,[w.HWND,c.POINTER(w.RECT)]),
            ('IsIconic',w.BOOL,[w.HWND]),('IsWindowVisible',w.BOOL,[w.HWND])]:
            fn=getattr(self.user,name);fn.restype=result;fn.argtypes=args
        text=Path(link_map).read_text(errors='replace')
        base=int(re.search(r'Preferred load address is\s+([0-9a-fA-F]+)',text)[1],16)
        self.symbols={}
        for name,size in FIELDS.items():
            hits=re.findall(r'^\s+[0-9a-fA-F]{4}:[0-9a-fA-F]+\s+'+re.escape(name)+r'\s+([0-9a-fA-F]{16})\s+.*$',text,re.M)
            if len(hits)!=1:raise ValueError(f'Need unique counter {name}')
            rva=int(hits[0],16)-base
            if not 0<=rva<=self.binding['image_size']-size:raise ValueError('Counter outside image')
            self.symbols[name]=(rva,size)
    def windows(self):
        rows=[]
        def callback(hwnd,unused):
            pid=w.DWORD();self.user.GetWindowThreadProcessId(hwnd,c.byref(pid))
            if pid.value==self.target.pid and self.user.IsWindowVisible(hwnd):
                rect=w.RECT()
                if self.user.GetClientRect(hwnd,c.byref(rect)):
                    rows.append(dict(handle=int(hwnd),iconic=bool(self.user.IsIconic(hwnd)),
                                     width=rect.right-rect.left,height=rect.bottom-rect.top))
            return True
        self.user.EnumWindows(self.callback_type(callback),0)
        return rows
    def snapshot(self):
        counters={}
        for name,(rva,size) in self.symbols.items():
            buf=(c.c_ubyte*size)();got=c.c_size_t()
            self.target.check(self.target.k.ReadProcessMemory(self.target.process,self.target.base+rva,buf,size,c.byref(got)))
            if got.value!=size:raise ValueError('Partial counter read')
            counters[name]=int.from_bytes(bytes(buf),'little')
        now=self.target.now();epoch=counters['g_render_perf_epoch']
        row=dict(elapsed_ms=(now-epoch)/1e6 if epoch else None,counters=counters,windows=self.windows())
        if self.session:
            try:row['session']=self.session.snapshot()
            except (OSError,ValueError) as error:row['session']={'error':str(error)}
        if self.cpu_load:
            idle,kernel,user=(w.FILETIME() for _ in range(3))
            self.target.check(self.target.k.GetSystemTimes(c.byref(idle),c.byref(kernel),c.byref(user)))
            def ns(t):return ((int(t.dwHighDateTime)<<32)|int(t.dwLowDateTime))*100
            game=self.target.cpu(self.target.process)
            row['cpu_load']={'time_ns':self.target.now(),'system_total_ns':ns(kernel)+ns(user),
                'system_idle_ns':ns(idle),'game_cpu_ns':game['cpu_ns'],'game_cycles':game['cycles'],
                'processor_groups':int(self.target.k.GetActiveProcessorGroupCount())}
        return row

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pid',type=int,required=True);p.add_argument('--exe',type=Path,required=True)
    p.add_argument('--map',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--seconds',type=float,default=175)
    p.add_argument('--cpu-load',action='store_true',help='Also record aggregate system/game CPU counters (no unrelated process enumeration)')
    p.add_argument('--session-state',action='store_true',help='Record console/RDP connection and lock state for the target session')
    a=p.parse_args()
    if not 1<=a.seconds<=900:raise ValueError('Invalid duration')
    observer=Observer(a.pid,a.exe,a.map,a.cpu_load,a.session_state)
    try:
        with a.output.open('x') as f:
            f.write(json.dumps({'binding':observer.binding,'method':'ReadProcessMemory + own-PID window queries, no suspension'})+'\n')
            previous=None;end=time.monotonic()+a.seconds
            while time.monotonic()<end and observer.target.alive():
                row=observer.snapshot()
                if previous:row['state']=classify(previous,row)
                f.write(json.dumps(row)+'\n');f.flush();previous=row
                time.sleep(.5)
    finally:observer.target.close()

if __name__=='__main__':main()
