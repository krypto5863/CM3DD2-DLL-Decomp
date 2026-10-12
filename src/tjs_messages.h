#pragma once

namespace cm3d2::internal {
// Install only defaults proven from the original binary. Call before scripts
// execute, after the vendored engine's message holders have been constructed.
void initializeTjsMessages();
}
