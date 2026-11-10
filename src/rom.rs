//! The cartridge: header checks and the address map of the ROM (`$08000000`, mirrored at `$0A000000` and `$0C000000`).

pub const ROM_BASE: u32 = 0x0800_0000;

/// A block of the ROM that the game copies into RAM at boot and runs from there (IWRAM code, mostly).
/// Seen from the analyzer it is code at its RAM addresses whose bytes live in the ROM.
#[derive(Debug, Clone, Copy)]
pub struct Overlay {
    pub ram: u32,
    pub rom: u32,
    pub len: u32,
}

pub struct Rom {
    pub data: Vec<u8>,
    pub overlays: Vec<Overlay>,
}

#[derive(Debug)]
pub struct Header {
    pub title: String,
    pub game_code: String,
    pub maker: String,
    pub version: u8,
    pub fixed_ok: bool,
    pub checksum_ok: bool,
    pub entry_target: Option<u32>,
}

impl Rom {
    pub fn load(path: &str) -> Result<Rom, String> {
        let data = std::fs::read(path).map_err(|e| format!("{path}: {e}"))?;
        if data.len() < 0xC0 {
            return Err(format!("{path}: too small for a GBA ROM"));
        }
        Ok(Rom { data, overlays: Vec::new() })
    }

    /// Offset into the ROM of a bus address, if the address is in the ROM area (any of the three mirrors).
    pub fn offset(&self, addr: u32) -> Option<usize> {
        for o in &self.overlays {
            if addr >= o.ram && addr < o.ram + o.len {
                return self.offset(o.rom + (addr - o.ram));
            }
        }
        if !(0x0800_0000..0x0E00_0000).contains(&addr) {
            return None;
        }
        let off = (addr & 0x01FF_FFFF) as usize;
        (off < self.data.len()).then_some(off)
    }

    pub fn read16(&self, addr: u32) -> Option<u16> {
        let o = self.offset(addr)?;
        Some(u16::from_le_bytes([*self.data.get(o)?, *self.data.get(o + 1)?]))
    }

    pub fn read32(&self, addr: u32) -> Option<u32> {
        let o = self.offset(addr)?;
        let b = self.data.get(o..o + 4)?;
        Some(u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
    }

    pub fn header(&self) -> Header {
        let d = &self.data;
        let text = |a: usize, n: usize| d[a..a + n].iter().take_while(|&&b| b != 0).map(|&b| b as char).collect::<String>();
        // the complement check covers $A0..=$BC: -(sum) - $19
        let mut chk: u8 = 0;
        for &b in &d[0xA0..=0xBC] {
            chk = chk.wrapping_sub(b);
        }
        chk = chk.wrapping_sub(0x19);
        let first = u32::from_le_bytes([d[0], d[1], d[2], d[3]]);
        // `B <target>`: cond AL, bits 27-24 = 1010
        let entry_target = if first >> 24 == 0xEA {
            let off = ((first & 0x00FF_FFFF) << 8) as i32 >> 6;
            Some(ROM_BASE.wrapping_add(8).wrapping_add(off as u32))
        } else {
            None
        };
        Header {
            title: text(0xA0, 12),
            game_code: text(0xAC, 4),
            maker: text(0xB0, 2),
            version: d[0xBC],
            fixed_ok: d[0xB2] == 0x96,
            checksum_ok: d[0xBD] == chk,
            entry_target,
        }
    }
}

/// CRC32 (IEEE), to identify a ROM without a table of hashes of the game in the repository.
pub fn crc32(data: &[u8]) -> u32 {
    let mut table = [0u32; 256];
    for (i, t) in table.iter_mut().enumerate() {
        let mut c = i as u32;
        for _ in 0..8 {
            c = if c & 1 != 0 { 0xEDB8_8320 ^ (c >> 1) } else { c >> 1 };
        }
        *t = c;
    }
    let mut c = 0xFFFF_FFFFu32;
    for &b in data {
        c = table[((c ^ b as u32) & 0xFF) as usize] ^ (c >> 8);
    }
    !c
}
