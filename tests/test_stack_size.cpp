#include "stubs/Core.h"
#include <iostream>
#include <limits>

#ifndef STACK_SIZE_SOURCE
#define STACK_SIZE_SOURCE "../src/StackSize.cpp"
#endif
#include STACK_SIZE_SOURCE

void Addmod_stack_sizeScripts();

namespace
{
    unsigned checks = 0;
    void Check(bool value, char const* label)
    {
        ++checks;
        if (!value)
            throw std::runtime_error(label);
    }

    struct Fixture
    {
        StackSizeWorldScript script;
        std::vector<ItemTemplate> items;
        Fixture()
        {
            TestCore::options.clear();
            TestCore::errors = 0;
            TestCore::objectMgr.store.clear();
            config = Config{};
            cacheSalt = 0; // A fresh fixture represents a new worldserver process.
        }
        void Option(std::string const& name, std::string const& value) { TestCore::options[name] = value; }
        void Start()
        {
            std::sort(items.begin(), items.end(), [](auto const& a, auto const& b) { return a.ItemId < b.ItemId; });
            TestCore::objectMgr.store = {nullptr};
            for (auto& item : items)
                TestCore::objectMgr.store.push_back(&item);
            if (script.HasHook(WORLDHOOK_ON_AFTER_CONFIG_LOAD))
                script.OnAfterConfigLoad(false);
            if (script.HasHook(WORLDHOOK_ON_BEFORE_WORLD_INITIALIZED))
                script.OnBeforeWorldInitialized();
        }
        ItemTemplate const& Get(uint32 entry) const
        {
            auto const* item = sObjectMgr->GetItemTemplate(entry);
            if (!item)
                throw std::runtime_error("Missing fixture item");
            return *item;
        }
        uint32 Cache(uint32 base = 1234567)
        {
            if (script.HasHook(WORLDHOOK_ON_BEFORE_FINALIZE_PLAYER_WORLD_SESSION))
                script.OnBeforeFinalizePlayerWorldSession(base);
            return base;
        }
    };

    void DefaultClassRules()
    {
        Fixture f;
        f.items = {{1, ITEM_CLASS_TRADE_GOODS, 20}, {2, ITEM_CLASS_REAGENT, 20},
                   {3, ITEM_CLASS_CONSUMABLE, 5}, {4, ITEM_CLASS_GEM, 20},
                   {5, ITEM_CLASS_WEAPON, 1}, {6, ITEM_CLASS_TRADE_GOODS, 1000},
                   {7, ITEM_CLASS_TRADE_GOODS, 1}, {8, ITEM_CLASS_TRADE_GOODS, -1}, {9, ITEM_CLASS_TRADE_GOODS, 0}};
        f.Start();
        Check(f.Get(1).Stackable == 999, "trade goods default");
        Check(f.Get(2).Stackable == 200, "reagents default");
        Check(f.Get(3).Stackable == 200, "consumables default");
        Check(f.Get(4).Stackable == 20, "disabled class unchanged");
        Check(f.Get(5).Stackable == 1, "unsupported class unchanged");
        Check(f.Get(6).Stackable == 1000, "class rule cannot lower");
        Check(f.Get(7).Stackable == 1, "class rule cannot create a stack");
        Check(f.Get(8).Stackable == -1, "unlimited stack unchanged");
        Check(f.Get(9).Stackable == 0, "zero stack sentinel unchanged");
        Check(f.Get(1).BuyCount == 1, "vendor purchase count unchanged");
    }

    void SwitchesAndClassOptions()
    {
        {
            Fixture f;
            f.items = {{1, ITEM_CLASS_TRADE_GOODS, 20}};
            f.Option("StackSize.Enable", "0");
            f.Start();
            Check(f.Get(1).Stackable == 20, "disabled module changes nothing");
            Check(f.Cache() == 1234567, "disabled module preserves cache version");
        }
        {
            Fixture f;
            f.items = {{1, ITEM_CLASS_TRADE_GOODS, 20}};
            f.Option("StackSize.ResetClientCache", "0");
            f.Start();
            Check(f.Get(1).Stackable == 999, "cache switch does not disable stack rules");
            Check(f.Cache() == 1234567, "disabled cache reset preserves version");
        }
        {
            Fixture f;
            f.Option("StackSize.TradeGoods", "0");
            f.items = {{1, ITEM_CLASS_TRADE_GOODS, 20}};
            f.Start();
            Check(f.Get(1).Stackable == 20, "zero class setting leaves stack alone");
            Check(f.Cache() == 1234567, "no effective changes means no cache reset");
        }
        {
            Fixture f;
            f.items = {{1, ITEM_CLASS_GEM, 2}, {2, ITEM_CLASS_PROJECTILE, 2},
                       {3, ITEM_CLASS_GLYPH, 2}, {4, ITEM_CLASS_QUEST, 2}, {5, ITEM_CLASS_MISC, 2}};
            for (auto const* option : {"Gems", "Projectiles", "Glyphs", "Quest", "Misc"})
                f.Option(std::string("StackSize.") + option, "50");
            f.Start();
            for (auto const& item : f.items)
                Check(item.Stackable == 50, "optional class setting applies");
        }
    }

