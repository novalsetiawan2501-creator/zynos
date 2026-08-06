//! loadtest — HTTP/2 Load Testing Tool
//!
//! Tool untuk mensimulasikan traffic tinggi ke sebuah endpoint HTTP/2 (GET only).
//! Dibangun di atas Tokio (async runtime), hyper (HTTP client), dan rustls (TLS/ALPN
//! yang dibutuhkan untuk negosiasi protokol HTTP/2).
//!
//! Formula total target rate (sesuai spesifikasi):
//!     total_rate = rate * thread * connection * 2
//!
//! Setiap worker (kombinasi thread x connection) menembak dengan rate = `rate * 2`
//! request/detik, sehingga jumlah seluruh worker (thread * connection) dikali
//! rate-per-worker menghasilkan total_rate di atas.

use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

use clap::Parser;
use hyper::{Body, Client, Method, Request, Uri};
use hyper_rustls::HttpsConnectorBuilder;
use indicatif::{ProgressBar, ProgressStyle};
use tokio::signal;
use tokio::time::{interval, sleep};
use tokio_util::sync::CancellationToken;

/// Definisi argumen CLI
#[derive(Parser, Debug, Clone)]
#[command(
    name = "loadtest",
    version,
    about = "HTTP/2 Load Testing Tool (Rust + Tokio + hyper + rustls)"
)]
struct Args {
    /// Target endpoint (harus https://... karena HTTP/2 butuh ALPN via TLS)
    #[arg(long)]
    url: String,

    /// Durasi pengujian dalam detik
    #[arg(long, default_value_t = 10)]
    time: u64,

    /// Request per detik (basis) per worker sebelum dikali faktor
    #[arg(long, default_value_t = 10)]
    rate: u64,

    /// Jumlah OS thread (worker thread Tokio runtime)
    #[arg(long, default_value_t = 4)]
    thread: usize,

    /// Jumlah koneksi paralel per thread
    #[arg(long, default_value_t = 4)]
    connection: usize,
}

/// Statistik global, diakses dari banyak task secara concurrent lewat Atomic
/// sehingga tidak butuh Mutex (non-blocking, murah, aman dari data race).
struct Stats {
    sent: AtomicU64,
    success: AtomicU64,
    failed: AtomicU64,
    bytes: AtomicU64,
    latency_us_sum: AtomicU64,
}

impl Stats {
    fn new() -> Self {
        Self {
            sent: AtomicU64::new(0),
            success: AtomicU64::new(0),
            failed: AtomicU64::new(0),
            bytes: AtomicU64::new(0),
            latency_us_sum: AtomicU64::new(0),
        }
    }
}

fn main() {
    let args = Args::parse();

    // ----- Validasi input -----
    let uri: Uri = match args.url.parse() {
        Ok(u) => u,
        Err(e) => {
            eprintln!("[ERROR] URL tidak valid: {e}");
            std::process::exit(1);
        }
    };

    if uri.scheme_str() != Some("https") {
        eprintln!("[ERROR] HTTP/2 di tool ini memerlukan TLS (ALPN). Gunakan URL berskema https://");
        std::process::exit(1);
    }

    if args.thread == 0 || args.connection == 0 || args.rate == 0 || args.time == 0 {
        eprintln!("[ERROR] Parameter time, rate, thread, dan connection harus lebih besar dari 0");
        std::process::exit(1);
    }

    let total_target_rate = args.rate * (args.thread as u64) * (args.connection as u64) * 2;

    println!("=========================================================");
    println!(" Rust HTTP/2 Load Testing Tool");
    println!("=========================================================");
    println!(" Target       : {}", args.url);
    println!(" Durasi       : {} detik", args.time);
    println!(" Rate (basis) : {} req/s", args.rate);
    println!(" Thread       : {}", args.thread);
    println!(" Connection   : {}", args.connection);
    println!(" Total target rate (rate*thread*connection*2): {} req/s", total_target_rate);
    println!("=========================================================\n");

    // Bangun Tokio runtime manual (multi-thread) dengan jumlah worker thread
    // sesuai parameter --thread. Ini yang memberikan multithreading nyata,
    // bukan cuma banyak task dalam satu thread.
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .worker_threads(args.thread)
        .enable_all()
        .thread_name("loadtest-worker")
        .build()
        .expect("Gagal membangun Tokio runtime");

    runtime.block_on(run(args, uri, total_target_rate));
}

