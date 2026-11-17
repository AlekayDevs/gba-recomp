//! The port builder behind `gba-port`: from a ROM and a game profile (`games/<id>.toml`) to a running game.
//! Identify the ROM by CRC32 against the profiles, generate the C (`gba-recomp gen`), compile it with TinyCC (or the
//! compiler the machine has) together with the runtime and SDL2, keep the result in a cache keyed by game and version,
//! and start it. Messages go through `Ui`, so the console and the launcher window say the same things.
use crate::rom::{crc32, Rom};
use std::path::{Path, PathBuf};
use std::process::Command;

/// What the builder tells the person: progress, errors and the questions it needs answered.
pub trait Ui {
    fn status(&self, msg: &str);
    fn error(&self, msg: &str);
    /// A yes/no question (none yet; the console says no).
    fn confirm(&self, _question: &str) -> bool {
        false
    }
    /// The game is about to start: the launcher window gets out of the way.
    fn launching(&self) {}
}

pub const EXIT_FAILED: i32 = 1;
pub const EXIT_UNKNOWN_ROM: i32 = 3;
pub const EXIT_NO_TOOLCHAIN: i32 = 4;
pub const EXIT_BUILD_FAILED: i32 = 5;

/// Why a build stopped: an exit code (it says the kind) and the message the console prints.
#[derive(Debug)]
pub struct Failure {
    pub code: i32,
    pub message: String,
}

fn fail<T>(code: i32, message: impl Into<String>) -> Result<T, Failure> {
    Err(Failure { code, message: message.into() })
}

pub struct ConsoleUi;
impl Ui for ConsoleUi {
    fn status(&self, msg: &str) {
        eprintln!("{msg}");
    }
    fn error(&self, msg: &str) {
        eprintln!("error: {msg}");
    }
}

#[derive(Debug, Clone)]
pub struct Game {
    pub id: String,
    pub title: String,
    pub crc32: u32,
    /// the profile file (its directory holds the roots file)
    pub profile: PathBuf,
}

/// Reads `[game]` of a profile: `id`, `title` and `crc32`. Nothing else is needed to identify a ROM.
pub fn parse_game(text: &str, profile: &Path) -> Option<Game> {
    let (mut id, mut title, mut crc) = (None, None, None);
    let mut in_game = false;
    for raw in text.lines() {
        let l = raw.split('#').next().unwrap_or("").trim();
        if l.starts_with('[') {
            in_game = l == "[game]";
            continue;
        }
        if !in_game {
            continue;
        }
        let Some((k, v)) = l.split_once('=') else { continue };
        let v = v.trim().trim_matches('"').to_string();
        match k.trim() {
            "id" => id = Some(v),
            "title" => title = Some(v),
            "crc32" => crc = u32::from_str_radix(&v, 16).ok(),
            _ => {}
        }
    }
    Some(Game { id: id?, title: title?, crc32: crc?, profile: profile.to_path_buf() })
}

pub fn load_games(dir: &Path) -> Vec<Game> {
    let mut v = Vec::new();
    if let Ok(rd) = std::fs::read_dir(dir) {
        for e in rd.flatten() {
            let p = e.path();
            if p.extension().is_some_and(|x| x == "toml") {
                if let Some(g) = std::fs::read_to_string(&p).ok().and_then(|t| parse_game(&t, &p)) {
                    v.push(g);
                }
            }
        }
    }
    v.sort_by(|a, b| a.id.cmp(&b.id));
    v
}

pub fn identify(games: &[Game], crc: u32) -> Option<&Game> {
    games.iter().find(|g| g.crc32 == crc)
}

/// The folder of the package or checkout: the first ancestor of the executable with `runtime/gba_rt.h`.
pub fn root_dir(exe: &Path) -> Option<PathBuf> {
    exe.ancestors().skip(1).find(|d| d.join("runtime").join("gba_rt.h").is_file()).map(|d| d.to_path_buf())
}

pub fn exe_name() -> &'static str {
    if cfg!(windows) { "play.exe" } else { "play" }
}

pub fn cache_dir() -> PathBuf {
    if let Ok(d) = std::env::var("GBA_PORT_CACHE") {
        return PathBuf::from(d);
    }
    let base = std::env::var_os("LOCALAPPDATA").map(PathBuf::from).or_else(|| std::env::var_os("XDG_CACHE_HOME").map(PathBuf::from)).or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".cache")));
    base.unwrap_or_else(std::env::temp_dir).join("gba-port")
}

