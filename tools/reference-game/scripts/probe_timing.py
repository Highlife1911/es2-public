"""Read-only DBSIM timing sampler; observed changes are not a complete per-tick log."""
import argparse
import ctypes
import hashlib
import json
import math
import struct
import sys
import time
from collections import Counter
from pathlib import Path

REFERENCE_SHA256 = '8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad'
IMAGE_BASE, IMAGE_SIZE = 0x400000, 0x2D8000
CLOCK_ADDRESS, FRAME_ADDRESS = 0x4D3B74, 0x4D257B
UINT32_MASK = 0xFFFFFFFF
SIGNATURES = {
    0x4678C0: bytes.fromhex('53 56 BE 6C 3B 4D 00 8B 1D 74 3B 4D 00 83 C3 28 E8 17 F0 02 00 89 06 3B D8 7F F5'),
    0x46791A: bytes.fromhex('01 05 74 3B 4D 00 5E 5B C3'),
    0x45F176: bytes.fromhex('BB BC 24 4D 00'),
    0x45F467: bytes.fromhex('E8 54 84 00 00'),
    0x45F4AD: bytes.fromhex('FF 83 BF 00 00 00 EB A3'),
}


def unsigned_delta(previous, current):
    if not 0 <= previous <= UINT32_MASK or not 0 <= current <= UINT32_MASK:
        raise ValueError('Expected unsigned 32-bit values')
    return (current - previous) & UINT32_MASK


def classify_change(previous, current):
    """Half-range convention; arbitrary resets cannot all be identified."""
    delta = unsigned_delta(previous, current)
    if not delta:
        return 'unchanged', 0
    if delta >= 0x80000000:
        return 'discontinuity', None
    return ('wrap' if current < previous else 'advance'), delta


def interval_statistics(values):
    """Linear-interpolated percentiles; all tied modes are retained."""
    ordered = sorted(values)
    if not ordered:
        return dict(count=0, min=None, max=None, mean=None, p50=None, p90=None,
                    p95=None, p99=None, mode=None, mode_count=0, modes=[], mode_tie_count=0)

    def percentile(fraction):
        at = (len(ordered) - 1) * fraction
        lower, upper = int(at), min(int(at) + 1, len(ordered) - 1)
        return ordered[lower] + (ordered[upper] - ordered[lower]) * (at - lower)

    counts = Counter(ordered)
    mode_count = max(counts.values())
    modes = sorted(value for value, count in counts.items() if count == mode_count)
    return dict(count=len(ordered), min=ordered[0], max=ordered[-1],
                mean=sum(ordered) / len(ordered), p50=percentile(.5), p90=percentile(.9),
                p95=percentile(.95), p99=percentile(.99), mode=modes[0] if len(modes) == 1 else None,
                mode_count=mode_count, modes=modes[:16], mode_tie_count=len(modes))


