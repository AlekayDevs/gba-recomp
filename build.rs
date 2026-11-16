//! `gba-port.exe` and `gba-recomp.exe` carry an icon as resource 1 (written here as a `.res` file, so no tool or crate is
//! needed) and `gba-port.exe` a manifest for the look of the controls and the screen scaling. The icon is
//! `assets/gba-port.ico` unless `GBA_PORT_ICON` names another `.ico` (a game's package build uses its own).

fn main() {
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed=assets/gba-port.manifest");
    println!("cargo:rerun-if-changed=assets/gba-port.ico");
    println!("cargo:rerun-if-env-changed=GBA_PORT_ICON");
    let target_windows_msvc = std::env::var("CARGO_CFG_TARGET_OS").is_ok_and(|v| v == "windows") && std::env::var("CARGO_CFG_TARGET_ENV").is_ok_and(|v| v == "msvc");
    if !target_windows_msvc {
        return;
    }
    let dir = std::path::PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let icon_path = std::env::var_os("GBA_PORT_ICON").map(std::path::PathBuf::from).unwrap_or_else(|| dir.join("assets").join("gba-port.ico"));
    if std::env::var_os("GBA_PORT_ICON").is_some() {
        println!("cargo:rerun-if-changed={}", icon_path.display());
    }
    let manifest = dir.join("assets").join("gba-port.manifest");
    let res = std::path::Path::new(&std::env::var("OUT_DIR").unwrap()).join("icon.res");
    let ico = std::fs::read(&icon_path).unwrap_or_else(|e| panic!("{}: {e}", icon_path.display()));
    std::fs::write(&res, icon_res(&ico)).expect("icon.res");
    println!("cargo:rustc-link-arg-bins={}", res.display());
    println!("cargo:rustc-link-arg-bin=gba-port=/MANIFEST:EMBED");
    println!("cargo:rustc-link-arg-bin=gba-port=/MANIFESTINPUT:{}", manifest.display());
}

/// One entry of a `.res` file: type and name are numeric ids, the data is padded to 4 bytes.
fn res_entry(kind: u16, id: u16, data: &[u8]) -> Vec<u8> {
    let mut v = Vec::new();
    v.extend((data.len() as u32).to_le_bytes());
    v.extend(32u32.to_le_bytes()); // header size
    v.extend([0xFF, 0xFF]);
    v.extend(kind.to_le_bytes());
    v.extend([0xFF, 0xFF]);
    v.extend(id.to_le_bytes());
    v.extend([0u8; 4]); // data version
    v.extend(0x1030u16.to_le_bytes()); // MOVEABLE | PURE | DISCARDABLE
    v.extend(0x0409u16.to_le_bytes()); // language
    v.extend([0u8; 8]); // version, characteristics
    v.extend(data);
    while v.len() % 4 != 0 {
        v.push(0);
    }
    v
}

/// The icon file as resources: one RT_ICON per image (ids 1..) and the RT_GROUP_ICON (id 1) that lists them.
fn icon_res(ico: &[u8]) -> Vec<u8> {
    let u16_at = |o: usize| u16::from_le_bytes([ico[o], ico[o + 1]]);
    let u32_at = |o: usize| u32::from_le_bytes([ico[o], ico[o + 1], ico[o + 2], ico[o + 3]]);
    let count = u16_at(4) as usize;
    let mut out = res_entry(0, 0, &[]); // the empty entry every .res starts with
    let mut group = vec![0, 0, 1, 0];
    group.extend((count as u16).to_le_bytes());
    for i in 0..count {
        let e = 6 + 16 * i;
        let (size, offset) = (u32_at(e + 8) as usize, u32_at(e + 12) as usize);
        out.extend(res_entry(3, i as u16 + 1, &ico[offset..offset + size]));
        group.extend(&ico[e..e + 12]);
        group.extend((i as u16 + 1).to_le_bytes());
    }
    out.extend(res_entry(14, 1, &group));
    out
}
