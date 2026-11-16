//! `gba-port <rom.gba>`: identifies the game, builds it the first time (cached) and plays it.
//!   gba-port <rom.gba> [--games DIR] [--cache DIR] [--no-launch] [-- <player args>]
//! Without a ROM it uses the last one played. Exit codes: 0 ok, 1 build or launch failed, 2 usage.
use gba_recomp::port::{self, ConsoleUi, Options, Ui};
use std::path::PathBuf;

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let mut rom: Option<PathBuf> = None;
    let (mut games, mut cache, mut launch) = (None, None, true);
    let mut game_args: Vec<String> = Vec::new();
    let mut it = args.iter();
    while let Some(a) = it.next() {
        match a.as_str() {
            "--games" => games = it.next().map(PathBuf::from),
            "--cache" => cache = it.next().map(PathBuf::from),
            "--no-launch" => launch = false,
            "--" => {
                game_args.extend(it.by_ref().cloned());
            }
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
    let Some(rom) = rom.or_else(|| port::recalled_rom(&cache)) else { usage() };
    // the profiles: --games, else games/ of the current folder, else games/ of the package
    let games_dir = games.unwrap_or_else(|| {
        let cwd = PathBuf::from("games");
        if cwd.is_dir() { cwd } else { root.join("games") }
    });
    let engine = exe.with_file_name(if cfg!(windows) { "gba-recomp.exe" } else { "gba-recomp" });
    let ui = ConsoleUi;
    let o = Options { rom: rom.clone(), root, engine, games_dir, cache: cache.clone() };
    let player = match port::build(&o, &ui) {
        Ok(p) => p,
        Err(e) => {
            ui.error(&e);
            std::process::exit(1)
        }
    };
    if !launch {
        println!("{}", player.display());
        return;
    }
    port::remember_rom(&cache, &rom);
    match std::process::Command::new(&player).arg(&rom).args(&game_args).status() {
        Ok(s) => std::process::exit(s.code().unwrap_or(1)),
        Err(e) => {
            ui.error(&format!("{}: {e}", player.display()));
            std::process::exit(1)
        }
    }
}

fn usage() -> ! {
    eprintln!("gba-port [<rom.gba>] [--games DIR] [--cache DIR] [--no-launch] [-- <player args>]\n  without a ROM, plays the last one");
    std::process::exit(2)
}
