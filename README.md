# mod-stack-size

An AzerothCore module that raises item stack sizes from the config file. By default, trade goods
(cloth, leather, ore, bars, herbs, meat, elemental items, enchanting materials) stack to **999**,
and reagents and consumables (food, drink, potions, flasks, scrolls, bandages) to **200**. Other
item classes and single items can be set too.

## How it works

The 3.3.5a client has no stack sizes of its own. It asks the server about each item and uses
`item_template.stackable` for tooltips and the split-stack dialog. That makes this a server-only
change: **no client patch is needed**.

At startup, after the core has loaded `item_template`, the module changes the stack sizes of the
loaded items in memory. It never writes to the database. To get the stock sizes back, set
`StackSize.Enable = 0` and restart.

- Class options (`StackSize.TradeGoods`, `StackSize.Reagents`, ...) only **raise** stacks, and only
  for items that already stack. An item that doesn't stack stays that way.
- `StackSize.Items` sets single items to any size, including lower sizes or making an item stack.
- `StackSize.Exclude` keeps items out of the class options.

### Client cache

Clients keep item data in `Cache/WDB` and would keep showing the old stack sizes. With
`StackSize.ResetClientCache = 1` (the default), the server sends a client cache version made from
the changed stacks. Each client then clears its cache once on its next login. The same settings
give the same version, so restarts don't clear the cache again. Changing the settings, or turning
the module off, clears it once more.

## Install

```bash
cd azerothcore-wotlk/modules
git clone https://github.com/buildthehomelab/wow-mod-stack-size.git mod-stack-size
```

The folder must be named `mod-stack-size`, because AzerothCore derives the loader name from it.
Re-run CMake, rebuild, and copy `mod_stack_size.conf.dist` to `mod_stack_size.conf`.

## Config

| Option | Default | What it does |
| --- | --- | --- |
| `StackSize.Enable` | 1 | Master switch |
| `StackSize.TradeGoods` | 999 | Trade goods |
| `StackSize.Reagents` | 200 | Spell reagents |
| `StackSize.Consumables` | 200 | Food, drink, potions, flasks, scrolls, bandages |
| `StackSize.Gems` | 0 | Gems that already stack |
| `StackSize.Projectiles` | 0 | Arrows and bullets |
| `StackSize.Glyphs` | 0 | Glyphs |
| `StackSize.Quest` | 0 | Quest items that already stack |
| `StackSize.Misc` | 0 | Miscellaneous items |
| `StackSize.Items` | "" | `item:size` pairs, e.g. `"6265:20 21877:500"` |
| `StackSize.Exclude` | "" | Items the class options skip |
| `StackSize.ResetClientCache` | 1 | Make clients clear their item cache when stacks change |

0 leaves that class alone. Changes need a worldserver restart.

## Notes

- Stacks up to 999 fit the bag slot. Four-digit counts crowd the icon but still work.
- A vendor's `BuyCount` (how many you get per purchase) is separate and doesn't change.
- Other code that reads the max stack follows the new sizes. That includes loot splitting, mail,
  the auction house, and playerbots guild tasks. Playerbots guild tasks ask for "one full stack" of
  some items, so with them enabled a task could ask for up to 999.

## License

MIT
