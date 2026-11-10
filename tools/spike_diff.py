"""Differential test of the generated C (Thumb spike) against Unicorn (a real ARM core as the reference).

    python tools/spike_diff.py <rom.gba> <gen.dll> <gen.c.list> [cases per function] [seed]

For every translated function: random registers, flags and memory (EWRAM/IWRAM; the ROM is the real one), run the function in
Unicorn and in the generated C, and compare r0-r14, N Z C V and every byte of EWRAM and IWRAM. A case is skipped when it faults,
does not finish, makes an unaligned access (the ARM7TDMI rotates, Unicorn's core does not) or calls a function not translated.
Needs `pip install unicorn`. Compiles with:  tcc -shared -Iruntime -o gen.dll gen.c runtime/gba_rt.c
"""
import ctypes, random, sys
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UcError
from unicorn.arm_const import *

rom_path, dll_path, list_path = sys.argv[1:4]
cases = int(sys.argv[4]) if len(sys.argv) > 4 else 8
rng = random.Random(int(sys.argv[5]) if len(sys.argv) > 5 else 1)
rom = open(rom_path, 'rb').read()
rom = rom + bytes(0x1000000 - len(rom)) if len(rom) < 0x1000000 else rom
dll = ctypes.CDLL(dll_path)
rom_buf = ctypes.create_string_buffer(rom, len(rom))
ctypes.c_void_p.in_dll(dll, 'rt_rom').value = ctypes.addressof(rom_buf)
ctypes.c_uint32.in_dll(dll, 'rt_rom_len').value = len(rom)
ewram = (ctypes.c_uint8 * 0x40000).in_dll(dll, 'rt_ewram')
iwram = (ctypes.c_uint8 * 0x8000).in_dll(dll, 'rt_iwram')
fault = ctypes.c_int.in_dll(dll, 'rt_fault')
unal = ctypes.c_int.in_dll(dll, 'rt_unaligned')
untr = ctypes.c_uint32.in_dll(dll, 'rt_untranslated_addr')

class Cpu(ctypes.Structure):
    _fields_ = [('r', ctypes.c_uint32 * 16), ('n', ctypes.c_uint32), ('z', ctypes.c_uint32), ('c', ctypes.c_uint32), ('v', ctypes.c_uint32)]

uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
from unicorn import UC_PROT_READ, UC_PROT_EXEC
uc.mem_map(0x08000000, 0x1000000, UC_PROT_READ | UC_PROT_EXEC)
uc.mem_write(0x08000000, rom)  # (writing before the protection: see below)
uc.mem_map(0x02000000, 0x40000)
uc.mem_map(0x03000000, 0x8000)
RET = 0x0DFFFFF0

def word():
    k = rng.random()
    if k < .45: return rng.randrange(0, 300)
    if k < .70: return 0x02000000 + rng.randrange(0, 0x10000) * 4
    if k < .85: return 0x03000000 + rng.randrange(0, 0x1C00) * 4
    if k < .90: return 0x08000000 + rng.randrange(0, 0x80000) * 4
    return rng.getrandbits(32)

# a few random memory images, reused: building one word by word is the slow part
IMAGES = [(b''.join(word().to_bytes(4, 'little') for _ in range(0x10000)), b''.join(word().to_bytes(4, 'little') for _ in range(0x2000))) for _ in range(6)]

REGS = [UC_ARM_REG_R0 + i for i in range(13)]
fails, ok, skipped, funcs_ok = [], 0, {}, 0
entries = [int(x, 16) for x in open(list_path).read().split()]
for entry in entries:
    good = 0
    for _ in range(cases):
        regs = [word() for _ in range(13)]
        regs[0] = rng.choice([word(), 0x02000000 + rng.randrange(0, 0x1000) * 4])
        sp = 0x03007000 + rng.randrange(0, 0x100) * 4
        flags = [rng.getrandbits(1) for _ in range(4)]
        mem_e, mem_i = rng.choice(IMAGES)
        # reference
        uc.mem_write(0x02000000, mem_e)
        uc.mem_write(0x03000000, mem_i)
        for r, v in zip(REGS, regs): uc.reg_write(r, v)
        uc.reg_write(UC_ARM_REG_SP, sp)
        uc.reg_write(UC_ARM_REG_LR, RET | 1)
        uc.reg_write(UC_ARM_REG_CPSR, 0x1F | (1 << 5) | sum(f << b for f, b in zip(flags, (31, 30, 29, 28))))
        try:
            uc.emu_start(entry | 1, RET, count=20000)
        except UcError:
            skipped['fault'] = skipped.get('fault', 0) + 1
            continue
        if uc.reg_read(UC_ARM_REG_PC) != RET:
            skipped['no end'] = skipped.get('no end', 0) + 1
            continue
        cpsr = uc.reg_read(UC_ARM_REG_CPSR)
        want_r = [uc.reg_read(r) for r in REGS] + [uc.reg_read(UC_ARM_REG_SP), uc.reg_read(UC_ARM_REG_LR)]
        want_f = [(cpsr >> b) & 1 for b in (31, 30, 29, 28)]
        want_e, want_i = bytes(uc.mem_read(0x02000000, 0x40000)), bytes(uc.mem_read(0x03000000, 0x8000))
        # generated C
        ctypes.memmove(ewram, mem_e, len(mem_e)); ctypes.memmove(iwram, mem_i, len(mem_i))
        c = Cpu()
        for i, v in enumerate(regs): c.r[i] = v
        c.r[13], c.r[14] = sp, RET | 1
        c.n, c.z, c.c, c.v = flags
        fault.value = unal.value = untr.value = 0
        getattr(dll, 'f_%08X' % entry)(ctypes.byref(c))
        if untr.value:
            skipped['callee not translated'] = skipped.get('callee not translated', 0) + 1
            continue
        if unal.value:
            skipped['unaligned'] = skipped.get('unaligned', 0) + 1
            continue
        got_r = list(c.r[:15]); got_f = [c.n, c.z, c.c, c.v]
        bad = []
        if fault.value: bad.append('C faulted, reference did not')
        for i in range(15):
            if got_r[i] != want_r[i]: bad.append('r%d got %08X want %08X' % (i, got_r[i], want_r[i]))
        if got_f != want_f: bad.append('NZCV got %s want %s' % (got_f, want_f))
        if bytes(ewram) != want_e: bad.append('EWRAM differs')
        if bytes(iwram) != want_i: bad.append('IWRAM differs')
        if bad:
            fails.append((entry, bad))
            break
        good += 1
        ok += 1
    if good: funcs_ok += 1
print('functions checked %d, with at least one valid case %d, valid cases %d, mismatching functions %d' % (len(entries), funcs_ok, ok, len(fails)))
print('skipped cases:', skipped)
for e, b in fails[:30]:
    print('  %08X: %s' % (e, '; '.join(b[:4])))
