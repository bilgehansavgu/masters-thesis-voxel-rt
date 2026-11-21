#pragma once

// Side: CPU

#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace thesis::core {

// Whether a plugin is the one a program starts with when the user names none.
enum class Pick { Normal, Default };

// A socket's registry: maps a name to a factory for one implementation of `Interface`.
// Implementations are added by plugins (see cmake/ThesisPlugins.cmake); callers only see names.
// One plugin may mark itself as the default, so no caller has to name a plugin.
template <typename Interface, typename... Args>
class Registry {
public:
    using Factory = std::function<std::unique_ptr<Interface>(Args...)>;

    struct Entry {
        std::string name;
        std::string description;
        Factory factory;
        bool isDefault = false;
    };

    void add(std::string name, std::string description, Factory factory, Pick pick = Pick::Normal)
    {
        if (contains(name)) {
            throw std::logic_error("Registry: '" + name + "' is registered twice");
        }
        const bool isDefault = pick == Pick::Default;
        if (isDefault &&
            std::any_of(entries_.begin(), entries_.end(), [](const Entry& entry) { return entry.isDefault; })) {
            throw std::logic_error("Registry: '" + name + "' is the second default");
        }
        entries_.push_back({std::move(name), std::move(description), std::move(factory), isDefault});
    }

    // The plugin marked as default, or else the first one registered. Throws if there is none.
    [[nodiscard]] const std::string& defaultName() const
    {
        if (entries_.empty()) {
            throw std::logic_error("Registry: nothing is registered");
        }
        const auto it =
            std::find_if(entries_.begin(), entries_.end(), [](const Entry& entry) { return entry.isDefault; });
        return it == entries_.end() ? entries_.front().name : it->name;
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
