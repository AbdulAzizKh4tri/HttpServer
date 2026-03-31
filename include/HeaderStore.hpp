#pragma once

#include <vector>

#include "utils.hpp"

class HeaderStore {
public:
  HeaderStore() { headers_.reserve(4); }

  std::string getHeader(const std::string &name) const { return getLastOrDefault(headers_, toLowerCase(name), ""); }

  std::vector<std::string> getHeaders(const std::string &name) const {
    return getAllValues(headers_, toLowerCase(name));
  }

  std::vector<std::pair<std::string, std::string>> &getAllHeaders() { return headers_; }
  const std::vector<std::pair<std::string, std::string>> &getAllHeaders() const { return headers_; }

  void setHeader(const std::string &name, const std::string &value) {
    std::string lowerKey = toLowerCase(name);
    std::erase_if(headers_, [&lowerKey](const auto &p) { return p.first == lowerKey; });
    headers_.emplace_back(lowerKey, value);
  }

  void setHeaderLower(const std::string_view &lowercaseKey, const std::string &value) {
    std::erase_if(headers_, [&lowercaseKey](const auto &p) { return p.first == lowercaseKey; });
    headers_.emplace_back(lowercaseKey, value);
  }

  void addHeader(const std::string &name, const std::string &value) {
    auto key = toLowerCase(name);
    if (std::ranges::contains(singletonHeaders_, key)) {
      if (std::find_if(headers_.begin(), headers_.end(), [&key](const auto &p) { return p.first == key; }) ==
          headers_.end())
        headers_.emplace_back(key, value);
      return;
    }
    headers_.emplace_back(key, value);
  }

  void addHeaderLower(const std::string_view &lowercaseKey, const std::string &value) {
    if (std::ranges::contains(singletonHeaders_, lowercaseKey)) {
      if (std::find_if(headers_.begin(), headers_.end(),
                       [&lowercaseKey](const auto &p) { return p.first == lowercaseKey; }) == headers_.end())
        headers_.emplace_back(lowercaseKey, value);
      return;
    }
    headers_.emplace_back(lowercaseKey, value);
  }

  void removeHeader(const std::string &name) {
    auto key = toLowerCase(name);
    std::erase_if(headers_, [&key](const auto &p) { return p.first == key; });
  }

  std::vector<std::pair<std::string, std::string>> &getHeaders() { return headers_; }

private:
  std::vector<std::pair<std::string, std::string>> headers_;

  static constexpr std::array singletonHeaders_ = {
      std::string_view("content-length"), std::string_view("content-type"), std::string_view("transfer-encoding"),
      std::string_view("date"),           std::string_view("server"),       std::string_view("location"),
  };
};
