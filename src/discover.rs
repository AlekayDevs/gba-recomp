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
    /// Candidates from the data that were discarded because the code they reach does not decode.
    pub rejected: BTreeSet<u32>,
    /// Recognised `switch` tables: address of the indirect jump -> targets.
    pub tables: BTreeMap<u32, Vec<u32>>,
    /// Where the `switch` tables live: base address -> number of entries.
    pub table_data: BTreeMap<u32, u32>,
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
        if w & 1 == 1 && rom.offset(w & !1).is_some() {
            if rom.read16(w & !1).is_some_and(|h| h & 0xFF00 == 0xB500) {
                out.insert(w & !1);
            }
        }
        a += 4;
    }
    out.into_iter().collect()
}

/// ARM function pointers in the data: aligned even words that point at an `STMFD SP!, {..., LR}`.
pub fn arm_pointers(rom: &Rom) -> Vec<u32> {
    let mut out = BTreeSet::new();
    let mut a = 0u32;
    while let Some(w) = rom.read32(crate::rom::ROM_BASE + a) {
        if w & 3 == 0 && rom.offset(w).is_some() && rom.read32(w).is_some_and(|i| i & 0xFFFF_4000 == 0xE92D_4000) {
            out.insert(w);
        }
        a += 4;
    }
    out.into_iter().collect()
}

pub fn discover_from(rom: &Rom, roots: &[(u32, bool)]) -> Discovery {
    discover_with(rom, roots, &[])
}

/// Roots that are certain (the entry, calls) are followed first; the candidates found in the data (`candidates`) are each tried on their own
/// and kept only if the code they reach decodes cleanly: no undefined instruction, and everything stays in the ROM or the overlays.
pub fn discover_with(rom: &Rom, roots: &[(u32, bool)], candidates: &[(u32, bool)]) -> Discovery {
    let mut d = Discovery::default();
    let mut work: VecDeque<(u32, bool)> = VecDeque::new();
    for &(a, t) in roots {
        let a = canon(a);
        d.functions.insert(a);
        work.push_back((a, t));
    }
    walk(rom, &mut d, None, work);
    for &(a, t) in candidates {
        let a = canon(a);
        if d.code.contains_key(&a) {
            d.functions.insert(a);
            continue;
        }
        let mut t_d = Discovery::default();
        t_d.functions.insert(a);
        walk(rom, &mut t_d, Some(&d), VecDeque::from([(a, t)]));
        if t_d.undefined.is_empty() && t_d.outside.is_empty() && t_d.code.len() >= 2 {
            d.merge(t_d);
        } else {
            d.rejected.insert(a);
        }
    }
    d
}

impl Discovery {
    fn merge(&mut self, o: Discovery) {
        self.code.extend(o.code);
        self.functions.extend(o.functions);
        self.blocks.extend(o.blocks);
        self.unresolved.extend(o.unresolved);
        self.resolved.extend(o.resolved);
        self.outside.extend(o.outside);
        for (k, v) in o.swis {
            *self.swis.entry(k).or_insert(0) += v;
        }
        self.tables.extend(o.tables);
        self.table_data.extend(o.table_data);
    }
}

