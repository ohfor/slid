# Changelog

All notable changes to this project will be documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.4.4] - 2026-03-16

### Added

- **Detect highlights usable containers** — casting the context power into empty air (Detect) now highlights persistent, usable containers in the current cell with a green shader, in addition to the existing white (masters), blue (tagged/assigned), and orange (sell) highlights. Helps identify which containers are safe to use with SLID before setting up a Link. Note: some mod-added containers may not display the shader effect due to their mesh/material setup — they still function correctly as SLID targets
- **Non-persistent container safeguards** — SLID now blocks non-persistent (temporary) containers from being used as Link masters or filter targets. Non-persistent containers are evicted from memory when their cell unloads, which previously caused networks and settings to vanish on reload. Casting the context power on a non-persistent container shows a warning notification. The container picker no longer lists non-persistent containers. Networks created on non-persistent containers before this update are preserved but shown as "(unavailable)" in the MCM with an explanation, and can be safely destroyed

- **Keyword fallback for jewelry filters** — `armor` and `valuables` root filters now include `keyword:VendorItemJewelry` as a fallback. Mods like Mara that strip biped slots from rings/amulets at runtime no longer cause jewelry to misroute into armor — items land in valuables instead

### Fixed

- **Slot matching for modded armor** — `slot:ring`, `slot:amulet`, etc. now use contains-check (`HasPartOf`) instead of exact equality. Armor pieces with extra biped slots (e.g. a ring that also occupies the Hair slot for visual effects) now match correctly
- **Networks and filter assignments no longer destroyed on reload** — `ValidateNetworks()` previously deleted entire networks and cleared filter/catch-all container assignments when `LookupByID` returned null. This happened when the referenced container's cell was unloaded, which is normal for non-persistent refs. Now preserves all data and marks affected networks as unavailable instead. Matches user reports of networks vanishing on reload (backlog #27)
- Mouse wheel scrolling now works in the context menu's Open container submenu. Previously only keyboard arrows and gamepad d-pad could scroll the list — mouse wheel events were not wired up
- Mouse wheel scrolling now works in the Whoosh category configurator when the grid exceeds visible height

## [1.4.3] - 2026-03-12

### Fixed

- User settings (intercept activation, welcome tutorial, sale percentages, etc.) lost on mod update. Settings now persist in a separate override file that survives mod manager overwrites
- Welcome tutorial blocked context menu on every power use until "don't show again" was checked. Now shows once per session, permanently dismissed via checkbox
- MCM settings page could fail to initialize on existing saves due to version tracking issue

## [1.4.2] - 2026-03-12

### Added

- Exported preset INI files now include the sell container (if one was set). On import, the sell container is restored unless one is already configured — a notification toast informs the user if skipped
- Exported preset INI files now include usage instructions in the header comments (placement, MCM activation steps)

### Fixed

- CTD or silent failure when reopening config menu after browsing a container (hold-A on chest icon). The engine needs ~700ms of game ticks to complete container close animation cleanup; the previous 250ms fixed delay was insufficient when ConfigMenu (kPausesGame) froze those ticks. Now polls `ExtraOpenCloseActivateRef` until the engine signals cleanup is done, then polls NiControllerSequence animation state on the game thread until the container's Close animation reaches `kInactive`
- Context power not granted on new game start — `GrantPowers()` ran during SKSE's `kNewGame` message which fires before the game world is loaded. Player character not fully initialized, `AddSpell` silently failed. Now deferred to first `TESCellFullyLoadedEvent` when the player is in-world
- INI presets not visible in MCM on new game until full game restart — `LoadConfigFromINI()` ran too early during `kNewGame`. Same deferred initialization fix
- Config menu catch-all row showed false prediction delta after Sort/Sweep (e.g. "4614 > 4382"). The catch-all base count was set from live container data while the predicted count came from the pipeline simulation — same discrepancy previously fixed for filter rows. Base count now syncs to the pipeline prediction, and Sort/Sweep flash animations no longer overwrite it

## [1.4.1] - 2026-03-09

### Fixed

- CJK characters rendered as boxes when a Chinese/Japanese/Korean translation was loaded into a non-CJK language file (e.g. SLID_ENGLISH.txt with Chinese text). Font selection now auto-detects CJK content in the loaded translation strings and switches to Noto Sans CJK regardless of game language setting

## [1.4.0] - 2026-03-09

