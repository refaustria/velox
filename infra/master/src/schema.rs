//! Heartbeat body (ADR 0007 section 2) and the published `servers.json` entry.

use serde::{Deserialize, Serialize};

pub const SCHEMA: u32 = 1;
pub const HOST: &str = "games.inceptum.at";
pub const MAX_ID: usize = 32;
pub const MAX_NAME: usize = 64;
pub const MAX_STR: usize = 128;
pub const MAX_COUNT: u32 = 1024;

/// `net_version` is a string in the file; the game may send a number (FNetworkVersion is a uint32).
#[derive(Debug, Clone, PartialEq, Deserialize)]
#[serde(untagged)]
pub enum StrOrNum {
    S(String),
    N(u64),
}

impl StrOrNum {
    fn into_string(self) -> String {
        match self {
            StrOrNum::S(s) => s,
            StrOrNum::N(n) => n.to_string(),
        }
    }
}

/// Raw wire body. Unknown fields are ignored (forward compatibility).
#[derive(Debug, Deserialize)]
pub struct RawHeartbeat {
    pub schema: u32,
    pub id: String,
    pub name: String,
    pub port: u16,
    pub map: String,
    pub map_display_name: String,
    pub mode: String,
    pub phase: String,
    pub time_left_s: u32,
    pub players: u32,
    pub max_players: u32,
    pub bots: u32,
    pub spectators: u32,
    pub version: String,
    pub net_version: StrOrNum,
    pub password: bool,
    #[serde(default)]
    pub token: Option<String>,
}

/// Validated heartbeat; field order is the published order. The token is stripped.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct Heartbeat {
    pub schema: u32,
    pub id: String,
    pub name: String,
    pub port: u16,
    pub map: String,
    pub map_display_name: String,
    pub mode: String,
    pub phase: String,
    pub time_left_s: u32,
    pub players: u32,
    pub max_players: u32,
    pub bots: u32,
    pub spectators: u32,
    pub version: String,
    pub net_version: String,
    pub password: bool,
}

impl RawHeartbeat {
    /// Types are already enforced by serde; here: lengths, charset, ranges.
    pub fn validate(self) -> Result<(Heartbeat, Option<String>), String> {
        if self.schema != SCHEMA {
            return Err(format!("unsupported schema {}", self.schema));
        }
        if self.id.is_empty()
            || self.id.chars().count() > MAX_ID
            || !self.id.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'_' || b == b'-')
        {
            return Err("id must be 1..=32 chars of [a-z0-9_-]".into());
        }
        if self.name.is_empty() || self.name.chars().count() > MAX_NAME {
            return Err("name must be 1..=64 chars".into());
        }
        if self.port == 0 {
            return Err("port must be non-zero".into());
        }
        let net_version = self.net_version.into_string();
        for (k, v) in [
            ("map", &self.map),
            ("map_display_name", &self.map_display_name),
            ("mode", &self.mode),
            ("phase", &self.phase),
            ("version", &self.version),
            ("net_version", &net_version),
        ] {
            if v.chars().count() > MAX_STR {
                return Err(format!("{k} longer than {MAX_STR} chars"));
            }
        }
        for (k, v) in [
            ("players", self.players),
            ("max_players", self.max_players),
            ("bots", self.bots),
            ("spectators", self.spectators),
        ] {
            if v > MAX_COUNT {
                return Err(format!("{k} out of range"));
            }
        }
        if self.token.as_ref().is_some_and(|t| t.chars().count() > MAX_STR) {
            return Err("token too long".into());
        }
        let hb = Heartbeat {
            schema: self.schema,
            id: self.id,
            name: self.name,
            port: self.port,
            map: self.map,
            map_display_name: self.map_display_name,
            mode: self.mode,
            phase: self.phase,
            time_left_s: self.time_left_s,
            players: self.players,
            max_players: self.max_players,
            bots: self.bots,
            spectators: self.spectators,
            version: self.version,
            net_version,
            password: self.password,
        };
        Ok((hb, self.token))
    }
}

/// One published server.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct Entry {
    #[serde(flatten)]
    pub hb: Heartbeat,
    pub address: String,
    pub host: &'static str,
    pub updated_s_ago: u64,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct ServersFile {
    pub schema: u32,
    pub generated_at: String,
    pub servers: Vec<Entry>,
}
