//! Update notice and installation for the desktop packages: the launcher asks GitHub for the latest release of the game's
//! repository, says when it is newer than the installed one and, if the person agrees, downloads the package of this
//! platform, checks it against the `SHA256SUMS` of the same release and copies its files over the installation (the saves
//! and anything else that is not in the package stay). Off by default: nothing goes to the network until the person turns
//! it on. The installation says what it is in `update.info` (`repo=owner/name`, `version=0.1.0`) next to the executable;
//! without that file there is nothing to check. Only `http_get` and `fetch_verified` touch the network, and they only
//! fetch: nothing of the ROM or the machine is sent. Same design as the one of `snes-recomp`, without dependencies.

use std::path::{Path, PathBuf};
use std::process::Command;

/// Where the installation says what it is.
#[derive(Debug, PartialEq, Eq)]
pub struct Info {
    pub repo: String,
    pub version: String,
}

pub fn read_info(exe_dir: &Path) -> Option<Info> {
    let text = std::fs::read_to_string(exe_dir.join("update.info")).ok()?;
    let get = |k: &str| text.lines().find_map(|l| l.trim().strip_prefix(k)?.strip_prefix('=')).map(|v| v.trim().to_string()).filter(|v| !v.is_empty());
    let (repo, version) = (get("repo")?, get("version")?);
    (repo.split('/').count() == 2 && !repo.contains(' ') && looks_like_version(&version)).then_some(Info { repo, version })
}

fn parts(v: &str) -> Option<Vec<u64>> {
    let v = v.strip_prefix('v').unwrap_or(v);
    v.split('.').map(|p| p.parse::<u64>().ok()).collect()
}

pub fn looks_like_version(v: &str) -> bool {
    parts(v).is_some_and(|p| !p.is_empty())
}

/// Whether `tag` (`v0.10`) is a newer version than `current` (`0.9`); numeric, so 0.10 > 0.9. A tag that is not a version never is.
pub fn is_newer(tag: &str, current: &str) -> bool {
    let (Some(mut a), Some(mut b)) = (parts(tag), parts(current)) else { return false };
    let n = a.len().max(b.len());
    a.resize(n, 0);
    b.resize(n, 0);
    a > b
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Release {
    pub tag: String,
    pub page: String,
    /// (name, download url)
    pub assets: Vec<(String, String)>,
}

/// The string value of `"key": "..."` found at or after `from`, and where it ends.
fn json_str(text: &str, key: &str, from: usize) -> Option<(String, usize)> {
    let pat = format!("\"{key}\"");
    let mut at = from;
    loop {
        let i = text[at..].find(&pat)? + at;
        let rest = text[i + pat.len()..].trim_start();
        if let Some(r) = rest.strip_prefix(':') {
            let r = r.trim_start();
            if let Some(body) = r.strip_prefix('"') {
                let mut out = String::new();
                let mut esc = false;
                for (n, c) in body.char_indices() {
                    if esc {
                        out.push(match c { 'n' => '\n', 't' => '\t', other => other });
                        esc = false;
                    } else if c == '\\' {
                        esc = true;
                    } else if c == '"' {
                        let end = text.len() - body.len() + n + 1;
                        return Some((out, end));
                    } else {
                        out.push(c);
                    }
                }
                return None;
            }
        }
        at = i + pat.len();
    }
}

/// The few fields of GitHub's "latest release" answer that are needed (`tag_name`, `html_url`, and each asset's `name` and
/// `browser_download_url`; in an asset the name comes before its URL).
pub fn parse_release(json: &str) -> Result<Release, String> {
    let (tag, _) = json_str(json, "tag_name", 0).ok_or("no tag_name")?;
    let (page, _) = json_str(json, "html_url", 0).ok_or("no html_url")?;
    if !safe_url(&page) || !looks_like_version(&tag) {
        return Err(format!("unusable release {tag}"));
    }
    let mut assets = Vec::new();
    let start = json.find("\"assets\"").unwrap_or(json.len());
    let mut at = start;
    while let Some((url, end)) = json_str(json, "browser_download_url", at) {
        // the name of this asset is the last "name" between the previous URL and this one
        let url_pos = end - url.len() - 1;
        let mut name = None;
        let mut p = at;
        while let Some((n, e)) = json_str(json, "name", p) {
            if e > url_pos {
                break;
            }
            name = Some(n);
            p = e;
        }
        if let (Some(n), true) = (name, safe_url(&url)) {
            assets.push((n, url));
        }
        at = end;
    }
    Ok(Release { tag, page, assets })
}

/// Only HTTPS links are followed (plus this machine, for the tests of the update with a local server).
fn safe_url(u: &str) -> bool {
    u.starts_with("https://") || u.starts_with("http://127.0.0.1") || u.starts_with("http://localhost")
}

/// The suffix of the package of this platform in the release.
pub fn platform_suffix() -> &'static str {
    if cfg!(windows) {
        "-win64.zip"
    } else {
        "-linux-x86_64.tar.gz"
    }
}

