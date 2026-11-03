#!/usr/bin/env python3
"""Paint boot progress strips from a kernel that has no console yet.

Run on a COPY of the kernel after `patch-xnu-entry-probe.py --no-reset`:

    python3 scripts/add-progress-strips.py kernel.macho

This board has no serial console, so the panel is the only output. Before the
MMU turns on, the frame buffer is added to the V=P bootstrap table and that
table's TTBR0 value is saved. Each checkpoint then switches TTBR0 back to the
bootstrap table, paints one strip through the identity mapping, and restores
TTBR0, so it keeps working after arm_vm_init replaces TTBR0.

Strips run top to bottom from STRIP_Y. Every fifth one is yellow. A strip means
the kernel was about to run that step, so the stall is between the last strip
drawn and the next one that would run. A missing strip followed by a later one
is a branch that was not taken.

Report lines at the bottom of the panel show why a stall happened: an exception
that reaches panic_with_thread_kernel_state, a panic() and an Assert().

Sites are found by symbol, so a relink does not move them. The fixed addresses
that remain belong to the entry code and its padding, and each is checked before
it is used. The tool refuses a kernel that does not match.
"""
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

FB, STRIDE = 0xE3940000, 4464
STRIP_Y, STRIP_ROWS, STRIP_STEP = 900, 26, 36
PANEL_ROWS = 2480
BLOCK = 0x200000
MMU_SITE = 0xfffffff0073d8350
STAGE2_CAVE = 0xfffffff0073d6560
AFTER_MMU = 0xfffffff0073d8370
MOV_X0_CPACR = 0xD2A00600
AREA, AREA_END = 0xfffffff0073d65c0, 0xfffffff0073d6ff0

# (function, symbol it calls, which call, label). None replaces `mov x0, #0x300000`.
SITES = [
    (None, None, 0, 'MMU on'),
    ('_kernel_bootstrap', '_load_context', 1, 'load_context'),
    ('_kernel_bootstrap_thread', '_idle_thread_create', 1, 'idle_thread_create'),
    ('_kernel_bootstrap_thread', '_sched_startup', 1, 'sched_startup'),
    ('_kernel_bootstrap_thread', '_thread_daemon_init', 1, 'thread_daemon_init'),
    ('_kernel_bootstrap_thread', '_kernel_startup_initialize_upto', 1, 'startup up to THREAD_CALL'),
    ('_kernel_bootstrap_thread', '_mapping_adjust', 1, 'mapping_adjust'),
    ('_kernel_bootstrap_thread', '_clock_service_create', 1, 'clock_service_create'),
    ('_kernel_bootstrap_thread', '_kdp_init', 1, 'kdp_init'),
    ('_kernel_bootstrap_thread', '_bootprofile_init', 1, 'bootprofile_init'),
    ('_kernel_bootstrap_thread', '_kernel_startup_initialize_upto', 2, 'startup up to SYSCTL'),
    ('_kernel_bootstrap_thread', '_PE_init_iokit', 1, 'PE_init_iokit'),
    ('_kernel_bootstrap_thread', '_kernel_startup_initialize_upto', 3, 'startup up to EARLY_BOOT'),
]

