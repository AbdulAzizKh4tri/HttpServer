#include <coroutine>
#include <exception>
#include <optional>

template <typename T> class Generator {
public:
  struct promise_type {
    T value;
    std::exception_ptr ex;

    Generator get_return_object() {
      return Generator{Handle::from_promise(*this)};
    }
    std::suspend_always initial_suspend() { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    std::suspend_always yield_value(const T &val) {
      value = val;
      return {};
    }
    void return_void() {}
    void unhandled_exception() { ex = std::current_exception(); }
  };

  using Handle = std::coroutine_handle<promise_type>;

  Generator() : handle_(nullptr) {}

  explicit Generator(Handle h) : handle_(h) {}

  Generator(Generator &&other) noexcept : handle_(other.handle_) {
    other.handle_ = {};
  }
  Generator &operator=(Generator &&other) noexcept {
    if (this == &other)
      return *this;
    if (handle_)
      handle_.destroy();
    handle_ = other.handle_;
    other.handle_ = {};
    return *this;
  }

  Generator(const Generator &) = delete;
  Generator &operator=(const Generator &) = delete;

  ~Generator() {
    if (handle_)
      handle_.destroy();
  }

  // returns nullopt when done
  std::optional<T> next() {
    if (!handle_ || handle_.done())
      return std::nullopt;
    handle_.resume();
    if (handle_.promise().ex)
      std::rethrow_exception(handle_.promise().ex);
    if (handle_.done())
      return std::nullopt;
    return std::move(handle_.promise().value);
  }

  bool done() const { return handle_.done(); }

private:
  Handle handle_;
};