    void PerItemAndCarryLimits()
    {
        Fixture f;
        f.items = {{6265, ITEM_CLASS_REAGENT, 1, 32}, {10, ITEM_CLASS_TRADE_GOODS, 200, 200},
                   {11, ITEM_CLASS_WEAPON, 1, 0}, {12, ITEM_CLASS_REAGENT, 20, 1000},
                   {13, ITEM_CLASS_REAGENT, 20, -1}, {14, ITEM_CLASS_REAGENT, 999, 32},
                   {15, ITEM_CLASS_TRADE_GOODS, 20, 32}};
        f.Option("StackSize.Items", "6265:999,10:20 11:50 12:999 13:999 14:999");
        f.Start();
        Check(f.Get(6265).Stackable == 999 && f.Get(6265).MaxCount == 999, "explicit shard size raises its carry limit");
        Check(f.Get(10).Stackable == 20 && f.Get(10).MaxCount == 200, "explicit lowering does not lower carry limit");
        Check(f.Get(11).Stackable == 50 && f.Get(11).MaxCount == 0, "explicit rule creates stack without adding a carry limit");
        Check(f.Get(12).MaxCount == 1000, "higher carry limit preserved");
        Check(f.Get(13).MaxCount == -1, "negative carry sentinel preserved");
        Check(f.Get(14).Stackable == 999 && f.Get(14).MaxCount == 999, "carry limit updates even if stack already matches");
        Check(f.Get(15).Stackable == 999 && f.Get(15).MaxCount == 32, "class rules never raise carry limits");
    }

    void ExclusionsAndParsing()
    {
        Fixture f;
        f.items = {{10, ITEM_CLASS_TRADE_GOODS, 20}, {11, ITEM_CLASS_REAGENT, 20},
                   {12, ITEM_CLASS_REAGENT, 20}, {13, ITEM_CLASS_REAGENT, 20},
                   {14, ITEM_CLASS_REAGENT, 20}, {15, ITEM_CLASS_MISC, 20}};
        f.Option("StackSize.Exclude", "10, 11 11 invalid -1 4294967296");
        f.Option("StackSize.Items", "12:30,12:40 13:0 :4 14: 14:3:4 14:-1 14:4294967296 invalid 15:50");
        f.Start();
        Check(f.Get(10).Stackable == 20 && f.Get(11).Stackable == 20, "comma and space separated exclusions");
        Check(f.Get(12).Stackable == 40, "last valid duplicate item rule wins");
        Check(f.Get(13).Stackable == 200 && f.Get(14).Stackable == 200, "bad overrides do not suppress valid class rule");
        Check(f.Get(15).Stackable == 50, "valid rule after invalid tokens still applies");
        Check(TestCore::errors == 10, "invalid tokens reported rather than silently accepted");
    }

    void ClampedSizes()
    {
        Fixture f;
        f.items = {{1, ITEM_CLASS_TRADE_GOODS, 20}, {2, ITEM_CLASS_REAGENT, 1, 32}};
        f.Option("StackSize.TradeGoods", "4294967295");
        f.Option("StackSize.Items", "2:4294967295");
        f.Start();
        Check(f.Get(1).Stackable == 2147483646, "class size clamped below core unlimited sentinel");
        Check(f.Get(2).Stackable == 2147483646 && f.Get(2).MaxCount == 2147483646,
              "explicit size and carry limit clamped below core unlimited sentinel");
    }

