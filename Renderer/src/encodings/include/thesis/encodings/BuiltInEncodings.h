#pragma once

// Side: CPU

#include "thesis/encoding/Encoding.h"

namespace thesis::plugins {

// Adds every encoding plugin that was built into this program (generated at configure time).
void registerBuiltInEncodings(encoding::EncodingRegistry& registry);

} // namespace thesis::plugins
