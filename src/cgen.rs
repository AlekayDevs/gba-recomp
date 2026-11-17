//! From a ROM and what the profile says to the C of the game (the body of `gba-recomp gen`). Shared by the command line
//! and by the C interface of the Android app (`capi`), which generates the C on the phone from the ROM the user picked.
use crate::discover::{arm_pointers, discover_with, thumb_pointers, Discovery};
use crate::rom::{Rom, ROM_BASE};
use std::collections::{BTreeMap, BTreeSet};

pub struct Generated {
    /// the C text of the game
    pub text: String,
    /// `ADDR t|a` per translated function
    pub list: String,
    pub functions: usize,
    pub translated: usize,
    /// callees called but not translated (they get a stub)
    pub missing: usize,
    /// why the functions that were not translated were not: (reason, count), most frequent first
    pub why: Vec<(String, usize)>,
}

/// Discovers the code of the ROM from its entry point, the extra roots and (with `pointers`) the function pointers in the data.
pub fn discover(rom: &Rom, entry: u32, extra_roots: &[(u32, bool)], pointers: bool) -> (Discovery, Option<String>) {
    let mut roots = vec![(entry, false)];
    roots.extend(extra_roots.iter().copied());
    if pointers {
        let ptrs = thumb_pointers(rom);
        let aptrs = arm_pointers(rom);
        let note = format!("thumb pointers {}, ARM pointers {}", ptrs.len(), aptrs.len());
        let mut cands: Vec<(u32, bool)> = ptrs.into_iter().map(|a| (a, true)).collect();
        cands.extend(aptrs.into_iter().map(|a| (a, false)));
        cands.sort();
        (discover_with(rom, &roots, &cands), Some(note))
    } else {
        (discover_with(rom, &roots, &[]), None)
    }
}

/// Translates every function discovered that the generator can, and the run-time dispatch table.
pub fn translate(rom: &Rom, d: &Discovery, entry: u32, resume: Option<u32>) -> Generated {
    let mut text = String::from("#include \"gba_rt.h\"\n\n");
    let mut why: BTreeMap<String, usize> = Default::default();
    let mut calls = BTreeSet::new();
    let funcs: Vec<u32> = d.functions.iter().copied().filter(|a| d.code.contains_key(a)).collect();
    let mut bodies = Vec::new();
    for &f in &funcs {
        match crate::gen::function(rom, d, f, &mut calls, resume) {
            Ok(b) => bodies.push((f, b)),
            Err(e) => {
                let k = e.split(" at 0x").next().unwrap_or(&e).to_string();
                if std::env::var("GEN_WHY").is_ok() {
                    eprintln!("  {f:08X}: {e}");
                }
                *why.entry(k.split_whitespace().take(1).collect::<Vec<_>>().join(" ")).or_insert(0) += 1;
            }
        }
    }
    let ok: BTreeSet<u32> = bodies.iter().map(|b| b.0).collect();
    for (f, _) in &bodies {
        text.push_str(&format!("void {}(Cpu *c);\n", crate::gen::name(*f)));
    }
    // the functions that are called but were not translated get a stub that the test can detect
    for c in calls.iter().filter(|c| !ok.contains(c)) {
        text.push_str(&format!("void {}(Cpu *c) {{ rt_untranslated(c, 0x{c:08X}u); }}\n", crate::gen::name(*c)));
    }
    text.push('\n');
    for (_, b) in &bodies {
        text.push_str(b);
        text.push('\n');
    }
    // run-time dispatch: a binary search over the translated functions (sorted by address)
    text.push_str(&format!("static const struct {{ u32 a; void (*f)(Cpu *); }} rt_table[{}] = {{\n", bodies.len().max(1)));
    for (f, _) in &bodies {
        text.push_str(&format!("    {{ 0x{f:08X}u, {} }},\n", crate::gen::name(*f)));
    }
    text.push_str("};\nvoid rt_dispatch(Cpu *c, u32 target) {\n    u32 t = (target & 1) ? target & ~1u : target & ~3u, lo = 0, hi = ");
    text.push_str(&format!("{};\n", bodies.len()));
    text.push_str(
        "    if (t >= 0x0A000000u && t < 0x0E000000u) t = 0x08000000u | (t & 0x01FFFFFFu); /* the ROM seen through its mirrors */\n    while (lo < hi) {\n        u32 m = (lo + hi) / 2;\n        if (rt_table[m].a == t) { rt_table[m].f(c); return; }\n        if (rt_table[m].a < t) lo = m + 1; else hi = m;\n    }\n    rt_missing(c, target);\n}\n",
    );
    text.push_str(&format!("void rt_entry(Cpu *c) {{ {}(c); }}\n", crate::gen::name(entry)));
    let list: String = bodies.iter().map(|b| format!("{:08X} {}\n", b.0, if d.code[&b.0].0 { "t" } else { "a" })).collect();
    let missing = calls.iter().filter(|c| !ok.contains(c)).count();
    let mut w: Vec<_> = why.into_iter().collect();
    w.sort_by_key(|x| std::cmp::Reverse(x.1));
    Generated { text, list, functions: funcs.len(), translated: bodies.len(), missing, why: w }
}

/// The whole thing: discovery and translation. `entry` defaults to the target of the branch at the start of the ROM.
pub fn generate(rom: &Rom, extra_roots: &[(u32, bool)], resume: Option<u32>, pointers: bool) -> Generated {
    let entry = rom.header().entry_target.unwrap_or(ROM_BASE);
    let (d, _) = discover(rom, entry, extra_roots, pointers);
    translate(rom, &d, entry, resume)
}