pub fn package_of(r: &Release) -> Option<&(String, String)> {
    r.assets.iter().find(|(n, _)| n.ends_with(platform_suffix()))
}

/// The hash of `name` in a `SHA256SUMS` text (`<hex>  <name>` per line).
pub fn hash_in_sums(sums: &str, name: &str) -> Option<String> {
    sums.lines().find_map(|l| {
        let (h, n) = l.split_once(char::is_whitespace)?;
        (n.trim().trim_start_matches('*') == name && h.len() == 64 && h.bytes().all(|b| b.is_ascii_hexdigit())).then(|| h.to_ascii_lowercase())
    })
}

/// GET with curl (Windows 10 and later have it) or wget; at most 6 s to connect and 20 s in all.
pub fn http_get(url: &str) -> Result<Vec<u8>, String> {
    let out = Command::new("curl")
        .args(["--fail", "--silent", "--location", "--connect-timeout", "6", "--max-time", "20", "-H", "User-Agent: gba-port-updater", "-H", "Accept: application/vnd.github+json"])
        .arg(url)
        .output();
    match out {
        Ok(o) if o.status.success() => return Ok(o.stdout),
        Ok(o) => return Err(format!("{url}: {}", String::from_utf8_lossy(&o.stderr).trim())),
        Err(_) => {}
    }
    let o = Command::new("wget").args(["-q", "-O", "-", "--timeout=10", "--user-agent=gba-port-updater"]).arg(url).output().map_err(|_| "neither curl nor wget is available".to_string())?;
    if o.status.success() {
        Ok(o.stdout)
    } else {
        Err(format!("{url}: download failed"))
    }
}

pub fn latest(repo: &str) -> Result<Release, String> {
    // GBA_PORT_UPDATE_API replaces the GitHub address (to try the update against a local server)
    let base = std::env::var("GBA_PORT_UPDATE_API").ok().filter(|b| safe_url(b)).unwrap_or_else(|| "https://api.github.com".to_string());
    let body = http_get(&format!("{base}/repos/{repo}/releases/latest"))?;
    parse_release(&String::from_utf8_lossy(&body))
}

/// Downloads `url` into `dest`, keeping it only if its SHA-256 is `sha256` (hex); a file that does not match is deleted.
pub fn fetch_verified(url: &str, sha256: &str, dest: &Path) -> Result<(), String> {
    let part = dest.with_extension("part");
    let _ = std::fs::remove_file(&part);
    let out = Command::new("curl")
        .args(["--fail", "--silent", "--show-error", "--location", "--connect-timeout", "10", "--max-time", "600", "--output"])
        .arg(&part)
        .arg(url)
        .output()
        .map_err(|e| format!("cannot run curl to download {url}: {e}"))?;
    if !out.status.success() {
        let _ = std::fs::remove_file(&part);
        return Err(format!("the download of {url} failed: {}", String::from_utf8_lossy(&out.stderr).trim()));
    }
    let data = std::fs::read(&part).map_err(|e| format!("{}: {e}", part.display()))?;
    let got = hex(&sha256_of(&data));
    if !got.eq_ignore_ascii_case(sha256) {
        let _ = std::fs::remove_file(&part);
        return Err(format!("the downloaded file does not have the expected SHA-256 (expected {sha256}, got {got}): discarded"));
    }
    std::fs::rename(&part, dest).map_err(|e| format!("{}: {e}", dest.display()))
}

