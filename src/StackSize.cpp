/*
 * mod-stack-size
 *
 * Raises the maximum stack size of items, by item class (trade goods, reagents, consumables...)
 * or per item, from the config file.
 *
 * The 3.3.5a client has no stack size of its own: it asks the server about each item and shows
 * whatever item_template.stackable says, both in tooltips and in the split-stack dialog. So this is
 * a server-only change. The loaded item templates are edited in memory at startup, after the core
 * has read item_template, so the database is never touched and turning the module off restores the
 * stock sizes on the next restart.
 *
 * Class rules only ever raise a stack, and only for items that already stack: an item that doesn't
 * stack (stackable 1) stays that way, and an item that already stacks higher is left alone.
 * StackSize.Items overrides single items and can set any size.
 *
 * The client keeps item data in its Cache folder and would keep showing the old sizes. With
 * StackSize.ResetClientCache the server sends a different client cache version whenever the set of
 * changed stacks differs, which makes every client throw its cache away once on its next login.
 *
 * Released under the MIT License.
 */

#include "Config.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "ScriptMgr.h"
#include "StringConvert.h"
#include "Tokenize.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace
{
    // One below the core's "no limit" value (2147483647), which GetMaxStackSize() treats specially.
    constexpr uint32 MaxStack = 0x7FFFFFFE;

    struct ClassRule
    {
        char const* option;
        uint32 itemClass;
        uint32 defaultSize;
    };

    constexpr ClassRule ClassRules[] =
    {
        { "StackSize.TradeGoods",  ITEM_CLASS_TRADE_GOODS, 999 },
        { "StackSize.Reagents",    ITEM_CLASS_REAGENT,     200 },
        { "StackSize.Consumables", ITEM_CLASS_CONSUMABLE,  200 },
        { "StackSize.Gems",        ITEM_CLASS_GEM,         0 },
        { "StackSize.Projectiles", ITEM_CLASS_PROJECTILE,  0 },
        { "StackSize.Glyphs",      ITEM_CLASS_GLYPH,       0 },
        { "StackSize.Quest",       ITEM_CLASS_QUEST,       0 },
        { "StackSize.Misc",        ITEM_CLASS_MISC,        0 },
    };

    struct Config
    {
        bool enabled = true;
        bool resetClientCache = true;
        std::unordered_map<uint32, uint32> classSizes;   // item class -> stack size, 0 = unchanged
        std::unordered_map<uint32, uint32> itemSizes;    // item entry -> stack size
        std::unordered_set<uint32> excluded;
    };

    Config config;

    // XORed into the client cache version; 0 until stacks are changed.
    uint32 cacheSalt = 0;

    std::string SplitList(std::string list)
    {
        std::replace(list.begin(), list.end(), ',', ' ');
        return list;
    }

    // "2589:500 2592:500" (commas work too).
    std::unordered_map<uint32, uint32> ParseItemSizes(std::string const& option)
    {
        std::unordered_map<uint32, uint32> sizes;
        std::string const list = SplitList(sConfigMgr->GetOption<std::string>(option, ""));

        for (std::string_view token : Acore::Tokenize(list, ' ', false))
        {
            std::size_t const colon = token.find(':');
            Optional<uint32> entry = colon == std::string_view::npos ? Optional<uint32>() : Acore::StringTo<uint32>(token.substr(0, colon));
            Optional<uint32> size = colon == std::string_view::npos ? Optional<uint32>() : Acore::StringTo<uint32>(token.substr(colon + 1));

            if (!entry || !size || !*size)
            {
                LOG_ERROR("module", "mod-stack-size: {} has a bad entry '{}', expected item:size", option, token);
                continue;
            }

            sizes[*entry] = std::min(*size, MaxStack);
        }

        return sizes;
    }

    std::unordered_set<uint32> ParseItemList(std::string const& option)
    {
        std::unordered_set<uint32> entries;
        std::string const list = SplitList(sConfigMgr->GetOption<std::string>(option, ""));

        for (std::string_view token : Acore::Tokenize(list, ' ', false))
        {
            if (Optional<uint32> entry = Acore::StringTo<uint32>(token))
                entries.insert(*entry);
            else
                LOG_ERROR("module", "mod-stack-size: {} has a bad item entry '{}'", option, token);
        }

        return entries;
    }

    // The stack size this module wants for an item, or 0 to leave it as it is.
    uint32 WantedSize(ItemTemplate const& proto)
    {
        if (config.excluded.count(proto.ItemId))
            return 0;

        if (auto itr = config.itemSizes.find(proto.ItemId); itr != config.itemSizes.end())
            return itr->second;

        auto itr = config.classSizes.find(proto.Class);
        if (itr == config.classSizes.end() || !itr->second)
            return 0;

        // -1 (no limit) and 1 (doesn't stack) are left alone, and class rules never lower a stack.
        if (proto.Stackable <= 1 || uint32(proto.Stackable) >= itr->second)
            return 0;

        return itr->second;
    }

    // FNV-1a, so the same config gives the same cache version across restarts.
    void Mix(uint32& hash, uint32 value)
    {
        for (uint8 i = 0; i < 4; ++i)
        {
            hash ^= (value >> (i * 8)) & 0xFF;
            hash *= 16777619u;
        }
    }

    void ApplyStackSizes()
    {
        uint32 changed = 0;
        uint32 hash = 2166136261u;

        // The fast store is indexed by entry, so it's walked in the same order every time.
        for (ItemTemplate* proto : *sObjectMgr->GetItemTemplateStoreFast())
        {
            if (!proto)
                continue;

            uint32 const size = WantedSize(*proto);
            if (!size || uint32(proto->Stackable) == size)
                continue;

            proto->Stackable = int32(size);
            Mix(hash, proto->ItemId);
            Mix(hash, size);
            ++changed;
        }

        for (auto const& [entry, size] : config.itemSizes)
            if (!sObjectMgr->GetItemTemplate(entry))
                LOG_ERROR("module", "mod-stack-size: StackSize.Items names item {}, which doesn't exist", entry);

        if (changed && config.resetClientCache)
            cacheSalt = hash | 1;

        LOG_INFO("module", "mod-stack-size: changed the stack size of {} items", changed);
    }
}

class StackSizeWorldScript : public WorldScript
{
public:
    StackSizeWorldScript() : WorldScript("StackSizeWorldScript", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD,
        WORLDHOOK_ON_BEFORE_WORLD_INITIALIZED,
        WORLDHOOK_ON_BEFORE_FINALIZE_PLAYER_WORLD_SESSION
    }) { }

    // Stack sizes are only applied at startup; a config reload has no effect until the next restart.
    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            return;

        config.enabled          = sConfigMgr->GetOption<bool>("StackSize.Enable", true);
        config.resetClientCache = sConfigMgr->GetOption<bool>("StackSize.ResetClientCache", true);

        for (ClassRule const& rule : ClassRules)
            config.classSizes[rule.itemClass] = std::min(sConfigMgr->GetOption<uint32>(rule.option, rule.defaultSize), MaxStack);

        config.itemSizes = ParseItemSizes("StackSize.Items");
        config.excluded  = ParseItemList("StackSize.Exclude");
    }

    // Runs after the core has loaded item_template.
    void OnBeforeWorldInitialized() override
    {
        if (config.enabled)
            ApplyStackSizes();
    }

    void OnBeforeFinalizePlayerWorldSession(uint32& cacheVersion) override
    {
        cacheVersion ^= cacheSalt;
    }
};

void AddStackSizeScripts()
{
    new StackSizeWorldScript();
}
