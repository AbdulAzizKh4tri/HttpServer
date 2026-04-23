#pragma once

namespace rukh {

class Executor;
extern thread_local Executor *tl_executor;
extern thread_local bool tl_timed_out;
} // namespace rukh