// ---- the person's choices, in a small file ----

#[derive(Default, Debug, PartialEq, Eq)]
pub struct Settings {
    pub check: bool,
    /// Unix seconds of the last check that worked.
    pub last: u64,
    pub tag: String,
    pub page: String,
    pub skipped: String,
}

fn settings_path(dir: &Path) -> PathBuf {
    dir.join("update.txt")
}

pub fn load_settings(dir: &Path) -> Settings {
    let text = std::fs::read_to_string(settings_path(dir)).unwrap_or_default();
    let get = |k: &str| text.lines().find_map(|l| l.strip_prefix(k)?.strip_prefix('=')).unwrap_or("").trim().to_string();
    Settings { check: get("check") == "1", last: get("last").parse().unwrap_or(0), tag: get("tag"), page: get("page"), skipped: get("skipped") }
}

pub fn save_settings(dir: &Path, s: &Settings) {
    let _ = std::fs::create_dir_all(dir);
    let _ = std::fs::write(settings_path(dir), format!("check={}\nlast={}\ntag={}\npage={}\nskipped={}\n", s.check as u8, s.last, s.tag, s.page, s.skipped));
}

fn now() -> u64 {
    std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0)
}

const DAY: u64 = 24 * 3600;

#[derive(Debug, PartialEq, Eq)]
pub enum Outcome {
    /// Nothing newer (or the check is off).
    UpToDate,
    /// A newer version that the person has not skipped.
    Available { tag: String, page: String },
    /// A newer version that the person chose to skip.
    Skipped,
    Failed(String),
}

/// The check of the launcher: with `force` it always asks; otherwise only when the check is on and the last answer is
/// more than a day old (a fresh answer is reused without touching the network).
pub fn check(dir: &Path, info: &Info, force: bool) -> Outcome {
    let mut s = load_settings(dir);
    if !force && !s.check {
        return Outcome::UpToDate;
    }
    if force || now().saturating_sub(s.last) >= DAY || s.tag.is_empty() {
        match latest(&info.repo) {
            Ok(r) => {
                s.last = now();
                s.tag = r.tag;
                s.page = r.page;
                save_settings(dir, &s);
            }
            Err(e) => return Outcome::Failed(e),
        }
    }
    if !is_newer(&s.tag, &info.version) {
        Outcome::UpToDate
    } else if s.skipped == s.tag {
        Outcome::Skipped
    } else {
        Outcome::Available { tag: s.tag, page: s.page }
    }
}

pub fn skip(dir: &Path, tag: &str) {
    let mut s = load_settings(dir);
    s.skipped = tag.to_string();
    save_settings(dir, &s);
}

pub fn set_check(dir: &Path, on: bool) {
    let mut s = load_settings(dir);
    s.check = on;
    save_settings(dir, &s);
}

// ---- installing ----

