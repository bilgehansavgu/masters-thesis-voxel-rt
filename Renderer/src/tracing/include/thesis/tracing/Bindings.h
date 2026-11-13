#pragma once

#include "thesis/shared/Bindings.h"

#include <cstdint>

namespace thesis::tracing {

// Typed C++ names for the slots in thesis/shared/Bindings.h, which shaders include as well.
//   0       ray list (read)
//   1       hit list (write)
//   2..7    reserved for the tracer itself (e.g. the GPU's search structure)
//   8       primitive bounds (every primitive kind gets these)
//   9..15   the primitive kind's own data (e.g. encoded bricks), see core::PrimitiveData
//
// Tracers only use 0..7 by name. Bindings 8..15 come from GpuScene::primitiveBindings().
inline constexpr std::uint32_t kRayBinding = THESIS_BINDING_RAYS;
inline constexpr std::uint32_t kHitBinding = THESIS_BINDING_HITS;
inline constexpr std::uint32_t kTracerBindingFirst = THESIS_BINDING_TRACER_FIRST;
inline constexpr std::uint32_t kTracerBindingLast = THESIS_BINDING_TRACER_LAST;
inline constexpr std::uint32_t kPrimitiveBoundsBinding = THESIS_BINDING_PRIMITIVE_BOUNDS;
inline constexpr std::uint32_t kPrimitiveDataBindingFirst = THESIS_BINDING_PRIMITIVE_DATA_FIRST;
inline constexpr std::uint32_t kPrimitiveDataBindingLast = THESIS_BINDING_PRIMITIVE_DATA_LAST;

} // namespace thesis::tracing
