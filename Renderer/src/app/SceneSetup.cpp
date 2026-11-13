#include "SceneSetup.h"

#include "thesis/core/BoxField.h"
#include "thesis/tracers/BuiltInTracers.h"

#include <stdexcept>

namespace thesis::app {

core::Scene makeScene(const Options& options)
{
    core::BoxFieldOptions boxField;
    boxField.gridSize = options.gridSize;
    boxField.seed = options.seed;
    return core::makeBoxField(boxField);
}

tracing::TracerRegistry makeTracerRegistry()
{
    tracing::TracerRegistry registry;
    plugins::registerBuiltInTracers(registry);
    return registry;
}

std::vector<std::string> selectTracers(const tracing::TracerRegistry& registry,
                                       const std::vector<std::string>& requested)
{
    if (requested.empty()) {
        return registry.names();
    }
    for (const std::string& name : requested) {
        if (!registry.contains(name)) {
            throw std::invalid_argument("Unknown tracer '" + name + "'. Use --list-tracers to see the options.");
        }
    }
    return requested;
}

} // namespace thesis::app