/// Copies the files of `from` over `to`. A file that cannot be replaced because it is running (the executable on
/// Windows) is moved aside as `<name>.old` first; `clean_old` removes those at the next start.
pub fn copy_over(from: &Path, to: &Path) -> Result<(), String> {
    std::fs::create_dir_all(to).map_err(|e| format!("{}: {e}", to.display()))?;
    for entry in std::fs::read_dir(from).map_err(|e| format!("{}: {e}", from.display()))? {
        let entry = entry.map_err(|e| e.to_string())?;
        let (src, dst) = (entry.path(), to.join(entry.file_name()));
        if entry.file_type().map_err(|e| e.to_string())?.is_dir() {
            copy_over(&src, &dst)?;
            continue;
        }
        let tmp = dst.with_file_name(format!("{}.new", entry.file_name().to_string_lossy()));
        std::fs::copy(&src, &tmp).map_err(|e| format!("{}: {e}", tmp.display()))?;
        if std::fs::rename(&tmp, &dst).is_err() {
            let old = dst.with_file_name(format!("{}.old", entry.file_name().to_string_lossy()));
            let _ = std::fs::remove_file(&old);
            std::fs::rename(&dst, &old).map_err(|e| format!("{}: {e}", dst.display()))?;
            std::fs::rename(&tmp, &dst).map_err(|e| format!("{}: {e}", dst.display()))?;
        }
    }
    Ok(())
}

/// Removes the `.old` files a previous update left (at start; whatever cannot be removed stays for the next time).
pub fn clean_old(dir: &Path) {
    if let Ok(rd) = std::fs::read_dir(dir) {
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() {
                clean_old(&p);
            } else if p.extension().is_some_and(|x| x == "old") {
                let _ = std::fs::remove_file(&p);
            }
        }
    }
}

/// Downloads the package of this platform of the latest release, checks it against its `SHA256SUMS` and installs it
/// over `install_dir`. Returns the version installed.
pub fn install(info: &Info, install_dir: &Path, work: &Path) -> Result<String, String> {
    let r = latest(&info.repo)?;
    if !is_newer(&r.tag, &info.version) {
        return Err("there is no newer version".into());
    }
    let (name, url) = package_of(&r).ok_or_else(|| format!("the release {} has no package for this system", r.tag))?;
    let (_, sums_url) = r.assets.iter().find(|(n, _)| n == "SHA256SUMS").ok_or("the release has no SHA256SUMS: not installing")?;
    let sums = String::from_utf8_lossy(&http_get(sums_url)?).to_string();
    let hash = hash_in_sums(&sums, name).ok_or_else(|| format!("SHA256SUMS does not list {name}: not installing"))?;
    let _ = std::fs::remove_dir_all(work);
    std::fs::create_dir_all(work).map_err(|e| format!("{}: {e}", work.display()))?;
    let pkg = work.join(name);
    fetch_verified(url, &hash, &pkg)?;
    let out = work.join("x");
    std::fs::create_dir_all(&out).map_err(|e| e.to_string())?;
    let t = Command::new("tar").arg("-xf").arg(&pkg).arg("-C").arg(&out).output().map_err(|e| format!("cannot run tar: {e}"))?;
    if !t.status.success() {
        return Err(format!("cannot unpack {name}: {}", String::from_utf8_lossy(&t.stderr).trim()));
    }
    // the package is one folder; it must say what it is or this is not a package of the game
    let root = std::fs::read_dir(&out).map_err(|e| e.to_string())?.flatten().map(|e| e.path()).find(|p| p.is_dir()).ok_or("the package is empty")?;
    if !root.join("update.info").is_file() {
        return Err("the package has no update.info: not installing".into());
    }
    copy_over(&root, install_dir)?;
    let _ = std::fs::remove_dir_all(work);
    Ok(r.tag.trim_start_matches('v').to_string())
}

// ---- SHA-256 ----

fn primes(n: usize) -> Vec<u64> {
    let mut v = Vec::new();
    let mut c = 2u64;
    while v.len() < n {
        if v.iter().all(|p| c % p != 0) {
            v.push(c);
        }
        c += 1;
    }
    v
}