async fn run(args: Args, uri: Uri, total_target_rate: u64) {
    // ----- Koneksi HTTPS dengan ALPN diarahkan ke HTTP/2 -----
    let https = HttpsConnectorBuilder::new()
        .with_webpki_roots()
        .https_only()
        .enable_http2()
        .build();

    // hyper::Client sudah punya connection pool internal per-host.
    // http2_only(true) memberitahu client bahwa semua koneksi adalah HTTP/2,
    // konsisten dengan enable_http2() di atas.
    let client: Arc<Client<_, Body>> = Arc::new(Client::builder().http2_only(true).build(https));

    let stats = Arc::new(Stats::new());
    let shutdown = CancellationToken::new();

    // rate efektif per worker (thread x connection) supaya total keseluruhan
    // sama dengan total_target_rate = rate * thread * connection * 2
    let per_worker_rate = (args.rate as f64) * 2.0;
    let worker_count = args.thread * args.connection;

    // ----- Spawn semua worker (connection pool paralel) -----
    let mut handles = Vec::with_capacity(worker_count);
    for id in 0..worker_count {
        let client = client.clone();
        let uri = uri.clone();
        let stats = stats.clone();
        let shutdown = shutdown.clone();
        handles.push(tokio::spawn(worker_loop(id, client, uri, per_worker_rate, stats, shutdown)));
    }

    // ----- Progress bar + statistik real-time tiap 5 detik -----
    let pb = ProgressBar::new(args.time);
    pb.set_style(
        ProgressStyle::with_template("{msg}\n[{bar:40.cyan/blue}] {pos}/{len}s")
            .unwrap()
            .progress_chars("=>-"),
    );

    let start = Instant::now();
    let stats_task = {
        let stats = stats.clone();
        let shutdown = shutdown.clone();
        let pb = pb.clone();
        let total_time = args.time;
        tokio::spawn(async move {
            let mut ticker = interval(Duration::from_secs(5));
            loop {
                tokio::select! {
                    _ = ticker.tick() => {
                        let elapsed = start.elapsed().as_secs();
                        print_stats(&stats, elapsed, total_time, &pb);
                        if elapsed >= total_time {
                            break;
                        }
                    }
                    _ = shutdown.cancelled() => break,
                }
            }
        })
    };

    // ----- Tunggu durasi selesai ATAU sinyal Ctrl+C (graceful shutdown) -----
    tokio::select! {
        _ = sleep(Duration::from_secs(args.time)) => {
            println!("\n[INFO] Durasi pengujian selesai.");
        }
        _ = signal::ctrl_c() => {
            println!("\n[INFO] Ctrl+C diterima, memulai graceful shutdown...");
        }
    }

    // Beri sinyal berhenti ke semua worker, lalu beri grace period singkat
    // agar request yang sedang in-flight sempat selesai / dibatalkan dengan rapi.
    shutdown.cancel();
    sleep(Duration::from_millis(500)).await;

    for h in handles {
        h.abort(); // pastikan tidak ada task menggantung -> mencegah leak
    }
    let _ = stats_task.await;

    pb.finish_and_clear();
    print_final_report(&stats, start.elapsed(), total_target_rate);
}

/// Loop worker: satu worker merepresentasikan satu "slot" thread x connection.
/// Mengirim request sesuai rate yang sudah dihitung, non-blocking (tiap request
/// di-spawn sebagai task terpisah agar timer rate-limit tidak ikut menunggu response).
async fn worker_loop(
    _id: usize,
    client: Arc<Client<hyper_rustls::HttpsConnector<hyper::client::HttpConnector>, Body>>,
    uri: Uri,
    rate_per_sec: f64,
    stats: Arc<Stats>,
    shutdown: CancellationToken,
) {
    let period = Duration::from_secs_f64(1.0 / rate_per_sec.max(0.001));
    let mut ticker = interval(period);

    loop {
        tokio::select! {
            _ = ticker.tick() => {
                let client = client.clone();
                let uri = uri.clone();
                let stats = stats.clone();
                // Non-blocking: request berjalan di task sendiri
                tokio::spawn(async move {
                    send_request(client, uri, stats).await;
                });
            }
            _ = shutdown.cancelled() => break,
        }
    }
}