    uint32 VersionFor(std::string const& entries, bool reverse = false)
    {
        Fixture f;
        f.items = {{100, ITEM_CLASS_WEAPON, 1}, {200, ITEM_CLASS_WEAPON, 1}};
        if (reverse)
            std::reverse(f.items.begin(), f.items.end());
        f.Option("StackSize.Items", entries);
        f.Start();
        return f.Cache();
    }

    void StableCacheVersions()
    {
        uint32 const first = VersionFor("100:50 200:80");
        Check(first != 1234567, "effective stack changes alter cache version");
        Check(first == VersionFor("200:80,100:50", true), "same effective changes give stable cache version");
        Check(first != VersionFor("100:51 200:80"), "changed stack target alters cache version");
        uint32 stackOnlyCache;
        {
            Fixture f;
            f.items = {{100, ITEM_CLASS_WEAPON, 20, 0}};
            f.Option("StackSize.Items", "100:50");
            f.Start();
            stackOnlyCache = f.Cache();
        }
        {
            Fixture f;
            f.items = {{100, ITEM_CLASS_WEAPON, 20, 32}};
            f.Option("StackSize.Items", "100:50");
            f.Start();
            Check(f.Cache() != stackOnlyCache, "carry-limit change contributes to cache version");
        }
        {
            Fixture f;
            f.items = {{100, ITEM_CLASS_WEAPON, 50, 32}};
            f.Option("StackSize.Items", "100:50");
            f.Start();
            Check(f.Get(100).MaxCount == 50, "carry-only change applied");
            Check(f.Cache() != 1234567, "carry-only change invalidates client cache");
            uint32 const base = 0x5A5A1234u;
            uint32 const salt = f.Cache(0);
            Check(f.Cache(base) == (base ^ salt), "cache salt combines with the core version");
        }
        {
            Fixture f;
            f.items = {{100, ITEM_CLASS_WEAPON, 50, 0}};
            f.Option("StackSize.Items", "100:50");
            f.Start();
            Check(f.Cache() == 1234567, "already matching explicit rule does not invalidate cache");
        }
        {
            Fixture f;
            f.Option("StackSize.Items", "999999:50");
            f.Start();
            Check(TestCore::errors == 1, "unknown explicit item reported");
            Check(f.Cache() == 1234567, "unknown item alone does not invalidate cache");
        }
    }

    void ReloadDoesNotApply()
    {
        Fixture f;
        f.items = {{1, ITEM_CLASS_TRADE_GOODS, 20}};
        f.Start();
        auto const oldCache = f.Cache();
        f.Option("StackSize.TradeGoods", "1500");
        f.Option("StackSize.Enable", "0");
        f.script.OnAfterConfigLoad(true);
        Check(WantedSize(ItemTemplate{2, ITEM_CLASS_TRADE_GOODS, 20}) == 999,
              "reload does not replace saved rule settings");
        f.script.OnBeforeWorldInitialized();
        Check(f.Get(1).Stackable == 999, "reload does not change startup rules");
        Check(f.Cache() == oldCache, "reload does not change cache salt");
    }

    void LoaderRegistersHooks()
    {
        auto* previous = WorldScript::registered;
        Addmod_stack_sizeScripts();
        Check(WorldScript::registered != previous, "module loader registers world script");
        for (WorldHook hook : {WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_BEFORE_WORLD_INITIALIZED,
                               WORLDHOOK_ON_BEFORE_FINALIZE_PLAYER_WORLD_SESSION})
            Check(WorldScript::registered->HasHook(hook), "module registers required core hook");
        delete WorldScript::registered;
        WorldScript::registered = nullptr;
        WorldScript::failRegistration = true;
        bool failed = false;
        try
        {
            Addmod_stack_sizeScripts();
        }
        catch (std::runtime_error const&)
        {
            failed = true;
        }
        WorldScript::failRegistration = false;
        Check(failed, "loader propagates script registration failure");
        Check(WorldScript::registered == nullptr, "failed registration leaves no registered script");
    }

}

void Addmod_stack_sizeScripts();
int main()
{
    try
    {
        DefaultClassRules();
        SwitchesAndClassOptions();
        PerItemAndCarryLimits();
        ExclusionsAndParsing();
        ClampedSizes();
        StableCacheVersions();
        ReloadDoesNotApply();
        LoaderRegistersHooks();
        std::cout << "PASS " << checks << " checks\n";
    }
    catch (std::exception const& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
