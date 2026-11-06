use veloxmaster::{router, App, Config};

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt().with_ansi(false).with_env_filter(tracing_subscriber::EnvFilter::from_default_env().add_directive("info".parse().unwrap())).init();
    let cfg = match Config::from_lookup(|k| std::env::var(k).ok()) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("veloxmaster: {e}");
            std::process::exit(2);
        }
    };
    let app = App::new(cfg.clone());
    if let Err(e) = app.flush() {
        eprintln!("veloxmaster: cannot write {}: {e}", cfg.output.display());
        std::process::exit(1);
    }
    let listener = match tokio::net::TcpListener::bind(&cfg.listen).await {
        Ok(l) => l,
        Err(e) => {
            eprintln!("veloxmaster: cannot bind {}: {e}", cfg.listen);
            std::process::exit(1);
        }
    };
    tracing::info!("listening on {}, writing {}", cfg.listen, cfg.output.display());
    tokio::spawn(app.clone().writer_loop());
    let shutdown = async {
        let mut term = tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate()).expect("sigterm");
        tokio::select! {
            _ = tokio::signal::ctrl_c() => {}
            _ = term.recv() => {}
        }
    };
    axum::serve(listener, router(app).into_make_service_with_connect_info::<std::net::SocketAddr>())
        .with_graceful_shutdown(shutdown)
        .await
        .expect("serve");
}