/// SHA-256 of `data`. The constants are computed (fractional parts of the square and cube roots of the first primes),
/// as the standard defines them, instead of typed in.
pub fn sha256_of(data: &[u8]) -> [u8; 32] {
    let frac = |x: f64| ((x - x.floor()) * 4294967296.0) as u32;
    let p = primes(64);
    let k: Vec<u32> = p.iter().map(|&q| frac((q as f64).cbrt())).collect();
    let mut h: [u32; 8] = std::array::from_fn(|i| frac((p[i] as f64).sqrt()));
    let mut msg = data.to_vec();
    msg.push(0x80);
    while msg.len() % 64 != 56 {
        msg.push(0);
    }
    msg.extend_from_slice(&((data.len() as u64) * 8).to_be_bytes());
    for chunk in msg.chunks(64) {
        let mut w = [0u32; 64];
        for i in 0..16 {
            w[i] = u32::from_be_bytes([chunk[4 * i], chunk[4 * i + 1], chunk[4 * i + 2], chunk[4 * i + 3]]);
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16].wrapping_add(s0).wrapping_add(w[i - 7]).wrapping_add(s1);
        }
        let mut v = h;
        for i in 0..64 {
            let s1 = v[4].rotate_right(6) ^ v[4].rotate_right(11) ^ v[4].rotate_right(25);
            let ch = (v[4] & v[5]) ^ (!v[4] & v[6]);
            let t1 = v[7].wrapping_add(s1).wrapping_add(ch).wrapping_add(k[i]).wrapping_add(w[i]);
            let s0 = v[0].rotate_right(2) ^ v[0].rotate_right(13) ^ v[0].rotate_right(22);
            let maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            let t2 = s0.wrapping_add(maj);
            v = [t1.wrapping_add(t2), v[0], v[1], v[2], v[3].wrapping_add(t1), v[4], v[5], v[6]];
        }
        for i in 0..8 {
            h[i] = h[i].wrapping_add(v[i]);
        }
    }
    let mut out = [0u8; 32];
    for i in 0..8 {
        out[4 * i..4 * i + 4].copy_from_slice(&h[i].to_be_bytes());
    }
    out
}

