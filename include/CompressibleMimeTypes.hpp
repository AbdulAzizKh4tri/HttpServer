#pragma once

#include <string>
#include <unordered_set>

static std::unordered_set<std::string> compressibleMimeTypes = {
    "application/javascript",
    "text/css",
    "text/html",
    "text/plain",
};
