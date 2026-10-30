# Compares two schema-2 tick logs (docs/reference-game/tick_log.md) by frame counter.
# Usage: python compare_tick_logs.py <run-a.jsonl> <run-b.jsonl>
import json, sys, collections
def load(p):
    rows = [json.loads(l) for l in open(p)]
    return {t['frame_counter_before_increment']: t for t in rows if t['event'] == 'tick'}
a, b = load(sys.argv[1]), load(sys.argv[2])
common = sorted(set(a) & set(b))
print('common counters', common[0], '-', common[-1], 'n =', len(common))
for name, run in (('A', a), ('B', b)):
    t = [run[c] for c in sorted(run)]
    print(name, 'clock deltas', collections.Counter(y['completed_clock_ms'] - x['completed_clock_ms'] for x, y in zip(t, t[1:])).most_common(3),
          'pause', dict(collections.Counter(x['loop_pause_flag'] for x in t)), 'stalls', t[-1].get('vt_stall_advances'), 'late', t[-1].get('vt_late_polls'),
          'wall s %.1f' % ((t[-1]['qpc'] - t[0]['qpc']) / 1e7))
fields = ['completed_clock_ms', 'clock_raw_hex', 'rng_a_hex', 'rng_b_hex', 'position_raw_i32', 'angles_raw_u16', 'speed_raw_i16', 'throttle_raw_q10']
for f in fields:
    d = [c for c in common if a[c].get(f) != b[c].get(f)]
    print('%-20s differs at %4d counters; first %s' % (f, len(d), d[:1]))
ta = [a[c] for c in common]
print('rngA changes', sum(1 for x, y in zip(ta, ta[1:]) if x['rng_a_hex'] != y['rng_a_hex']), 'rngB changes', sum(1 for x, y in zip(ta, ta[1:]) if x['rng_b_hex'] != y['rng_b_hex']))
