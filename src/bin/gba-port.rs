//! `gba-port`: identifies the game, builds it the first time (cached) and plays it.
//!   gba-port                          the launcher window (Windows): choose the ROM once, then Play
//!   gba-port <rom.gba> [--games DIR] [--cache DIR] [--no-launch] [-- <player args>]
//! Without a ROM in a terminal it plays the last one. Exit codes: 0 ok, 1 failed, 2 usage, 3 unknown ROM,
//! 4 no compiler or SDL2, 5 build failed.
//! On Windows the program belongs to the windows subsystem (no console on a double click); started from a terminal it
//! takes that terminal's console, so messages and exit codes are the usual ones.
#![cfg_attr(windows, windows_subsystem = "windows")]
use gba_recomp::port::{self, ConsoleUi, Options};
use std::path::PathBuf;

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    #[cfg(windows)]
    let console = gba_recomp::launcher_win::attach_console();
    #[cfg(not(windows))]
    let console = true;

    if args.iter().any(|a| a == "--update") {
        std::process::exit(update_from_console());
    }
    let mut rom: Option<PathBuf> = None;
    let (mut games, mut cache, mut launch) = (None, None, true);
    let mut game_args: Vec<String> = Vec::new();
    let mut it = args.iter();
    while let Some(a) = it.next() {
        match a.as_str() {
            "--games" => games = it.next().map(PathBuf::from),
            "--cache" => cache = it.next().map(PathBuf::from),
            "--no-launch" => launch = false,
            "--" => game_args.extend(it.by_ref().cloned()),
            "-h" | "--help" => usage(),
            s if s.starts_with('-') => usage(),
            s => rom = Some(PathBuf::from(s)),
        }
    }
    let exe = std::env::current_exe().unwrap_or_default();
    let Some(root) = port::root_dir(&exe) else {
        eprintln!("error: runtime/ not found next to the program");
        std::process::exit(1)
    };
    let cache = cache.unwrap_or_else(port::cache_dir);
    // the profiles: --games, else games/ of the current folder, else games/ of the package
    let games_dir = games.unwrap_or_else(|| {
        let cwd = PathBuf::from("games");
        if cwd.is_dir() { cwd } else { root.join("games") }
    });
    let engine = exe.with_file_name(if cfg!(windows) { "gba-recomp.exe" } else { "gba-recomp" });

    #[cfg(windows)]
    {
        use gba_recomp::launcher_win::{self, Setup};
        let brand = gba_recomp::launcher::brand_for(&games_dir, &cache);
        let setup = Setup { root: root.clone(), engine: engine.clone(), games_dir: games_dir.clone(), brand };
        // no terminal and no ROM: the launcher; no terminal and a ROM (dropped on the icon): build and play with a window only when there is something to say
        if !console {
            match rom {
                None => std::process::exit(launcher_win::run_launcher(setup, &cache)),
                Some(r) => std::process::exit(launcher_win::run_rom(setup, r, &cache, launch, &game_args)),
            }
        }
    }

    let Some(rom) = rom.or_else(|| port::recalled_rom(&cache)) else { usage() };
    let o = Options { rom, root, engine, games_dir, cache };
    std::process::exit(port::run(&o, launch, &game_args, &ConsoleUi));
}

/// `gba-port --update`: asks for the latest release and installs it over this installation (what the launcher does with
/// its check box on, without the window). Needs `update.info` next to the program.
fn update_from_console() -> i32 {
    use gba_recomp::update;
    let exe = std::env::current_exe().unwrap_or_default();
    let dir = exe.parent().map(|p| p.to_path_buf()).unwrap_or_default();
    let Some(info) = update::read_info(&dir) else {
        eprintln!("error: this installation does not update itself (no update.info)");
        return 1;
    };
    match update::check(&port::cache_dir(), &info, true) {
        update::Outcome::Available { tag, .. } => {
            eprintln!("version {tag} is available (installed {}); installing...", info.version);
            match update::install(&info, &dir, &port::cache_dir().join("update-work")) {
                Ok(v) => { eprintln!("updated to {v}"); 0 }
                Err(e) => { eprintln!("error: {e}"); 1 }
            }
        }
        update::Outcome::UpToDate | update::Outcome::Skipped => { eprintln!("up to date ({})", info.version); 0 }
        update::Outcome::Failed(e) => { eprintln!("error: {e}"); 1 }
    }
}

fn usage() -> ! {
    eprintln!("gba-port [<rom.gba>] [--games DIR] [--cache DIR] [--no-launch] [-- <player args>]\n  without a ROM, plays the last one (the launcher window opens when there is no terminal)");
    std::process::exit(2)
}
