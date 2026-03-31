#pragma once

#include <chrono>
#include <vector>

#include "Cookie.hpp"
#include "utils.hpp"

class CookieStore {
public:
  void setCookie(Cookie cookie) { cookies_.push_back(cookie); }

  void unsetCookie(const std::string &name) {
    std::erase_if(cookies_, [&name](const auto &p) { return p.name == name; });
  }

  void deleteCookie(const std::string &name, const std::string &path = "/") {
    cookies_.emplace_back(name, "", path, 0, std::chrono::system_clock::from_time_t(0));
  }

  std::vector<Cookie> getAllCookies() const { return cookies_; }

  std::optional<Cookie> getCookie(const std::string &name) const {
    for (const auto &cookie : cookies_)
      if (cookie.name == name)
        return cookie;
    return std::nullopt;
  }

  template <typename WriteFn> void serializeUsing(WriteFn &&write) const {
    for (const auto &cookie : cookies_) {
      write("set-cookie: ");
      write(cookie.name);
      write("=");
      write(cookie.value);
      write("; Path=");
      write(cookie.path);
      write("; SameSite=");
      write(cookie.sameSite);

      if (cookie.httpOnly)
        write("; HttpOnly");
      if (cookie.secure)
        write("; Secure");

      if (cookie.domain != "") {
        write("; Domain=");
        write(cookie.domain);
      }

      if (cookie.expires != std::chrono::system_clock::time_point::max()) {
        write("; Expires=");
        write(toHttpDate(cookie.expires));
      }
      if (cookie.maxAge > -1) {
        write("; Max-Age=");
        write(std::to_string(cookie.maxAge));
      }
      write("\r\n");
    }
  }

  size_t getSerializedSize() const {
    size_t total = 0;
    for (const auto &cookie : cookies_) {
      total += strlen("Set-Cookie: ");

      total += cookie.name.size() + strlen("1") + cookie.value.size() + strlen("; Path=") + cookie.path.size() +
               strlen("; SameSite=") + cookie.sameSite.size();

      if (cookie.httpOnly)
        total += strlen("; HttpOnly");
      if (cookie.secure)
        total += strlen("; Secure");

      if (cookie.domain != "")
        total += strlen("; Domain=") + cookie.domain.size();

      if (cookie.expires != std::chrono::system_clock::time_point::max())
        total += strlen("; Expires=") + toHttpDate(cookie.expires).size();
      if (cookie.maxAge > -1) {
        total += strlen("; Max-Age=") + digit_count(cookie.maxAge);
      }

      total += strlen("\r\n");
    }
    return total;
  }

private:
  std::vector<Cookie> cookies_;
};
