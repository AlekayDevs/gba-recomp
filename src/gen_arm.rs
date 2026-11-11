//! C generation for ARM (32-bit) functions: same shape as the Thumb one (`gen.rs`): registers and N Z C V as locals,
//! `if (cond) { ... }` around every conditional instruction, `goto` for local branches, calls and tail calls for the rest.

use crate::discover::Discovery;
use crate::gen::name;
use crate::rom::Rom;
use crate::{arm, Flow};
use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Write;

fn r(i: u32) -> String {
    format!("r{i}")
}

fn cond(c: u32) -> Option<&'static str> {
    Some(match c {
        0 => "Z",
        1 => "!Z",
        2 => "C",
        3 => "!C",
        4 => "N",
        5 => "!N",
        6 => "V",
        7 => "!V",
        8 => "C && !Z",
        9 => "!C || Z",
        10 => "N == V",
        11 => "N != V",
        12 => "!Z && N == V",
        13 => "Z || N != V",
        _ => return None, // 14: always
    })
}

/// A register read as an operand: PC reads as the address of the instruction plus 8.
fn val(i: u32, pc: u32) -> String {
    if i == 15 { format!("0x{:08X}u", pc.wrapping_add(8)) } else { r(i) }
}

/// The second operand of a data-processing instruction or a load/store offset with a shifted register.
/// Returns the C expression for the value and the one for the shifter carry (statements before them go in `pre`).
fn shifted(w: u32, pc: u32, pre: &mut String) -> Result<String, String> {
    let rm = val(w & 0xF, pc);
    let ty = (w >> 5) & 3;
    let f = ["lsl_c", "lsr_c", "asr_c", "ror_c"][ty as usize];
    if w & 0x10 != 0 {
        if w & 0xF == 15 || (w >> 8) & 0xF == 15 {
            return Err("PC in a register-shifted operand".into());
        }
        write!(pre, "b = {f}({rm}, {} & 0xFF, &sc); ", r((w >> 8) & 0xF)).unwrap();
        return Ok("b".into());
    }
    let n = (w >> 7) & 0x1F;
    match (ty, n) {
        (0, 0) => write!(pre, "b = {rm}; ").unwrap(),
        (3, 0) => write!(pre, "b = (C << 31) | ({rm} >> 1); sc = {rm} & 1; ").unwrap(),
        (1, 0) | (2, 0) => write!(pre, "b = {f}({rm}, 32, &sc); ").unwrap(),
        _ => write!(pre, "b = {f}({rm}, {n}, &sc); ").unwrap(),
    }
    Ok("b".into())
}

