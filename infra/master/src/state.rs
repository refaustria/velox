//! In-memory table, TTL, address substitution, RFC 3339, atomic file write.

use crate::schema::{Entry, Heartbeat, ServersFile, HOST, SCHEMA};
use std::collections::HashMap;
use std::io::Write;
use std::net::IpAddr;
use std::path::Path;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

struct Stored {
    hb: Heartbeat,
    address: String,
    updated: Instant,
}

pub struct State {
    ttl: Duration,
    public_address: String,
    map: HashMap<String, Stored>,
}

impl State {
    pub fn new(ttl: Duration, public_address: String) -> Self {
        Self { ttl, public_address, map: HashMap::new() }
    }

    /// Returns true when the published content changed (new id, changed fields or address).
    pub fn upsert(&mut self, hb: Heartbeat, source: IpAddr, now: Instant) -> bool {
        let address = self.address_for(source, hb.port);
        match self.map.get_mut(&hb.id) {
            Some(s) => {
                let changed = s.hb != hb || s.address != address;
                s.hb = hb;
                s.address = address;
                s.updated = now;
                changed
            }
            None => {
                self.map.insert(hb.id.clone(), Stored { hb, address, updated: now });
                true
            }
        }
    }

    /// Loopback, private, link-local, unspecified sources are replaced by the public address.
    pub fn address_for(&self, source: IpAddr, port: u16) -> String {
        let source = match source {
            IpAddr::V6(v6) => v6.to_ipv4_mapped().map(IpAddr::V4).unwrap_or(IpAddr::V6(v6)),
            v4 => v4,
        };
        let internal = match source {
            IpAddr::V4(a) => a.is_loopback() || a.is_private() || a.is_link_local() || a.is_unspecified(),
            IpAddr::V6(a) => {
                a.is_loopback()
                    || a.is_unspecified()
                    || (a.segments()[0] & 0xfe00) == 0xfc00
                    || (a.segments()[0] & 0xffc0) == 0xfe80
            }
        };
        if internal {
            format!("{}:{}", self.public_address, port)
        } else if let IpAddr::V6(a) = source {
            format!("[{a}]:{port}")
        } else {
            format!("{source}:{port}")
        }
    }

    /// Drop entries older than the TTL. Returns true when something was removed.
    pub fn expire(&mut self, now: Instant) -> bool {
        let ttl = self.ttl;
        let before = self.map.len();
        self.map.retain(|_, s| now.saturating_duration_since(s.updated) < ttl);
        self.map.len() != before
    }

    /// Time until the oldest entry expires (None when empty).
    pub fn next_expiry(&self, now: Instant) -> Option<Duration> {
        self.map
            .values()
            .map(|s| self.ttl.saturating_sub(now.saturating_duration_since(s.updated)))
            .min()
    }

    pub fn len(&self) -> usize {
        self.map.len()
    }

    pub fn is_empty(&self) -> bool {
        self.map.is_empty()
    }

    pub fn render(&self, now: Instant, unix_now: u64) -> ServersFile {
        let mut servers: Vec<Entry> = self
            .map
            .values()
            .filter(|s| now.saturating_duration_since(s.updated) < self.ttl)
            .map(|s| Entry {
                hb: s.hb.clone(),
                address: s.address.clone(),
                host: HOST,
                updated_s_ago: now.saturating_duration_since(s.updated).as_secs(),
            })
            .collect();
        servers.sort_by(|a, b| a.hb.id.cmp(&b.hb.id));
        ServersFile { schema: SCHEMA, generated_at: rfc3339(unix_now), servers }
    }
}

pub fn unix_now() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0)
}

/// RFC 3339 UTC, whole seconds (Howard Hinnant's civil_from_days).
pub fn rfc3339(unix: u64) -> String {
    let days = (unix / 86_400) as i64;
    let rem = unix % 86_400;
    let z = days + 719_468;
    let era = z.div_euclid(146_097);
    let doe = z.rem_euclid(146_097);
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let y = yoe + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = if m <= 2 { y + 1 } else { y };
    format!("{y:04}-{m:02}-{d:02}T{:02}:{:02}:{:02}Z", rem / 3600, rem % 3600 / 60, rem % 60)
}

