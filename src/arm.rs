//! ARM (32-bit) instruction decoder for the ARM7TDMI (ARMv4T): text for a disassembly and the flow of control.

use crate::{Flow, Insn};

const COND: [&str; 16] = ["eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "", "nv"];
const DP: [&str; 16] = ["and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc", "tst", "teq", "cmp", "cmn", "orr", "mov", "bic", "mvn"];
const SHIFT: [&str; 4] = ["lsl", "lsr", "asr", "ror"];

pub fn reg(n: u32) -> String {
    match n {
        13 => "sp".into(),
        14 => "lr".into(),
        15 => "pc".into(),
        _ => format!("r{n}"),
    }
}

fn reglist(mask: u32) -> String {
    let mut parts = Vec::new();
    let mut i = 0;
    while i < 16 {
        if mask & (1 << i) != 0 {
            let mut j = i;
            while j + 1 < 16 && mask & (1 << (j + 1)) != 0 {
                j += 1;
            }
            parts.push(if j > i + 1 { format!("{}-{}", reg(i), reg(j)) } else if j == i + 1 { format!("{}, {}", reg(i), reg(j)) } else { reg(i) });
            i = j + 1;
        } else {
            i += 1;
        }
    }
    format!("{{{}}}", parts.join(", "))
}

fn signed(v: i64) -> String {
    if v < 0 {
        format!("-0x{:X}", -v)
    } else {
        format!("0x{v:X}")
    }
}

/// The shifted register operand (`Rm, LSL #n` / `Rm, LSR Rs` / `Rm, RRX`).
fn shifted_reg(insn: u32) -> String {
    let rm = reg(insn & 0xF);
    let ty = ((insn >> 5) & 3) as usize;
    if insn & 0x10 != 0 {
        return format!("{rm}, {} {}", SHIFT[ty], reg((insn >> 8) & 0xF));
    }
    let amount = (insn >> 7) & 0x1F;
    match (ty, amount) {
        (0, 0) => rm,
        (3, 0) => format!("{rm}, rrx"),
        (1, 0) | (2, 0) => format!("{rm}, {} #32", SHIFT[ty]),
        _ => format!("{rm}, {} #{amount}", SHIFT[ty]),
    }
}

fn msr_fields(insn: u32) -> String {
    let mut s = String::new();
    for (bit, c) in [(19, 'f'), (18, 's'), (17, 'x'), (16, 'c')] {
        if insn & (1 << bit) != 0 {
            s.push(c);
        }
    }
    s
}

/// Decodes the instruction `insn` found at `pc`.
pub fn decode(insn: u32, pc: u32) -> Insn {
    let cond = insn >> 28;
    let c = COND[cond as usize];
    let always = cond == 14;
    let mk = |text: String, flow: Flow| Insn { size: 4, text, flow };
    // condition 15 (never) is undefined on the ARM7TDMI
    if cond == 15 {
        return mk(format!(".word 0x{insn:08X}"), Flow::Undefined);
    }

    // BX
    if insn & 0x0FFF_FFF0 == 0x012F_FF10 {
        let rm = insn & 0xF;
        let ret = rm == 14;
        let flow = if always { Flow::Indirect { ret, thumb_switch: true } } else { Flow::CondIndirect { ret, thumb_switch: true } };
        return mk(format!("bx{c} {}", reg(rm)), flow);
    }

    match (insn >> 25) & 7 {
        0b000 | 0b001 => {
            let imm = insn & (1 << 25) != 0;
            if !imm && insn & 0x90 == 0x90 {
                return decode_extra(insn, c);
            }
            // PSR transfers
            if !imm && insn & 0x0FBF_0FFF == 0x010F_0000 {
                let psr = if insn & (1 << 22) != 0 { "spsr" } else { "cpsr" };
                return mk(format!("mrs{c} {}, {psr}", reg((insn >> 12) & 0xF)), Flow::Next);
            }
            if (!imm && insn & 0x0FB0_FFF0 == 0x0120_F000) || (imm && insn & 0x0FB0_F000 == 0x0320_F000) {
                let psr = if insn & (1 << 22) != 0 { "spsr" } else { "cpsr" };
                let op = if imm { format!("#0x{:X}", (insn & 0xFF).rotate_right(((insn >> 8) & 0xF) * 2)) } else { reg(insn & 0xF) };
                return mk(format!("msr{c} {psr}_{}, {op}", msr_fields(insn)), Flow::Next);
            }
            let opc = ((insn >> 21) & 0xF) as usize;
            let s = insn & (1 << 20) != 0;
            let rn = (insn >> 16) & 0xF;
            let rd = (insn >> 12) & 0xF;
            if (8..=11).contains(&opc) && !s {
                return mk(format!(".word 0x{insn:08X}"), Flow::Undefined);
            }
            let op2 = if imm { format!("#0x{:X}", (insn & 0xFF).rotate_right(((insn >> 8) & 0xF) * 2)) } else { shifted_reg(insn) };
            let name = DP[opc];
            let text = match opc {
                8..=11 => format!("{name}{c} {}, {op2}", reg(rn)),
                13 | 15 => format!("{name}{c}{} {}, {op2}", if s { "s" } else { "" }, reg(rd)),
                _ => format!("{name}{c}{} {}, {}, {op2}", if s { "s" } else { "" }, reg(rd), reg(rn)),
            };
            let writes_pc = rd == 15 && !(8..=11).contains(&opc);
            let flow = if writes_pc {
                // MOV PC, LR / SUBS PC, LR, #n are returns
                let ret = (opc == 13 && !imm && insn & 0xFFF == 14) || (opc == 2 && rn == 14);
                if always { Flow::Indirect { ret, thumb_switch: false } } else { Flow::CondIndirect { ret, thumb_switch: false } }
            } else {
                Flow::Next
            };
            mk(text, flow)
        }
        0b010 | 0b011 => {
            let reg_off = insn & (1 << 25) != 0;
            if reg_off && insn & 0x10 != 0 {
                return mk(format!(".word 0x{insn:08X}"), Flow::Undefined);
            }
            let (p, u, b, w, l) = (insn & (1 << 24) != 0, insn & (1 << 23) != 0, insn & (1 << 22) != 0, insn & (1 << 21) != 0, insn & (1 << 20) != 0);
            let rn = (insn >> 16) & 0xF;
            let rd = (insn >> 12) & 0xF;
            let off = if reg_off {
                format!("{}{}", if u { "" } else { "-" }, shifted_reg(insn))
            } else {
                let v = (insn & 0xFFF) as i64;
                format!("#{}", signed(if u { v } else { -v }))
            };
            let name = format!("{}{c}{}{}", if l { "ldr" } else { "str" }, if b { "b" } else { "" }, if !p && w { "t" } else { "" });
            let addr = if p {
                if reg_off || (insn & 0xFFF) != 0 || !u { format!("[{}, {off}]{}", reg(rn), if w { "!" } else { "" }) } else { format!("[{}]", reg(rn)) }
            } else {
                format!("[{}], {off}", reg(rn))
            };
            let text = format!("{name} {}, {addr}", reg(rd));
            let flow = if l && rd == 15 {
                if always { Flow::Indirect { ret: false, thumb_switch: false } } else { Flow::CondIndirect { ret: false, thumb_switch: false } }
            } else {
                Flow::Next
            };
            let _ = pc;
            mk(text, flow)
        }
        0b100 => {
            let (p, u, s, w, l) = (insn & (1 << 24) != 0, insn & (1 << 23) != 0, insn & (1 << 22) != 0, insn & (1 << 21) != 0, insn & (1 << 20) != 0);
            let rn = (insn >> 16) & 0xF;
            let list = insn & 0xFFFF;
            let mode = format!("{}{}", if u { "i" } else { "d" }, if p { "b" } else { "a" });
            let text = format!("{}{c}{mode} {}{}, {}{}", if l { "ldm" } else { "stm" }, reg(rn), if w { "!" } else { "" }, reglist(list), if s { "^" } else { "" });
            let flow = if l && list & 0x8000 != 0 {
                let ret = rn == 13;
                if always { Flow::Indirect { ret, thumb_switch: false } } else { Flow::CondIndirect { ret, thumb_switch: false } }
            } else {
                Flow::Next
            };
            mk(text, flow)
        }
        0b101 => {
            let off = (((insn & 0x00FF_FFFF) << 8) as i32 >> 6) as u32;
            let target = pc.wrapping_add(8).wrapping_add(off);
            if insn & (1 << 24) != 0 {
                mk(format!("bl{c} 0x{target:08X}"), if always { Flow::Call(target) } else { Flow::CondCall(target) })
            } else {
                mk(format!("b{c} 0x{target:08X}"), if always { Flow::Branch(target) } else { Flow::CondBranch(target) })
            }
        }
        0b111 if insn & (1 << 24) != 0 => {
            let n = insn & 0x00FF_FFFF;
            mk(format!("swi{c} 0x{n:X}"), Flow::Swi(n >> 16))
        }
        _ => mk(format!(".word 0x{insn:08X}"), Flow::Undefined),
    }
}

/// Multiplies, `SWP` and the halfword/signed transfers (bits 7 and 4 set, outside the data-processing space).
fn decode_extra(insn: u32, c: &str) -> Insn {
    let mk = |text: String, flow: Flow| Insn { size: 4, text, flow };
    let sh = (insn >> 5) & 3;
    if sh == 0 {
        if insn & 0x0FC0_00F0 == 0x0000_0090 {
            // MUL / MLA
            let (rd, rn, rs, rm) = ((insn >> 16) & 0xF, (insn >> 12) & 0xF, (insn >> 8) & 0xF, insn & 0xF);
            let s = if insn & (1 << 20) != 0 { "s" } else { "" };
            return if insn & (1 << 21) != 0 {
                mk(format!("mla{c}{s} {}, {}, {}, {}", reg(rd), reg(rm), reg(rs), reg(rn)), Flow::Next)
            } else {
                mk(format!("mul{c}{s} {}, {}, {}", reg(rd), reg(rm), reg(rs)), Flow::Next)
            };
        }
        if insn & 0x0F80_00F0 == 0x0080_0090 {
            // UMULL / UMLAL / SMULL / SMLAL
            let (hi, lo, rs, rm) = ((insn >> 16) & 0xF, (insn >> 12) & 0xF, (insn >> 8) & 0xF, insn & 0xF);
            let n = match ((insn >> 22) & 1, (insn >> 21) & 1) {
                (0, 0) => "umull",
                (0, 1) => "umlal",
                (1, 0) => "smull",
                _ => "smlal",
            };
            let s = if insn & (1 << 20) != 0 { "s" } else { "" };
            return mk(format!("{n}{c}{s} {}, {}, {}, {}", reg(lo), reg(hi), reg(rm), reg(rs)), Flow::Next);
        }
        if insn & 0x0FB0_0FF0 == 0x0100_0090 {
            let (rn, rd, rm) = ((insn >> 16) & 0xF, (insn >> 12) & 0xF, insn & 0xF);
            let b = if insn & (1 << 22) != 0 { "b" } else { "" };
            return mk(format!("swp{c}{b} {}, {}, [{}]", reg(rd), reg(rm), reg(rn)), Flow::Next);
        }
        return mk(format!(".word 0x{insn:08X}"), Flow::Undefined);
    }
    // halfword and signed byte/halfword transfers
    let (p, u, i, w, l) = (insn & (1 << 24) != 0, insn & (1 << 23) != 0, insn & (1 << 22) != 0, insn & (1 << 21) != 0, insn & (1 << 20) != 0);
    let rn = (insn >> 16) & 0xF;
    let rd = (insn >> 12) & 0xF;
    let name = match (l, sh) {
        (false, 1) => "strh",
        (true, 1) => "ldrh",
        (true, 2) => "ldrsb",
        (true, 3) => "ldrsh",
        _ => return mk(format!(".word 0x{insn:08X}"), Flow::Undefined),
    };
    let off = if i {
        let v = (((insn >> 4) & 0xF0) | (insn & 0xF)) as i64;
        format!("#{}", signed(if u { v } else { -v }))
    } else {
        format!("{}{}", if u { "" } else { "-" }, reg(insn & 0xF))
    };
    let addr = if p { format!("[{}, {off}]{}", reg(rn), if w { "!" } else { "" }) } else { format!("[{}], {off}", reg(rn)) };
    mk(format!("{name}{c} {}, {addr}", reg(rd)), Flow::Next)
}
