#pragma once

// Only external core services are replaced. tests/test_stack_size.cpp includes
// the production translation unit; no stack-size or cache policy lives here.
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using int32 = std::int32_t;
template <typename T> using Optional = std::optional<T>;

enum ItemClass : uint32
{
    ITEM_CLASS_CONSUMABLE = 0, ITEM_CLASS_WEAPON = 2, ITEM_CLASS_GEM = 3,
    ITEM_CLASS_REAGENT = 5, ITEM_CLASS_PROJECTILE = 6, ITEM_CLASS_TRADE_GOODS = 7,
    ITEM_CLASS_QUEST = 12, ITEM_CLASS_MISC = 15, ITEM_CLASS_GLYPH = 16
};

struct ItemTemplate
{
    uint32 ItemId;
    uint32 Class;
    int32 Stackable;
    int32 MaxCount = 0;
    uint32 BuyCount = 1;
};

namespace TestCore
{
    inline unsigned errors = 0;
    inline std::unordered_map<std::string, std::string> options;

    struct ConfigMgr
    {
        template <typename T> T GetOption(std::string const& name, T fallback) const
        {
            auto const it = options.find(name);
            if (it == options.end())
                return fallback;
            if constexpr (std::is_same_v<T, std::string>)
                return it->second;
            else if constexpr (std::is_same_v<T, bool>)
                return it->second == "1";
            else
            {
                T value{};
                auto const result = std::from_chars(it->second.data(), it->second.data() + it->second.size(), value);
                if (result.ec != std::errc{} || result.ptr != it->second.data() + it->second.size())
                    throw std::runtime_error("Invalid numeric fixture option");
                return value;
            }
        }
    };

    struct ObjectMgr
    {
        std::vector<ItemTemplate*> store;
        std::vector<ItemTemplate*>* GetItemTemplateStoreFast() { return &store; }
        ItemTemplate const* GetItemTemplate(uint32 entry) const
        {
            for (auto* item : store)
                if (item && item->ItemId == entry)
                    return item;
            return nullptr;
        }
    };

    inline ConfigMgr configMgr;
    inline ObjectMgr objectMgr;
}

inline auto* sConfigMgr = &TestCore::configMgr;
inline auto* sObjectMgr = &TestCore::objectMgr;
#define LOG_ERROR(...) (++TestCore::errors)
#define LOG_INFO(...) ((void)0)

namespace Acore
{
    // Matches the integral conversion contract: full decimal token, range
    // checked, no leading sign for uint32. Core compatibility is checked in CI.
    template <typename T> Optional<T> StringTo(std::string_view text)
    {
        T value{};
        auto const result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
            return std::nullopt;
        return value;
    }

    inline std::vector<std::string_view> Tokenize(std::string_view text, char separator, bool keepEmpty)
    {
        std::vector<std::string_view> tokens;
        std::size_t start = 0;
        for (;;)
        {
            auto const end = text.find(separator, start);
            auto const token = text.substr(start, end == std::string_view::npos ? end : end - start);
            if (keepEmpty || !token.empty())
                tokens.push_back(token);
            if (end == std::string_view::npos)
                return tokens;
            start = end + 1;
        }
    }
}

enum WorldHook
{
    WORLDHOOK_ON_AFTER_CONFIG_LOAD,
    WORLDHOOK_ON_BEFORE_WORLD_INITIALIZED,
    WORLDHOOK_ON_BEFORE_FINALIZE_PLAYER_WORLD_SESSION
};

class WorldScript
{
public:
    static inline WorldScript* registered = nullptr;
    static inline bool failRegistration = false;
    std::vector<WorldHook> hooks;
    WorldScript(char const*, std::initializer_list<WorldHook> subscribed) : hooks(subscribed)
    {
        if (failRegistration)
            throw std::runtime_error("Injected registration failure");
        registered = this;
    }
    bool HasHook(WorldHook hook) const { return std::find(hooks.begin(), hooks.end(), hook) != hooks.end(); }
    virtual ~WorldScript() = default;
    virtual void OnAfterConfigLoad(bool) { }
    virtual void OnBeforeWorldInitialized() { }
    virtual void OnBeforeFinalizePlayerWorldSession(uint32&) { }
};
