use axum::body::Body;
use axum::extract::ConnectInfo;
use axum::http::{Request, StatusCode};
use http_body_util::BodyExt;
use std::net::SocketAddr;
use std::time::{Duration, Instant};
use tower::ServiceExt;
use veloxmaster::schema::RawHeartbeat;
use veloxmaster::state::State;
use veloxmaster::{router, App, Config};

const FFA: &str = include_str!("fixtures/fixture_ffa.json");
const DUEL: &str = include_str!("fixtures/fixture_duel.json");

fn cfg(token: Option<&str>, out: &str) -> Config {
    let t = token.map(String::from);
    let o = out.to_string();
    Config::from_lookup(move |k| match k {
        "VELOXMASTER_PUBLIC_ADDRESS" => Some("77.244.242.34".into()),
        "VELOXMASTER_OUTPUT" => Some(o.clone()),
        "VELOXMASTER_TOKEN" => t.clone(),
        _ => None,
    })
    .unwrap()
}

async fn post(app: &axum::Router, body: Vec<u8>, peer: &str) -> StatusCode {
    let mut req = Request::post("/api/v1/heartbeat").body(Body::from(body)).unwrap();
    req.extensions_mut().insert(ConnectInfo(peer.parse::<SocketAddr>().unwrap()));
    app.clone().oneshot(req).await.unwrap().status()
}

async fn get(app: &axum::Router, path: &str) -> (StatusCode, String) {
    let mut req = Request::get(path).body(Body::empty()).unwrap();
    req.extensions_mut().insert(ConnectInfo("127.0.0.1:1".parse::<SocketAddr>().unwrap()));
    let resp = app.clone().oneshot(req).await.unwrap();
    let st = resp.status();
    (st, String::from_utf8(resp.into_body().collect().await.unwrap().to_bytes().to_vec()).unwrap())
}

fn with(json: &str, f: impl FnOnce(&mut serde_json::Value)) -> Vec<u8> {
    let mut v: serde_json::Value = serde_json::from_str(json).unwrap();
    f(&mut v);
    serde_json::to_vec(&v).unwrap()
}

#[tokio::test]
async fn accepts_and_publishes() {
    let app = App::new(cfg(None, "/nonexistent/x.json"));
    let r = router(app);
    assert_eq!(post(&r, FFA.into(), "127.0.0.1:5000").await, StatusCode::NO_CONTENT);
    assert_eq!(post(&r, DUEL.into(), "203.0.113.9:5000").await, StatusCode::NO_CONTENT);
    let (st, body) = get(&r, "/api/v1/servers").await;
    assert_eq!(st, StatusCode::OK);
    let v: serde_json::Value = serde_json::from_str(&body).unwrap();
    assert_eq!(v["servers"][0]["id"], "duel");
    assert_eq!(v["servers"][0]["address"], "203.0.113.9:7778");
    assert_eq!(v["servers"][1]["address"], "77.244.242.34:7777");
    assert_eq!(v["servers"][1]["host"], "games.inceptum.at");
    assert!(v["servers"][1].get("token").is_none());
    let (st, body) = get(&r, "/healthz").await;
    assert_eq!(st, StatusCode::OK);
    let h: serde_json::Value = serde_json::from_str(&body).unwrap();
    assert_eq!(h["entries"], 2);
    assert!(h["last_write"].is_null());
}

#[tokio::test]
async fn validation_failures() {
    let r = router(App::new(cfg(None, "/nonexistent/x.json")));
    let bad = |f: fn(&mut serde_json::Value)| with(FFA, f);
    assert_eq!(post(&r, b"not json".to_vec(), "127.0.0.1:1").await, StatusCode::BAD_REQUEST);
    assert_eq!(post(&r, b"{}".to_vec(), "127.0.0.1:1").await, StatusCode::BAD_REQUEST);
    assert_eq!(post(&r, bad(|v| v["id"] = "Bad ID".into()), "127.0.0.1:1").await, StatusCode::BAD_REQUEST);
    assert_eq!(post(&r, bad(|v| v["id"] = "a".repeat(33).into()), "127.0.0.1:1").await, StatusCode::BAD_REQUEST);
    assert_eq!(post(&r, bad(|v| v["id"] = "".into()), "127.0.0.1:1").await, StatusCode::BAD_REQUEST);
    assert_eq!(post(&r, bad(|v| v["name"] = "n".repeat(65).into()), "127.0.0.1:1").await, StatusCode::BAD_REQUEST);
    assert_eq!(post(&r, bad(|v| v["map"] = "m".repeat(129).into()), "127.0.0.1:1").await, StatusCode::BAD_REQUEST);
    assert_eq!(post(&r, bad(|v| v["port"] = "7777".into()), "127.0.0.1:1").await, StatusCode::BAD_REQUEST);
    assert_eq!(post(&r, bad(|v| v["port"] = 70000.into()), "127.0.0.1:1").await, StatusCode::BAD_REQUEST);
    assert_eq!(post(&r, bad(|v| v["players"] = (-1).into()), "127.0.0.1:1").await, StatusCode::BAD_REQUEST);
    assert_eq!(post(&r, bad(|v| v["schema"] = 2.into()), "127.0.0.1:1").await, StatusCode::BAD_REQUEST);
    assert_eq!(post(&r, bad(|v| v["password"] = "no".into()), "127.0.0.1:1").await, StatusCode::BAD_REQUEST);
    // numeric net_version is accepted and normalised to a string
    assert_eq!(post(&r, bad(|v| v["net_version"] = 12345.into()), "127.0.0.1:1").await, StatusCode::NO_CONTENT);
    let (_, body) = get(&r, "/api/v1/servers").await;
    assert!(body.contains("\"net_version\": \"12345\""));
}

