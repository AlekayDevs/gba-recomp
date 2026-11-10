//! Thumb (16-bit) instruction decoder for the ARM7TDMI: text for a disassembly and the flow of control.
//! `BL` is a pair of halfwords: pass the second one in `next` and the decoder returns a 4-byte instruction.

use crate::arm::reg;
use crate::{Flow, Insn};

const COND: [&str; 16] = ["eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "", ""];
const ALU: [&str; 16] = ["and", "eor", "lsl", "lsr", "asr", "adc", "sbc", "ror", "tst", "neg", "cmp", "cmn", "orr", "mul", "bic", "mvn"];

fn list(mask: u32, extra: Option<&str>) -> String {
    let mut parts: Vec<String> = (0..8).filter(|i| mask & (1 << i) != 0).map(|i| format!("r{i}")).collect();
    if let Some(e) = extra {
        parts.push(e.to_string());
    }
    format!("{{{}}}", parts.join(", "))
}

/// Decodes the halfword `h` at `pc`; `next` is the halfword after it (needed for the `BL` pair).
pub fn decode(h: u16, next: u16, pc: u32) -> Insn {
    let h = h as u32;
    let mk = |text: String, flow: Flow| Insn { size: 2, text, flow };
    let r3 = |sh: u32| reg((h >> sh) & 7);
    match h >> 11 {
        // shift by immediate
        0b00000..=0b00010 => {
            let name = ["lsl", "lsr", "asr"][(h >> 11) as usize];
            let mut amt = (h >> 6) & 0x1F;
            if amt == 0 && h >> 11 != 0 {
                amt = 32;
            }
            mk(format!("{name}s {}, {}, #{amt}", r3(0), r3(3)), Flow::Next)
        }
        // add / subtract
        0b00011 => {
            let name = if h & (1 << 9) != 0 { "subs" } else { "adds" };
            let op = if h & (1 << 10) != 0 { format!("#{}", (h >> 6) & 7) } else { r3(6) };
            mk(format!("{name} {}, {}, {op}", r3(0), r3(3)), Flow::Next)
        }
        // mov / cmp / add / sub immediate
        0b00100..=0b00111 => {
            let name = ["movs", "cmp", "adds", "subs"][((h >> 11) & 3) as usize];
            mk(format!("{name} {}, #{}", r3(8), h & 0xFF), Flow::Next)
        }
        0b01000 => {
            if h & (1 << 10) == 0 {
                // ALU operations
                let op = ((h >> 6) & 0xF) as usize;
                let suffix = if matches!(op, 8 | 10 | 11) { "" } else { "s" };
                mk(format!("{}{suffix} {}, {}", ALU[op], r3(0), r3(3)), Flow::Next)
            } else {
                // high-register operations / BX
                let rs = ((h >> 3) & 0xF) as u32;
                let rd = ((h & 7) | ((h >> 4) & 8)) as u32;
                match (h >> 8) & 3 {
                    0 => {
                        let flow = if rd == 15 { Flow::Indirect { ret: false, thumb_switch: false } } else { Flow::Next };
                        mk(format!("add {}, {}", reg(rd), reg(rs)), flow)
                    }
                    1 => mk(format!("cmp {}, {}", reg(rd), reg(rs)), Flow::Next),
                    2 => {
                        let flow = if rd == 15 { Flow::Indirect { ret: rs == 14, thumb_switch: false } } else { Flow::Next };
                        mk(format!("mov {}, {}", reg(rd), reg(rs)), flow)
                    }
                    _ => mk(format!("bx {}", reg(rs)), Flow::Indirect { ret: rs == 14, thumb_switch: true }),
                }
            }
        }
        // PC-relative load
        0b01001 => {
            let addr = ((pc + 4) & !3) + (h & 0xFF) * 4;
            mk(format!("ldr {}, [pc, #0x{:X}] ; 0x{addr:08X}", r3(8), (h & 0xFF) * 4), Flow::Next)
        }
        // load / store with register offset, and sign-extended / halfword
        0b01010 | 0b01011 => {
            if h & (1 << 9) == 0 {
                let name = ["str", "strb", "ldr", "ldrb"][((h >> 10) & 3) as usize];
                mk(format!("{name} {}, [{}, {}]", r3(0), r3(3), r3(6)), Flow::Next)
            } else {
                let name = ["strh", "ldsb", "ldrh", "ldsh"][((h >> 10) & 3) as usize].replace("ldsb", "ldrsb").replace("ldsh", "ldrsh");
                mk(format!("{name} {}, [{}, {}]", r3(0), r3(3), r3(6)), Flow::Next)
            }
        }
        // load / store with immediate offset
        0b01100..=0b01111 => {
            let byte = h & (1 << 12) != 0;
            let load = h & (1 << 11) != 0;
            let off = ((h >> 6) & 0x1F) * if byte { 1 } else { 4 };
            let name = format!("{}{}", if load { "ldr" } else { "str" }, if byte { "b" } else { "" });
            mk(format!("{name} {}, [{}, #0x{off:X}]", r3(0), r3(3)), Flow::Next)
        }
        // halfword immediate
        0b10000 | 0b10001 => {
            let name = if h & (1 << 11) != 0 { "ldrh" } else { "strh" };
            mk(format!("{name} {}, [{}, #0x{:X}]", r3(0), r3(3), ((h >> 6) & 0x1F) * 2), Flow::Next)
        }
        // SP-relative
        0b10010 | 0b10011 => {
            let name = if h & (1 << 11) != 0 { "ldr" } else { "str" };
            mk(format!("{name} {}, [sp, #0x{:X}]", r3(8), (h & 0xFF) * 4), Flow::Next)
        }
        // load address
        0b10100 | 0b10101 => {
            let (base, v) = if h & (1 << 11) != 0 { ("sp", (h & 0xFF) * 4) } else { ("pc", (h & 0xFF) * 4) };
            mk(format!("add {}, {base}, #0x{v:X}", r3(8)), Flow::Next)
        }
        // misc: add to SP, push / pop
        0b10110 | 0b10111 => {
            if h & 0xFF00 == 0xB000 {
                let v = (h & 0x7F) * 4;
                mk(if h & 0x80 != 0 { format!("sub sp, #0x{v:X}") } else { format!("add sp, #0x{v:X}") }, Flow::Next)
            } else if h & 0x0600 == 0x0400 {
                let pop = h & (1 << 11) != 0;
                let r = h & (1 << 8) != 0;
                if pop {
                    let flow = if r { Flow::Indirect { ret: true, thumb_switch: false } } else { Flow::Next };
                    mk(format!("pop {}", list(h & 0xFF, r.then_some("pc"))), flow)
                } else {
                    mk(format!("push {}", list(h & 0xFF, r.then_some("lr"))), Flow::Next)
                }
            } else {
                mk(format!(".hword 0x{h:04X}"), Flow::Undefined)
            }
        }
        // multiple load / store
        0b11000 | 0b11001 => {
            let name = if h & (1 << 11) != 0 { "ldmia" } else { "stmia" };
            mk(format!("{name} {}!, {}", r3(8), list(h & 0xFF, None)), Flow::Next)
        }
        // conditional branch / SWI
        0b11010 | 0b11011 => {
            let cond = (h >> 8) & 0xF;
            if cond == 0xF {
                mk(format!("swi 0x{:X}", h & 0xFF), Flow::Swi(h & 0xFF))
            } else if cond == 0xE {
                mk(format!(".hword 0x{h:04X}"), Flow::Undefined)
            } else {
                let off = ((h & 0xFF) as u8 as i8 as i32) * 2;
                let target = pc.wrapping_add(4).wrapping_add(off as u32);
                mk(format!("b{} 0x{target:08X}", COND[cond as usize]), Flow::CondBranch(target))
            }
        }
        // unconditional branch
        0b11100 => {
            let off = (((h & 0x7FF) << 21) as i32 >> 20) as u32;
            let target = pc.wrapping_add(4).wrapping_add(off);
            mk(format!("b 0x{target:08X}"), Flow::Branch(target))
        }
        // BL: first halfword (offset high), then 0b11111 (offset low)
        0b11110 => {
            let nx = next as u32;
            if nx >> 11 == 0b11111 {
                let hi = (((h & 0x7FF) << 21) as i32 >> 9) as u32;
                let lo = (nx & 0x7FF) << 1;
                let target = pc.wrapping_add(4).wrapping_add(hi).wrapping_add(lo);
                Insn { size: 4, text: format!("bl 0x{target:08X}"), flow: Flow::Call(target) }
            } else {
                mk(format!(".hword 0x{h:04X}"), Flow::Undefined)
            }
        }
        _ => mk(format!(".hword 0x{h:04X}"), Flow::Undefined),
    }
}
