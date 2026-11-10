//! Code discovery: follows the flow of control from the entry point of the ROM, in ARM and Thumb, and reports what it found.
//! A first pass, not the final analyzer: `BX Rm` is resolved only when `Rm` was loaded from a literal pool a few instructions earlier.

use crate::rom::Rom;
use crate::{arm, thumb, Flow};
use std::collections::{BTreeMap, BTreeSet, VecDeque};

#[derive(Default)]
pub struct Discovery {
    /// Every instruction reached: address -> (thumb, size).
    pub code: BTreeMap<u32, (bool, u32)>,
    /// Entry points of functions: the entry of the ROM and the targets of calls.
    pub functions: BTreeSet<u32>,
    /// Block starts that are not functions (targets of branches).
    pub blocks: BTreeSet<u32>,
    /// `BX Rm` / writes to PC whose target is unknown: address -> thumb.
    pub unresolved: BTreeMap<u32, bool>,
    /// `BX Rm` resolved through a literal: address -> target.
    pub resolved: BTreeMap<u32, u32>,
    /// Addresses we tried to follow that are outside the ROM (BIOS, IWRAM, EWRAM...): they run from RAM or the BIOS.
    pub outside: BTreeSet<u32>,
    /// BIOS calls used.
    pub swis: BTreeMap<u32, u32>,
    pub undefined: BTreeSet<u32>,
}

/// If `insn` is an ARM `LDR Rd, [PC, #±imm]`, the register and the address of the literal.
fn arm_literal(insn: u32, pc: u32) -> Option<(u32, u32)> {
    if insn >> 28 == 15 || insn & 0x0F7F_0000 != 0x051F_0000 {
        return None;
    }
    let imm = insn & 0xFFF;
    let base = pc.wrapping_add(8);
    let addr = if insn & (1 << 23) != 0 { base.wrapping_add(imm) } else { base.wrapping_sub(imm) };
    Some(((insn >> 12) & 0xF, addr))
}

pub fn discover(rom: &Rom, entry: u32) -> Discovery {
    discover_from(rom, &[(entry, false)])
}

/// Thumb function pointers found in the data of the ROM: aligned words, odd, that point at a `PUSH {..., LR}`.
/// The pointer tables and literal pools that the flow cannot reach hold the functions that are called indirectly.
pub fn thumb_pointers(rom: &Rom) -> Vec<u32> {
    let mut out = BTreeSet::new();
    let mut a = 0u32;
    while let Some(w) = rom.read32(crate::rom::ROM_BASE + a) {
        if w & 1 == 1 && (0x0800_0000..0x0800_0000 + rom.data.len() as u32).contains(&w) {
            if rom.read16(w & !1).is_some_and(|h| h & 0xFF00 == 0xB500) {
                out.insert(w & !1);
            }
        }
        a += 4;
    }
    out.into_iter().collect()
}

pub fn discover_from(rom: &Rom, roots: &[(u32, bool)]) -> Discovery {
    let mut d = Discovery::default();
    let mut work: VecDeque<(u32, bool)> = VecDeque::new();
    for &(a, t) in roots {
        d.functions.insert(a);
        work.push_back((a, t));
    }

    while let Some((start, thumb)) = work.pop_front() {
        let mut pc = start;
        // register -> (constant, instructions ago) from the literals loaded just before
        let mut lits: Vec<(u32, u32, u32)> = Vec::new();
        let mut prev = String::new();
        loop {
            if d.code.contains_key(&pc) {
                break;
            }
            if rom.offset(pc).is_none() {
                d.outside.insert(pc);
                break;
            }
            let (size, flow, lit, text) = if thumb {
                let Some(h) = rom.read16(pc) else { break };
                let next = rom.read16(pc + 2).unwrap_or(0);
                let i = thumb::decode(h, next, pc);
                let lit = if h >> 11 == 0b01001 {
                    let addr = ((pc + 4) & !3) + (h as u32 & 0xFF) * 4;
                    rom.read32(addr).map(|v| ((h as u32 >> 8) & 7, v))
                } else {
                    None
                };
                (i.size, i.flow, lit, i.text)
            } else {
                let Some(w) = rom.read32(pc) else { break };
                let i = arm::decode(w, pc);
                let lit = arm_literal(w, pc).and_then(|(rd, a)| rom.read32(a).map(|v| (rd, v)));
                (i.size, i.flow, lit, i.text)
            };
            d.code.insert(pc, (thumb, size));
            for l in lits.iter_mut() {
                l.2 += 1;
            }
            lits.retain(|l| l.2 <= 4);
            // an instruction that writes a register drops the literal it held
            lits.retain(|l| !writes(&text, l.0));
            if let Some((rd, v)) = lit {
                lits.retain(|l| l.0 != rd);
                lits.push((rd, v, 0));
            }
            let mut push = |d: &mut Discovery, t: u32, th: bool, func: bool| {
                if rom.offset(t).is_none() {
                    d.outside.insert(t);
                    return;
                }
                if func {
                    d.functions.insert(t);
                } else {
                    d.blocks.insert(t);
                }
                if !d.code.contains_key(&t) {
                    work.push_back((t, th));
                }
            };
            match flow {
                Flow::Next | Flow::CondIndirect { .. } => {}
                Flow::Swi(n) => *d.swis.entry(n).or_insert(0) += 1,
                Flow::CondBranch(t) => push(&mut d, t, thumb, false),
                Flow::Call(t) | Flow::CondCall(t) => push(&mut d, t, thumb, true),
                Flow::Branch(t) => {
                    push(&mut d, t, thumb, false);
                    break;
                }
                Flow::Indirect { ret, thumb_switch } => {
                    // BX Rm with Rm loaded from a literal a few instructions earlier
                    let rm = if thumb_switch && !ret {
                        if thumb {
                            rom.read16(pc).map(|h| (h as u32 >> 3) & 0xF)
                        } else {
                            rom.read32(pc).map(|w| w & 0xF)
                        }
                    } else {
                        None
                    };
                    let resolved = rm.and_then(|rm| lits.iter().find(|l| l.0 == rm).map(|l| l.1));
                    // `POP {Rm}` then `BX Rm` is a return
                    let popped = rm.is_some_and(|rm| prev.starts_with("pop") && prev.contains(&arm::reg(rm)));
                    match resolved {
                        Some(t) => {
                            d.resolved.insert(pc, t);
                            push(&mut d, t & !1, t & 1 != 0, true);
                        }
                        None if !ret && !popped => {
                            d.unresolved.insert(pc, thumb);
                        }
                        None => {}
                    }
                    break;
                }
                Flow::Undefined => {
                    d.undefined.insert(pc);
                    break;
                }
            }
            prev = text;
            pc += size;
        }
    }
    d
}

/// Whether the instruction `text` writes register `r` (first operand, or the list of a `pop` / `ldm`). Conservative: a store also counts.
fn writes(text: &str, r: u32) -> bool {
    let name = arm::reg(r);
    let mut it = text.splitn(2, ' ');
    let op = it.next().unwrap_or("");
    let rest = it.next().unwrap_or("");
    if ["bx", "cmp", "cmn", "tst", "teq", "str", "push", "stm", "swi"].iter().any(|n| op.starts_with(n)) && !op.starts_with("strh") {
        return false;
    }
    if op.starts_with("pop") || op.starts_with("ldm") {
        return rest.split(|c: char| !c.is_alphanumeric()).any(|w| w == name);
    }
    rest.split(',').next().is_some_and(|f| f.trim().trim_end_matches('!') == name)
}