#[tokio::test]
async fn body_limit_413() {
    let r = router(App::new(cfg(None, "/nonexistent/x.json")));
    let big = with(FFA, |v| v["junk"] = "x".repeat(9000).into());
    assert_eq!(post(&r, big, "127.0.0.1:1").await, StatusCode::PAYLOAD_TOO_LARGE);
}

#[tokio::test]
async fn token_enforced() {
    let r = router(App::new(cfg(Some("s3cret"), "/nonexistent/x.json")));
    assert_eq!(post(&r, FFA.into(), "127.0.0.1:1").await, StatusCode::UNAUTHORIZED);
    assert_eq!(post(&r, with(FFA, |v| v["token"] = "wrong".into()), "127.0.0.1:1").await, StatusCode::UNAUTHORIZED);
    assert_eq!(post(&r, with(FFA, |v| v["token"] = "s3cret".into()), "127.0.0.1:1").await, StatusCode::NO_CONTENT);
    let (_, body) = get(&r, "/api/v1/servers").await;
    assert!(!body.contains("s3cret"));
}

#[test]
fn config_defaults_and_errors() {
    let c = Config::from_lookup(|k| (k == "VELOXMASTER_PUBLIC_ADDRESS").then(|| "1.2.3.4".into())).unwrap();
    assert_eq!(c.listen, "127.0.0.1:8790");
    assert_eq!(c.ttl, Duration::from_secs(90));
    assert_eq!(c.write_interval, Duration::from_secs(15));
    assert!(c.token.is_none());
    assert!(Config::from_lookup(|_| None).is_err());
    assert!(Config::from_lookup(|k| match k {
        "VELOXMASTER_PUBLIC_ADDRESS" => Some("1.2.3.4".into()),
        "VELOXMASTER_TTL_S" => Some("abc".into()),
        _ => None,
    })
    .is_err());
}

#[test]
fn golden_json_shape() {
    let t0 = Instant::now();
    let mut s = State::new(Duration::from_secs(90), "77.244.242.34".into());
    for (j, age) in [(DUEL, 12), (FFA, 3)] {
        let raw: RawHeartbeat = serde_json::from_str(j).unwrap();
        let (hb, _) = raw.validate().unwrap();
        s.upsert(hb, "127.0.0.1".parse().unwrap(), t0 + Duration::from_secs(20 - age));
    }
    let f = s.render(t0 + Duration::from_secs(20), 1_782_950_400);
    let got = serde_json::to_string_pretty(&f).unwrap() + "\n";
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/golden/servers.json");
    if std::env::var("UPDATE_GOLDEN").is_ok() {
        std::fs::write(path, &got).unwrap();
    }
    assert_eq!(got, std::fs::read_to_string(path).unwrap());
}

#[tokio::test]
async fn writer_writes_file_and_expires() {
    let dir = std::env::temp_dir().join(format!("vm-writer-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let out = dir.join("servers.json");
    let mut c = cfg(None, out.to_str().unwrap());
    c.ttl = Duration::from_secs(1);
    let app = App::new(c);
    tokio::spawn(app.clone().writer_loop());
    let r = router(app);
    assert_eq!(post(&r, FFA.into(), "127.0.0.1:1").await, StatusCode::NO_CONTENT);
    tokio::time::sleep(Duration::from_millis(100)).await;
    let v: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(&out).unwrap()).unwrap();
    assert_eq!(v["servers"].as_array().unwrap().len(), 1);
    tokio::time::sleep(Duration::from_millis(1300)).await;
    let v: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(&out).unwrap()).unwrap();
    assert_eq!(v["servers"].as_array().unwrap().len(), 0);
    std::fs::remove_dir_all(&dir).unwrap();
}
