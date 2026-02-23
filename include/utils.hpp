#pragma once

template <typename Func>
class ScopeGuard {
public:
    explicit ScopeGuard(Func fn) : fn_(fn) {}
    ~ScopeGuard() { fn_(); }

private:
    Func fn_;
};
