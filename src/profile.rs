//! Per-game profile (`games/<id>.toml`): only addresses and sizes, never bytes of the game.
//! A small reader for the subset used: `[[overlay]]` tables with `ram`/`rom`/`len` (hex strings),
//! and `[roots]` with `file = "..."` (relative to the profile; one hex address per line, odd = Thumb).
use crate::rom::Overlay;
use std::path::Path;

#[derive(Default, Debug)]
pub struct Profile {
    pub overlays: Vec<Overlay>,
    pub roots: Vec<(u32, bool)>,
}

fn hex(s: &str) -> Result<u32, String> {
    u32::from_str_radix(s.trim_start_matches("0x"), 16).map_err(|_| format!("bad hex value '{s}'"))
}

pub fn parse_roots(text: &str) -> Result<Vec<(u32, bool)>, String> {
    let mut v = Vec::new();
    for l in text.lines() {
        let l = l.trim();
        if l.is_empty() || l.starts_with('#') {
            continue;
        }
        let a = crate::discover::canon(hex(l)?);
        v.push((a & !1, a & 1 == 1));
    }
    Ok(v)
}

pub fn parse(text: &str, dir: &Path) -> Result<Profile, String> {
    let mut p = Profile::default();
    let mut section = String::new();
    let (mut ram, mut rom, mut len) = (None, None, None);
    let flush = |p: &mut Profile, ram: &mut Option<u32>, rom: &mut Option<u32>, len: &mut Option<u32>| -> Result<(), String> {
        if let (Some(r), Some(o), Some(l)) = (ram.take(), rom.take(), len.take()) {
            p.overlays.push(Overlay { ram: r, rom: o, len: l });
        }
        Ok(())
    };
    for raw in text.lines() {
        let l = raw.split('#').next().unwrap_or("").trim();
        if l.is_empty() {
            continue;
        }
        if l.starts_with('[') {
            if section == "[[overlay]]" {
                flush(&mut p, &mut ram, &mut rom, &mut len)?;
            }
            section = l.to_string();
            continue;
        }
        let Some((k, v)) = l.split_once('=') else { continue };
        let (k, v) = (k.trim(), v.trim().trim_matches('"'));
        match (section.as_str(), k) {
            ("[[overlay]]", "ram") => ram = Some(hex(v)?),
            ("[[overlay]]", "rom") => rom = Some(hex(v)?),
            ("[[overlay]]", "len") => len = Some(hex(v)?),
            ("[roots]", "file") => {
                let f = dir.join(v);
                let t = std::fs::read_to_string(&f).map_err(|e| format!("{}: {e}", f.display()))?;
                p.roots.extend(parse_roots(&t)?);
            }
            _ => {}
        }
    }
    if section == "[[overlay]]" {
        flush(&mut p, &mut ram, &mut rom, &mut len)?;
    }
    Ok(p)
}

pub fn load(path: &str) -> Result<Profile, String> {
    let t = std::fs::read_to_string(path).map_err(|e| format!("{path}: {e}"))?;
    parse(&t, Path::new(path).parent().unwrap_or(Path::new(".")))
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn overlays() {
        let t = "[game]\nid=\"x\"\n[[overlay]]\nname=\"a\"\nram=\"030056F0\"\nrom=\"080B197C\" # c\nlen=\"135C\"\n[[overlay]]\nram=\"0300404C\"\nrom=\"080AF3A4\"\nlen=\"380\"\n";
        let p = parse(t, Path::new(".")).unwrap();
        assert_eq!(p.overlays.len(), 2);
        assert_eq!((p.overlays[0].ram, p.overlays[0].rom, p.overlays[0].len), (0x030056F0, 0x080B197C, 0x135C));
        assert_eq!(p.overlays[1].len, 0x380);
    }
}
