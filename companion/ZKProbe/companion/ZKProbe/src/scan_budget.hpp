#pragma once
#include <cstddef>
#include <chrono>
namespace zk {
struct ScanBudget {
    size_t bytes=0, readBytes=0, readCalls=0, readFailures=0;
    bool exhausted=false;
    const char *reason="none";
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
};
inline ScanBudget& scanBudget() { static ScanBudget state; return state; }
inline void resetScanBudget() { scanBudget()=ScanBudget{}; }
inline bool budgetTimeOK() {
    auto &b=scanBudget();
    if(b.exhausted) return false;
    if(std::chrono::steady_clock::now()-b.start>=std::chrono::seconds(2)) {
        b.exhausted=true; b.reason="elapsed_limit"; return false;
    }
    return true;
}
inline bool allowRead(size_t n) {
    auto &b=scanBudget();
    if(!budgetTimeOK()) return false;
    constexpr size_t limit=4096u;
    if(n>limit-b.readBytes || b.readCalls>=128) {
        b.exhausted=true; b.reason="read_limit"; return false;
    }
    b.readBytes+=n; ++b.readCalls; return true;
}
inline bool allowScan(size_t n) {
    if(!budgetTimeOK()) return false;
    scanBudget().bytes+=n; return true;
}
}
