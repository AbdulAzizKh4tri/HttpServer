#pragma once

namespace ServerConfig {
// ── Server identity ───────────────────────────────────────
static const std::string SERVER_NAME = "Azooz /v2.7.01";
static const std::string SERVER_LINE = "server: " + SERVER_NAME + "\r\n";

// ── Timeouts (seconds) ────────────────────────────────────
static constexpr int INACTIVITY_TIMEOUT_S = 20;
static constexpr int FORMATION_TIMEOUT_S = 120;
static constexpr int GRACEFUL_SHUTDOWN_TIMEOUT_S = 20;
static constexpr int EPOLL_WAIT_TIMEOUT = 1;

// ── Request/Response limits ────────────────────────────────────────
static constexpr size_t MAX_HEADER_BYTES = 8 * 1024;
static constexpr size_t MAX_CONTENT_LENGTH = 1 * 1024 * 1024;
static constexpr size_t MAX_WRITE_BUFFER_BYTES = 10 * 1024 * 1024;
static constexpr size_t MAX_TE_CHUNK_LENGTH = 1 * 1024 * 1024;
static constexpr size_t MAX_TE_LENGTH = 10 * 1024 * 1024;

// ── Multipart ────────────────────────────────────────────────
static constexpr size_t MULTIPART_BUFFER_SIZE = 1024;
static constexpr size_t MAX_MULTIPART_CONTENT_LENGTH = 50 * 1024 * 1024;
static constexpr size_t MAX_MULTIPART_TE_LENGTH = 50 * 1024 * 1024;
static constexpr size_t MAX_MULTIPART_FIELD_SIZE = 64 * 1024;
static constexpr size_t MAX_MULTIPART_FILE_SIZE = 20 * 1024 * 1024;
static constexpr size_t MAX_MULTIPART_PARTS = 256;
static constexpr size_t MAX_MULTIPART_BOUNDARY_LENGTH = 64;

// ── Static File Serving ───────────────────────────────────
static constexpr size_t STATIC_STREAM_THRESHOLD_BYTES = 1 * 1024;
static constexpr size_t STATIC_STREAM_CHUNK_SIZE = 4096;
static constexpr std::string STATIC_CACHE_DIR = "./.server_cache";

// ── File IO (IoUring) ─────────────────────────────────────
static constexpr int IO_URING_RING_SIZE = 64;

// ── Compression ───────────────────────────────────────────
static constexpr int COMPRESS_MIN_BYTES = 256;
static constexpr int STATIC_COMPRESS_LEVEL = 9;

// ── Connection limits ─────────────────────────────────────
static constexpr int CONNECTION_LIMIT = 100000;

} // namespace ServerConfig
