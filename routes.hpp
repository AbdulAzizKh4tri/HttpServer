#pragma once
#include "ErrorFactory.hpp"
#include "Router.hpp"
#include "ThreadPool.hpp"

void registerRoutes(Router &router, const ErrorFactory &errorFactory, ThreadPool *threadPool);