pub fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn temp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("gba-port-update-{}-{name}", std::process::id()));
        let _ = std::fs::remove_dir_all(&d);
        std::fs::create_dir_all(&d).unwrap();
        d
    }

    #[test]
    fn sha256_known_answers() {
        assert_eq!(hex(&sha256_of(b"")), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        assert_eq!(hex(&sha256_of(b"abc")), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        let long = vec![b'a'; 1000];
        assert_eq!(hex(&sha256_of(&long)), "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
    }

    #[test]
    fn versions_compare_numerically() {
        assert!(is_newer("v0.10", "0.9"));
        assert!(is_newer("0.1.1", "0.1.0"));
        assert!(!is_newer("v0.1.0", "0.1.0"));
        assert!(!is_newer("nightly", "0.1.0"));
        assert!(!is_newer("v0.0.9", "0.1"));
    }

    #[test]
    fn a_github_answer_is_read() {
        let json = r#"{"url":"https://api.github.com/repos/o/r/releases/1","html_url":"https://github.com/o/r/releases/tag/v0.2.0","id":1,
          "author":{"login":"x","html_url":"https://github.com/x"},"tag_name":"v0.2.0","name":"Version 0.2.0",
          "assets":[{"url":"u1","id":5,"name":"SHA256SUMS","uploader":{"login":"x"},"size":10,"browser_download_url":"https://github.com/o/r/releases/download/v0.2.0/SHA256SUMS"},
          {"url":"u2","id":6,"name":"minishcap-recomp-0.2.0-win64.zip","size":99,"browser_download_url":"https://github.com/o/r/releases/download/v0.2.0/minishcap-recomp-0.2.0-win64.zip"}]}"#;
        let r = parse_release(json).unwrap();
        assert_eq!((r.tag.as_str(), r.page.as_str()), ("v0.2.0", "https://github.com/o/r/releases/tag/v0.2.0"));
        assert_eq!(r.assets.len(), 2);
        assert_eq!(r.assets[0].0, "SHA256SUMS");
        assert_eq!(r.assets[1].0, "minishcap-recomp-0.2.0-win64.zip");
        assert!(r.assets[1].1.ends_with("0.2.0-win64.zip"));
        assert_eq!(package_of(&r).map(|p| p.0.as_str()), if cfg!(windows) { Some("minishcap-recomp-0.2.0-win64.zip") } else { None });
        assert!(parse_release(r#"{"tag_name":"nightly","html_url":"https://x"}"#).is_err());
        assert!(parse_release(r#"{"tag_name":"v1.0","html_url":"ftp://x"}"#).is_err());
    }

    #[test]
    fn the_hash_is_found_by_name() {
        let h = "a".repeat(64);
        let sums = format!("{h}  other.zip\n{}  pkg-win64.zip\n", "B".repeat(64));
        assert_eq!(hash_in_sums(&sums, "pkg-win64.zip"), Some("b".repeat(64)));
        assert_eq!(hash_in_sums(&sums, "missing.zip"), None);
        assert_eq!(hash_in_sums("zz  pkg-win64.zip", "pkg-win64.zip"), None);
    }

    #[test]
    fn update_info_is_read_and_checked() {
        let d = temp("info");
        assert_eq!(read_info(&d), None);
        std::fs::write(d.join("update.info"), "repo=AlekayDevs/minishcap-recomp\nversion=0.1.0\n").unwrap();
        assert_eq!(read_info(&d), Some(Info { repo: "AlekayDevs/minishcap-recomp".into(), version: "0.1.0".into() }));
        std::fs::write(d.join("update.info"), "repo=not a repo\nversion=0.1.0\n").unwrap();
        assert_eq!(read_info(&d), None);
        let _ = std::fs::remove_dir_all(&d);
    }

    #[test]
    fn off_by_default_and_nothing_asked_when_off() {
        let d = temp("off");
        let info = Info { repo: "o/r".into(), version: "0.1.0".into() };
        assert!(!load_settings(&d).check);
        assert_eq!(check(&d, &info, false), Outcome::UpToDate, "no network when it is off");
        set_check(&d, true);
        assert!(load_settings(&d).check);
        skip(&d, "v0.2.0");
        assert_eq!(load_settings(&d).skipped, "v0.2.0");
        // a fresh answer is reused: newer and skipped -> Skipped; newer and not skipped -> Available
        let mut s = load_settings(&d);
        s.last = now();
        s.tag = "v0.2.0".into();
        s.page = "https://github.com/o/r/releases/tag/v0.2.0".into();
        save_settings(&d, &s);
        assert_eq!(check(&d, &info, false), Outcome::Skipped);
        s.skipped.clear();
        save_settings(&d, &s);
        assert!(matches!(check(&d, &info, false), Outcome::Available { .. }));
        let _ = std::fs::remove_dir_all(&d);
    }

    #[test]
    fn copy_over_replaces_files_and_keeps_the_rest() {
        let (a, b) = (temp("from"), temp("to"));
        std::fs::create_dir_all(a.join("games")).unwrap();
        std::fs::write(a.join("games/x.toml"), "new").unwrap();
        std::fs::write(a.join("gba-port.exe"), "new exe").unwrap();
        std::fs::create_dir_all(b.join("saves")).unwrap();
        std::fs::write(b.join("saves/a.sav"), "mine").unwrap();
        std::fs::write(b.join("gba-port.exe"), "old exe").unwrap();
        copy_over(&a, &b).unwrap();
        assert_eq!(std::fs::read_to_string(b.join("gba-port.exe")).unwrap(), "new exe");
        assert_eq!(std::fs::read_to_string(b.join("games/x.toml")).unwrap(), "new");
        assert_eq!(std::fs::read_to_string(b.join("saves/a.sav")).unwrap(), "mine", "saves stay");
        clean_old(&b);
        let _ = std::fs::remove_dir_all(&a);
        let _ = std::fs::remove_dir_all(&b);
    }
}
