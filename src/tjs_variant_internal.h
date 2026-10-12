#pragma once

#include <cstdint>
#include <string>
#include "tjsVariant.h"

namespace cm3d2::internal {

// Tags as read by the native helpers (FUN_1800eb5e0, FUN_18006f040, FUN_1800730e0).
constexpr std::int32_t kTjsVoid = 0;
constexpr std::int32_t kTjsObject = 1;
constexpr std::int32_t kTjsString = 2;
constexpr std::int32_t kTjsOctet = 3;
constexpr std::int32_t kTjsInteger = 4;
constexpr std::int32_t kTjsReal = 5;

// Semantic handle, not the original packed native layout. Owned values use the
// VM's ref-counted variant: CopyRef retains both closure pointers and octets,
// and Clear/destruction releases them (native FUN_1800eb780 / FUN_1800eb5e0).
// Callback views point directly at the VM slot without retaining its contents.
// Reference aliases this facade; it never transfers ownership or extends a view's lifetime.
struct TjsVariant {
    TjsVariant() = default;
    explicit TjsVariant(TJS::tTJSVariant* borrowed_value) : borrowed(borrowed_value) {}
    TJS::tTJSVariant& native() { return borrowed != nullptr ? *borrowed : owned; }
    const TJS::tTJSVariant& native() const { return borrowed != nullptr ? *borrowed : owned; }

    TJS::tTJSVariant owned;
    TJS::tTJSVariant* borrowed = nullptr;
};

} // namespace cm3d2::internal
