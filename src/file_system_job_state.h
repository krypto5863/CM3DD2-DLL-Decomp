#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <Windows.h>
#include <thread>
#include <system_error>

namespace cm3d2::internal {

inline constexpr std::size_t autoPathWorkerCount = 8;

// The native manager uses MSVC mutex records initialized with type 2, owner -1,
// count 0 and an SRW lock. Model the counter explicitly: Windows worker 18007e300
// unlocks twice on an already-present slot. std::mutex::unlock twice would be UB,
// whereas native 180136154 decrements again and releases only at count == 0.
// Atomic fields are a C++ memory-model substitution, not a literal 0x50 layout.
class AutoPathRootMutex {
public:
    void lock() {
        const DWORD thread = GetCurrentThreadId();
        if (owner_.load(std::memory_order_relaxed) != thread) AcquireSRWLockExclusive(&lock_);
        const auto previous = count_.fetch_add(1, std::memory_order_relaxed);
        const auto next = previous + 1;
        if (static_cast<std::int32_t>(next) < 2) {
            owner_.store(thread, std::memory_order_relaxed);
        } else {
            count_.store(previous, std::memory_order_relaxed);
            throw std::system_error(std::make_error_code(std::errc::resource_deadlock_would_occur));
        }
    }
    void unlock() {
        if (count_.fetch_sub(1, std::memory_order_relaxed) == 1) {
            owner_.store(~DWORD(0), std::memory_order_relaxed);
            ReleaseSRWLockExclusive(&lock_);
        }
    }
    std::int32_t count() const { return static_cast<std::int32_t>(count_.load(std::memory_order_relaxed)); }
private:
    SRWLOCK lock_ = SRWLOCK_INIT;
    std::atomic<DWORD> owner_{~DWORD(0)};
    std::atomic<std::uint32_t> count_{0};
};

// FUN_1800828d0: count is read once for the quotient, again for worker 7's
// remainder. Launch is invoked even for empty ranges, with (index, begin, end).
// This describes scheduling only; the caller supplies the detached launcher
// and must keep the owning filesystem and its root records alive.
template<class Count, class Launch>
void launchAutoPathWorkerRanges(Count count, Launch launch) {
    const std::int32_t quotient = count() / 8;
    for (std::int32_t worker = 0; worker < 8; ++worker) {
        const std::int32_t remainder = worker == 7 ? count() % 8 : 0;
        launch(worker, worker * quotient, (worker + 1) * quotient + remainder);
    }
}

// Constructors initialize eight zero bytes; workers set their own byte to one.
// Native start and release do NOT reset these bytes. Atomic publication is our
// C++ memory-model substitution for the original ordinary concurrent byte writes;
// this helper is not a reconstruction of the native manager's object layout.
class AutoPathJobCompletion {
public:
    AutoPathJobCompletion() {
        for (auto& flag : done_) flag.store(false, std::memory_order_relaxed);
    }

    void markFinished(std::size_t worker) {
        done_[worker].store(true, std::memory_order_release);
    }

    bool allFinished() const {
        for (const auto& flag : done_)
            if (!flag.load(std::memory_order_acquire)) return false;
        return true;
    }

    // DLL_FileSystem_IsFinishedAddAutoPathJob (18006a730): disabled mode
    // reports true regardless of flags. Waiting rechecks mode after Sleep(16).
    // Sleep is injected so contracts need no wall-clock waits.
    template<class Enabled, class Sleep>
    bool isFinished(bool wait, Enabled enabled, Sleep sleep) const {
        while (enabled()) {
            if (allFinished()) return true;
            if (!wait) return false;
            sleep(16);
        }
        return true;
    }

private:
    std::array<std::atomic<bool>, autoPathWorkerCount> done_;
};

} // namespace cm3d2::internal
