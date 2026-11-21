// The box of every primitive. Every primitive kind gets this buffer at the same binding
// (thesis/shared/Bindings.h), so it is declared once, here: the kinds read it, and so may a
// tracer whose own code tests the boxes (a ray tracing pipeline's intersection shader).

import types;
#include "thesis/shared/Bindings.h"

[[vk::binding(THESIS_BINDING_PRIMITIVE_BOUNDS, 0)]]
public StructuredBuffer<Aabb> gPrimitiveBounds;
