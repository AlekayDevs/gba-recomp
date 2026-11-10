use gba_recomp::rom::{crc32, Rom, ROM_BASE};
use gba_recomp::{arm, discover::{discover, discover_from, thumb_pointers}, thumb};

fn usage() -> ! {
    eprintln!(
        "gba-recomp <order>\n  info <rom.gba>                      header, checks and CRC32\n  disasm <rom.gba> <addr> [n] [thumb]  disassemble n instructions (default 32) at a bus address (hex)\n  trace <rom.gba> [entry] [--pointers] follow the flow of control from the entry point and summarize (--pointers: also from the Thumb function pointers in the data)"
    );
    std::process::exit(2)
}

fn parse_addr(s: &str) -> u32 {
    u32::from_str_radix(s.trim_start_matches("0x"), 16).unwrap_or_else(|_| usage())
}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let Some(cmd) = args.first() else { usage() };
    let rom = match args.get(1) {
        Some(p) => Rom::load(p).unwrap_or_else(|e| {
            eprintln!("{e}");
            std::process::exit(1)
        }),
        None => usage(),
    };
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
        "trace" => {
            let entry = args.get(2).filter(|s| !s.starts_with('-')).map(|s| parse_addr(s)).or(rom.header().entry_target).unwrap_or(ROM_BASE);
            let d = if args.iter().any(|a| a == "--pointers") {
                let ptrs = thumb_pointers(&rom);
                println!("thumb pointers {}", ptrs.len());
                let mut roots = vec![(entry, false)];
                roots.extend(ptrs.into_iter().map(|a| (a, true)));
                discover_from(&rom, &roots)
            } else {
                discover(&rom, entry)
            };
            let arm_n = d.code.values().filter(|(t, _)| !*t).count();
            let thumb_n = d.code.values().filter(|(t, _)| *t).count();
            let bytes: u32 = d.code.values().map(|(_, s)| *s).sum();
            println!("entry 0x{entry:08X}");
            println!("instructions  {} (ARM {arm_n}, Thumb {thumb_n}), {bytes} bytes of {}", d.code.len(), rom.data.len());
            println!("functions     {}", d.functions.len());
            println!("blocks        {}", d.blocks.len());
            println!("BX resolved   {}", d.resolved.len());
            println!("unresolved    {} indirect jumps", d.unresolved.len());
            println!("outside ROM   {} targets", d.outside.len());
            println!("undefined     {}", d.undefined.len());
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
            for (a, t) in d.unresolved.iter().take(12) {
                println!("  unresolved {} at 0x{a:08X}", if *t { "thumb" } else { "arm" });
            }
        }
        _ => usage(),
    }
}