# Each report line is 32 bit cells, most significant bit on the left, white for 1
# and black for 0, with red between cells and at the right end.
SUB_SP_MASK, SUB_SP = 0xFF000000, 0xD1000000
SAVED_X = lambda n: 8 + 8 * n
SAVED_LR, SAVED_PC, SAVED_FAR, SAVED_ESR = 0xf8, 0x108, 0x118, 0x120
REPORT_Y, REPORT_STEP = 1720, 66
# (function entry, [(label, instructions that load x15)]). Rows follow in order.
REPORTS = [
    ('_panic_with_thread_kernel_state', [
        ('fault PC low', f'  ldr x15, [x1, #{SAVED_PC:#x}]\n'),
        ('fault FAR high', f'  ldr x15, [x1, #{SAVED_FAR:#x}]\n  lsr x15, x15, #32\n'),
        ('fault FAR low', f'  ldr x15, [x1, #{SAVED_FAR:#x}]\n'),
        ('fault ESR', f'  ldr w15, [x1, #{SAVED_ESR:#x}]\n'),
        ('fault LR low', f'  ldr x15, [x1, #{SAVED_LR:#x}]\n'),
        ('fault x0 low', f'  ldr x15, [x1, #{SAVED_X(0)}]\n'),
    ]),
    ('_panic', [
        ('panic caller low', '  mov x15, x30\n'),
        ('panic format string low', '  mov x15, x0\n'),
        # Darwin arm64 passes variadic arguments on the stack.
        ('panic first argument low', '  ldr x15, [sp]\n'),
        ('panic second argument low', '  ldr x15, [sp, #8]\n'),
    ]),
    ('_Assert', [
        ('assert caller low', '  mov x15, x30\n'),
    ]),
]

# Device memory (attr index 5), EL1 read/write only, execute never.
ATTRS = 1 | (5 << 2) | (2 << 8) | (1 << 10) | (3 << 53)
L2_DESC = (FB & ~(BLOCK - 1)) | ATTRS
L1_DESC = 0xC0000000 | ATTRS
L2_INDEX = ((FB & ~(BLOCK - 1)) - 0xC0000000) >> 21
L1_INDEX = 3

HELPERS = f""".text
.global _start
_start:
  mrs x5, TTBR0_EL1
  adr x4, slot
  str x5, [x4]
  dc ivac, x4
  and x1, x5, #0x0000fffffffff000
  ldr x2, [x1, #{L1_INDEX * 8}]
  and x3, x2, #3
  cmp x3, #3
  b.ne 3f
  and x2, x2, #0x0000fffffffff000
  add x2, x2, #{L2_INDEX * 8}
  ldr x3, l2_desc
  mov x4, #7
1:
  str x3, [x2], #8
  add x3, x3, #{BLOCK:#x}
  subs x4, x4, #1
  b.ne 1b
  b 4f
3:
  ldr x3, l1_desc
  str x3, [x1, #{L1_INDEX * 8}]
4:
  dsb ish
  tlbi vmalle1
  dsb ish
  isb
  b stage2
.balign 8
l2_desc: .quad {L2_DESC:#x}
l1_desc: .quad {L1_DESC:#x}
slot: .quad 0
paint:
  mrs x15, DAIF
  msr DAIFSet, #15
  mrs x12, TTBR0_EL1
  ldr x13, slot
  msr TTBR0_EL1, x13
  isb
  mov x14, #{STRIDE * STRIP_ROWS // 8}
1:
  stp w11, w11, [x9], #8
  subs x14, x14, #1
  b.ne 1b
  dsb sy
  msr TTBR0_EL1, x12
  isb
  tlbi vmalle1
  dsb ish
  isb
  msr DAIF, x15
  br x10
.global paint
barcode:
  stp x0, x1, [sp, #-32]!
  stp x2, x3, [sp, #16]
  mrs x16, DAIF
  msr DAIFSet, #15
  mrs x12, TTBR0_EL1
  ldr x13, slot
  msr TTBR0_EL1, x13
  isb
  movz w0, #0xffff
  movk w0, #0xffff, lsl #16
  movz w1, #0xff00, lsl #16
  movz w3, #0xffff, lsl #16
  mov x14, #60
1:
  mov x11, #32
  mov x17, x15
2:
  tst x17, #0x80000000
  csel w13, w0, w1, ne
  lsl x17, x17, #1
  mov x2, #14
3:
  stp w13, w13, [x9], #8
  subs x2, x2, #1
  b.ne 3b
  stp w3, w3, [x9], #8
  stp w3, w3, [x9], #8
  subs x11, x11, #1
  b.ne 2b
  mov x2, #46
4:
  stp w3, w3, [x9], #8
  subs x2, x2, #1
  b.ne 4b
  subs x14, x14, #1
  b.ne 1b
  dsb sy
  msr TTBR0_EL1, x12
  isb
  tlbi vmalle1
  dsb ish
  isb
  msr DAIF, x16
  ldp x2, x3, [sp, #16]
  ldp x0, x1, [sp], #32
  br x10
.global barcode
"""