### Added

- **Open submenu** — The "Open" action in the context menu now expands a right-popout submenu listing all containers in the active network. Master container pinned at top with gold text. Linked containers show tag name, filter name, or base form name. Navigate with Enter/A to enter, UP/DOWN to select, Enter/A to open. Mouse hover auto-shows the submenu. Supports network cycling — submenu updates when switching networks. Works from master, linked, sell, and air contexts. Scrollbar for networks with 8+ containers
- **Container display names** — Linked containers show meaningful names on the HUD crosshair prompt and container UI title instead of generic "Chest". Master containers show "NetworkName: TagOrBaseName", sell containers show "Sell: TagOrBaseName", and tagged containers show their tag name. Names update on tag/rename/role changes and restore to base form name when a container is unlinked. Re-applied automatically on game load
- **Intercept Container Activation toggle** — MCM Settings toggle (`bInterceptActivation`, default OFF) gates the activation hook's master/sell container interception. When OFF, activating master or sell containers opens them normally — use the context power for SLID actions. When ON, the legacy MessageBox flow fires on activation. Vendor NPC interception stays unconditional regardless of toggle
- Context menu subtitle shows container identity when casting on a linked container — tag name if renamed, base form name otherwise. Combined with network name when available (e.g. "Breezehome — Armor Chest")
- Context menu subtitle now shows sell container identity (tag name or base form name) — previously the sell context had no subtitle
- Rename action added to master container context menu — was previously only available on linked and sell containers

### Fixed

- Create Link name suggestion defaulted to cell name even when a network with that name already existed (e.g. "Breezehome" suggested when "Breezehome" was taken). Now auto-increments to "Breezehome 2", "Breezehome 3", etc.
- Welcome tutorial now triggers on the very first context power cast instead of mid-flow during Create Link or Set Sell. Blocks the context menu from appearing until dismissed
- Whoosh deposited unrelated items (arrows, pickaxes, linen wraps, modded misc items) when filter families were partially checked. Family root filters — broad trait unions lacking FormType gates — were re-added to the effective filter set and matched items via COBJ-based traits (e.g. mod-added smelter breakdown recipes). Root filters now never participate in Whoosh matching; only explicitly checked child filters determine what gets deposited

## [1.3.1] - 2026-03-06

### Added

- **Noto Sans font** — Replaced Arial with Noto Sans for Latin/Cyrillic/Greek scripts and Noto Sans CJK SC for Chinese/Japanese/Korean. Language detected from `sLanguage:General` INI setting. Supports all 13 SLID translation languages natively
- **Font Test Menu** — New MCM option under Maintenance ("Localisation Support") opens a popup showing all 13 supported languages with native sample text, so users can verify font rendering in their setup

### Fixed

- Whoosh skipped unequipped items when multi-slot equip mods (extra ring/amulet slots) left stale `ExtraWorn` flags on inventory entries
- Whoosh ignored partially-checked filter families. Unchecking a child filter (e.g., "Unlearned Spell Tomes") had no effect when a sibling was still checked
- Config menu prediction counts showed wrong values when multiple filters shared a container, producing false delta arrows
- Config menu origin panel showed raw master item count instead of the predicted post-sort total

## [1.3.0] - 2026-03-02

### Added

