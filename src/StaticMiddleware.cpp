#include "StaticMiddleware.hpp"

#include <filesystem>

#include "AsyncFileReader.hpp"
#include "AsyncFileWriter.hpp"
#include "CompressibleMimeTypes.hpp"
#include "ErrorFactory.hpp"
#include "GzipCompressor.hpp"
#include "HttpResponse.hpp"
#include "HttpStreamResponse.hpp"
#include "MimeTypes.hpp"
#include "ServerConfig.hpp"
#include "utils.hpp"

using Compressor = std::unique_ptr<ICompressor>;

inline Compressor getCompressor(std::string_view encoding) {
  if (encoding == "identity")
    return nullptr;
  if (encoding == "gzip" || encoding == "*")
    return std::make_unique<GzipCompressor>(ServerConfig::STATIC_COMPRESS_LEVEL);
  return nullptr;
}

StaticMiddleware::StaticMiddleware(ErrorFactory &errorFactory, StaticConfig config)
    : config_(config), errorFactory_(errorFactory) {
  canonicalRoot_ = std::filesystem::weakly_canonical(config_.root);
  compressedRoot_ = std::filesystem::weakly_canonical(ServerConfig::STATIC_CACHE_DIR);
}

StaticMiddleware::StaticMiddleware(ErrorFactory &errorFactory, const std::string &root, const std::string &prefix)
    : errorFactory_(errorFactory) {
  config_.root = root;
  config_.prefix = getNormalizedPath(prefix);
  canonicalRoot_ = std::filesystem::weakly_canonical(root);
  compressedRoot_ = std::filesystem::weakly_canonical(ServerConfig::STATIC_CACHE_DIR);
}

std::string generateETag(const std::filesystem::directory_entry &entry, const std::string &compression = "") {
  auto mtime = entry.last_write_time().time_since_epoch().count();
  auto size = entry.file_size();
  return '"' + std::to_string(mtime) + '-' + std::to_string(size) + '-' + compression + '"';
}

template <typename Res>
void addCacheHeaders(Res &response, const std::string &etag,
                     const std::chrono::sys_time<std::chrono::file_clock::duration> lastWrite,
                     const std::string &cacheControl) {
  response.headers.setCacheControl(cacheControl);
  if (cacheControl.contains("no-store"))
    return;

  response.headers.setHeaderLower("etag", etag);
  response.headers.setHeaderLower("last-modified", toHttpDate(lastWrite));
}