pub fn function(rom: &Rom, d: &Discovery, entry: u32, calls: &mut BTreeSet<u32>) -> Result<String, String> {
    let mut insns: BTreeMap<u32, u32> = BTreeMap::new();
    let mut targets: BTreeSet<u32> = BTreeSet::new();
    let mut work = vec![entry];
    while let Some(mut pc) = work.pop() {
        loop {
            if insns.contains_key(&pc) {
                break;
            }
            if pc != entry && d.functions.contains(&pc) {
                break;
            }
            let w = rom.read32(pc).ok_or("outside the ROM")?;
            let i = arm::decode(w, pc);
            insns.insert(pc, w);
            match i.flow {
                Flow::Next | Flow::Call(_) | Flow::CondCall(_) | Flow::Swi(_) | Flow::CondIndirect { .. } => {}
                Flow::CondBranch(t) => {
                    targets.insert(t);
                    work.push(t);
                }
                Flow::Branch(t) => {
                    targets.insert(t);
                    work.push(t);
                    break;
                }
                Flow::Indirect { .. } => {
                    // `MOV LR, PC` + `BX Rm` is a call through a register: the code goes on after it
                    if !insns.get(&pc.wrapping_sub(4)).is_some_and(|&p| p & 0x0FFF_FFFF == 0x01A0_E00F) {
                        break;
                    }
                }
                Flow::Undefined => return Err(format!("undefined at 0x{pc:08X}")),
            }
            pc += 4;
        }
    }

    let mut o = String::new();
    writeln!(o, "void {}(Cpu *c) {{", name(entry)).unwrap();
    writeln!(o, "    LOAD();").unwrap();
    writeln!(o, "    RT_ENTER(0x{entry:08X}u);").unwrap();
    writeln!(o, "    RT_DECL();").unwrap();
    if insns.keys().next() != Some(&entry) {
        targets.insert(entry);
        writeln!(o, "    goto L_{entry:08X};").unwrap();
    }
    for (&pc, &w) in &insns {
        if targets.contains(&pc) {
            writeln!(o, "L_{pc:08X}:;").unwrap();
        }
        let i = arm::decode(w, pc);
        let cc = w >> 28;
        if cc == 15 {
            return Err(format!("never-condition at 0x{pc:08X}"));
        }
        let guard = cond(cc);
        // `stmts` run only when the condition holds
        let mut s = String::new();
        let rd = (w >> 12) & 0xF;
        let rn = (w >> 16) & 0xF;
        let sbit = w & (1 << 20) != 0;
        // jumps to a known address: a local label or another function
        let jump = |s: &mut String, t: u32, calls: &mut BTreeSet<u32>| {
            if t != entry && d.functions.contains(&t) {
                calls.insert(t);
                write!(s, "STORE(); {}(c); return;", name(t)).unwrap();
            } else if t <= pc {
                write!(s, "YIELD_AT(0x{pc:08X}u); goto L_{t:08X};").unwrap();
            } else {
                write!(s, "goto L_{t:08X};").unwrap();
            }
        };
        let after_mov_lr_pc = insns.get(&pc.wrapping_sub(4)).is_some_and(|&p| p & 0x0FFF_FFFF == 0x01A0_E00F);
        let bx_ret = w & 0x0FFF_FFF0 == 0x012F_FF10;
        if bx_ret {
            let rm = w & 0xF;
            let saved_lr = insns.values().any(|&x| x & 0x0FFF_0FFF == (0x01A0_000E | (rm << 12)) && rm != 14);
            if rm == 14 || saved_lr {
                s.push_str("STORE(); return;");
            } else if let (Some(&t), true) = (d.resolved.get(&pc), after_mov_lr_pc) {
                calls.insert(t & !1);
                write!(s, "RT_CHK_BEFORE(); STORE(); {}(c); RELOAD(); RT_CHK_AFTER(0x{pc:08X}u, 0x{:08X}u);", name(t & !1), t & !1).unwrap();
            } else if let Some(&t) = d.resolved.get(&pc) {
                jump(&mut s, t & !1, calls);
            } else if after_mov_lr_pc {
                // `MOV LR, PC` + `BX Rm`: a call through a register (the callee returns to the next instruction)
                write!(s, "RT_CHK_BEFORE(); RT_TG({0}); STORE(); rt_site = 0x{pc:08X}u; rt_dispatch(c, {0}); RELOAD(); RT_CHK_AFTER(0x{pc:08X}u, tg_chk_);", r(rm)).unwrap();
            } else {
                write!(s, "STORE(); rt_site = 0x{pc:08X}u; rt_dispatch(c, {}); return;", r(rm)).unwrap();
            }
        } else {
            match (w >> 25) & 7 {
                0b000 | 0b001 => {
                    let imm = w & (1 << 25) != 0;
                    if !imm && w & 0x90 == 0x90 {
                        extra(&mut s, w, pc, rd, rn, sbit)?;
                    } else if !imm && w & 0x0FBF_0FFF == 0x010F_0000 {
                        let f = if w & (1 << 22) != 0 { "rt_mrs_spsr" } else { "rt_mrs" };
                        write!(s, "STORE(); {} = {f}(c);", r(rd)).unwrap();
                    } else if (!imm && w & 0x0FB0_FFF0 == 0x0120_F000) || (imm && w & 0x0FB0_F000 == 0x0320_F000) {
                        let f = if w & (1 << 22) != 0 { "rt_msr_spsr" } else { "rt_msr" };
                        let v = if imm { format!("0x{:08X}u", (w & 0xFF).rotate_right(((w >> 8) & 0xF) * 2)) } else { val(w & 0xF, pc) };
                        write!(s, "STORE(); {f}(c, 0x{:X}, {v}); RELOAD();", (w >> 16) & 0xF).unwrap();
                    } else {
                        data_processing(&mut s, w, pc, imm, rd, rn, sbit, calls)?;
                    }
                }
                0b010 | 0b011 => load_store(&mut s, w, pc, rom, rd, rn)?,
                0b100 => block_transfer(&mut s, w, pc, rn)?,
                0b101 => {
                    let (Flow::Branch(t) | Flow::CondBranch(t) | Flow::Call(t) | Flow::CondCall(t)) = i.flow else { unreachable!() };
                    if w & (1 << 24) != 0 {
                        calls.insert(t);
                        write!(s, "r14 = 0x{:08X}u; RT_CHK_BEFORE(); STORE(); {}(c); RELOAD(); RT_CHK_AFTER(0x{pc:08X}u, 0x{t:08X}u);", pc + 4, name(t)).unwrap();
                    } else {
                        jump(&mut s, t, calls);
                    }
                }
                0b111 if w & (1 << 24) != 0 => write!(s, "STORE(); rt_swi(c, {}); RELOAD();", (w & 0x00FF_FFFF) >> 16).unwrap(),
                _ => return Err(format!("{} at 0x{pc:08X}", i.text)),
            }
        }
        match guard {
            Some(g) => writeln!(o, "    if ({g}) {{ {s} }} // {pc:08X} {}", i.text).unwrap(),
            None => writeln!(o, "    {s} // {pc:08X} {}", i.text).unwrap(),
        }
        // falls through to something we did not follow: another function (tail call)
        let end = pc + 4;
        let falls = !matches!(i.flow, Flow::Branch(_)) && !(matches!(i.flow, Flow::Indirect { .. }) && !after_mov_lr_pc);
        if falls && !insns.contains_key(&end) {
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

#[allow(clippy::too_many_arguments)]
fn data_processing(
    s: &mut String,
    w: u32,
    pc: u32,
    imm: bool,
    rd: u32,
    rn: u32,
    sbit: bool,
    _calls: &mut BTreeSet<u32>,
) -> Result<(), String> {
    let opc = (w >> 21) & 0xF;
    let mut pre = String::from("u32 sc = C, b; ");
    let b = if imm {
        let rot = ((w >> 8) & 0xF) * 2;
        let v = (w & 0xFF).rotate_right(rot);
        if rot != 0 {
            write!(pre, "sc = {}; ", v >> 31).unwrap();
        }
        format!("0x{v:08X}u")
    } else {
        shifted(w, pc, &mut pre)?
    };
    let a = val(rn, pc);
    let test = (8..=11).contains(&opc);
    if sbit && rd == 15 && !test {
        return Err(format!("S-form write to PC (exception return) at 0x{pc:08X}"));
    }
    let dst = if rd == 15 && !test { "t_".to_string() } else { r(rd) };
    let mut body = String::new();
    // arithmetic operations set C and V from the operation, the logical ones C from the shifter
    let logic = |expr: String, body: &mut String| {
        if test {
            write!(body, "{{ u32 t2_ = {expr}; SETNZ(t2_); C = sc; }}").unwrap();
        } else if sbit {
            write!(body, "{dst} = {expr}; SETNZ({dst}); C = sc;").unwrap();
        } else {
            write!(body, "{dst} = {expr};").unwrap();
        }
    };
    match opc {
        0 | 8 => logic(format!("{a} & {b}"), &mut body),
        1 | 9 => logic(format!("{a} ^ {b}"), &mut body),
        12 => logic(format!("{a} | {b}"), &mut body),
        14 => logic(format!("{a} & ~{b}"), &mut body),
        13 => logic(b.clone(), &mut body),
        15 => logic(format!("~{b}"), &mut body),
        2 | 3 | 4 | 10 | 11 => {
            let (x, y) = if opc == 3 { (b.clone(), a.clone()) } else { (a.clone(), b.clone()) };
            if opc == 10 {
                write!(body, "CMP({x}, {y});").unwrap();
            } else if opc == 11 {
                write!(body, "CMN({x}, {y});").unwrap();
            } else if sbit {
                write!(body, "{}({dst}, {x}, {y});", if opc == 4 { "ADDS" } else { "SUBS" }).unwrap();
            } else {
                write!(body, "{dst} = {x} {} {y};", if opc == 4 { '+' } else { '-' }).unwrap();
            }
        }
        5 => {
            if sbit { write!(body, "ADCS({dst}, {a}, {b});").unwrap() } else { write!(body, "{dst} = {a} + {b} + C;").unwrap() }
        }
        6 | 7 => {
            let (x, y) = if opc == 7 { (b.clone(), a.clone()) } else { (a.clone(), b.clone()) };
            if sbit { write!(body, "SBCS({dst}, {x}, {y});").unwrap() } else { write!(body, "{dst} = {x} - {y} - (1 - C);").unwrap() }
        }
        _ => unreachable!(),
    }
    write!(s, "{{ {pre}{body} }}").unwrap();
    if rd == 15 && !test {
        // a write to PC: MOV PC, LR returns, anything else is a jump to a run-time address
        if opc == 13 && !imm && w & 0xFFF == 14 {
            write!(s, " STORE(); return;").unwrap();
        } else {
            write!(s, " STORE(); rt_site = 0x{pc:08X}u; rt_dispatch(c, t_); return;").unwrap();
        }
    }
    if rd == 15 && !test {
        // `t_` must exist: wrap in a block that declares it
        *s = format!("{{ u32 t_; {s} }}");
    }
    Ok(())
}

fn load_store(s: &mut String, w: u32, pc: u32, rom: &Rom, rd: u32, rn: u32) -> Result<(), String> {
    let reg_off = w & (1 << 25) != 0;
    if reg_off && w & 0x10 != 0 {
        return Err("undefined".into());
    }
    let (p, u, byte, wb, load) = (w & (1 << 24) != 0, w & (1 << 23) != 0, w & (1 << 22) != 0, w & (1 << 21) != 0, w & (1 << 20) != 0);
    if !p && wb {
        return Err(format!("LDRT/STRT at 0x{pc:08X}"));
    }
    // `ldr Rd, [pc, #imm]` into a constant when the literal is in the ROM
    if load && !byte && rn == 15 && p && !wb && !reg_off && rd != 15 {
        let imm = w & 0xFFF;
        let base = pc.wrapping_add(8);
        let a = if u { base.wrapping_add(imm) } else { base.wrapping_sub(imm) };
        if let Some(v) = rom.read32(a) {
            write!(s, "{} = 0x{v:08X}u;", r(rd)).unwrap();
            return Ok(());
        }
    }
    let mut pre = String::from("u32 sc = C, b; (void)sc; ");
    let off = if reg_off {
        let v = shifted(w, pc, &mut pre)?;
        v
    } else {
        format!("{}", w & 0xFFF)
    };
    if rd == 15 && !load {
        return Err(format!("STR of PC at 0x{pc:08X}"));
    }
    let base = val(rn, pc);
    let mut t = format!("{{ {pre}u32 a_ = {base}, e_ = a_ {} {off}, ad_ = {};", if u { '+' } else { '-' }, if p { "e_" } else { "a_" });
    if load {
        let rdv = if rd == 15 { "t_".to_string() } else { r(rd) };
        write!(t, " u32 t_ = {}(ad_);", if byte { "rd8" } else { "rd32" }).unwrap();
        if !p || wb {
            write!(t, " {} = e_;", r(rn)).unwrap();
        }
        if rd == 15 {
            write!(t, " STORE(); rt_site = 0x{pc:08X}u; rt_dispatch(c, t_); return; }}").unwrap();
        } else {
            write!(t, " {rdv} = t_; }}").unwrap();
        }
    } else {
        write!(t, " u32 v_ = {}; {}(ad_, v_);", val(rd, pc), if byte { "wr8" } else { "wr32" }).unwrap();
        if !p || wb {
            write!(t, " {} = e_;", r(rn)).unwrap();
        }
        t.push_str(" }");
    }
    s.push_str(&t);
    Ok(())
}

fn block_transfer(s: &mut String, w: u32, pc: u32, rn: u32) -> Result<(), String> {
    let (p, u, psr, wb, load) = (w & (1 << 24) != 0, w & (1 << 23) != 0, w & (1 << 22) != 0, w & (1 << 21) != 0, w & (1 << 20) != 0);
    let list: Vec<u32> = (0..16).filter(|b| w & (1 << b) != 0).collect();
    if psr {
        return Err(format!("LDM/STM with ^ at 0x{pc:08X}"));
    }
    if list.is_empty() || rn == 15 {
        return Err(format!("odd LDM/STM at 0x{pc:08X}"));
    }
    if !load && list.contains(&15) {
        return Err(format!("STM of PC at 0x{pc:08X}"));
    }
    let n = list.len() as u32 * 4;
    let base = r(rn);
    // the lowest address that is transferred
    let start = match (u, p) {
        (true, false) => format!("{base}"),
        (true, true) => format!("{base} + 4"),
        (false, false) => format!("{base} - {} + 4", n),
        (false, true) => format!("{base} - {n}"),
    };
    let newbase = if u { format!("{base} + {n}") } else { format!("{base} - {n}") };
    let mut t = format!("{{ u32 a_ = {start}, nb_ = {newbase};");
    if !load && wb && list.contains(&rn) && list[0] != rn {
        return Err(format!("STM with the base register in the list at 0x{pc:08X}"));
    }
    let mut pcv = false;
    for (k, &b) in list.iter().enumerate() {
        if load {
            if b == 15 {
                write!(t, " u32 pcv_ = rd32(a_ + {});", k * 4).unwrap();
                pcv = true;
            } else {
                write!(t, " {} = rd32(a_ + {});", r(b), k * 4).unwrap();
            }
        } else {
            write!(t, " wr32(a_ + {}, {});", k * 4, val(b, pc)).unwrap();
        }
    }
    if wb && !(load && list.contains(&rn)) {
        write!(t, " {base} = nb_;").unwrap();
    }
    if pcv {
        // LDM {..., PC} with SP is a return; anything else jumps to a run-time address
        if rn == 13 {
            t.push_str(" STORE(); return;");
        } else {
            write!(t, " STORE(); rt_site = 0x{pc:08X}u; rt_dispatch(c, pcv_); return;").unwrap();
        }
    }
    t.push_str(" }");
    s.push_str(&t);
    Ok(())
}

/// Multiplies, `SWP` and the halfword / signed transfers.
fn extra(s: &mut String, w: u32, pc: u32, rd: u32, rn: u32, sbit: bool) -> Result<(), String> {
    let sh = (w >> 5) & 3;
    if sh == 0 {
        if w & 0x0FC0_00F0 == 0x0000_0090 {
            let (rd, rn, rs, rm) = ((w >> 16) & 0xF, (w >> 12) & 0xF, (w >> 8) & 0xF, w & 0xF);
            if [rd, rn, rs, rm].contains(&15) {
                return Err("PC in a multiply".into());
            }
            let acc = if w & (1 << 21) != 0 { format!(" + {}", r(rn)) } else { String::new() };
            write!(s, "{} = {} * {}{acc};", r(rd), r(rm), r(rs)).unwrap();
            if sbit {
                write!(s, " SETNZ({});", r(rd)).unwrap();
            }
        } else if w & 0x0F80_00F0 == 0x0080_0090 {
            let (hi, lo, rs, rm) = ((w >> 16) & 0xF, (w >> 12) & 0xF, (w >> 8) & 0xF, w & 0xF);
            if [hi, lo, rs, rm].contains(&15) {
                return Err("PC in a long multiply".into());
            }
            let signed = w & (1 << 22) != 0;
            let acc = w & (1 << 21) != 0;
            let (ty, tys) = if signed { ("int64_t", "(int64_t)(s32)") } else { ("u64", "(u64)") };
            write!(s, "{{ {ty} p_ = {tys}{} * {tys}{};", r(rm), r(rs)).unwrap();
            if acc {
                write!(s, " p_ += (({ty})(((u64){} << 32) | {}));", r(hi), r(lo)).unwrap();
            }
            write!(s, " {} = (u32)((u64)p_ >> 32); {} = (u32)p_;", r(hi), r(lo)).unwrap();
            if sbit {
                write!(s, " N = {} >> 31; Z = ((u64)p_ == 0);", r(hi)).unwrap();
            }
            s.push_str(" }");
        } else if w & 0x0FB0_0FF0 == 0x0100_0090 {
            let rm = w & 0xF;
            let byte = w & (1 << 22) != 0;
            let (rd_, rn_) = (rd, rn);
            let (rd_f, wr_f) = if byte { ("rd8", "wr8") } else { ("rd32", "wr32") };
            write!(s, "{{ u32 a_ = {}, t_ = {rd_f}(a_); {wr_f}(a_, {}); {} = t_; }}", r(rn_), r(rm), r(rd_)).unwrap();
        } else {
            return Err(format!("undefined at 0x{pc:08X}"));
        }
        return Ok(());
    }
    let (p, u, imm, wb, load) = (w & (1 << 24) != 0, w & (1 << 23) != 0, w & (1 << 22) != 0, w & (1 << 21) != 0, w & (1 << 20) != 0);
    if rd == 15 || (!p && wb) {
        return Err(format!("odd halfword transfer at 0x{pc:08X}"));
    }
    let off = if imm { format!("{}", ((w >> 4) & 0xF0) | (w & 0xF)) } else { r(w & 0xF) };
    let base = val(rn, pc);
    let mut t = format!("{{ u32 a_ = {base}, e_ = a_ {} {off}, ad_ = {};", if u { '+' } else { '-' }, if p { "e_" } else { "a_" });
    if load {
        let f = match sh {
            1 => "rd16",
            2 => "rdsb",
            _ => "rdsh",
        };
        write!(t, " u32 t_ = {f}(ad_);").unwrap();
        if !p || wb {
            write!(t, " {} = e_;", r(rn)).unwrap();
        }
        write!(t, " {} = t_; }}", r(rd)).unwrap();
    } else {
        if sh != 1 {
            return Err("undefined".into());
        }
        write!(t, " u32 v_ = {}; wr16(ad_, v_);", val(rd, pc)).unwrap();
        if !p || wb {
            write!(t, " {} = e_;", r(rn)).unwrap();
        }
        t.push_str(" }");
    }
    s.push_str(&t);
    Ok(())
}