def tool(name):
    found = shutil.which(name) or f'/opt/homebrew/opt/llvm/bin/{name}'
    if not Path(found).exists():
        raise SystemExit(f'{name} not found')
    return found


def assemble(source, va, workdir, symbols):
    asm, obj, elf, binary = (workdir / name for name in ('a.S', 'a.o', 'a.elf', 'a.bin'))
    asm.write_text(source)
    subprocess.run([tool('clang'), '--target=aarch64-none-elf', '-c', asm, '-o', obj], check=True)
    defs = [f'--defsym={name}={value:#x}' for name, value in symbols.items()]
    subprocess.run([tool('ld.lld'), f'-Ttext={va:#x}', *defs, '-e', '_start', obj, '-o', elf], check=True)
    subprocess.run([tool('llvm-objcopy'), '-O', 'binary', elf, binary], check=True)
    return binary.read_bytes(), elf


def symbols_of(path):
    output = subprocess.run([tool('llvm-nm'), str(path)], capture_output=True, text=True, check=True).stdout
    by_name, by_address = {}, {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3:
            address = int(parts[0], 16)
            by_name.setdefault(parts[2], address)
            by_address.setdefault(address, set()).add(parts[2])
    return by_name, by_address


def elf_symbol(elf, name):
    output = subprocess.run([tool('llvm-nm'), str(elf)], capture_output=True, text=True, check=True).stdout
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise SystemExit(f'{name} missing from helper code')


def segments_of(data):
    count = struct.unpack_from('<I', data, 16)[0]
    position, found = 32, []
    for _ in range(count):
        command, size = struct.unpack_from('<II', data, position)
        if command == 0x19:
            va, _, fileoff, filesize = struct.unpack_from('<QQQQ', data, position + 24)
            found.append((va, fileoff, filesize))
        position += size
    return found


def immediate(reg, value):
    return f'  movz {reg}, #{value & 0xffff}\n  movk {reg}, #{(value >> 16) & 0xffff}, lsl #16\n'


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    path = Path(sys.argv[1])
    data = bytearray(path.read_bytes())
    original = bytes(data)  # call sites are counted before any of them is patched
    names, addresses = symbols_of(path)
    segments = segments_of(data)
    ordered = sorted(addresses)

    def offset(va):
        for start, fileoff, size in segments:
            if size and start <= va < start + size:
                return fileoff + va - start
        raise SystemExit(f'{va:#x} is not file backed')

    def word(va):
        return struct.unpack_from('<I', data, offset(va))[0]

    def branch(at, target):
        return 0x14000000 | (((target - at) // 4) & 0x3ffffff)

    def bl_target(at):
        value = struct.unpack_from('<I', original, offset(at))[0]
        if value >> 26 != 0x25:
            return None
        delta = value & 0x3ffffff
        if delta & (1 << 25):
            delta -= 1 << 26
        return at + delta * 4

    def find_call(function, callee, occurrence):
        start = names[function]
        end = next(address for address in ordered if address > start)
        hits = [at for at in range(start, end, 4) if bl_target(at) == names[callee]]
        if len(hits) < occurrence:
            raise SystemExit(f'{function} has {len(hits)} calls to {callee}, wanted call {occurrence}')
        return hits[occurrence - 1]

    tcr = struct.unpack_from('<Q', data, offset(names['_sysreg_restore']))[0]
    t0sz, tg0, epd0 = tcr & 0x3f, (tcr >> 14) & 3, (tcr >> 7) & 1
    if t0sz not in range(25, 34) or tg0 != 0 or epd0 != 0:
        raise SystemExit(f'TCR {tcr:#x}: TTBR0 root is not a 4K L1 table with walks enabled')
    if any(data[offset(AREA):offset(AREA_END)]):
        raise SystemExit('Padding is not empty: run on a kernel with only the --no-reset probes')
    if word(MMU_SITE) != branch(MMU_SITE, STAGE2_CAVE):
        raise SystemExit('Stage 2 probe is not installed')
    report_lines = sum(len(lines) for _, lines in REPORTS)
    if STRIP_Y + len(SITES) * STRIP_STEP > REPORT_Y:
        raise SystemExit('Strips run into the reports')
    if REPORT_Y + report_lines * REPORT_STEP - 6 > PANEL_ROWS:
        raise SystemExit('Reports run off the panel')
    last_byte = FB + STRIDE * PANEL_ROWS
    if last_byte > (FB & ~(BLOCK - 1)) + 7 * BLOCK:
        raise SystemExit('The panel runs past the mapped frame buffer')

    cursor = AREA
    with tempfile.TemporaryDirectory() as tmp:
        workdir = Path(tmp)
        code, elf = assemble(HELPERS, cursor, workdir, {'stage2': STAGE2_CAVE})
        paint = elf_symbol(elf, 'paint')
        barcode = elf_symbol(elf, 'barcode')
        data[offset(cursor):offset(cursor) + len(code)] = code
        struct.pack_into('<I', data, offset(MMU_SITE), branch(MMU_SITE, cursor))
        print(f'helpers {cursor:#x} ({len(code)} bytes), paint {paint:#x}, L2 index {L2_INDEX}')
        cursor = (cursor + len(code) + 15) & ~15

        for index, (function, callee, occurrence, label) in enumerate(SITES):
            extra = {}
            if function is None:
                site = AFTER_MMU
                old = word(site)
                if old != MOV_X0_CPACR:
                    raise SystemExit(f'{site:#x} holds {old:#x}, not mov x0, #0x300000')
                displaced = '  movz x0, #0x30, lsl #16\n'
            else:
                site = find_call(function, callee, occurrence)
                displaced = '  bl callee\n'
                extra = {'callee': names[callee]}
            yellow = index % 5 == 4
            row = STRIP_Y + index * STRIP_STEP
            source = ('.text\n.global _start\n_start:\n'
                      + immediate('x9', FB + STRIDE * row)
                      + immediate('w11', 0xFFFFFF00 if yellow else 0xFFFFFFFF)
                      + '  adr x10, 1f\n  b paint\n1:\n' + displaced + '  b back\n')
            code, _ = assemble(source, cursor, workdir, {**extra, 'paint': paint, 'back': site + 4})
            if cursor + len(code) > AREA_END:
                raise SystemExit('Padding exhausted')
            data[offset(cursor):offset(cursor) + len(code)] = code
            struct.pack_into('<I', data, offset(site), branch(site, cursor))
            print(f'strip {index + 1:2d} {"yellow" if yellow else "white "} row {row:4d} {site:#x} -> {cursor:#x}  {label}')
            cursor = (cursor + len(code) + 15) & ~15

        row = REPORT_Y
        for function, lines in REPORTS:
            site = names[function]
            old = word(site)
            if old & SUB_SP_MASK != SUB_SP:
                raise SystemExit(f'{function} at {site:#x} does not start with sub sp, sp, #imm')
            source = '.text\n.global _start\n_start:\n'
            for label, load in lines:
                source += immediate('x9', FB + STRIDE * row) + load + '  adr x10, 1f\n  b barcode\n1:\n'
                print(f'report row {row} {label}')
                row += REPORT_STEP
            source += f'  .inst {old:#x}\n  b back\n'
            code, _ = assemble(source, cursor, workdir, {'barcode': barcode, 'back': site + 4})
            if cursor + len(code) > AREA_END:
                raise SystemExit('Padding exhausted')
            data[offset(cursor):offset(cursor) + len(code)] = code
            struct.pack_into('<I', data, offset(site), branch(site, cursor))
            cursor = (cursor + len(code) + 15) & ~15

    path.write_bytes(data)
    print(f'padding used up to {cursor:#x}, limit {AREA_END:#x}')


if __name__ == '__main__':
    main()
