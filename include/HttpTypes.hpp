#pragma once

#include <functional>

#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpStreamResponse.hpp"
#include "Task.hpp"

using Response = std::variant<HttpResponse, HttpStreamResponse>;

using Handler = std::move_only_function<Task<Response>(const HttpRequest &)>;

using Next = std::function<Task<Response>()>;
using Middleware = std::function<Task<Response>(HttpRequest &, Next)>;
