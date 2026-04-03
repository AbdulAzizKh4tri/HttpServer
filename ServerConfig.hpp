#pragma once

namespace ServerConfig {
// ── Server identity ───────────────────────────────────────
static constexpr const char *SERVER_NAME = "Azooz";

// ── Timeouts (seconds) ────────────────────────────────────
static constexpr int INACTIVITY_TIMEOUT_S = 20;
static constexpr int FORMATION_TIMEOUT_S = 120;
static constexpr int GRACEFUL_SHUTDOWN_TIMEOUT_S = 20;

// ── Request limits ────────────────────────────────────────
static constexpr size_t MAX_HEADER_BYTES = 8 * 1024;
static constexpr size_t MAX_CONTENT_LENGTH = 1 * 1024 * 1024;

// ── Static File Serving ───────────────────────────────────
static constexpr size_t STATIC_STREAM_THRESHOLD_BYTES = 256 * 1024;
static constexpr size_t STATIC_STREAM_CHUNK_SIZE = 4096;

// ── File IO (IoUring) ─────────────────────────────────────
static constexpr int IO_URING_RING_SIZE = 64;

} // namespace ServerConfig
