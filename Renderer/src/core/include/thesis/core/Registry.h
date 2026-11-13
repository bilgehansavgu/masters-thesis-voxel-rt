#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace thesis::core {

// A socket's registry: maps a name to a factory for one implementation of `Interface`.
// Implementations are added by plugins (see cmake/ThesisPlugins.cmake); callers only see names.
template <typename Interface, typename... Args>
class Registry {
public:
    using Factory = std::function<std::unique_ptr<Interface>(Args...)>;

    struct Entry {
        std::string name;
        std::string description;
        Factory factory;
    };

    void add(std::string name, std::string description, Factory factory)
    {
        if (contains(name)) {
            throw std::logic_error("Registry: '" + name + "' is registered twice");
        }
        entries_.push_back({std::move(name), std::move(description), std::move(factory)});
    }

    [[nodiscard]] bool contains(std::string_view name) const { return find(name) != nullptr; }

    [[nodiscard]] std::unique_ptr<Interface> create(std::string_view name, Args... args) const
    {
        const Entry* entry = find(name);
        if (entry == nullptr) {
            throw std::invalid_argument("Registry: no implementation named '" + std::string(name) + "'");
        }
        return entry->factory(std::forward<Args>(args)...);
    }

    // Names in registration order.
    [[nodiscard]] std::vector<std::string> names() const
    {
        std::vector<std::string> result;
        result.reserve(entries_.size());
        for (const Entry& entry : entries_) {
            result.push_back(entry.name);
        }
        return result;
    }

    [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }

private:
    [[nodiscard]] const Entry* find(std::string_view name) const
    {
        const auto it =
            std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.name == name; });
        return it == entries_.end() ? nullptr : &*it;
    }

    std::vector<Entry> entries_;
};

} // namespace thesis::core