def analyze_samples(samples, elapsed_ns):
    """Analyze (elapsed_perf_counter_ns, clock_u32, frame_u32) observations.

    Counter advance includes increments missed by polling, but does not recover
    their individual times/states. Stalls cover successful reads only.
    """
    samples = list(samples)
    if elapsed_ns < 0:
        raise ValueError('Negative observation duration')
    for index, (stamp, clock, frame) in enumerate(samples):
        if stamp < 0 or stamp > elapsed_ns:
            raise ValueError('Sample timestamp outside observation')
        if index and stamp <= samples[index - 1][0]:
            raise ValueError('Sample timestamps must increase strictly')
        unsigned_delta(clock, clock)
        unsigned_delta(frame, frame)
    clock_deltas, change_intervals, poll_intervals, frame_deltas = [], [], [], []
    clock_changes = frame_changes = clock_wraps = frame_wraps = 0
    clock_discontinuities = frame_discontinuities = 0
    clock_stall_ns = frame_stall_ns = longest_gap_ns = 0
    if samples:
        last_clock_change = last_frame_change = samples[0][0]
        for previous, current in zip(samples, samples[1:]):
            stamp, clock, frame = current
            old_stamp, old_clock, old_frame = previous
            gap = stamp - old_stamp
            longest_gap_ns = max(longest_gap_ns, gap)
            poll_intervals.append(gap / 1_000_000)
            if clock == old_clock:
                clock_stall_ns = max(clock_stall_ns, stamp - last_clock_change)
            else:
                clock_changes += 1
                kind, delta = classify_change(old_clock, clock)
                observed_ms = (stamp - last_clock_change) / 1_000_000
                # Treat a clock jump >1s beyond elapsed host time as an
                # initialization/discontinuity, rather than a cadence interval.
                if delta is not None and delta > observed_ms + 1000:
                    kind = 'discontinuity'
                if kind == 'discontinuity':
                    clock_discontinuities += 1
                else:
                    clock_wraps += kind == 'wrap'
                    clock_deltas.append(delta)
                    change_intervals.append(observed_ms)
                last_clock_change = stamp
            if frame == old_frame:
                frame_stall_ns = max(frame_stall_ns, stamp - last_frame_change)
            else:
                frame_changes += 1
                kind, delta = classify_change(old_frame, frame)
                if kind == 'discontinuity':
                    frame_discontinuities += 1
                else:
                    frame_wraps += kind == 'wrap'
                    frame_deltas.append(delta)
                last_frame_change = stamp
    covered_ns = samples[-1][0] - samples[0][0] if len(samples) > 1 else 0
    covered_seconds = covered_ns / 1_000_000_000
    advance = sum(frame_deltas)
    frame_hz = advance / covered_seconds if covered_seconds and not frame_discontinuities else None
    return {
        'samples': len(samples), 'elapsed_seconds': elapsed_ns / 1_000_000_000,
        'successful_sample_span_seconds': covered_seconds,
        'observed_tick_changes': clock_changes, 'valid_observed_clock_changes': len(clock_deltas),
        'effective_observed_clock_change_hz': len(clock_deltas) / covered_seconds if covered_seconds else None,
        'clock_delta_ms': interval_statistics(clock_deltas),
        'clock_change_observation_interval_ms': interval_statistics(change_intervals),
        'poll_interval_ms': interval_statistics(poll_intervals),
        'clock_wraps': clock_wraps, 'clock_discontinuities': clock_discontinuities,
        'clock_deltas_above_nominal_40ms': sum(delta > 40 for delta in clock_deltas),
        'observed_frame_transitions': frame_changes, 'frame_advance_total': advance,
        'frame_advance_hz': frame_hz, 'frame_advance_per_observation': interval_statistics(frame_deltas),
        'frame_wraps': frame_wraps, 'frame_discontinuities': frame_discontinuities,
        'polls_with_multiple_frame_advance': sum(delta > 1 for delta in frame_deltas),
        'intermediate_frames_not_individually_observed': sum(delta - 1 for delta in frame_deltas),
        'longest_observed_stall_seconds': clock_stall_ns / 1_000_000_000,
        'longest_observed_frame_stall_seconds': frame_stall_ns / 1_000_000_000,
        'largest_successful_poll_gap_seconds': longest_gap_ns / 1_000_000_000,
        'stall_semantics': 'Lower bound from repeated equal values; excludes time after final successful read.',
        'counter_semantics': 'Unsigned half-range deltas; arbitrary resets and multiple wraps between reads cannot all be identified.',
        'snapshot_atomic': False, 'per_tick_complete': False, 'gameplay_acceptance': False,
    }


def validate_image(read, base, module_size):
    """Gate the live fixed-base PE32 and instruction sites without writes."""
    if base != IMAGE_BASE or module_size != IMAGE_SIZE:
        raise ValueError('DBSIM loaded image base/size differs from the reference')
    dos = read(base, 64)
    offset = struct.unpack_from('<I', dos, 0x3C)[0]
    if dos[:2] != b'MZ' or not 64 <= offset <= 4096 - 248:
        raise ValueError('Invalid loaded DOS/PE header')
    pe = read(base + offset, 248)
    if (pe[:4] != b'PE\0\0' or struct.unpack_from('<H', pe, 4)[0] != 0x14C or
            struct.unpack_from('<H', pe, 24)[0] != 0x10B or
            struct.unpack_from('<I', pe, 40)[0] != 0x1000 or
            struct.unpack_from('<I', pe, 52)[0] != IMAGE_BASE or
            struct.unpack_from('<I', pe, 80)[0] != IMAGE_SIZE):
        raise ValueError('Loaded PE32 header differs from DBSIM reference')
    checked = []
    for address, signature in SIGNATURES.items():
        if read(address, len(signature)) != signature:
            raise ValueError(f'Loaded timing instruction signature differs at {address:#x}')
        checked.append({'address': hex(address), 'bytes': signature.hex(' ')})
    return checked


