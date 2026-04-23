#pragma once

namespace rukh {

struct IPoolTask {
  virtual void runTask() = 0;
  virtual ~IPoolTask() = default;
};
} // namespace rukh