/// Kirim satu request GET dan catat hasilnya ke statistik.
async fn send_request(
    client: Arc<Client<hyper_rustls::HttpsConnector<hyper::client::HttpConnector>, Body>>,
    uri: Uri,
    stats: Arc<Stats>,
) {
    stats.sent.fetch_add(1, Ordering::Relaxed);

    let req = match Request::builder()
        .method(Method::GET)
        .uri(uri)
        .header("User-Agent", "rust-loadtest/0.1")
        .header("Accept", "*/*")
        .body(Body::empty())
    {
        Ok(r) => r,
        Err(_) => {
            stats.failed.fetch_add(1, Ordering::Relaxed);
            return;
        }
    };

    let started = Instant::now();

    match client.request(req).await {
        Ok(resp) => {
            let status = resp.status();

            // Konsumsi body sampai habis supaya koneksi/stream HTTP/2
            // dikembalikan dengan bersih ke connection pool (mencegah leak).
            if let Ok(body) = hyper::body::to_bytes(resp.into_body()).await {
                stats.bytes.fetch_add(body.len() as u64, Ordering::Relaxed);
            }

            let elapsed_us = started.elapsed().as_micros() as u64;
            stats.latency_us_sum.fetch_add(elapsed_us, Ordering::Relaxed);

            if status.is_success() || status.is_redirection() {
                stats.success.fetch_add(1, Ordering::Relaxed);
            } else {
                stats.failed.fetch_add(1, Ordering::Relaxed);
            }
        }
        Err(_e) => {
            // Error jaringan / TLS / protokol dicatat sebagai failed,
            // tool tetap lanjut jalan (tidak panic/crash).
            stats.failed.fetch_add(1, Ordering::Relaxed);
        }
    }
}

/// Cetak statistik berjalan (dipanggil tiap 5 detik).
fn print_stats(stats: &Stats, elapsed: u64, total: u64, pb: &ProgressBar) {
    let sent = stats.sent.load(Ordering::Relaxed);
    let success = stats.success.load(Ordering::Relaxed);
    let failed = stats.failed.load(Ordering::Relaxed);
    let bytes = stats.bytes.load(Ordering::Relaxed);
    let lat_sum = stats.latency_us_sum.load(Ordering::Relaxed);

    let completed = success + failed;
    let avg_latency_ms = if completed > 0 {
        (lat_sum as f64 / completed as f64) / 1000.0
    } else {
        0.0
    };
    let rps = if elapsed > 0 { sent as f64 / elapsed as f64 } else { 0.0 };

    pb.set_position(elapsed.min(total));
    pb.set_message(format!(
        "[t={:>3}s] sent={:<7} ok={:<7} fail={:<6} rps={:>8.1} avg_latency={:>7.2}ms data={:.2}MB",
        elapsed,
        sent,
        success,
        failed,
        rps,
        avg_latency_ms,
        bytes as f64 / 1_048_576.0
    ));
    pb.tick();
}

/// Cetak ringkasan akhir setelah pengujian selesai / dihentikan.
fn print_final_report(stats: &Stats, total_elapsed: Duration, total_target_rate: u64) {
    let sent = stats.sent.load(Ordering::Relaxed);
    let success = stats.success.load(Ordering::Relaxed);
    let failed = stats.failed.load(Ordering::Relaxed);
    let bytes = stats.bytes.load(Ordering::Relaxed);
    let lat_sum = stats.latency_us_sum.load(Ordering::Relaxed);

    let completed = success + failed;
    let avg_latency_ms = if completed > 0 {
        (lat_sum as f64 / completed as f64) / 1000.0
    } else {
        0.0
    };
    let secs = total_elapsed.as_secs_f64().max(0.001);
    let actual_rps = sent as f64 / secs;
    let success_rate = if sent > 0 { (success as f64 / sent as f64) * 100.0 } else { 0.0 };

    println!("\n=========================================================");
    println!(" HASIL AKHIR LOAD TEST");
    println!("=========================================================");
    println!(" Durasi aktual        : {:.2} detik", secs);
    println!(" Total request dikirim: {}", sent);
    println!(" Sukses               : {} ({:.2}%)", success, success_rate);
    println!(" Gagal                : {}", failed);
    println!(" Rata-rata latency    : {:.2} ms", avg_latency_ms);
    println!(" Total data diterima  : {:.2} MB", bytes as f64 / 1_048_576.0);
    println!(" Throughput aktual    : {:.1} req/s", actual_rps);
    println!(" Target total rate    : {} req/s", total_target_rate);
    println!("=========================================================");
}