Task<Response> StaticMiddleware::operator()(const HttpRequest &request, Next next) {
  const std::string &method = request.getMethod();

  if (not(method == "GET" || method == "HEAD"))
    co_return co_await next();

  const std::string &path = request.getPath();

  if (not(path == config_.prefix || path.starts_with(config_.prefix + "/")))
    co_return co_await next();

  std::string relative = path.substr(config_.prefix.size());
  if (!relative.empty() && relative.front() == '/')
    relative.erase(0, 1);

  std::filesystem::path resolved = canonicalRoot_ / relative;

  if (not resolved.native().starts_with(canonicalRoot_.native())) {
    co_return buildErrorResponse(request, 403);
  }

  std::filesystem::directory_entry entry(resolved);
  if (!entry.exists())
    co_return co_await next();
  if (entry.is_directory()) {
    resolved /= "index.html";
    relative = relative.empty() ? "index.html" : relative + "/index.html";
    entry.assign(resolved);
    if (!entry.exists())
      co_return co_await next();
  }

  auto fileSize = entry.file_size();

  std::string extension = resolved.extension().string(); // e.g. ".html"
  std::string mime = getOrDefault(MIME_TYPES, extension, "application/octet-stream");

  bool isCompressible =
      std::find(compressibleMimeTypes.begin(), compressibleMimeTypes.end(), mime) != compressibleMimeTypes.end();
  isCompressible = isCompressible && fileSize >= ServerConfig::COMPRESS_MIN_BYTES;

  std::string compressedExtension = "";
  std::string compressedEncoding = "";
  Compressor compressor;
  if (isCompressible) {
    const auto acceptEncodingHeader = toLowerCase(request.getHeaderLower("accept-encoding"));
    std::vector<std::pair<std::string, float>> encodingPrefs;
    std::vector<std::string> excluded;

    parseQValues(acceptEncodingHeader, encodingPrefs, excluded);

    std::sort(encodingPrefs.begin(), encodingPrefs.end(),
              [](const auto &a, const auto &b) { return a.second > b.second; });

    bool identityAllowed = true;

    if (std::find(excluded.begin(), excluded.end(), "identity") != excluded.end()) {
      identityAllowed = false;
    }

    for (const auto &encoding : encodingPrefs) {
      compressor = getCompressor(encoding.first);
      if (compressor || encoding.first == "identity") {
        break;
      }
    }

    if (not compressor && not identityAllowed) {
      co_return buildErrorResponse(request, 406);
    }

    if (compressor) {
      compressedEncoding = compressor->getEncoding();
      compressedExtension = "." + compressedEncoding;
      auto compressedRelative = relative + compressedExtension;
      std::filesystem::path compressedPath = compressedRoot_ / compressedRelative;

      std::filesystem::directory_entry compressedEntry(compressedPath);
      bool originalFileModified = false;
      if (compressedEntry.exists()) {
        originalFileModified = entry.last_write_time() > compressedEntry.last_write_time();
        if (not originalFileModified) {
          SPDLOG_DEBUG("NOT MODIFIED");
          entry.assign(compressedEntry);
          resolved = compressedPath;
          fileSize = entry.file_size();
        }
      }
      if (not compressedEntry.exists() || originalFileModified) {
        std::optional<AsyncFileReader> srcOpt = AsyncFileReader::open(resolved, fileSize);
        if (!srcOpt)
          co_return buildErrorResponse(request, 500);

        std::string raw = co_await srcOpt->readAll();
        std::string compressed = compressor->compress(raw);

        std::filesystem::create_directories(compressedPath.parent_path());

        static std::atomic<uint64_t> threadIdCounter = 0;
        thread_local uint64_t myThreadId = threadIdCounter++;
        thread_local uint64_t tmpCounter = 0;
        std::filesystem::path tmpPath =
            compressedPath.string() + ".tmp." + std::to_string(myThreadId) + "." + std::to_string(tmpCounter++);

        std::optional<AsyncFileWriter> writerOpt = AsyncFileWriter::open(tmpPath);
        if (!writerOpt)
          co_return buildErrorResponse(request, 500);

        bool ok = co_await writerOpt->writeAll(compressed);
        if (!ok) {
          std::filesystem::remove(tmpPath);
          co_return buildErrorResponse(request, 500);
        }

        std::error_code ec;
        std::filesystem::rename(tmpPath, compressedPath, ec);
        if (ec) {
          std::filesystem::remove(tmpPath);
          co_return buildErrorResponse(request, 500);
        }

        entry.assign(compressedPath);
        resolved = compressedPath;
        fileSize = compressed.size();
      }
    }
  }

  SPDLOG_DEBUG("Serving {} ({})", resolved.native(), fileSize);

  std::optional<AsyncFileReader> fileOpt = AsyncFileReader::open(resolved, fileSize);
  if (not fileOpt.has_value())
    co_return buildErrorResponse(request, 403);

  // Design decision: checking for 304 AFTER the open in case permissions change.
  auto cacheControl = getOrDefault(config_.mimeCacheControl, mime, config_.defaultCacheControl);
  auto lastWrite = std::chrono::file_clock::to_sys(entry.last_write_time());
  auto lastWriteSeconds = std::chrono::time_point_cast<std::chrono::seconds>(lastWrite);
  std::string eTag = "";

  if (not cacheControl.contains("no-store")) {
    eTag = generateETag(entry, compressedEncoding);

    auto ifNoneMatch = request.getHeaderLower("if-none-match");
    if (not ifNoneMatch.empty() && etagMatches(ifNoneMatch, eTag)) {
      HttpResponse response(304);
      addCacheHeaders(response, eTag, lastWrite, cacheControl);
      response.headers.addHeaderLower("vary", "accept-encoding");
      co_return response;
    }

    std::string ifModifiedSince = std::string(request.getHeaderLower("if-modified-since"));
    SPDLOG_DEBUG(toHttpDate(lastWrite));
    SPDLOG_DEBUG(ifModifiedSince);
    if (not ifModifiedSince.empty()) {
      auto dateOpt = parseHttpDate(ifModifiedSince);
      if (not dateOpt.has_value())
        co_return buildErrorResponse(request, 400);

      if (lastWriteSeconds <= *dateOpt) {
        HttpResponse response(304);
        addCacheHeaders(response, eTag, lastWrite, cacheControl);
        response.headers.addHeaderLower("vary", "accept-encoding");
        co_return response;
      }
    }
  }

  AsyncFileReader &file = fileOpt.value();

  if (fileSize <= ServerConfig::STATIC_STREAM_THRESHOLD_BYTES) {
    std::string body = co_await file.readAll();
    HttpResponse response(200, mime, std::move(body));
    if (compressor)
      response.headers.setHeaderLower("content-encoding", compressedEncoding);
    response.headers.addHeaderLower("vary", "accept-encoding");
    addCacheHeaders(response, eTag, lastWrite, cacheControl);
    if (method == "HEAD")
      response.stripBody();
    co_return response;
  } else {
    if (method == "HEAD") {
      HttpResponse response(200);
      if (compressor)
        response.headers.setHeaderLower("content-encoding", compressedEncoding);
      response.headers.addHeaderLower("vary", "accept-encoding");
      response.headers.setHeaderLower("content-type", mime);
      response.headers.setHeaderLower("content-length", std::to_string(fileSize));
      addCacheHeaders(response, eTag, lastWrite, cacheControl);
      co_return response;
    }

    HttpStreamResponse response(200, [file = std::move(file)]() mutable -> Task<std::optional<std::string>> {
      co_return co_await file.readChunk(ServerConfig::STATIC_STREAM_CHUNK_SIZE);
    });
    if (compressor)
      response.headers.setHeaderLower("content-encoding", compressedEncoding);
    response.headers.addHeaderLower("vary", "accept-encoding");
    response.headers.setHeaderLower("content-type", mime);
    addCacheHeaders(response, eTag, lastWrite, cacheControl);
    co_return response;
  }
}

void StaticMiddleware::setRoot(const std::string &root) {
  config_.root = root;
  canonicalRoot_ = std::filesystem::weakly_canonical(root);
}

void StaticMiddleware::setPrefix(const std::string &prefix) { config_.prefix = prefix; }

void StaticMiddleware::setMimeCacheControl(const std::string &mimeType, const std::string &cacheControlHeader) {
  config_.mimeCacheControl[mimeType] = cacheControlHeader;
};

void StaticMiddleware::setDefaultCacheControl(const std::string &cacheControlHeader) {
  config_.defaultCacheControl = cacheControlHeader;
}

HttpResponse StaticMiddleware::buildErrorResponse(const HttpRequest &request, const int statusCode,
                                                  const std::string &message) const {
  HttpResponse response = errorFactory_.build(request, statusCode, message);
  if (request.getMethod() == "HEAD")
    response.stripBody();
  return response;
}
