#include "StaticMiddleware.hpp"

#include "AsyncFileReader.hpp"
#include "ErrorFactory.hpp"
#include "HttpResponse.hpp"
#include "HttpStreamResponse.hpp"
#include "MimeTypes.hpp"
#include "utils.hpp"

#include <filesystem>

StaticMiddleware::StaticMiddleware(ErrorFactory &errorFactory, StaticConfig config)
    : config_(config), errorFactory_(errorFactory) {
  canonical_root_ = std::filesystem::weakly_canonical(config_.root);
}

StaticMiddleware::StaticMiddleware(ErrorFactory &errorFactory, const std::string &root, const std::string &prefix)
    : errorFactory_(errorFactory) {
  config_.root = root;
  config_.prefix = prefix;
  canonical_root_ = std::filesystem::weakly_canonical(root);
}

std::string generateETag(const std::filesystem::directory_entry &entry) {
  auto mtime = entry.last_write_time().time_since_epoch().count();
  auto size = entry.file_size();
  return '"' + std::to_string(mtime) + '-' + std::to_string(size) + '"';
}

template <typename Res>
void addCacheHeaders(Res &response, const std::string &etag, const std::filesystem::file_time_type last_write,
                     const std::string &cacheControl) {
  response.headers.setCacheControl(cacheControl);
  if (cacheControl.contains("no-store"))
    return;

  response.headers.setHeaderLower("etag", etag);
  response.headers.setHeaderLower("last-modified", toHttpDate(std::chrono::file_clock::to_sys(last_write)));
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

  std::filesystem::path resolved = canonical_root_ / relative;

  if (not resolved.native().starts_with(canonical_root_.native())) {
    co_return buildErrorResponse(request, 403);
  }

  std::filesystem::directory_entry entry(resolved);
  if (!entry.exists())
    co_return co_await next();
  if (entry.is_directory()) {
    resolved /= "index.html";
    entry.assign(resolved); // refresh for the index.html
    if (!entry.exists())
      co_return co_await next();
  }

  auto fileSize = entry.file_size();

  std::string extension = resolved.extension().string(); // e.g. ".html"
  std::string mime = getOrDefault(MIME_TYPES, extension, "application/octet-stream");

  std::optional<AsyncFileReader> fileOpt = AsyncFileReader::open(resolved, fileSize);
  if (not fileOpt.has_value())
    co_return buildErrorResponse(request, 403);

  // Design decision: checking for 304 AFTER the open in case permissions change.
  auto cacheControl = getOrDefault(config_.mimeCacheControl, mime, config_.defaultCacheControl);
  auto lastWrite = entry.last_write_time();
  std::string eTag = "";

  if (not cacheControl.contains("no-store")) {
    eTag = generateETag(entry);

    auto ifNoneMatch = request.getHeaderLower("if-none-match");
    if (not ifNoneMatch.empty() && etagMatches(ifNoneMatch, eTag)) {
      HttpResponse response(304);
      addCacheHeaders(response, eTag, lastWrite, cacheControl);
      co_return response;
    }

    std::string ifModifiedSince = std::string(request.getHeaderLower("if-modified-since"));
    if (not ifModifiedSince.empty()) {
      auto dateOpt = parseHttpDate(ifModifiedSince);
      if (not dateOpt.has_value())
        co_return buildErrorResponse(request, 400);

      if (lastWrite <= std::chrono::file_clock::from_sys(*dateOpt)) {
        HttpResponse response(304);
        addCacheHeaders(response, eTag, lastWrite, cacheControl);
        co_return response;
      }
    }
  }

  AsyncFileReader &file = fileOpt.value();

  if (fileSize <= STATIC_STREAM_THRESHOLD_BYTES) {
    std::string body = co_await file.readAll();
    HttpResponse response(200, std::move(body));
    response.headers.setHeaderLower("content-type", mime);
    addCacheHeaders(response, eTag, lastWrite, cacheControl);
    if (method == "HEAD")
      response.stripBody();
    co_return response;
  } else {
    if (method == "HEAD") {
      HttpResponse response(200);
      response.headers.setHeaderLower("content-type", mime);
      response.headers.setHeaderLower("content-length", std::to_string(fileSize));
      addCacheHeaders(response, eTag, lastWrite, cacheControl);
      co_return response;
    }

    HttpStreamResponse response(200, [file = std::move(file)]() mutable -> Task<std::optional<std::string>> {
      co_return co_await file.readChunk(STATIC_STREAM_CHUNK_SIZE);
    });
    response.headers.setHeaderLower("content-type", mime);
    addCacheHeaders(response, eTag, lastWrite, cacheControl);
    co_return response;
  }
}

void StaticMiddleware::setRoot(const std::string &root) {
  config_.root = root;
  canonical_root_ = std::filesystem::weakly_canonical(root);
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