fn walk(rom: &Rom, d: &mut Discovery, base: Option<&Discovery>, mut work: VecDeque<(u32, bool)>) {
    while let Some((start, thumb)) = work.pop_front() {
        let mut pc = start;
        // register -> (constant, instructions ago) from the literals loaded just before
        let mut lits: Vec<(u32, u32, u32)> = Vec::new();
        let mut prev = String::new();
        // ARM: `MOV R12, LR` seen in this run, so a later `BX R12` is the return of the function (the usual GCC/ARM idiom)
        let mut r12_is_lr = false;
        // the last Thumb halfwords, to recognise the `switch` sequence
        let mut hist: Vec<u16> = Vec::new();
        loop {
            if d.code.contains_key(&pc) || base.is_some_and(|b| b.code.contains_key(&pc)) {
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
                } else if h >> 11 == 0b10100 {
                    // ADD Rd, PC, #imm: the address of code that follows, to be reached with BX
                    Some(((h as u32 >> 8) & 7, ((pc + 4) & !3) + (h as u32 & 0xFF) * 4))
                } else {
                    None
                };
                (i.size, i.flow, lit, i.text)
            } else {
                let Some(w) = rom.read32(pc) else { break };
                let i = arm::decode(w, pc);
                let lit = arm_literal(w, pc).and_then(|(rd, a)| rom.read32(a).map(|v| (rd, v))).or_else(|| {
                    // ADD Rd, PC, #imm (the usual `adr` + `bx` to switch to Thumb)
                    if w >> 28 != 15 && w & 0x0FFF_0000 == 0x028F_0000 {
                        let imm = (w & 0xFF).rotate_right(((w >> 8) & 0xF) * 2);
                        Some(((w >> 12) & 0xF, pc.wrapping_add(8).wrapping_add(imm)))
                    } else {
                        None
                    }
                });
                (i.size, i.flow, lit, i.text)
            };
            d.code.insert(pc, (thumb, size));
            let is_bx_r12 = !thumb && text == "bx r12";
            if !thumb && !is_bx_r12 {
                if text == "mov r12, lr" {
                    r12_is_lr = true;
                } else if writes(&text, 12) {
                    r12_is_lr = false;
                }
            }
            for l in lits.iter_mut() {
                l.2 += 1;
            }
            lits.retain(|l| l.2 <= 6);
            // an instruction that writes a register drops the literal it held
            lits.retain(|l| !writes(&text, l.0));
            if let Some((rd, v)) = lit {
                lits.retain(|l| l.0 != rd);
                lits.push((rd, v, 0));
            }
            let mut push = |d: &mut Discovery, t: u32, th: bool, func: bool| {
                let t = canon(t);
                if rom.offset(t).is_none() {
                    d.outside.insert(t);
                    return;
                }
                if func {
                    d.functions.insert(t);
                } else {
                    d.blocks.insert(t);
                }
                if !d.code.contains_key(&t) && !base.is_some_and(|b| b.code.contains_key(&t)) {
                    work.push_back((t, th));
                }
            };
            match flow {
                Flow::Next | Flow::CondIndirect { .. } => {}
                Flow::Swi(n) => *d.swis.entry(n).or_insert(0) += 1,
                Flow::CondBranch(t) => push(d, t, thumb, false),
                Flow::Call(t) | Flow::CondCall(t) => push(d, t, thumb, true),
                Flow::Branch(t) => {
                    push(d, t, thumb, false);
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
                    let popped = rm.is_some_and(|rm| prev.starts_with("pop") && prev.contains(&arm::reg(rm))) || (is_bx_r12 && r12_is_lr);
                    match resolved {
                        Some(t) => {
                            d.resolved.insert(pc, t);
                            push(d, t & !1, t & 1 != 0, true);
                        }
                        None if !ret && !popped => {
                            // `CMP Rx, #N` ... `LDR Rt, =table` ... `LDR Rx, [Rx]` ... `MOV PC, Rx`: a table of N+1 targets
                            let table = if thumb { switch_table(rom, pc) } else { None };
                            match table {
                                Some((base, targets)) => {
                                    d.table_data.insert(base, targets.len() as u32);
                                    for &t in &targets {
                                        push(d, t & !1, true, false);
                                    }
                                    d.tables.insert(pc, targets);
                                }
                                None => {
                                    d.unresolved.insert(pc, thumb);
                                }
                            }
                        }
                        None => {}
                    }
                    // `MOV LR, PC` + `BX Rm` / `LDR PC, ..`: a call through a register, the code goes on after it
                    if !(!thumb && prev == "mov lr, pc") {
                        break;
                    }
                }
                Flow::Undefined => {
                    d.undefined.insert(pc);
                    break;
                }
            }
            if thumb {
                if let Some(h) = rom.read16(pc) {
                    hist.push(h);
                    if hist.len() > 12 {
                        hist.remove(0);
                    }
                }
            }
            prev = text;
            pc += size;
        }
    }
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

/// Looks back in the code for the compare that bounds the index (`CMP Rx, #N`) and the literal that holds the base of the table,
/// reading the halfwords before the jump (the block may have been entered after the compare, at the `BLS` target).
fn switch_table(rom: &Rom, pc: u32) -> Option<(u32, Vec<u32>)> {
    let mut n = None;
    let mut base = None;
    for i in 1..=12u32 {
        let at = pc.checked_sub(2 * i)?;
        let h = rom.read16(at)? as u32;
        if n.is_none() && h & 0xF800 == 0x2800 {
            n = Some(h & 0xFF);
        }
        if base.is_none() && h >> 11 == 0b01001 {
            let v = rom.read32(((at + 4) & !3) + (h & 0xFF) * 4)?;
            if rom.offset(v).is_some() && v & 3 == 0 {
                base = Some(v);
            }
        }
    }
    let (n, base) = (n?, base?);
    let mut out = Vec::new();
    for i in 0..=n {
        let t = rom.read32(base + i * 4)?;
        // every target must be in the ROM and near the jump (a table of data would fail here)
        rom.offset(t & !1)?;
        if (t & 0x01FF_FFFF).abs_diff(pc & 0x01FF_FFFF) > 0x10000 {
            return None;
        }
        out.push(t);
    }
    Some((base, out))
}

/// The address of ROM code seen through the mirrors (`$0A000000`, `$0C000000`) as `$08000000`.
pub fn canon(a: u32) -> u32 {
    if (0x0A00_0000..0x0E00_0000).contains(&a) { 0x0800_0000 | (a & 0x01FF_FFFF) } else { a }
}
