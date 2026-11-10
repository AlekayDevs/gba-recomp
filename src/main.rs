use gba_recomp::rom::{crc32, Overlay, Rom, ROM_BASE};
use gba_recomp::map::{Kind, Map};
use gba_recomp::{arm, discover::{arm_pointers, discover, discover_with, thumb_pointers}, thumb};

fn usage() -> ! {
    eprintln!(
        "gba-recomp <order>\n  info <rom.gba>                      header, checks and CRC32\n  disasm <rom.gba> <addr> [n] [thumb]  disassemble n instructions (default 32) at a bus address (hex)\n  map <rom.gba> [--pointers] [--overlay ...]  what is code, pools, tables and what is left unclaimed
  trace <rom.gba> [entry] [--pointers] [--overlay RAM:ROM:LEN]... follow the flow of control from the entry point and summarize (--pointers: also from the Thumb function pointers in the data)"
    );
    std::process::exit(2)
}

fn parse_addr(s: &str) -> u32 {
    u32::from_str_radix(s.trim_start_matches("0x"), 16).unwrap_or_else(|_| usage())
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let Some(cmd) = args.first() else { usage() };
    let mut rom = match args.get(1) {
        Some(p) => Rom::load(p).unwrap_or_else(|e| {
            eprintln!("{e}");
            std::process::exit(1)
        }),
        None => usage(),
    };
    // --overlay RAM:ROM:LEN (hex, repeatable): a block copied from the ROM to RAM at boot
    let mut it = args.iter();
    while let Some(a) = it.next() {
        if a == "--overlay" {
            let v: Vec<u32> = it.next().map(|s| s.split(':').map(parse_addr).collect()).unwrap_or_else(|| usage());
            if v.len() != 3 {
                usage()
            }
            rom.overlays.push(Overlay { ram: v[0], rom: v[1], len: v[2] });
        }
    }
    match cmd.as_str() {
        "info" => {
            let h = rom.header();
            println!("title      {:?}", h.title);
            println!("game code  {} (maker {}, version {})", h.game_code, h.maker, h.version);
            println!("size       {} bytes", rom.data.len());
            println!("crc32      {:08X}", crc32(&rom.data));
            println!("fixed $96  {}", h.fixed_ok);
            println!("checksum   {}", h.checksum_ok);
            match h.entry_target {
                Some(t) => println!("entry      b 0x{t:08X}"),
                None => println!("entry      not a branch"),
            }
        }
        "disasm" => {
            let mut addr = parse_addr(args.get(2).unwrap_or_else(|| usage()));
            let n: usize = args.get(3).and_then(|s| s.parse().ok()).unwrap_or(32);
            let thumb_mode = args.get(4).is_some_and(|s| s == "thumb");
            for _ in 0..n {
                let size = if thumb_mode {
                    let Some(h) = rom.read16(addr) else { break };
                    let i = thumb::decode(h, rom.read16(addr + 2).unwrap_or(0), addr);
                    let raw = if i.size == 4 { format!("{h:04X} {:04X}", rom.read16(addr + 2).unwrap_or(0)) } else { format!("{h:04X}     ") };
                    println!("{addr:08X}  {raw}  {}", i.text);
                    i.size
                } else {
                    let Some(w) = rom.read32(addr) else { break };
                    let i = arm::decode(w, addr);
                    println!("{addr:08X}  {w:08X}  {}", i.text);
                    i.size
                };
                addr += size;
            }
        }
        "trace" | "map" => {
            let entry = args.get(2).filter(|s| !s.starts_with('-') && !s.contains(':')).map(|s| parse_addr(s)).or(rom.header().entry_target).unwrap_or(ROM_BASE);
            let d = if args.iter().any(|a| a == "--pointers") {
                let ptrs = thumb_pointers(&rom);
                let aptrs = arm_pointers(&rom);
                println!("thumb pointers {}, ARM pointers {}", ptrs.len(), aptrs.len());
                let mut cands: Vec<(u32, bool)> = ptrs.into_iter().map(|a| (a, true)).collect();
                cands.extend(aptrs.into_iter().map(|a| (a, false)));
                cands.sort();
                discover_with(&rom, &[(entry, false)], &cands)
            } else {
                discover(&rom, entry)
            };
            if cmd == "map" {
                let m = Map::build(&rom, &d);
                let total = rom.data.len();
                let pct = |n: usize| 100.0 * n as f64 / total as f64;
                println!("ROM {total} bytes");
                for (name, k) in [("header", Kind::Header), ("ARM code", Kind::Arm), ("Thumb code", Kind::Thumb), ("overlay code", Kind::OverlayCode), ("literal pools", Kind::Pool), ("switch tables", Kind::Table), ("unclaimed", Kind::Unknown)] {
                    let n = m.count(k);
                    println!("  {name:<14}{n:>9} bytes  {:5.2}%", pct(n));
                }
                let gaps = m.gaps(&rom);
                let (pad, data): (Vec<_>, Vec<_>) = gaps.iter().partition(|g| g.fill.is_some());
                println!("unclaimed runs {} ({} of padding/fill, {} bytes), {} with content ({} bytes)", gaps.len(), pad.len(), pad.iter().map(|g| g.len as usize).sum::<usize>(), data.len(), data.iter().map(|g| g.len as usize).sum::<usize>());
                let mut big = data.clone();
                big.sort_by_key(|g| std::cmp::Reverse(g.len));
                println!("largest runs with content:");
                for g in big.iter().take(25) {
                    println!("  0x{:08X}  {:>8} bytes", g.start, g.len);
                }
                // the code area: up to the last byte claimed as code
                let last = m.kinds.iter().rposition(|k| matches!(k, Kind::Arm | Kind::Thumb)).unwrap_or(0);
                let span = last + 1;
                let in_span: Vec<_> = data.iter().filter(|g| ((g.start - ROM_BASE) as usize) < span).collect();
                println!("code area 0x{:08X}..0x{:08X} ({span} bytes): code {:.2}%, pools+tables {:.2}%, unclaimed with content {} bytes in {} runs",
                    ROM_BASE, ROM_BASE + span as u32,
                    100.0 * m.kinds[..span].iter().filter(|k| matches!(k, Kind::Arm | Kind::Thumb | Kind::OverlayCode)).count() as f64 / span as f64,
                    100.0 * m.kinds[..span].iter().filter(|k| matches!(k, Kind::Pool | Kind::Table)).count() as f64 / span as f64,
                    in_span.iter().map(|g| g.len as usize).sum::<usize>(), in_span.len());
                let mut inb = in_span.clone();
                inb.sort_by_key(|g| std::cmp::Reverse(g.len));
                for g in inb.iter().take(15) {
                    println!("  in code area 0x{:08X}  {:>8} bytes", g.start, g.len);
                }
                // histogram of sizes
                let mut h = [0usize; 6];
                for g in &data {
                    h[match g.len { 0..=7 => 0, 8..=63 => 1, 64..=511 => 2, 512..=4095 => 3, 4096..=65535 => 4, _ => 5 }] += g.len as usize;
                }
                println!("bytes with content by run size: <8 {}, <64 {}, <512 {}, <4K {}, <64K {}, bigger {}", h[0], h[1], h[2], h[3], h[4], h[5]);
                return;
            }
            let arm_n = d.code.values().filter(|(t, _)| !*t).count();
            let thumb_n = d.code.values().filter(|(t, _)| *t).count();
            let bytes: u32 = d.code.values().map(|(_, s)| *s).sum();
            println!("entry 0x{entry:08X}");
            println!("instructions  {} (ARM {arm_n}, Thumb {thumb_n}), {bytes} bytes of {}", d.code.len(), rom.data.len());
            println!("functions     {}", d.functions.len());
            println!("blocks        {}", d.blocks.len());
            println!("BX resolved   {}", d.resolved.len());
            println!("switch tables {} ({} targets)", d.tables.len(), d.tables.values().map(|t| t.len()).sum::<usize>());
            println!("unresolved    {} indirect jumps", d.unresolved.len());
            println!("outside ROM   {} targets", d.outside.len());
            println!("undefined     {}", d.undefined.len());
            println!("rejected      {} candidates from the data", d.rejected.len());
            println!("BIOS calls    {:?}", d.swis);
            for a in &d.undefined {
                println!("  undefined at 0x{a:08X}");
            }
            for a in d.outside.iter().take(8) {
                println!("  outside 0x{a:08X}");
            }
            for (a, t) in &d.resolved {
                println!("  resolved bx at 0x{a:08X} -> 0x{t:08X}");
            }
            for (a, t) in d.unresolved.iter().take(if args.iter().any(|x| x == "--all") { usize::MAX } else { 12 }) {
                println!("  unresolved {} at 0x{a:08X}", if *t { "thumb" } else { "arm" });
            }
        }
        _ => usage(),
    }
}
