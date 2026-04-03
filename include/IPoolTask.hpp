#pragma once

struct IPoolTask {
  virtual void runTask() = 0;
  virtual ~IPoolTask() = default;
};
