#pragma once

namespace ServerConfig {
// ── Server identity ───────────────────────────────────────
static constexpr const char *SERVER_NAME = "Azooz";
static constexpr std::string SERVER_LINE = "server: " + std::string(SERVER_NAME) + "\r\n";

// ── Timeouts (seconds) ────────────────────────────────────
static constexpr int INACTIVITY_TIMEOUT_S = 20;
static constexpr int FORMATION_TIMEOUT_S = 120;
static constexpr int GRACEFUL_SHUTDOWN_TIMEOUT_S = 20;

// ── Request limits ────────────────────────────────────────
static constexpr size_t MAX_HEADER_BYTES = 8 * 1024;
static constexpr size_t MAX_CONTENT_LENGTH = 1 * 1024 * 1024;

// ── Static File Serving ───────────────────────────────────
static constexpr size_t STATIC_STREAM_THRESHOLD_BYTES = 1 * 1024;
static constexpr size_t STATIC_STREAM_CHUNK_SIZE = 4096;
static constexpr std::string STATIC_CACHE_DIR = "./.server_cache";

// ── File IO (IoUring) ─────────────────────────────────────
static constexpr int IO_URING_RING_SIZE = 64;

// ── Compression ───────────────────────────────────────────
static constexpr int COMPRESS_MIN_BYTES = 256;
static constexpr int STATIC_COMPRESS_LEVEL = 9;

} // namespace ServerConfig
