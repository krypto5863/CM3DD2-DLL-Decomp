#include "file_system_job_state.h"

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    using namespace cm3d2::internal;
    for (const std::int32_t count : {0, 1, 7, 8, 9, 15, 16, 17, 63, 64, 65,
                                   std::numeric_limits<std::int32_t>::max()}) {
        int count_reads = 0;
        int launched = 0;
        std::int32_t last_end = 0;
        launchAutoPathWorkerRanges([&] { ++count_reads; return count; },
            [&](std::int32_t index, std::int32_t begin, std::int32_t end) {
                require(index == launched++, "worker order/count");
                require(begin == last_end, "contiguous ranges");
                require(end - begin == count / 8 + (index == 7 ? count % 8 : 0),
                        "last worker owns remainder");
                last_end = end;
            });
        require(launched == 8 && count_reads == 2 && last_end == count,
                "eight launches and two count reads");
    }
    int reads = 0;
    launchAutoPathWorkerRanges([&] { return ++reads == 1 ? 16 : 19; },
        [](std::int32_t index, std::int32_t begin, std::int32_t end) {
            require(begin == index * 2 && end == (index + 1) * 2 + (index == 7 ? 3 : 0),
                    "final count is reread for remainder, not quotient");
        });

    AutoPathJobCompletion state;
    int sleeps = 0;
    auto sleep = [&](int) { ++sleeps; };
    require(!state.allFinished(), "initial completion bytes are zero");
    require(state.isFinished(false, [] { return false; }, sleep), "disabled poll");
    require(state.isFinished(true, [] { return false; }, sleep), "disabled wait");
    require(!state.isFinished(false, [] { return true; }, sleep), "enabled poll before start");
    require(sleeps == 0, "polling and disabled waits never sleep");
    for (std::size_t index = 0; index < 7; ++index) state.markFinished(index);
    require(!state.allFinished(), "eighth byte matters");
    require(state.isFinished(true, [] { return true; }, [&](int ms) {
        require(ms == 16, "native polling interval");
        ++sleeps;
        state.markFinished(7);
    }), "wait exits on completion");
    require(sleeps == 1 && state.allFinished(), "one wait then complete");
    require(state.isFinished(false, [] { return true; }, sleep), "completed poll");

    AutoPathJobCompletion unfinished;
    bool enabled = true;
    int mode_reads = 0;
    require(unfinished.isFinished(true, [&] { ++mode_reads; return enabled; }, [&](int ms) {
        require(ms == 16, "mode-change wait interval");
        enabled = false;
    }), "wait exits when mode becomes disabled");
    require(mode_reads == 2 && !unfinished.allFinished(), "mode rechecked independently of flags");

    AutoPathRootMutex mutex;
    mutex.lock();
    require(mutex.count() == 1, "native root lock count");
    bool rejected_recursive = false;
    try { mutex.lock(); } catch (const std::system_error&) { rejected_recursive = true; }
    require(rejected_recursive && mutex.count() == 1, "type 2 rejects recursive acquisition");
    mutex.unlock();
    require(mutex.count() == 0, "unlock releases at zero");
    mutex.unlock();
    require(mutex.count() == -1, "native duplicate unlock underflows without double SRW release");
}
