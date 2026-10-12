#pragma once

namespace TJS {
class tTJS;
}

namespace cm3d2::kag {

// The embedded KAGParser reaches the engine and the file system through free functions (as in Kirikiri).
// The KAG object sets the context for the duration of each call that can reach them. Calls on one KAG object
// must be serialized by the owner, like the rest of the TJS wrapper.
struct HostContext {
    TJS::tTJS* engine = nullptr;
    const void* file_system = nullptr;  // borrowed wide file system interface (script host context, or SetFileSystem)
    void* script = nullptr;             // TjsScript handle whose console receives parser log lines and error reports
};

class HostScope {
public:
    explicit HostScope(const HostContext& context);
    ~HostScope();
    HostScope(const HostScope&) = delete;
    HostScope& operator=(const HostScope&) = delete;

private:
    const HostContext* previous_;
    HostContext context_;
};

const HostContext& currentHost();


} // namespace cm3d2::kag