class WindowsProcess:
    """Only SYNCHRONIZE, QUERY_LIMITED_INFORMATION and VM_READ rights."""
    def __init__(self, pid):
        if sys.platform != 'win32':
            raise OSError('Live sampling requires Windows')
        from ctypes import wintypes as W
        self.W, self.pid = W, pid
        self.kernel = k = ctypes.WinDLL('kernel32', use_last_error=True)
        k.OpenProcess.argtypes, k.OpenProcess.restype = [W.DWORD, W.BOOL, W.DWORD], W.HANDLE
        k.ReadProcessMemory.argtypes = [W.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        k.ReadProcessMemory.restype = W.BOOL
        k.GetExitCodeProcess.argtypes, k.GetExitCodeProcess.restype = [W.HANDLE, ctypes.POINTER(W.DWORD)], W.BOOL
        k.WaitForSingleObject.argtypes, k.WaitForSingleObject.restype = [W.HANDLE, W.DWORD], W.DWORD
        k.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, ctypes.POINTER(W.DWORD)]
        k.QueryFullProcessImageNameW.restype = W.BOOL
        k.CloseHandle.argtypes, k.CloseHandle.restype = [W.HANDLE], W.BOOL
        self.handle = k.OpenProcess(0x101010, False, pid)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None

    def exit_code(self):
        wait = self.kernel.WaitForSingleObject(self.handle, 0)
        if wait == 258:  # WAIT_TIMEOUT: eventual exit code259 is not confused with alive.
            return None
        if wait != 0:
            raise ctypes.WinError(ctypes.get_last_error())
        status = self.W.DWORD()
        if not self.kernel.GetExitCodeProcess(self.handle, ctypes.byref(status)):
            raise ctypes.WinError(ctypes.get_last_error())
        return status.value

    def read(self, address, size):
        buffer, count = ctypes.create_string_buffer(size), ctypes.c_size_t()
        if not self.kernel.ReadProcessMemory(self.handle, address, buffer, size, ctypes.byref(count)):
            raise ctypes.WinError(ctypes.get_last_error())
        if count.value != size:
            raise OSError(f'Partial ReadProcessMemory: {count.value}/{size} bytes at {address:#x}')
        return buffer.raw

    def read_sample(self):
        # One read reduces skew; this is not an atomic game snapshot.
        data = self.read(FRAME_ADDRESS, CLOCK_ADDRESS - FRAME_ADDRESS + 4)
        return struct.unpack_from('<I', data, CLOCK_ADDRESS - FRAME_ADDRESS)[0], struct.unpack_from('<I', data)[0]

    def validate(self):
        k, W = self.kernel, self.W
        name = ctypes.create_unicode_buffer(32768)
        length = W.DWORD(len(name))
        if not k.QueryFullProcessImageNameW(self.handle, 0, name, ctypes.byref(length)):
            raise ctypes.WinError(ctypes.get_last_error())
        exe = Path(name.value)
        actual = hashlib.sha256(exe.read_bytes()).hexdigest()
        if exe.name.lower() != 'dbsim.exe' or actual != REFERENCE_SHA256:
            raise ValueError('Not the selected unmodified DBSIM reference')

        class Module(ctypes.Structure):
            _fields_ = [('dwSize', W.DWORD), ('th32ModuleID', W.DWORD), ('th32ProcessID', W.DWORD),
                        ('GlblcntUsage', W.DWORD), ('ProccntUsage', W.DWORD),
                        ('modBaseAddr', ctypes.c_void_p), ('modBaseSize', W.DWORD), ('hModule', W.HMODULE),
                        ('szModule', W.WCHAR * 256), ('szExePath', W.WCHAR * 260)]

        k.CreateToolhelp32Snapshot.argtypes, k.CreateToolhelp32Snapshot.restype = [W.DWORD, W.DWORD], W.HANDLE
        k.Module32FirstW.argtypes, k.Module32FirstW.restype = [W.HANDLE, ctypes.POINTER(Module)], W.BOOL
        k.Module32NextW.argtypes, k.Module32NextW.restype = [W.HANDLE, ctypes.POINTER(Module)], W.BOOL
        invalid = ctypes.c_void_p(-1).value
        snapshot = invalid
        for _ in range(3):
            snapshot = k.CreateToolhelp32Snapshot(0x18, self.pid)
            if snapshot != invalid or ctypes.get_last_error() != 24:
                break
        if snapshot == invalid:
            raise ctypes.WinError(ctypes.get_last_error())
        base = size = None
        try:
            module = Module()
            module.dwSize = ctypes.sizeof(module)
            ok = k.Module32FirstW(snapshot, ctypes.byref(module))
            while ok:
                if Path(module.szExePath) == exe:
                    base, size = module.modBaseAddr, module.modBaseSize
                    break
                ok = k.Module32NextW(snapshot, ctypes.byref(module))
        finally:
            k.CloseHandle(snapshot)
        if base is None:
            raise ValueError('DBSIM main module not found in module snapshot')
        signatures = validate_image(self.read, base, size)
        return dict(exe=str(exe), sha256=actual, image_base=hex(base), image_size=size, validated_signatures=signatures)


