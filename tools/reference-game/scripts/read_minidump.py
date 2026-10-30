"""Read x86 Windows minidump registers, frame chains and bitmap headers (stdlib only)."""
import argparse
import json
import mmap
import struct
from pathlib import Path


class Dump:
    def __init__(self, path):
        self.file = path.open('rb')
        self.data = mmap.mmap(self.file.fileno(), 0, access=mmap.ACCESS_READ)
        if self.data[:4] != b'MDMP':
            raise ValueError('Not a Windows minidump')
        count, directory = self.unpack('<II', 8)
        self.streams = {}
        for index in range(count):
            kind, size, offset = self.unpack('<III', directory + 12 * index)
            self.streams[kind] = (offset, size)
        self.memory = []
        if 9 in self.streams:
            offset, _ = self.streams[9]
            count, cursor = self.unpack('<QQ', offset)
            for index in range(count):
                address, size = self.unpack('<QQ', offset + 16 + 16 * index)
                self.memory.append((address, address + size, cursor))
                cursor += size
        if 5 in self.streams:
            offset, _ = self.streams[5]
            count, = self.unpack('<I', offset)
            for index in range(count):
                address, size, cursor = self.unpack('<QII', offset + 4 + 16 * index)
                self.memory.append((address, address + size, cursor))
        self.modules = []
        if 4 in self.streams:
            offset, _ = self.streams[4]
            count, = self.unpack('<I', offset)
            for index in range(count):
                base, size, _, _, name = self.unpack('<QIIII', offset + 4 + 108 * index)
                length, = self.unpack('<I', name)
                self.modules.append((base, base + size, self.data[name+4:name+4+length].decode('utf-16-le', 'replace')))
        self.threads = []
        if 3 in self.streams:
            offset, _ = self.streams[3]
            count, = self.unpack('<I', offset)
            for index in range(count):
                cursor = offset + 4 + 48 * index
                tid, _, _, _, teb, stack, size, rva, ctxsize, ctx = self.unpack('<IIIIQQIIII', cursor)
                self.memory.append((stack, stack + size, rva))
                self.threads.append({'thread_id': tid, 'teb': hex(teb), 'stack': hex(stack),
                                     'stack_size': size, 'registers': self.context(ctx, ctxsize)})
        self.exception = None
        if 6 in self.streams:
            offset, _ = self.streams[6]
            tid, = self.unpack('<I', offset)
            code, flags, record, address, count, _ = self.unpack('<IIQQII', offset + 8)
            parameters = self.unpack('<' + 'Q' * min(count, 15), offset + 40)
            size, ctx = self.unpack('<II', offset + 160)
            self.exception = {'thread_id': tid, 'code': hex(code), 'flags': hex(flags),
                              'address': hex(address), 'parameters': [hex(x) for x in parameters],
                              'registers': self.context(ctx, size)}

    def unpack(self, fmt, offset):
        return struct.unpack_from(fmt, self.data, offset)

    def context(self, offset, size):
        if size < 204:
            return {'unsupported_context_size': size}
        flags, = self.unpack('<I', offset)
        if flags & 0x10000 == 0:
            return {'unsupported_context_flags': flags, 'context_size': size}
        names = ['edi', 'esi', 'ebx', 'edx', 'ecx', 'eax', 'ebp', 'eip', 'cs', 'eflags', 'esp', 'ss']
        return dict(zip(names, self.unpack('<12I', offset + 156)))

    def read(self, address, size):
        for begin, end, offset in self.memory:
            if begin <= address and address + size <= end:
                return self.data[offset + address - begin:offset + address - begin + size]
        raise ValueError(f'Memory unavailable at {address:#x} ({size} bytes)')

    def symbol(self, address):
        for begin, end, name in self.modules:
            if begin <= address < end:
                return f'{name}+0x{address-begin:x}'
        return None

    def analyze(self, registers):
        result = {'registers': {key: hex(value) for key, value in registers.items()}}
        if 'ebp' not in registers:
            return result
        result['eip_module'] = self.symbol(registers['eip'])
        frame = registers['ebp']
        visited = set()
        result['frames'] = []
        for _ in range(64):
            if frame in visited or not frame:
                break
            visited.add(frame)
            try:
                values = struct.unpack('<8I', self.read(frame, 32))
            except ValueError:
                break
            result['frames'].append({'ebp': hex(frame), 'return': hex(values[1]),
                                     'module': self.symbol(values[1]),
                                     'arguments': [hex(x) for x in values[2:]]})
            frame = values[0]
        try:
            bitmap = self.read(registers['ebx'], 64)
            size, pointer, codec = struct.unpack_from('<IIH', bitmap, 10)
            result['ebx_candidate_bitmap'] = {'address': hex(registers['ebx']), 'header': bitmap.hex(' '),
                                            'width_height': struct.unpack_from('<HH', bitmap, 4),
                                            'size': size, 'data_pointer': hex(pointer), 'codec': codec}
            try:
                result['ebx_candidate_bitmap']['payload_first64'] = self.read(pointer, 64).hex(' ')
            except ValueError as error:
                result['ebx_candidate_bitmap']['payload_error'] = str(error)
        except ValueError as error:
            result['ebx_candidate_bitmap_error'] = str(error)
        result['stack_code_candidates'] = []
        for offset in range(0, 4096, 4):
            try:
                value, = struct.unpack('<I', self.read(registers['esp'] + offset, 4))
            except ValueError:
                break
            symbol = self.symbol(value)
            if symbol:
                result['stack_code_candidates'].append({'stack': hex(registers['esp'] + offset),
                                                         'value': hex(value), 'module': symbol})
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path)
    parser.add_argument('--thread-id', type=int)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    dump = Dump(args.dump)
    report = {'path': str(args.dump), 'stream_types': list(dump.streams),
              'modules': [{'base': hex(a), 'size': b-a, 'path': c} for a, b, c in dump.modules],
              'exception': dump.exception, 'threads': []}
    for thread in dump.threads:
        if args.thread_id is not None and thread['thread_id'] != args.thread_id:
            continue
        registers = thread.pop('registers')
        if dump.exception and dump.exception['thread_id'] == thread['thread_id']:
            registers = dump.exception['registers']
        report['threads'].append(dict(thread, **dump.analyze(registers)))
    text = json.dumps(report, indent=2)
    if args.output:
        args.output.write_text(text + '\n', encoding='utf-8')
    else:
        print(text)


if __name__ == '__main__':
    main()