const RUNTIME: &[&str] = &["gba_cpu.c", "gba_bus.c", "gba_sys.c", "gba_ppu.c", "gba_apu.c", "gba_state.c", "gba_save.c", "gba_bios.c", "play.c"];

/// Changes whenever something that shapes the built game changes: the generator, the runtime, the profile and its roots.
pub fn version_tag(root: &Path, engine: &Path, game: &Game) -> String {
    let mut buf = std::fs::read(engine).unwrap_or_default();
    for f in RUNTIME.iter().copied().chain(["gba_rt.h", "gba_sys.h", "play_menu.h", "play_ui.h"]) {
        buf.extend(std::fs::read(root.join("runtime").join(f)).unwrap_or_default());
    }
    buf.extend(std::fs::read(&game.profile).unwrap_or_default());
    if let Some(dir) = game.profile.parent() {
        if let Ok(rd) = std::fs::read_dir(dir) {
            let mut names: Vec<_> = rd.flatten().map(|e| e.path()).filter(|p| p.extension().is_some_and(|x| x == "txt")).collect();
            names.sort();
            for p in names {
                buf.extend(std::fs::read(p).unwrap_or_default());
            }
        }
    }
    for p in game_sources(game) {
        buf.extend(std::fs::read(p).unwrap_or_default());
    }
    format!("{:08x}", crc32(&buf))
}

/// The folder with a game's own files (`games/<id>/`): C that patches the ROM in memory (`*.c`), `lang/`, an icon.
pub fn game_dir(game: &Game) -> Option<PathBuf> {
    game.profile.parent().map(|p| p.join(&game.id))
}

/// The game's own C files, sorted (none when the game has nothing of its own).
pub fn game_sources(game: &Game) -> Vec<PathBuf> {
    let mut v: Vec<PathBuf> = game_dir(game)
        .and_then(|d| std::fs::read_dir(d).ok())
        .map(|rd| rd.flatten().map(|e| e.path()).filter(|p| p.extension().is_some_and(|x| x == "c")).collect())
        .unwrap_or_default();
    v.sort();
    v
}

/// A C compiler: `GBA_PORT_CC`/`TCC`, the TinyCC of the package (`toolchain/tcc`), `tcc` on the PATH, or one of a
/// sibling checkout while developing.
pub fn find_tcc(root: &Path) -> Option<PathBuf> {
    let name = if cfg!(windows) { "tcc.exe" } else { "tcc" };
    let mut c: Vec<PathBuf> = Vec::new();
    for v in ["GBA_PORT_CC", "TCC"] {
        if let Some(p) = std::env::var_os(v) {
            c.push(PathBuf::from(p));
        }
    }
    c.push(root.join("toolchain").join("tcc").join(name));
    if let Some(parent) = root.parent() {
        if let Ok(rd) = std::fs::read_dir(parent) {
            for e in rd.flatten() {
                c.push(e.path().join("toolchain").join("tcc").join(name));
            }
        }
    }
    c.push(PathBuf::from(name));
    c.into_iter().find(|p| Command::new(p).arg("-v").output().is_ok_and(|o| o.status.success()))
}

/// The SDL2 development files (`include/`, `lib/x64/SDL2.dll`): `GBA_PORT_SDL2`, `sdl2/` of the package, or a sibling checkout's.
pub fn find_sdl2(root: &Path) -> Option<PathBuf> {
    let ok = |p: &Path| p.join("include").is_dir() && (p.join("lib").join("x64").join("SDL2.dll").is_file() || !cfg!(windows));
    let mut c: Vec<PathBuf> = Vec::new();
    if let Some(p) = std::env::var_os("GBA_PORT_SDL2") {
        c.push(PathBuf::from(p));
    }
    c.push(root.join("sdl2"));
    if let Some(parent) = root.parent() {
        if let Ok(rd) = std::fs::read_dir(parent) {
            for e in rd.flatten() {
                c.push(e.path().join("sdl2"));
            }
        }
    }
    c.into_iter().find(|p| ok(p))
}

fn tail(s: &str) -> String {
    let lines: Vec<&str> = s.lines().collect();
    lines[lines.len().saturating_sub(12)..].join("\n")
}

pub struct Options {
    pub rom: PathBuf,
    pub root: PathBuf,
    pub engine: PathBuf,
    pub games_dir: PathBuf,
    pub cache: PathBuf,
}

