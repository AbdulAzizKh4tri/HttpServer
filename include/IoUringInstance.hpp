#pragma once

#include <cstdint>
#include <liburing.h>

#include "ServerConfig.hpp"

class IoUringInstance {
public:
  IoUringInstance(int depth = ServerConfig::IO_URING_RING_SIZE) {
    if (io_uring_queue_init(depth, &ring_, 0) < 0)
      throw std::runtime_error("io_uring_queue_init failed");
  }

  ~IoUringInstance() { io_uring_queue_exit(&ring_); }

  IoUringInstance(const IoUringInstance &) = delete;
  IoUringInstance &operator=(const IoUringInstance &) = delete;
  IoUringInstance(IoUringInstance &&) = delete;
  IoUringInstance &operator=(IoUringInstance &&) = delete;

  bool prepRead(int fd, void *buf, size_t len, uint64_t userData,
                uint64_t offset) {
    auto *sqe = io_uring_get_sqe(&ring_);
    if (not sqe)
      return false;
    io_uring_prep_read(sqe, fd, buf, len, offset);
    io_uring_sqe_set_data64(sqe, userData);
    return true;
  }

  bool prepWrite(int fd, void *buf, size_t len, uint64_t userData,
                 uint64_t offset) {
    auto *sqe = io_uring_get_sqe(&ring_);
    if (not sqe)
      return false;
    io_uring_prep_write(sqe, fd, buf, len, offset);
    io_uring_sqe_set_data64(sqe, userData);
    return true;
  }

  void ioSubmit() { io_uring_submit(&ring_); }

  template <typename Callback> void drainCompletions(Callback callback) {
    io_uring_cqe *cqe;
    while (io_uring_peek_cqe(&ring_, &cqe) == 0) {
      callback(cqe->user_data, cqe->res);
      io_uring_cqe_seen(&ring_, cqe);
    }
  }

private:
  io_uring ring_;
};
