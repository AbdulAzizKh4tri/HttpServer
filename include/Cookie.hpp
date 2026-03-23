#pragma once
#include <chrono>
#include <string>

struct Cookie {
  std::string name;
  std::string value;
  std::string path = "/";
  bool httpOnly = false;
  bool secure = false;
  std::string sameSite = "Lax";
  int maxAge = -1;
  std::chrono::system_clock::time_point expires =
      std::chrono::system_clock::time_point::max();
  std::string domain = "";
};
