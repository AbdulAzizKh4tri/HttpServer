#pragma once

#include <functional>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"

using Next = std::function<HttpResponse()>;
using Middleware = std::function<HttpResponse(HttpRequest &, Next)>;
