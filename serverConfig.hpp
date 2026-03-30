#pragma once

// ── Server identity ───────────────────────────────────────
#define SERVER_NAME "Azooz"

// ── Concurrency ───────────────────────────────────────────
#define THREAD_COUNT 6

// ── Timeouts (seconds) ────────────────────────────────────
#define INACTIVITY_TIMEOUT_S 20
#define FORMATION_TIMEOUT_S 120
#define GRACEFUL_SHUTDOWN_TIMEOUT_S 20

// ── Request limits ────────────────────────────────────────
#define MAX_HEADER_BYTES (8 * 1024)
#define MAX_BODY_BYTES (1 * 1024 * 1024)

// ── Static File Serving ───────────────────────────────────
#define STATIC_STREAM_THRESHOLD_BYTES (256 * 1024)
#define STATIC_STREAM_CHUNK_SIZE 4096

// ── File IO (IoUring)──────────────────────────────────────
#define IO_URING_RING_SIZE 64