/// Builds the game for `rom` if the cache does not hold it already; returns the player's path.
pub fn build(o: &Options, ui: &dyn Ui) -> Result<(PathBuf, Game), Failure> {
    let rom = Rom::load(&o.rom.to_string_lossy()).map_err(|m| Failure { code: EXIT_FAILED, message: m })?;
    let crc = crc32(&rom.data);
    let games = load_games(&o.games_dir);
    if games.is_empty() {
        return fail(EXIT_UNKNOWN_ROM, format!("no game profiles (games/*.toml) in {}", o.games_dir.display()));
    }
    let Some(game) = identify(&games, crc) else {
        let list: Vec<String> = games.iter().map(|g| format!("  {} (CRC32 {:08X})", g.title, g.crc32)).collect();
        return fail(EXIT_UNKNOWN_ROM, format!("{}: unknown ROM (CRC32 {crc:08X}). Supported games:\n{}", o.rom.display(), list.join("\n")));
    };
    let dir = o.cache.join(format!("{}-{:08x}-{}", game.id, crc, version_tag(&o.root, &o.engine, game)));
    let exe = dir.join(exe_name());
    if exe.is_file() {
        return Ok((exe, game.clone()));
    }
    ui.status(&format!("Preparing {} (only the first time)...", game.title));
    let Some(tcc) = find_tcc(&o.root) else {
        return fail(EXIT_NO_TOOLCHAIN, "no C compiler found (looked for toolchain/tcc/tcc, GBA_PORT_CC and tcc on the PATH)");
    };
    let Some(sdl) = find_sdl2(&o.root) else {
        return fail(EXIT_NO_TOOLCHAIN, "SDL2 development files not found (sdl2/ next to the program, or GBA_PORT_SDL2)");
    };
    let tmp = o.cache.join(format!("build-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&tmp);
    if let Err(e) = std::fs::create_dir_all(&tmp) {
        return fail(EXIT_FAILED, format!("{}: {e}", tmp.display()));
    }
    let result = (|| -> Result<(), String> {
        let gen_c = tmp.join("gen.c");
        ui.status("Translating the game to C...");
        let out = Command::new(&o.engine)
            .args(["gen"]).arg(&o.rom).arg(&gen_c).arg("--pointers").arg("--profile").arg(&game.profile)
            .output().map_err(|e| format!("{}: {e}", o.engine.display()))?;
        if !out.status.success() {
            return Err(format!("generating the C failed:\n{}", tail(&String::from_utf8_lossy(&out.stderr))));
        }
        let dll_src = sdl.join("lib").join("x64").join("SDL2.dll");
        if cfg!(windows) {
            std::fs::copy(&dll_src, tmp.join("SDL2.dll")).map_err(|e| format!("SDL2.dll: {e}"))?;
            let r = Command::new(&tcc).arg("-impdef").arg("SDL2.dll").current_dir(&tmp).output().map_err(|e| e.to_string())?;
            if !r.status.success() {
                return Err(format!("tcc -impdef failed:\n{}", tail(&String::from_utf8_lossy(&r.stderr))));
            }
        }
        ui.status("Compiling (a few seconds)...");
        let rt = o.root.join("runtime");
        let mut cmd = Command::new(&tcc);
        cmd.arg(format!("-I{}", rt.display())).arg(format!("-I{}", sdl.join("include").display())).arg(format!("-I{}", sdl.join("include").join("SDL2").display()));
        cmd.arg(format!("-L{}", tmp.display())).arg("-o").arg(tmp.join(exe_name())).arg(&gen_c);
        for f in RUNTIME {
            cmd.arg(rt.join(f));
        }
        let own = game_sources(game);
        if !own.is_empty() {
            cmd.arg("-DRT_GAME");
            for f in &own {
                cmd.arg(f);
            }
        }
        cmd.arg("-lSDL2");
        let r = cmd.output().map_err(|e| format!("{}: {e}", tcc.display()))?;
        if !r.status.success() {
            return Err(format!("compiling failed:\n{}", tail(&String::from_utf8_lossy(&r.stderr))));
        }
        std::fs::remove_file(&gen_c).ok(); // generated from the ROM: not kept
        Ok(())
    })();
    match result {
        Ok(()) => {
            let _ = std::fs::remove_dir_all(&dir);
            if let Err(e) = std::fs::rename(&tmp, &dir) {
                return fail(EXIT_FAILED, format!("{}: {e}", dir.display()));
            }
            Ok((exe, game.clone()))
        }
        Err(e) => {
            let _ = std::fs::remove_dir_all(&tmp);
            fail(EXIT_BUILD_FAILED, e)
        }
    }
}

/// Builds if needed, remembers the ROM and plays it, waiting for the game to end. Returns the exit code.
pub fn run(o: &Options, launch: bool, game_args: &[String], ui: &dyn Ui) -> i32 {
    let (player, game) = match build(o, ui) {
        Ok(p) => p,
        Err(f) => {
            ui.error(&f.message);
            return f.code;
        }
    };
    if !launch {
        println!("{}", player.display());
        return 0;
    }
    remember_rom(&o.cache, &o.rom);
    ui.launching();
    let saves = saves_dir(o);
    let _ = std::fs::create_dir_all(&saves);
    match Command::new(&player).arg(&o.rom).arg("--saves").arg(&saves).arg("--title").arg(&game.title)
        .arg("--game-dir").arg(game_dir(&game).unwrap_or_default())
        .arg("--language").arg(std::env::var("GBA_PORT_LANGUAGE").unwrap_or_default())
        .args(theme_args(&game))
        .args(game_args).status() {
        Ok(s) => s.code().unwrap_or(EXIT_FAILED),
        Err(e) => {
            ui.error(&format!("{}: {e}", player.display()));
            EXIT_FAILED
        }
    }
}

/// The look of the player's menu from `[launcher]` of the profile: `--brand NAME --theme BG,PANEL,GOLD,INK` (RRGGBB).
fn theme_args(game: &Game) -> Vec<String> {
    let Ok(text) = std::fs::read_to_string(&game.profile) else { return Vec::new() };
    let b = crate::launcher::parse_brand(&text, game.profile.parent().unwrap_or(Path::new(".")));
    let rgb = |c: u32| format!("{:02X}{:02X}{:02X}", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF); // 0x00BBGGRR to RRGGBB
    let t = b.theme;
    vec!["--brand".into(), b.name, "--theme".into(), format!("{},{},{},{}", rgb(t.bg), rgb(t.panel), rgb(t.gold), rgb(t.ink))]
}

/// Where saves and save states go: `saves/` next to the profiles (so, next to the program in a package), or in the
/// cache when that folder cannot be written.
pub fn saves_dir(o: &Options) -> PathBuf {
    let beside = std::path::absolute(&o.games_dir).unwrap_or_else(|_| o.games_dir.clone()).parent().map(|p| p.join("saves"));
    if let Some(d) = beside {
        let ok = std::fs::create_dir_all(&d).is_ok() && {
            let probe = d.join(".write-test");
            let w = std::fs::write(&probe, b"").is_ok();
            let _ = std::fs::remove_file(&probe);
            w
        };
        if ok {
            return d;
        }
    }
    o.cache.join("saves")
}

/// Where the ROM dialog opens: the folder of the remembered ROM.
pub fn start_dir(cache: &Path) -> PathBuf {
    let p = PathBuf::from(std::fs::read_to_string(cache.join("last-rom.txt")).unwrap_or_default().trim());
    p.parent().map(Path::to_path_buf).unwrap_or_default()
}

pub fn remember_rom(cache: &Path, rom: &Path) {
    let _ = std::fs::create_dir_all(cache);
    let _ = std::fs::write(cache.join("last-rom.txt"), rom.to_string_lossy().as_bytes());
}

pub fn recalled_rom(cache: &Path) -> Option<PathBuf> {
    let p = PathBuf::from(std::fs::read_to_string(cache.join("last-rom.txt")).ok()?.trim());
    p.is_file().then_some(p)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn game_header() {
        let t = "[game]\nid = \"x\"\ntitle = \"X Game\"\ncrc32 = \"ABCEBBB1\"  # c\n[[overlay]]\nid = \"no\"\n";
        let g = parse_game(t, Path::new("games/x.toml")).unwrap();
        assert_eq!((g.id.as_str(), g.title.as_str(), g.crc32), ("x", "X Game", 0xABCEBBB1));
        assert!(identify(&[g], 0xABCEBBB1).is_some());
    }
    #[test]
    fn no_game_section() {
        assert!(parse_game("[roots]\nfile=\"a\"\n", Path::new("a.toml")).is_none());
    }
}
