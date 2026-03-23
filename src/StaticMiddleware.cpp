#include "StaticMiddleware.hpp"

#include "AsyncFileReader.hpp"
#include "ErrorFactory.hpp"
#include "HttpResponse.hpp"
#include "HttpStreamResponse.hpp"
#include "MimeTypes.hpp"
#include "utils.hpp"

#include <filesystem>

StaticMiddleware::StaticMiddleware(const std::string root,
                                   const std::string prefix,
                                   ErrorFactory &errorFactory)
    : root_(root), prefix_(prefix), errorFactory_(errorFactory) {
  canonical_root_ = std::filesystem::weakly_canonical(root_);
}

Task<Response> StaticMiddleware::operator()(const HttpRequest &request,
                                            Next next) {
  const std::string &method = request.getMethod();

  if (not(method == "GET" || method == "HEAD"))
    co_return co_await next();

  const std::string &path = request.getPath();

  if (not path.starts_with(prefix_))
    co_return co_await next();

  std::string relative = path.substr(prefix_.size());
  if (!relative.empty() && relative.front() == '/')
    relative.erase(0, 1);

  std::filesystem::path resolved = std::filesystem::weakly_canonical(
      std::filesystem::path(root_) / relative);

  if (not resolved.native().starts_with(canonical_root_.native())) {
    co_return buildErrorResponse(request, 403);
  }

  if (std::filesystem::is_directory(resolved))
    resolved /= "index.html";

  if (not std::filesystem::exists(resolved))
    co_return co_await next();

  auto fileSize = std::filesystem::file_size(resolved);
  std::string ext = resolved.extension().string(); // e.g. ".html"
  std::string mime = getOrDefault(MIME_TYPES, ext, "application/octet-stream");

  std::optional<AsyncFileReader> fileOpt = AsyncFileReader::open(resolved);
  if (not fileOpt.has_value())
    co_return buildErrorResponse(request, 403);
  AsyncFileReader &file = fileOpt.value();

  if (fileSize <= STATIC_STREAM_THRESHOLD_BYTES) {
    std::string body = co_await file.readAll();
    HttpResponse response(200, body);
    response.setHeader("Content-Type", mime);
    if (method == "HEAD")
      response.stripBody();
    co_return response;
  } else {
    if (method == "HEAD") {
      HttpResponse response(200);
      response.setHeader("Content-Type", mime);
      response.setHeader("Content-Length", std::to_string(fileSize));
      co_return response;
    }

    HttpStreamResponse response(
        200,
        [file = std::move(file)]() mutable -> Task<std::optional<std::string>> {
          co_return co_await file.readChunk(STATIC_STREAM_CHUNK_SIZE);
        });
    response.setHeader("Content-Type", mime);
    co_return response;
  }
}

HttpResponse
StaticMiddleware::buildErrorResponse(const HttpRequest &request,
                                     const int statusCode,
                                     const std::string &message) const {
  HttpResponse response = errorFactory_.build(request, statusCode, message);
  if (request.getMethod() == "HEAD")
    response.stripBody();
  return response;
}