/// Write `bytes` to `path` via a temp file in the same directory plus rename (mode 0644).
pub fn write_atomic(path: &Path, bytes: &[u8]) -> std::io::Result<()> {
    use std::os::unix::fs::PermissionsExt;
    let dir = path.parent().filter(|p| !p.as_os_str().is_empty()).unwrap_or(Path::new("."));
    let name = path
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_else(|| "servers.json".into());
    let tmp = dir.join(format!(".{name}.tmp.{}", std::process::id()));
    {
        let mut f = std::fs::File::create(&tmp)?;
        f.write_all(bytes)?;
        f.sync_all()?;
        f.set_permissions(std::fs::Permissions::from_mode(0o644))?;
    }
    std::fs::rename(&tmp, path).inspect_err(|_| {
        let _ = std::fs::remove_file(&tmp);
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::net::Ipv4Addr;

    fn hb(id: &str, port: u16) -> Heartbeat {
        Heartbeat {
            schema: 1,
            id: id.into(),
            name: "n".into(),
            port,
            map: "L_DM1".into(),
            map_display_name: "Praesidium".into(),
            mode: "ffa".into(),
            phase: "match".into(),
            time_left_s: 1,
            players: 1,
            max_players: 8,
            bots: 0,
            spectators: 0,
            version: "0.6.0".into(),
            net_version: "1".into(),
            password: false,
        }
    }

    #[test]
    fn rfc3339_known() {
        assert_eq!(rfc3339(0), "1970-01-01T00:00:00Z");
        assert_eq!(rfc3339(1_782_950_400), "2026-07-02T00:00:00Z");
        assert_eq!(rfc3339(951_782_400), "2000-02-29T00:00:00Z");
    }

    #[test]
    fn ttl_expiry() {
        let t0 = Instant::now();
        let mut s = State::new(Duration::from_secs(90), "1.2.3.4".into());
        assert!(s.upsert(hb("a", 7777), Ipv4Addr::LOCALHOST.into(), t0));
        assert!(!s.expire(t0 + Duration::from_secs(89)));
        assert_eq!(s.render(t0 + Duration::from_secs(89), 0).servers[0].updated_s_ago, 89);
        assert_eq!(s.next_expiry(t0 + Duration::from_secs(80)), Some(Duration::from_secs(10)));
        assert!(s.expire(t0 + Duration::from_secs(90)));
        assert!(s.is_empty());
        s.upsert(hb("a", 7777), Ipv4Addr::LOCALHOST.into(), t0);
        assert!(s.render(t0 + Duration::from_secs(91), 0).servers.is_empty());
    }

    #[test]
    fn refresh_keeps_alive_and_reports_change() {
        let t0 = Instant::now();
        let mut s = State::new(Duration::from_secs(90), "1.2.3.4".into());
        s.upsert(hb("a", 7777), Ipv4Addr::LOCALHOST.into(), t0);
        let later = t0 + Duration::from_secs(60);
        assert!(!s.upsert(hb("a", 7777), Ipv4Addr::LOCALHOST.into(), later));
        assert!(!s.expire(t0 + Duration::from_secs(120)));
        let mut changed = hb("a", 7777);
        changed.players = 2;
        assert!(s.upsert(changed, Ipv4Addr::LOCALHOST.into(), later));
    }

    #[test]
    fn address_substitution() {
        let s = State::new(Duration::from_secs(90), "77.244.242.34".into());
        for src in ["127.0.0.1", "10.1.2.3", "192.168.0.5", "172.16.0.1", "169.254.1.1", "::1", "fd00::1", "::ffff:127.0.0.1"] {
            assert_eq!(s.address_for(src.parse().unwrap(), 7777), "77.244.242.34:7777", "{src}");
        }
        assert_eq!(s.address_for("203.0.113.9".parse().unwrap(), 7778), "203.0.113.9:7778");
        assert_eq!(s.address_for("2001:db8::1".parse().unwrap(), 7778), "[2001:db8::1]:7778");
    }

    #[test]
    fn sorted_by_id() {
        let t0 = Instant::now();
        let mut s = State::new(Duration::from_secs(90), "1.2.3.4".into());
        s.upsert(hb("ffa", 7777), Ipv4Addr::LOCALHOST.into(), t0);
        s.upsert(hb("duel", 7778), Ipv4Addr::LOCALHOST.into(), t0);
        let ids: Vec<_> = s.render(t0, 0).servers.into_iter().map(|e| e.hb.id).collect();
        assert_eq!(ids, ["duel", "ffa"]);
    }

    #[test]
    fn atomic_write_readable_while_writing() {
        let dir = std::env::temp_dir().join(format!("vm-atomic-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("servers.json");
        let a = format!("{{\"x\":\"{}\"}}", "a".repeat(200_000));
        let b = format!("{{\"x\":\"{}\"}}", "b".repeat(200_000));
        write_atomic(&path, a.as_bytes()).unwrap();
        let stop = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
        let (p2, s2, a2, b2) = (path.clone(), stop.clone(), a.clone(), b.clone());
        let writer = std::thread::spawn(move || {
            for i in 0..300 {
                write_atomic(&p2, if i % 2 == 0 { b2.as_bytes() } else { a2.as_bytes() }).unwrap();
            }
            s2.store(true, std::sync::atomic::Ordering::SeqCst);
        });
        let mut reads = 0;
        while !stop.load(std::sync::atomic::Ordering::SeqCst) {
            let got = std::fs::read_to_string(&path).unwrap();
            assert!(got == a || got == b, "partial file observed");
            reads += 1;
        }
        writer.join().unwrap();
        assert!(reads > 0);
        assert_eq!(std::fs::read_dir(&dir).unwrap().count(), 1, "temp file left behind");
        std::fs::remove_dir_all(&dir).unwrap();
    }
}
