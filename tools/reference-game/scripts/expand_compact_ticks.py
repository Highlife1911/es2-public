"""Explicit schema-3 RNG expansion for experiments, never automatic in oracle.

Output is a new schema-2 JSONL file. Full RNG states remain lossless; schema 3
and its distinct tick event require separate capture/golden qualification.
"""
import argparse
import json
import re


def expand(events):
    previous = {}
    sequence = 0
    started = False
    for event in events:
        kind = event.get('event')
        if kind == 'start':
            if started or event.get('schema') != 3:
                raise ValueError('Expected exactly one schema-3 header')
            started = True
            yield dict(event, schema=2)
            continue
        if not started:
            raise ValueError('Missing schema-3 header')
        if kind == 'tick':
            raise ValueError('Mixed compact and ordinary ticks')
        if kind != 'tick_compact':
            yield event
            continue
        if event.get('sequence') != sequence + 1:
            raise ValueError('Missing, duplicated or reordered compact tick')
        sequence += 1
        result = {}
        for key, value in event.items():
            if key in ('rng_a_same', 'rng_b_same'):
                bank = key[4]
                full = 'rng_' + bank + '_hex'
                if value is not True or bank not in previous or full in event:
                    raise ValueError('Invalid RNG reuse')
                result[full] = previous[bank]
            else:
                result[key] = 'tick' if key == 'event' else value
        for bank in ('a', 'b'):
            value = result.get('rng_' + bank + '_hex')
            if not isinstance(value, str) or not re.fullmatch('[0-9a-f]{228}', value):
                raise ValueError('Expected a complete 114-byte RNG state')
            previous[bank] = value
        yield result
    if not started or not sequence:
        raise ValueError('Empty compact tick log')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input')
    p.add_argument('output')
    a = p.parse_args()
    # Exclusive create; a malformed input leaves a visibly partial output.
    with open(a.input, encoding='utf-8') as source, open(a.output, 'x', encoding='utf-8', newline='') as out:
        for event in expand(json.loads(line) for line in source):
            out.write(json.dumps(event, separators=(',', ':')) + '\r\n')


if __name__ == '__main__':
    main()