- **Unified Context Power** — All 6 Lesser Powers replaced with a single "SLID" power. Casting determines context from crosshair target and shows a popup menu with relevant actions. Master containers offer Open/Whoosh/Sort/Sweep/Configure/Destroy Link. Sell containers offer Summary/Rename/Remove. Tagged containers offer Rename/Remove. Unknown containers offer Create Link/Add to Link/Set as Sell. Casting at nothing (air) with existing Links shows all operations with network cycling. Casting at nothing with no Links fires Detect directly
- **Hold-to-confirm actions** — Destructive actions (Destroy Link, Remove) require holding the confirm button for 1 second, preventing accidental activation. Whoosh supports tap-to-execute and hold-to-reconfigure (opens category config after holding)
- **Restock** — Pull items from Link storage back to player up to configured quantities. 15 category families with ~55 leaf categories covering Restore (Health/Magicka/Stamina/Vampire Blood/Rare), Resist (Fire/Frost/Shock/Magic/Poison/Disease), Fortify (Combat/Magic/Stealth/Attributes/Regen/Misc), Cure (Disease/Poison), Utility (Invisibility/Waterbreathing/Waterwalking/Become Ethereal), Poisons, Food, Drinks, Ammo (Arrows/Bolts), Soul Gems (Empty), and Supplies (Torches/Firewood). Effect-based automatic classification using archetypes, ActorValues, and effect flags — works with vanilla potions, alchemy overhaul mods (power-modifier AVs 60-163), and player-brewed items. Per-network configuration via RestockConfigMenu with family/child checkboxes and quantity spinners. Quality-first sorting for ammo (damage) and soul gems (capacity); natural order for alchemy items. Opt-in exception categories for Vampire Blood and Rare Restoratives (Welkynd Stones etc.) — disabled by default to prevent depleting rare items
- **Whoosh & Restock** — Combined context menu action that deposits inventory to master (Whoosh) then pulls configured loadout back (Restock) in a single operation
- **Restock context menu actions** — Restock appears in master and air context menus. Tap to execute (uses saved config), hold to reconfigure (green fill, opens RestockConfigMenu). First use always opens config with defaults
- **ChecklistGrid quantity spinners** — Grid items can optionally show editable quantity values with left/right adjustment. Used by RestockConfigMenu for per-category target counts. WhooshConfigMenu unaffected (no quantities)
- **Restock cosave persistence** — New `RSTK` cosave record preserves per-network restock configuration across saves. V1→V2 migration included (old saves auto-convert)
- **Restock preset export** — `GeneratePresetINI` now includes `[Preset:Name:Restock]` section with per-category quantities

### Improved

- **Shared ButtonBar component** — Extracted duplicated button drawing, hover, selection, hold-to-confirm, and flash code from ActionBar, RestockConfigMenu, WhooshConfigMenu, and ConfirmDialog into a single reusable `ButtonBar` class. Shared `ButtonColors` palette ensures consistent visuals across all menus. WhooshConfigMenu gains hold-to-confirm on Default and Clear buttons (matching RestockConfigMenu)
- **RestockConfigMenu visual overhaul** — Centered "SLID: Restock Configurator" title with gold accent line and soft outer glow (matching ContextMenu). Column headers "Restock Options" and "Restock Pad" above the two panels. Context-aware guide text with 8 states adapting to keyboard vs gamepad input. Pad labels show qualified category names (e.g., "Restore: Health" vs "Fortify: Attributes: Health") to disambiguate same-named children across families
- **Restock data model simplified** — Single `itemQuantities` map (leaf category → quantity) replaces the previous two-map design (`familyQuantities` + `enabledCategories`). Per-item quantities instead of per-family — each leaf category stores its own target count
- Detect now shows a breakdown of container sources (e.g. "5 linked. 30 General Stores. 23 LOTD Safehouse") instead of just a total count

### Changed

- Existing saves: old powers automatically removed, new unified power granted on load

### Fixed

- Config menu Sort action did not refresh prediction counts (Keep/Pass rows lost their item counts after sorting)

## [1.2.1] - 2026-02-27

### Fixed

- Powers (Create Link, Add Container, etc.) equipped to left/right hand like spells instead of the Power/Shout key (Z). SPEL records were missing the ETYP subrecord (`00025BEE` Voice equipment slot)
- Dropdown singleton pointer (`s_openInstance`) could become stale or dangling, potentially blocking all mouse and gamepad input in the config menu. Added destructor, move semantics, and unconditional pointer cleanup in Cancel/Destroy. `IsAnyOpen()` now validates the pointed-to dropdown is actually open
- Config menu mouse handler checked ActionBar hit test before checking for open dropdowns, allowing accidental button activation while a dropdown was visible
- Conjure Link Chest showed "Steal from Chest" prompt instead of the normal activation text when summoned inside merchant cells. Spawned chest now has player ownership set explicitly
- Conjure Link Chest could leave an orphaned permanent chest in the world if the player left the cell before the 2-minute timer expired. Added cell-detach event listener that despawns the chest and dispels the effect when the player exits the cell
- Conjure Link Chest could leave an orphaned chest in the save if the player saved and loaded while the chest was active. `Clear()` now attempts `Disable`/`SetDelete` on the tracked ref before resetting state

## [1.2.0] - 2026-02-21

### Added

