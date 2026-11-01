//! veloxmaster: loopback heartbeat intake, TTL table, atomic `servers.json` writer.

pub mod schema;
pub mod state;

use axum::{
    body::Bytes,
    extract::{ConnectInfo, DefaultBodyLimit, State as AxState},
    http::{header, StatusCode},
    response::{IntoResponse, Response},
    routing::{get, post},
    Router,
};
use schema::RawHeartbeat;
use state::{rfc3339, unix_now, write_atomic, State};
use std::net::SocketAddr;
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tokio::sync::Notify;

pub const MAX_BODY: usize = 8 * 1024;

#[derive(Debug, Clone)]
pub struct Config {
    pub listen: String,
    pub output: PathBuf,
    pub public_address: String,
    pub ttl: Duration,
    pub write_interval: Duration,
    pub token: Option<String>,
}

impl Config {
    pub fn from_lookup(get: impl Fn(&str) -> Option<String>) -> Result<Self, String> {
        let num = |k: &str, d: u64| -> Result<u64, String> {
            match get(k) {
                None => Ok(d),
                Some(v) => v.trim().parse::<u64>().ok().filter(|n| *n > 0).ok_or(format!("{k} must be a positive integer")),
            }
        };
        let public_address = get("VELOXMASTER_PUBLIC_ADDRESS")
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty())
            .ok_or("VELOXMASTER_PUBLIC_ADDRESS is required")?;
        Ok(Self {
            listen: get("VELOXMASTER_LISTEN").unwrap_or_else(|| "127.0.0.1:8790".into()),
            output: get("VELOXMASTER_OUTPUT").unwrap_or_else(|| "./servers.json".into()).into(),
            public_address,
            ttl: Duration::from_secs(num("VELOXMASTER_TTL_S", 90)?),
            write_interval: Duration::from_secs(num("VELOXMASTER_WRITE_INTERVAL_S", 15)?),
            token: get("VELOXMASTER_TOKEN").filter(|t| !t.is_empty()),
        })
    }
}

pub struct App {
    pub cfg: Config,
    state: Mutex<State>,
    notify: Notify,
    last_write: Mutex<Option<u64>>,
}

impl App {
    pub fn new(cfg: Config) -> Arc<Self> {
        let state = State::new(cfg.ttl, cfg.public_address.clone());
        Arc::new(Self { cfg, state: Mutex::new(state), notify: Notify::new(), last_write: Mutex::new(None) })
    }

    fn render_json(&self) -> String {
        let f = self.state.lock().unwrap().render(Instant::now(), unix_now());
        serde_json::to_string_pretty(&f).expect("serialize") + "\n"
    }

    /// Expire, render and write the file once.
    pub fn flush(&self) -> std::io::Result<()> {
        self.state.lock().unwrap().expire(Instant::now());
        let json = self.render_json();
        write_atomic(&self.cfg.output, json.as_bytes())?;
        *self.last_write.lock().unwrap() = Some(unix_now());
        Ok(())
    }

    /// Writer loop: wakes on change, on the next expiry, and at least every write interval.
    pub async fn writer_loop(self: Arc<Self>) {
        loop {
            let wait = {
                let st = self.state.lock().unwrap();
                st.next_expiry(Instant::now()).map_or(self.cfg.write_interval, |e| e.min(self.cfg.write_interval))
            };
            tokio::select! {
                _ = self.notify.notified() => {}
                _ = tokio::time::sleep(wait + Duration::from_millis(5)) => {}
            }
            let app = self.clone();
            match tokio::task::spawn_blocking(move || app.flush()).await {
                Ok(Ok(())) => {}
                Ok(Err(e)) => tracing::error!("write {}: {e}", self.cfg.output.display()),
                Err(e) => tracing::error!("writer task: {e}"),
            }
        }
    }
}

pub fn router(app: Arc<App>) -> Router {
    Router::new()
        .route("/api/v1/heartbeat", post(heartbeat))
        .route("/api/v1/servers", get(servers))
        .route("/healthz", get(healthz))
        .layer(DefaultBodyLimit::max(MAX_BODY))
        .with_state(app)
}

fn json_response(body: String) -> Response {
    ([(header::CONTENT_TYPE, "application/json"), (header::CACHE_CONTROL, "no-cache")], body).into_response()
}

fn tokens_equal(a: &str, b: &str) -> bool {
    let (a, b) = (a.as_bytes(), b.as_bytes());
    let mut diff = a.len() ^ b.len();
    for i in 0..a.len().max(b.len()) {
        diff |= (*a.get(i).unwrap_or(&0) ^ *b.get(i).unwrap_or(&0)) as usize;
    }
    diff == 0
}

async fn heartbeat(AxState(app): AxState<Arc<App>>, ConnectInfo(peer): ConnectInfo<SocketAddr>, body: Bytes) -> Response {
    let raw: RawHeartbeat = match serde_json::from_slice(&body) {
        Ok(r) => r,
        Err(e) => return (StatusCode::BAD_REQUEST, format!("bad body: {e}\n")).into_response(),
    };
    let (hb, token) = match raw.validate() {
        Ok(v) => v,
        Err(e) => return (StatusCode::BAD_REQUEST, format!("{e}\n")).into_response(),
    };
    if let Some(want) = &app.cfg.token {
        if !token.as_deref().is_some_and(|t| tokens_equal(t, want)) {
            return (StatusCode::UNAUTHORIZED, "bad token\n").into_response();
        }
    }
    let changed = app.state.lock().unwrap().upsert(hb, peer.ip(), Instant::now());
    if changed {
        app.notify.notify_one();
    }
    StatusCode::NO_CONTENT.into_response()
}

async fn servers(AxState(app): AxState<Arc<App>>) -> Response {
    json_response(app.render_json())
}

async fn healthz(AxState(app): AxState<Arc<App>>) -> Response {
    let n = app.state.lock().unwrap().len();
    let lw = app.last_write.lock().unwrap().map(rfc3339);
    json_response(serde_json::json!({ "entries": n, "last_write": lw }).to_string())
}
