# Changelog

All notable changes to this project will be documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-02-17

### Added

**Storage Network**
- Create storage networks by designating a master container with the Create Link power
- Link containers to filters for automatic item routing
- Config menu for drag-and-drop filter priority reordering
- Sort operation reshuffles the entire network based on current filters
- Sweep operation gathers all items from linked containers back to master
- Whoosh operation quickly deposits inventory items to master (configurable per-network)
- Detect Containers power highlights your network with colored shaders

**52 Item Filters in 12 Families**
- Weapons: Ranged, 1-Handed, 2-Handed, Staves
- Armor: Light Armor, Heavy Armor, Shields, Clothing
- Valuables: Rings, Amulets & Circlets, Gemstones, Soul Gems
- Consumables: Potions, Poisons, Food Cooked, Food Raw
- Books: Spell Tomes, Skill Books, Notes & Letters, Scrolls, Books Unread, Spell Tomes Unlearned
- Crafting Materials: Ingots, Ores, Pelts & Hides, Leathers, Creature Parts, Ingredients, Building Materials
- Enchanted Items: Unknown Enchantments, Enchanted Weapons, Enchanted Armor, Enchanted Valuables
- Unique Items: Daedric Artifacts, Dragon Claws, Dragon Priest Masks, Guild Equipment, Unique Weapons, Unique Armors, Unique Valuables
- Plus: Ammo, Keys, Misc
- LOTD Integration: Museum Needed filter (requires TCC)

**Sell Container**
- Designate a sell container for automatic item sales
- Items placed in it are sold for gold when you sleep or wait
- Configurable sale price percentage and batch sizes
- Sell overview shows transaction history with timestamps

**Vendor Arrangements**
- Establish wholesale trade arrangements with merchant NPCs
- Requires Investor perk (Speech 70) and 5,000 gold investment
- Vendors visit on independent schedules to buy from your sell container
- 5% price bonus for vendors you've invested in via the vanilla perk
- 39 vanilla vendors supported; mod authors can add more via INI

**Summoned Chest**
- Summon a temporary chest anywhere for remote network access
- 2-minute duration with HUD countdown
- Full access to Sort, Sweep, and Whoosh operations

**Container Tagging**
- Name any container with the Add Container to Link power
- Tagged containers appear with custom names in the config menu
- Easier identification than generic "Chest" labels

**SkyUI MCM Integration**
- 7 configuration pages: Settings, Link, Sales, Compatibility, Maintenance, Mod Author, About
- All settings persist immediately
- Vendor details on hover in Sales page

**Compatibility**
- SCIE (Skyrim Crafting Inventory Extender) integration option
- Legacy of the Dragonborn museum filter
- Mod author INI system for adding custom filters and vendors

### Technical

- 6 lesser powers auto-granted on game load
- Settings stored in `SKSE/Plugins/SLID.ini`
- Filter definitions in `SKSE/Plugins/SLID_Filters.ini` (user-extensible)
- 13 language translations available (English + 12 others in optional Babel package)
