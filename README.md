# SLID - Skyrim Linked Item Distribution

Intelligent storage management for Skyrim SE/AE. Create storage networks that automatically route items from a master container to designated destinations based on customizable filters.

## Features

- **Intelligent sorting** - Items automatically route to the right containers based on customizable filters
- **52 filters in 12 families** - Weapons, Armor, Valuables, Consumables, Books, Crafting Materials, Enchanted Items, Unique Items, and more
- **Context Power** - One power does everything. Cast it at a container, NPC, or into the air — SLID reads the context and shows a popup menu with the right actions
- **Whoosh** - Quick-deposit inventory items to your master chest with configurable categories
- **Restock** - Pull potions, ammo, food, and supplies from Link storage back to you, up to quantities you configure per category. Effect-based alchemy classification works with overhaul mods and player-brewed items
- **Whoosh & Restock** - Dump inventory and restock essentials in one action
- **Sell automation** - Items in your sell container are automatically sold for gold on sleep/wait
- **Vendor wholesale** - Register merchants for scheduled visits with better rates (requires Investor perk)
- **Summoned chest** - Access your master container from anywhere with a 2-minute conjured chest
- **LOTD integration** - Museum-needed items filter to a dedicated container (requires TCC)
- **SCIE integration** - Link containers work as crafting sources, SCIE containers can join Links
- **Follower storage** - [NFF](https://www.nexusmods.com/skyrimspecialedition/mods/55653) and [KWF](https://www.nexusmods.com/skyrimspecialedition/mods/2227) follower containers appear in the picker (auto-detected)
- **Link Presets** - Import ready-made Links from INI files. Ships with [Eli's Breezehome](https://www.nexusmods.com/skyrimspecialedition/mods/2829) preset. Mod authors can create presets for any player home
- **Container Lists** - Access containers from other cells in the picker. Ships with [LOTD](https://www.nexusmods.com/skyrimspecialedition/mods/11802) Safehouse (23 containers) and [General Stores](https://www.nexusmods.com/skyrimspecialedition/mods/4322) (27 containers). Enable/disable per list in MCM
- **Custom config menu** - Scaleform UI with drag-to-reorder filter priority
- **SkyUI MCM** - 7 configuration pages for settings, sales, compatibility, and maintenance
- **13 languages** - English plus 12 translations in optional Babel package

## How It Works

### The Context Power

SLID gives you one power: **SLID**. Cast it, and it reads the situation:

- **Looking at a container?** — Create a Link, add to a Link, set as sell, or manage it if already linked
- **Looking at your master?** — Open, Whoosh, Sort, Sweep, Restock, Configure, or Destroy Link
- **Looking at a vendor?** — Establish a wholesale arrangement (if eligible)
- **Looking at nothing?** — Access all your Links with network cycling, Detect, Summon, Whoosh, Restock

Destructive actions (Destroy Link, Remove) require holding the button for one second to confirm.

### Creating a Link

1. Find a container you want as your master chest
2. Cast **SLID** while looking at it and select **Create Link**
3. Name your Link (defaults to the cell name)
4. A config menu opens - assign filters to nearby containers
5. Sort reshuffles all items across the network based on your filter configuration

### Sorting Actions

- **Sort** - Reshuffle the entire network based on current filters
- **Sweep** - Reverse: pull everything from linked containers back to master
- **Whoosh** - Quick-deposit inventory items to master (configurable category selection)

### Restock

Pull items from Link storage back to you, up to quantities you configure per category:

- **~55 categories in 15 families** — Restore, Resist, Fortify, Cure, Utility potions, Poisons, Food, Drinks, Ammo, Soul Gems, Supplies
- **Smart classification** — Uses effect archetypes and ActorValues, not hardcoded form lists. Works with vanilla, overhaul mods, and player-brewed items
- **Per-category quantities** — Set each category individually (e.g., 5 Restore Health, 20 Arrows)
- **Quality-first** — Restocks strongest potions, highest-damage arrows, largest soul gems first
- **Whoosh & Restock** — Combined action to dump inventory and restock essentials in one button

### Selling Items

1. Cast **SLID** on a container and select **Set as Sell**
2. Drop items you want to sell into it
3. Sleep or wait - SLID automatically sells items and deposits gold

**Vendor Wholesale:** Register merchants for better rates. Requires the Investor perk and 5,000 gold. Vendors visit your sell container on their own schedule.

### Remote Access

Cast **SLID** at the air and select **Summon** to conjure your master container anywhere for 2 minutes with full access to Sort, Sweep, Whoosh, and Restock.

## Filter Families

| Family | Children |
|--------|----------|
| Weapons | Ranged, 1-Handed, 2-Handed, Staves |
| Armor | Light Armor, Heavy Armor, Shields, Clothing |
| Valuables | Rings, Amulets & Circlets, Gemstones, Soul Gems |
| Consumables | Potions, Poisons, Food Cooked, Food Raw |
| Books | Spell Tomes, Skill Books, Notes & Letters, Scrolls, Books Unread, Spell Tomes Unlearned |
| Crafting Materials | Ingots, Ores, Pelts & Hides, Leathers, Creature Parts, Ingredients, Building Materials |
| Enchanted Items | Unknown Enchantments, Enchanted Weapons, Enchanted Armor, Enchanted Valuables |
| Unique Items | Daedric Artifacts, Dragon Claws, Dragon Priest Masks, Guild Equipment, Unique Weapons, Unique Armors, Unique Valuables |
| Ammo | Arrows and bolts |
| Keys | All keys |
| Misc | Miscellaneous items |
| Museum Needed | LOTD museum items (requires [TCC](https://www.nexusmods.com/skyrimspecialedition/mods/38529)) |

## Requirements

- Skyrim SE/AE (1.5.97+)
- [SKSE64](https://skse.silverlock.org/) (matching your game version)
- [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) (for MCM)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

## Building from Source

See [docs/Build.md](docs/Build.md) for prerequisites and setup.

```cmd
cmake --preset release
cmake --build build/release --config Release
```

Output: `build/release/Release/SLID.dll`

## For Mod Authors

SLID supports custom filters, presets, container lists, and vendor whitelists via INI files. See [docs/ModAuthorGuide.md](docs/ModAuthorGuide.md) for:

- Trait reference for filter definitions
- Preset and container list authoring
- INI file format and naming conventions
- Plugin API for SKSE messaging integration

Example filter:

```ini
[Filter:your_items]
DisplayName = Your Mod Items
Description = Items added by Your Mod
RequireTrait = formlist:YourMod.esp|0x123ABC
Parent = misc
WhooshDefault = true
```

Example container list:

```ini
[ContainerList:My Storage]
RequirePlugin = MyMod.esp
Description = Storage containers from My Mod

[ContainerList:My Storage:Containers]
MyMod.esp|0xABC = Weapon Chest
MyMod.esp|0xDEF = Armor Chest
```

## Documentation

- [Build Guide](docs/Build.md) - Prerequisites, compilation, deployment
- [Mod Author Guide](docs/ModAuthorGuide.md) - Custom filters and API
- [Changelog](docs/CHANGELOG.md) - Version history

## License

MIT
