//! Map of the ROM: what each byte is according to the discovery (code, literal pools, `switch` tables) and what is left over.

use crate::discover::Discovery;
use crate::rom::Rom;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Kind {
    Unknown,
    Header,
    Arm,
    Thumb,
    /// Words loaded by `LDR Rd, [PC, #imm]`.
    Pool,
    /// Jump tables of `switch`.
    Table,
    /// ROM bytes that the game copies to RAM and the analysis sees as code at RAM addresses (counted as code).
    OverlayCode,
}

pub struct Map {
    pub kinds: Vec<Kind>,
}

/// A run of bytes nobody claimed.
pub struct Gap {
    pub start: u32,
    pub len: u32,
    /// All bytes equal (`00` or `FF`): padding or erased flash.
    pub fill: Option<u8>,
}

impl Map {
    pub fn build(rom: &Rom, d: &Discovery) -> Map {
        let mut kinds = vec![Kind::Unknown; rom.data.len()];
        let mark = |kinds: &mut Vec<Kind>, addr: u32, len: u32, k: Kind, rom_only: bool| {
            let Some(o) = rom.offset(addr) else { return };
            let overlay = !(0x0800_0000..0x0E00_0000).contains(&addr);
            if rom_only && overlay {
                return;
            }
            let k = if overlay && matches!(k, Kind::Arm | Kind::Thumb) { Kind::OverlayCode } else { k };
            for i in 0..len as usize {
                if let Some(slot) = kinds.get_mut(o + i) {
                    // code wins over a pool that happens to overlap
                    if *slot == Kind::Unknown || (k != Kind::Pool && *slot == Kind::Pool) {
                        *slot = k;
                    }
                }
            }
        };
        mark(&mut kinds, 0x0800_0000, 0xC0, Kind::Header, true);
        for (&a, &(thumb, size)) in &d.code {
            mark(&mut kinds, a, size, if thumb { Kind::Thumb } else { Kind::Arm }, false);
        }
        for (&a, &(thumb, _)) in &d.code {
            // literal pools: PC-relative loads
            let lit = if thumb {
                rom.read16(a).filter(|h| h >> 11 == 0b01001).map(|h| ((a + 4) & !3) + (h as u32 & 0xFF) * 4)
            } else {
                rom.read32(a).and_then(|w| {
                    if w >> 28 == 15 || w & 0x0F7F_0000 != 0x051F_0000 {
                        return None;
                    }
                    let imm = w & 0xFFF;
                    let base = a.wrapping_add(8);
                    Some(if w & (1 << 23) != 0 { base.wrapping_add(imm) } else { base.wrapping_sub(imm) })
                })
            };
            if let Some(l) = lit {
                mark(&mut kinds, l, 4, Kind::Pool, false);
            }
        }
        for (&base, &n) in &d.table_data {
            mark(&mut kinds, base, n * 4, Kind::Table, false);
        }
        Map { kinds }
    }

    pub fn count(&self, k: Kind) -> usize {
        self.kinds.iter().filter(|&&x| x == k).count()
    }

    /// Runs of unclaimed bytes, in address order.
    pub fn gaps(&self, rom: &Rom) -> Vec<Gap> {
        let mut out = Vec::new();
        let mut i = 0;
        while i < self.kinds.len() {
            if self.kinds[i] != Kind::Unknown {
                i += 1;
                continue;
            }
            let s = i;
            while i < self.kinds.len() && self.kinds[i] == Kind::Unknown {
                i += 1;
            }
            let b = &rom.data[s..i];
            let fill = b.iter().all(|&x| x == b[0]).then_some(b[0]).filter(|&x| x == 0 || x == 0xFF);
            out.push(Gap { start: 0x0800_0000 + s as u32, len: (i - s) as u32, fill });
        }
        out
    }
}
