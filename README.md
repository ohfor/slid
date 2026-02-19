# SLID - Skyrim Linked Item Distribution

Intelligent storage management for Skyrim SE/AE. Create storage networks that automatically route items from a master container to designated destinations based on customizable filters.

## Features

- **Intelligent sorting** - Items automatically route to the right containers based on customizable filters
- **52 filters in 12 families** - Weapons, Armor, Valuables, Consumables, Books, Crafting Materials, Enchanted Items, Unique Items, and more
- **Whoosh** - Quick-deposit inventory items to your master chest with configurable categories
- **Sell automation** - Items in your sell container are automatically sold for gold on sleep/wait
- **Vendor wholesale** - Register merchants for scheduled visits with better rates (requires Investor perk)
- **Summoned chest** - Access your master container from anywhere with a 2-minute conjured chest
- **LOTD integration** - Museum-needed items filter to a dedicated container (requires TCC)
- **SCIE integration** - Link containers work as crafting sources, SCIE containers can join Links
- **Follower storage** - [NFF](https://www.nexusmods.com/skyrimspecialedition/mods/55653) and [KWF](https://www.nexusmods.com/skyrimspecialedition/mods/2227) follower containers appear in the picker (auto-detected)
- **Custom config menu** - Scaleform UI with drag-to-reorder filter priority
- **SkyUI MCM** - 7 configuration pages for settings, sales, compatibility, and maintenance
- **13 languages** - English plus 12 translations in optional Babel package

## How It Works

### Creating a Link

1. Find a container you want as your master chest
2. Cast **Create Link** while looking at it
3. Name your Link (defaults to the cell name)
4. A config menu opens - assign filters to nearby containers
5. Sort reshuffles all items across the network based on your filter configuration

### Sorting Actions

- **Sort** - Reshuffle the entire network based on current filters
- **Sweep** - Reverse: pull everything from linked containers back to master
- **Whoosh** - Quick-deposit inventory items to master (configurable category selection)

### Selling Items

1. Cast **Set Sell Container** on a container
2. Drop items you want to sell into it
3. Sleep or wait - SLID automatically sells items and deposits gold

**Vendor Wholesale:** Register merchants for better rates. Requires the Investor perk and 5,000 gold. Vendors visit your sell container on their own schedule.

### Remote Access

Cast **Conjure Link Chest** to summon your master container anywhere for 2 minutes with full access to Sort, Sweep, and Whoosh.

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

## Powers

SLID adds 6 lesser powers (auto-granted on game load):

- **Create Link** - Designate a container as Link master and open config menu
- **Add Container to Link** - Give a container a custom name for easy identification
- **Remove Container from Link** - Remove a container from your Link
- **Detect Linked Containers** - Highlight Link containers (white=master, blue=linked, orange=sell)
- **Set Sell Container** - Designate a container for automatic selling
- **Conjure Link Chest** - Summon your master container anywhere for 2 minutes

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

SLID supports custom filters and vendor whitelists via INI files. See [docs/ModAuthorGuide.md](docs/ModAuthorGuide.md) for:

- Trait reference for filter definitions
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

## Documentation

- [Build Guide](docs/Build.md) - Prerequisites, compilation, deployment
- [Mod Author Guide](docs/ModAuthorGuide.md) - Custom filters and API
- [Changelog](docs/CHANGELOG.md) - Version history

## License

MIT