def error_record(operation, error):
    return dict(operation=operation, message=str(error), winerror=getattr(error, 'winerror', None))


def collect_samples(process, seconds, interval_ms, emit, clock_ns=time.perf_counter_ns,
                    sleeper=time.sleep, progress=None):
    """Injectable collector: process supplies exit_code() and read_sample()."""
    if not math.isfinite(seconds) or seconds <= 0 or not math.isfinite(interval_ms) or interval_ms <= 0:
        raise ValueError('Duration and polling interval must be finite and positive')
    interval_ns = max(1, round(interval_ms * 1_000_000))
    start = clock_ns()
    deadline = start + max(1, round(seconds * 1_000_000_000))
    next_poll, next_progress = start, start + 30_000_000_000
    samples = []
    status, error, exit_code = 'completed_observation', None, None
    skipped_deadlines, previous = 0, None
    try:
        while clock_ns() < deadline:
            try:
                exit_code = process.exit_code()
            except OSError as problem:
                status, error = 'process_status_failed', error_record('process_status', problem)
                break
            if exit_code is not None:
                status = 'process_exited'
                break
            before = clock_ns()
            try:
                game_clock, frame = process.read_sample()
            except OSError as problem:
                status, error = 'read_failed', error_record('read_memory', problem)
                try:
                    exit_code = process.exit_code()
                    if exit_code is not None:
                        status = 'process_exited'
                except OSError:
                    pass
                break
            after = clock_ns()
            stamp = (before + after) // 2 - start
            samples.append((stamp, game_clock, frame))
            if previous != (game_clock, frame):
                emit(dict(event='sample', elapsed_ns=stamp, elapsed_ms=stamp / 1_000_000,
                          game_last_tick=game_clock, frame_counter=frame, read_duration_ns=after - before))
                previous = (game_clock, frame)
            if progress and after >= next_progress:
                progress(dict(event='progress', elapsed_seconds=(after - start) / 1_000_000_000,
                              samples=len(samples), game_last_tick=game_clock, frame_counter=frame))
                next_progress = after + 30_000_000_000
            next_poll += interval_ns
            now = clock_ns()
            if next_poll < now:
                missed = (now - next_poll) // interval_ns + 1
                skipped_deadlines += missed
                next_poll += missed * interval_ns
            delay = min(next_poll, deadline) - now
            if delay > 0:
                sleeper(delay / 1_000_000_000)
    except KeyboardInterrupt:
        status = 'interrupted'
    result = analyze_samples(samples, clock_ns() - start)
    result.update(event='summary', status=status, error=error, process_exit_code=exit_code,
                  requested_seconds=seconds, requested_interval_ms=interval_ms,
                  skipped_poll_deadlines=skipped_deadlines)
    emit(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--seconds', type=float, default=600)
    parser.add_argument('--interval-ms', type=float, default=5)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.pid <= 0 or args.pid > UINT32_MASK:
        parser.error('pid must be a positive DWORD')
    if not math.isfinite(args.seconds) or args.seconds <= 0:
        parser.error('seconds must be finite and positive')
    if not math.isfinite(args.interval_ms) or args.interval_ms <= 0:
        parser.error('interval-ms must be finite and positive')
    process = None
    try:
        with args.output.open('x', encoding='utf-8') as output:
            def emit(record):
                output.write(json.dumps(record) + '\n')
                output.flush()

            process = WindowsProcess(args.pid)
            validation = process.validate()
            emit(dict(event='start', pid=args.pid, mode='read_only_sampling', seconds=args.seconds,
                      interval_ms=args.interval_ms, clock_source='perf_counter_ns',
                      address=hex(CLOCK_ADDRESS), frame_address=hex(FRAME_ADDRESS),
                      process_access='SYNCHRONIZE|QUERY_LIMITED_INFORMATION|VM_READ',
                      per_tick_complete=False, **validation))
            result = collect_samples(process, args.seconds, args.interval_ms, emit,
                                     progress=lambda record: print(json.dumps(record), flush=True))
            print(json.dumps(result), flush=True)
            return 0 if result['status'] == 'completed_observation' and result['frame_advance_total'] else 1
    except (OSError, ValueError) as problem:
        print(json.dumps(dict(event='error', **error_record('setup_or_output', problem))), file=sys.stderr, flush=True)
        return 2
    finally:
        if process:
            process.close()


if __name__ == '__main__':
    raise SystemExit(main())
