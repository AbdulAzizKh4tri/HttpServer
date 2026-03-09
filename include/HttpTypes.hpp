#pragma once

#include <functional>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpStreamResponse.hpp"

using Response = std::variant<HttpResponse, HttpStreamResponse>;

using Handler = std::function<Response(const HttpRequest &)>;

using Next = std::function<Response()>;
using Middleware = std::function<Response(HttpRequest &, Next)>;