- **INI Network Presets** — Entire Links (master + filter assignments + tags + whoosh config) can be declared in INI files as ready-to-use templates. Import via MCM Presets page
- **Eli's Breezehome preset** — Pre-configured Link for Elianora's Breezehome Overhaul with 20 containers mapped across all major filter categories (requires `Eli_Breezehome.esp`)
- **Container Lists** — INI `[ContainerList:*]` sections make named containers from other cells available in the SLID picker for any Link. Mod authors ship container lists alongside presets; SLID resolves FormIDs at load time and prunes when plugins are missing
- **Container list enable/disable toggles** — Per-save MCM toggles for individual container lists on the Presets page. Disabled lists are excluded from the picker
- **LOTD Safehouse container list** — 23 verified Safehouse and Quality Armory containers from Legacy of the Dragonborn (requires `LegacyoftheDragonborn.esm`)
- **General Stores container list** — 27 categorized cloud storage containers from General Stores, accessible from anywhere via the Storage Cistern (requires `GeneralStores.esm`)
- **Per-preset descriptions** — Presets and container lists support a `Description` field shown in MCM info text on highlight
- **Per-list section headers in container picker** — Container list entries grouped under their list name (e.g. "LOTD Safehouse", "General Stores") with non-selectable header rows
- **MCM Presets page** — Four sections: Your Links (per-link export), Your Presets (user-generated), Presets (mod-authored with Import/Unavailable status), and Container Lists (toggles)
- Importing a preset whose master container is already used by another Link shows a Replace/Cancel dialog
- Preset activation warnings: notices and conditional warnings shown before import
- Preset Keep/Pass keywords for mod authors writing INI presets

### Changed

- **Data INI files moved to subfolder** — Filter definitions, unique items, vendor whitelist, and preset INI files now live in `SKSE/Plugins/SLID/` instead of `SKSE/Plugins/`. The main settings file `SLID.ini` and DLL remain in `SKSE/Plugins/`. Users upgrading from v1.1.0 should reinstall the mod (mod managers handle this automatically)
- `SLID_LOTD.ini` renamed to `SLID_TCC_LOTD_Filter.ini` for clarity (it gates on The Curators Companion, not LOTD itself)
- **Mod Author MCM page renamed to Presets** — Consolidated preset listing and export onto a single "Presets" page with per-link export rows
- **"Activate" renamed to "Import"** — Presets are fire-and-forget templates. Importing creates a Link from the template; no ongoing "active" state
- MCM page order: Presets moved to position 3 (after Link)

### Fixed

- Ctrl+A in text input popup (container naming, Link naming) cleared the text and typed 'a' instead of selecting all
- MCM Link page container list showed duplicate entries when multiple filters pointed to the same container
- Crash on startup when other SKSE mods have non-ASCII characters in their INI filenames (e.g., Japanese, Korean, Cyrillic)
- Config menu hold-to-confirm buttons (Whoosh, Defaults) now respond to left mouse button in addition to gamepad A and Enter
- MCM closes cleanly before the preset naming popup appears (no more single-frame flash)

## [1.1.0] - 2026-02-19

### Added

- **Nether's Follower Framework integration** — NFF follower storage containers appear in the container picker and can be assigned to filters. Auto-detected when NFF is installed
- **Khajiit Will Follow integration** — Storage containers for all 4 KWF followers (Bikhai, Ma'kara, Nanak, S'ariq) available in the picker. Auto-detected when KWF is installed
- Container picker now groups entries: special routing, follower storage, tagged, SCIE, nearby containers

### Fixed

- **All Scaleform menus broken in v1.0.0** — SLIDConfig.swf was missing from the release package, breaking the config menu, tag input, whoosh config, sell overview, and welcome tutorial
- Sort and Sweep no longer touch unavailable containers (e.g., SCIE containers when SCIE integration is disabled)
- Predictions no longer show phantom item counts from unavailable containers

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
- 7 configuration pages: Settings, Link, Presets, Sales, Compatibility, Maintenance, About
- All settings persist immediately
- Vendor details on hover in Sales page

**Compatibility**
- SCIE (Skyrim Crafting Inventory Extender) integration option
- Legacy of the Dragonborn museum filter
- Mod author INI system for adding custom filters and vendors

### Technical

- 6 lesser powers auto-granted on game load
- Settings stored in `SKSE/Plugins/SLID.ini`
- Filter definitions in `SKSE/Plugins/SLID/SLID_Filters.ini` (user-extensible)
- 13 language translations available (English + 12 others in optional Babel package)
