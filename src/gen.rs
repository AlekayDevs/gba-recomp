//! C generation (spike, Thumb only): one C function per discovered function, registers as locals, flags N, Z, C, V as locals.
//! `void f_XXXXXXXX(Cpu *c)`: loads the registers and flags from `c` at entry and stores them before every call and return.
//! A `BL` becomes `r14 = return address; store; call; load`, a return (`BX LR`, `POP {.., PC}`) stores and returns.

use crate::discover::Discovery;
use crate::rom::Rom;
use crate::{thumb, Flow};
use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Write;

fn r(i: u32) -> String {
    format!("r{i}")
}

fn cond(c: u32) -> &'static str {
    [
        "Z", "!Z", "C", "!C", "N", "!N", "V", "!V", "C && !Z", "!C || Z", "N == V", "N != V", "!Z && N == V", "Z || N != V",
    ][c as usize]
}

pub fn name(addr: u32) -> String {
    format!("f_{addr:08X}")
}

/// Generates the C function at `entry`, or says why it cannot (an instruction or a jump the spike does not translate).
/// `calls` receives the functions it calls.
pub fn function(rom: &Rom, d: &Discovery, entry: u32, calls: &mut BTreeSet<u32>, resume: Option<u32>) -> Result<String, String> {
    if d.code.get(&entry).is_some_and(|c| !c.0) {
        return crate::gen_arm::function(rom, d, entry, calls);
    }
    // 1. the instructions of the function: follow the flow, stopping at other functions
    let mut insns: BTreeMap<u32, (u16, u16)> = BTreeMap::new();
    let mut targets: BTreeSet<u32> = BTreeSet::new();
    let mut work = vec![entry];
    while let Some(mut pc) = work.pop() {
        loop {
            if insns.contains_key(&pc) {
                break;
            }
            let h = rom.read16(pc).ok_or("outside the ROM")?;
            // falls into another function: handled as a tail call when emitting. Except a `BX Rm` shared with that function (`POP {Rm}` right before
            // it is this function's return): it is part of this one too
            if pc != entry && d.functions.contains(&pc) && h & 0xFF87 != 0x4700 {
                break;
            }
            let next = rom.read16(pc + 2).unwrap_or(0);
            let i = thumb::decode(h, next, pc);
            insns.insert(pc, (h, next));
            match i.flow {
                Flow::Next | Flow::Call(_) => {}
                Flow::CondBranch(t) => {
                    targets.insert(t);
                    work.push(t);
                }
                Flow::Branch(t) => {
                    targets.insert(t);
                    work.push(t);
                    break;
                }
                Flow::Indirect { ret: true, .. } => break,
                Flow::Indirect { .. } => {
                    // `switch` table: every target is a block of this function (or a tail call); a literal `BX`: one target
                    if let Some(t) = d.tables.get(&pc) {
                        for &t in t {
                            targets.insert(t & !1);
                            work.push(t & !1);
                        }
                    } else if let Some(&t) = d.resolved.get(&pc) {
                        // ARM targets are functions of their own: they become tail calls
                        if t & 1 != 0 {
                            targets.insert(t & !1);
                            work.push(t & !1);
                        }
                    }
                    break;
                }
                Flow::Swi(_) => {}
                _ => return Err(format!("{} at 0x{pc:08X}", i.text)),
            }
            pc += i.size;
        }
    }

    // 2. emit
    let mut o = String::new();
    writeln!(o, "void {}(Cpu *c) {{", name(entry)).unwrap();
    writeln!(o, "    LOAD();").unwrap();
    writeln!(o, "    RT_ENTER(0x{entry:08X}u);").unwrap();
    writeln!(o, "    RT_DECL();").unwrap();
    // the return address at entry: a `bx rN` to it (after `pop {.., rN}`) is a return, not a tail call
    writeln!(o, "    u32 lr0 = r14; (void)lr0;").unwrap();
    // the instructions are emitted in address order: when code that belongs to this function sits before the entry, jump to it
    if insns.keys().next() != Some(&entry) {
        targets.insert(entry);
        writeln!(o, "    goto L_{entry:08X};").unwrap();
    }
    let indirect = |o: &mut String, pc: u32, rs: u32, calls: &mut BTreeSet<u32>| -> Result<(), String> {
        let tgt = |o: &mut String, t: u32, calls: &mut BTreeSet<u32>, indent: &str| {
            if t != entry && d.functions.contains(&t) {
                calls.insert(t);
                writeln!(o, "{indent}STORE(); {}(c); return;", name(t)).unwrap();
            } else {
                writeln!(o, "{indent}goto L_{t:08X};").unwrap();
            }
        };
        writeln!(o, "    // {pc:08X} indirect jump through {}", r(rs)).unwrap();
        if let Some(t) = d.tables.get(&pc) {
            writeln!(o, "    switch ({} & ~1u) {{", r(rs)).unwrap();
            let mut seen = BTreeSet::new();
            for &t in t {
                if seen.insert(t & !1) {
                    writeln!(o, "    case 0x{:08X}u:", t & !1).unwrap();
                    tgt(o, t & !1, calls, "        ");
                }
            }
            writeln!(o, "    default: if ({0} == lr0) {{ STORE(); return; }} STORE(); rt_site = 0x{pc:08X}u; rt_dispatch(c, {0}); return;
    }}", r(rs)).unwrap();
        } else if let Some(&t) = d.resolved.get(&pc) {
            tgt(o, t & !1, calls, "    ");
        } else {
            // the target is only known at run time: look it up among the translated functions (a tail call)
            if rs != 14 { writeln!(o, "    if ({} == lr0) {{ STORE(); return; }}", r(rs)).unwrap(); }
            writeln!(o, "    STORE(); rt_site = 0x{pc:08X}u; rt_dispatch(c, {}); return;", r(rs)).unwrap();
        }
        Ok(())
    };
    for (&pc, &(h, next)) in &insns {
        if targets.contains(&pc) {
            writeln!(o, "L_{pc:08X}:;").unwrap();
        }
        // the one place where save states are taken and loaded (the main loop of the game): the whole CPU is in registers here
        if resume == Some(pc) {
            writeln!(o, "    if (rt_state_req) {{ STORE(); rt_state_point(c); RELOAD(); }}").unwrap();
        }
        let i = thumb::decode(h, next, pc);
        let h = h as u32;
        let t3 = |sh: u32| (h >> sh) & 7;
        let jump = |o: &mut String, t: u32, calls: &mut BTreeSet<u32>, indent: &str| {
            if t != entry && d.functions.contains(&t) {
                calls.insert(t);
                writeln!(o, "{indent}STORE(); {}(c); return;", name(t)).unwrap();
            } else if t <= pc {
                writeln!(o, "{indent}YIELD_AT(0x{pc:08X}u); goto L_{t:08X};").unwrap();
            } else {
                writeln!(o, "{indent}goto L_{t:08X};").unwrap();
            }
        };
        let line = |o: &mut String, s: String| writeln!(o, "    {s} // {:08X} {}", pc, i.text.replace("*/", "")).unwrap();
        match h >> 11 {
            0b00000..=0b00010 => {
                let f = ["lsl_c", "lsr_c", "asr_c"][(h >> 11) as usize];
                let mut n = (h >> 6) & 0x1F;
                if n == 0 && h >> 11 != 0 {
                    n = 32;
                }
                line(&mut o, format!("{} = {f}({}, {n}, &C); SETNZ({});", r(t3(0)), r(t3(3)), r(t3(0))));
            }
            0b00011 => {
                let op = if h & (1 << 10) != 0 { format!("{}", t3(6)) } else { r(t3(6)) };
                let m = if h & (1 << 9) != 0 { "SUBS" } else { "ADDS" };
                line(&mut o, format!("{m}({}, {}, {op});", r(t3(0)), r(t3(3))));
            }
            0b00100..=0b00111 => {
                let rd = r(t3(8));
                let imm = h & 0xFF;
                line(
                    &mut o,
                    match (h >> 11) & 3 {
                        0 => format!("{rd} = {imm}; SETNZ({rd});"),
                        1 => format!("CMP({rd}, {imm});"),
                        2 => format!("ADDS({rd}, {rd}, {imm});"),
                        _ => format!("SUBS({rd}, {rd}, {imm});"),
                    },
                );
            }
            0b01000 => {
                if h & (1 << 10) == 0 {
                    let (rd, rs) = (r(t3(0)), r(t3(3)));
                    let s = match (h >> 6) & 0xF {
                        0 => format!("{rd} &= {rs}; SETNZ({rd});"),
                        1 => format!("{rd} ^= {rs}; SETNZ({rd});"),
                        2 => format!("{rd} = lsl_c({rd}, {rs} & 0xFF, &C); SETNZ({rd});"),
                        3 => format!("{rd} = lsr_c({rd}, {rs} & 0xFF, &C); SETNZ({rd});"),
                        4 => format!("{rd} = asr_c({rd}, {rs} & 0xFF, &C); SETNZ({rd});"),
                        5 => format!("ADCS({rd}, {rd}, {rs});"),
                        6 => format!("SBCS({rd}, {rd}, {rs});"),
                        7 => format!("{rd} = ror_c({rd}, {rs} & 0xFF, &C); SETNZ({rd});"),
                        8 => format!("{{ u32 t_ = {rd} & {rs}; SETNZ(t_); }}"),
                        9 => format!("SUBS({rd}, 0, {rs});"),
                        10 => format!("CMP({rd}, {rs});"),
                        11 => format!("CMN({rd}, {rs});"),
                        12 => format!("{rd} |= {rs}; SETNZ({rd});"),
                        13 => format!("{rd} *= {rs}; SETNZ({rd});"),
                        14 => format!("{rd} &= ~{rs}; SETNZ({rd});"),
                        _ => format!("{rd} = ~{rs}; SETNZ({rd});"),
                    };
                    line(&mut o, s);
                } else {
                    let rs = (h >> 3) & 0xF;
                    let rd = (h & 7) | ((h >> 4) & 8);
                    let val = |x: u32| if x == 15 { format!("0x{:08X}u", pc + 4) } else { r(x) };
                    match (h >> 8) & 3 {
                        2 if rd == 15 && rs == 14 => line(&mut o, "STORE(); return;".into()),
                        2 if rd == 15 => indirect(&mut o, pc, rs, calls)?,
                        0 if rd == 15 => return Err(format!("write to PC at 0x{pc:08X}")),
                        0 => line(&mut o, format!("{} += {};", r(rd), val(rs))),
                        1 => line(&mut o, format!("CMP({}, {});", val(rd), val(rs))),
                        2 => line(&mut o, format!("{} = {};", r(rd), val(rs))),
                        _ => {
                            // `POP {Rm}` then `BX Rm` is a return (ARMv4T cannot pop into PC and switch state)
                            let popped = rs < 8 && rom.read16(pc.wrapping_sub(2)).is_some_and(|p| p & 0xFF00 == 0xBC00 && p & (1 << rs) != 0 && insns.contains_key(&(pc - 2)));
                            // `MOV Rm, LR` somewhere in the function and `BX Rm`: the same, with the return address kept in a high register
                            let saved_lr = insns.values().any(|&(hh, _)| hh as u32 == (0x4670 | ((rs >> 3) << 7) | (rs & 7)));
                            if rs == 14 || popped || saved_lr {
                                line(&mut o, "STORE(); return;".into());
                            } else {
                                indirect(&mut o, pc, rs, calls)?;
                            }
                        }
                    }
                }
            }
            0b01001 => {
                let a = ((pc + 4) & !3) + (h & 0xFF) * 4;
                let v = rom.read32(a).ok_or("literal outside the ROM")?;
                line(&mut o, format!("{} = 0x{v:08X}u;", r(t3(8))));
            }
            0b01010 | 0b01011 => {
                let (rd, rb, ro) = (r(t3(0)), r(t3(3)), r(t3(6)));
                let a = format!("{rb} + {ro}");
                let s = match (h >> 9) & 7 {
                    0 => format!("wr32({a}, {rd});"),
                    1 => format!("wr16({a}, {rd});"),
                    2 => format!("wr8({a}, {rd});"),
                    3 => format!("{rd} = (u32)(s32)(int8_t)rd8({a});"),
                    4 => format!("{rd} = rd32({a});"),
                    5 => format!("{rd} = rd16({a});"),
                    6 => format!("{rd} = rd8({a});"),
                    _ => format!("{rd} = rdsh({a});"),
                };
                line(&mut o, s);
            }
            0b01100..=0b01111 => {
                let byte = h & (1 << 12) != 0;
                let load = h & (1 << 11) != 0;
                let off = ((h >> 6) & 0x1F) * if byte { 1 } else { 4 };
                let (rd, a) = (r(t3(0)), format!("{} + {off}", r(t3(3))));
                let s = match (load, byte) {
                    (false, false) => format!("wr32({a}, {rd});"),
                    (false, true) => format!("wr8({a}, {rd});"),
                    (true, false) => format!("{rd} = rd32({a});"),
                    (true, true) => format!("{rd} = rd8({a});"),
                };
                line(&mut o, s);
            }
            0b10000 | 0b10001 => {
                let a = format!("{} + {}", r(t3(3)), ((h >> 6) & 0x1F) * 2);
                let rd = r(t3(0));
                line(&mut o, if h & (1 << 11) != 0 { format!("{rd} = rd16({a});") } else { format!("wr16({a}, {rd});") });
            }
            0b10010 | 0b10011 => {
                let a = format!("r13 + {}", (h & 0xFF) * 4);
                let rd = r(t3(8));
                line(&mut o, if h & (1 << 11) != 0 { format!("{rd} = rd32({a});") } else { format!("wr32({a}, {rd});") });
            }
            0b10100 | 0b10101 => {
                let imm = (h & 0xFF) * 4;
                let rd = r(t3(8));
                line(&mut o, if h & (1 << 11) != 0 { format!("{rd} = r13 + {imm};") } else { format!("{rd} = 0x{:08X}u;", ((pc + 4) & !3) + imm) });
            }
            0b10110 | 0b10111 => {
                if h & 0xFF00 == 0xB000 {
                    let v = (h & 0x7F) * 4;
                    line(&mut o, format!("r13 {}= {v};", if h & 0x80 != 0 { '-' } else { '+' }));
                } else if h & 0xF600 == 0xB400 {
                    let list: Vec<u32> = (0..8).filter(|b| h & (1 << b) != 0).collect();
                    let pop = h & 0x800 != 0;
                    let extra = h & 0x100 != 0;
                    let n = list.len() as u32 + extra as u32;
                    if !pop {
                        let mut s = format!("r13 -= {};", n * 4);
                        for (k, &b) in list.iter().enumerate() {
                            write!(s, " wr32(r13 + {}, {});", k * 4, r(b)).unwrap();
                        }
                        if extra {
                            write!(s, " wr32(r13 + {}, r14);", list.len() * 4).unwrap();
                        }
                        line(&mut o, s);
                    } else {
                        let mut s = String::new();
                        for (k, &b) in list.iter().enumerate() {
                            write!(s, "{} = rd32(r13 + {}); ", r(b), k * 4).unwrap();
                        }
                        write!(s, "r13 += {};", n * 4).unwrap();
                        if extra {
                            write!(s, " STORE(); return;").unwrap();
                        }
                        line(&mut o, s);
                    }
                } else {
                    return Err(format!("{} at 0x{pc:08X}", i.text));
                }
            }
            0b11000 | 0b11001 => {
                let rb = (h >> 8) & 7;
                let list: Vec<u32> = (0..8).filter(|b| h & (1 << b) != 0).collect();
                if list.is_empty() || list.contains(&rb) {
                    return Err(format!("{} at 0x{pc:08X}", i.text));
                }
                let mut s = String::new();
                for (k, &b) in list.iter().enumerate() {
                    if h & (1 << 11) != 0 {
                        write!(s, "{} = rd32({} + {}); ", r(b), r(rb), k * 4).unwrap();
                    } else {
                        write!(s, "wr32({} + {}, {}); ", r(rb), k * 4, r(b)).unwrap();
                    }
                }
                write!(s, "{} += {};", r(rb), list.len() * 4).unwrap();
                line(&mut o, s);
            }
            0b11010 | 0b11011 => {
                if let Flow::Swi(n) = i.flow {
                    line(&mut o, format!("STORE(); rt_swi(c, {n}); RELOAD();"));
                    // fallthrough handled below
                } else {
                let Flow::CondBranch(t) = i.flow else { return Err(format!("{} at 0x{pc:08X}", i.text)) };
                writeln!(o, "    if ({}) {{ // {:08X} {}", cond((h >> 8) & 0xF), pc, i.text).unwrap();
                jump(&mut o, t, calls, "        ");
                writeln!(o, "    }}").unwrap();
                }
            }
            0b11100 => {
                let Flow::Branch(t) = i.flow else { unreachable!() };
                writeln!(o, "    // {:08X} {}", pc, i.text).unwrap();
                jump(&mut o, t, calls, "    ");
            }
            0b11110 => {
                let Flow::Call(t) = i.flow else { return Err(format!("{} at 0x{pc:08X}", i.text)) };
                calls.insert(t);
                line(&mut o, format!("r14 = 0x{:08X}u; RT_CHK_BEFORE(); STORE(); {}(c); RELOAD(); RT_CHK_AFTER(0x{pc:08X}u, 0x{t:08X}u);", (pc + 4) | 1, name(t)));
            }
            _ => return Err(format!("{} at 0x{pc:08X}", i.text)),
        }
        // falls through to something we did not follow: another function (tail call) or nothing we know
        let end = pc + i.size;
        if matches!(i.flow, Flow::Next | Flow::Call(_) | Flow::CondBranch(_) | Flow::Swi(_)) && !insns.contains_key(&end) {
            if d.functions.contains(&end) && end != entry {
                calls.insert(end);
                writeln!(o, "    STORE(); {}(c); return;", name(end)).unwrap();
            } else {
                return Err(format!("runs off the end at 0x{end:08X}"));
            }
        }
    }
    writeln!(o, "}}").unwrap();
    Ok(o)
}
