#pragma once
#include "ErrorFactory.hpp"
#include "Router.hpp"

void registerRoutes(Router &router, const ErrorFactory &errorFactory);
