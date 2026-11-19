#pragma once

#include <auralis/mixer/SignalChain.h>

#include <memory>

namespace auralis::mixer {

// Creates the bounded-capacity production chain on a control/setup thread.
[[nodiscard]] std::unique_ptr<SignalChain> createSignalChain();

} // namespace auralis::mixer
