"""Optional test oracle: execute original 434B70/434C60, never production code.

stdin/stdout are little-endian u32 words. The Rust test supplies authored UV
tables as logical triangle/corner indices; only this fixture makes pointers.
Requires pefile, unicorn and an explicitly supplied original tankrace.exe.
"""
import struct
import sys

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP

source = pefile.PE(sys.argv[1])
image = source.get_memory_mapped_image()
machine = Uc(UC_ARCH_X86, UC_MODE_32)
machine.mem_map(source.OPTIONAL_HEADER.ImageBase, (len(image) + 4095) & ~4095)
machine.mem_write(source.OPTIONAL_HEADER.ImageBase, image)
OBJECT, STACK, RETURN = 0x10000000, 0x20000000, 0x30000000
machine.mem_map(OBJECT, 0x20000)
machine.mem_map(STACK, 0x10000)
machine.mem_map(RETURN, 0x1000)

data = sys.stdin.buffer.read()
words = iter(struct.unpack('<' + 'I' * (len(data) // 4), data))
output = bytearray()


def write(address, value, width=4):
    machine.mem_write(address, (value & ((1 << (width * 8)) - 1)).to_bytes(width, 'little'))


def emit(value):
    output.extend(struct.pack('<I', value))


for _ in range(next(words)):
    machine.mem_write(OBJECT, bytes(0x20000))
    write(0x53e414, 0)
    write(0x53e96c, 0)
    for offset in (0xd84, 0xd88, 0xa4, 0xb8, 0x10bc):
        write(OBJECT + offset, next(words), 1 if offset == 0x10bc else 4)
    faces = []
    for side in range(2):
        count = next(words)
        base = OBJECT + 0x4000 + side * 0x8000
        faces.append((base, count))
        for triangle in range(count):
            for corner in range(3):
                write(base + triangle * 24 + 12 + corner * 2, next(words), 1)
                write(base + triangle * 24 + 13 + corner * 2, next(words), 1)
        table = OBJECT + 0xe22 + side * 300
        for group, (pointer, bases) in enumerate(((8, 0), (32, 0), (56, 248), (152, 272))):
            count = next(words)
            write(table + 296 + group, count, 1)
            for index in range(count):
                triangle, corner, snapped = next(words), next(words), next(words)
                write(table + pointer + index * 4, base + triangle * 24 + 13 + corner * 2)
                if group >= 2:
                    write(table + bases + index, snapped, 1)
    write(STACK + 0xff00, RETURN)
    machine.reg_write(UC_X86_REG_ESP, STACK + 0xff00)
    machine.reg_write(UC_X86_REG_ECX, OBJECT)
    machine.emu_start(0x434b70, RETURN, count=20000)
    for offset in (0xd84, 0xd88):
        emit(int.from_bytes(machine.mem_read(OBJECT + offset, 4), 'little'))
    for base, count in faces:
        for triangle in range(count):
            for corner in range(3):
                emit(int.from_bytes(machine.mem_read(base + triangle * 24 + 12 + corner * 2, 1), 'little'))
                emit(int.from_bytes(machine.mem_read(base + triangle * 24 + 13 + corner * 2, 1), 'little'))

assert next(words, None) is None
sys.stdout.buffer.write(output)
