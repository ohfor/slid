# SLID Mod Author Integration Guide

This guide explains how mod authors can integrate their mods with SLID (Skyrim Linked Item Distribution) to provide custom filters for item sorting.

## Overview

SLID automatically discovers INI files matching `*SLID*.ini` in the `Data/SKSE/Plugins/SLID/` folder. By shipping a simple INI file with your mod, you can:

- Add custom filters that appear in SLID's config menu
- Filter items by keyword, FormList membership, or other traits
- Gate filters behind your mod's ESP (only appears if your mod is installed)

No DLL required. No API calls. Just data.

---

## Quick Start

Create a file named `YourModName_SLID.ini` in `Data/SKSE/Plugins/SLID/`:

```ini
; YourModName_SLID.ini
[Filter:yourmod_special]
Enabled = true
Display = My Special Items
Description = Items tagged with YourMod's special keyword.
RequirePlugin = YourMod.esp
RequireTrait = keyword:YourMod_SpecialItemKW
DefaultExclude = false
```

That's it. When SLID loads, it discovers your filter and makes it available to users.

---

## Filter Definition Reference

### Section Header

```ini
[Filter:unique_id]
```

The `unique_id` should be unique across all mods. Use a prefix like `yourmod_` to avoid collisions.

### Required Fields

| Field | Description |
|-------|-------------|
| `Enabled` | `true` to enable the filter (set `false` to disable without removing) |
| `Display` | Display name shown in SLID's menus (keep it short) |
| `Description` | Longer description shown in guide text |

### Optional Fields

| Field | Description |
|-------|-------------|
| `RequirePlugin` | ESP/ESM/ESL filename. Filter only loads if this plugin is active. |
| `Parent` | ID of parent filter (for hierarchical grouping). |
| `DefaultExclude` | `true` to exclude from Whoosh by default. |

### Matching Traits

Use these fields to define what items match your filter:

| Field | Description |
|-------|-------------|
| `RequireTrait` | Item must match this trait to be included. |
| `ExcludeTrait` | Item must NOT match this trait. |
| `RequireAnyTrait` | Item must match at least one (pipe-separated list). |

---

## Trait Reference

Traits are predicates that test whether an item matches certain criteria.

### `keyword:EditorID`

Matches items with a specific keyword.

```ini
RequireTrait = keyword:VendorItemGem
RequireTrait = keyword:YourMod_SpecialKW
```

Keywords are looked up by EditorID at runtime. Works for vanilla and mod-added keywords.

### `formlist:EditorID@Plugin.esp`

Matches items that are members of a FormList.

```ini
RequireTrait = formlist:dbmMaster@DBM_RelicNotifications.esp
ExcludeTrait = formlist:dbmDisp@DBM_RelicNotifications.esp
```

The `@Plugin.esp` suffix specifies which plugin contains the FormList. Required because FormList EditorIDs can collide across mods.

### `formtype:Type`

Matches items by form type.

```ini
RequireTrait = formtype:Weapon
RequireTrait = formtype:Armor
RequireTrait = formtype:Potion
RequireTrait = formtype:Ingredient
RequireTrait = formtype:Book
RequireTrait = formtype:Misc
RequireTrait = formtype:Ammo
RequireTrait = formtype:Key
RequireTrait = formtype:SoulGem
RequireTrait = formtype:Scroll
```

### `weapon_type:Type`

Matches weapons by weapon type.

```ini
RequireTrait = weapon_type:sword
RequireTrait = weapon_type:dagger
RequireTrait = weapon_type:waraxe
RequireTrait = weapon_type:mace
RequireTrait = weapon_type:greatsword
RequireTrait = weapon_type:battleaxe
RequireTrait = weapon_type:warhammer
RequireTrait = weapon_type:bow
RequireTrait = weapon_type:crossbow
RequireTrait = weapon_type:staff
```

### `armor_weight:Type`

Matches armor by weight class.

```ini
RequireTrait = armor_weight:light
RequireTrait = armor_weight:heavy
RequireTrait = armor_weight:clothing
```

### `slot:SlotNumber`

Matches armor by equip slot.

```ini
RequireTrait = slot:32   ; Head
RequireTrait = slot:33   ; Hair
RequireTrait = slot:35   ; Amulet
RequireTrait = slot:36   ; Ring
RequireTrait = slot:37   ; Feet
```

### Combining Traits

You can combine traits for precise matching:

```ini
; Enchanted light armor (but not jewelry)
RequireTrait = armor_weight:light
RequireTrait = enchanted
ExcludeTrait = is_jewelry

; Weapons made of ebony OR daedric
RequireAnyTrait = keyword:WeapMaterialEbony|keyword:WeapMaterialDaedric
```

---

## Examples

### Example 1: Keyword-based Filter

Your mod adds a keyword `MyMod_Artifact` to special items:

```ini
; MyMod_SLID.ini
[Filter:mymod_artifacts]
Enabled = true
Display = MyMod Artifacts
Description = Powerful artifacts added by MyMod.
RequirePlugin = MyMod.esp
RequireTrait = keyword:MyMod_Artifact
DefaultExclude = true
```

### Example 2: FormList-based Filter

Your mod maintains a FormList of collectible items:

```ini
; MyMod_SLID.ini
[Filter:mymod_collectibles]
Enabled = true
Display = MyMod Collectibles
Description = Collectible items tracked by MyMod.
RequirePlugin = MyMod.esp
RequireTrait = formlist:MyModCollectiblesFL@MyMod.esp
DefaultExclude = true
```

### Example 3: Child Filter (Hierarchical)

Add a child filter under an existing SLID family:

```ini
; MyMod_SLID.ini
[Filter:mymod_weapons]
Enabled = true
Display = MyMod Weapons
Description = Custom weapons from MyMod.
RequirePlugin = MyMod.esp
Parent = weapons
RequireTrait = keyword:MyMod_WeaponKW
```

This filter appears as a child of the Weapons family in SLID's UI.

### Example 4: Complex Matching

Items that are weapons, have your keyword, but exclude staves:

```ini
[Filter:mymod_combat_weapons]
Enabled = true
Display = MyMod Combat Weapons
Description = Combat weapons (no staves) from MyMod.
RequirePlugin = MyMod.esp
RequireTrait = formtype:Weapon
RequireTrait = keyword:MyMod_WeaponKW
ExcludeTrait = weapon_type:staff
```

---

## Pre-configured Links (Presets)

The most powerful integration: ship a complete Link preset that users activate with one click. A preset declares a master container, filter assignments, container tags, and whoosh configuration — everything needed for a fully working Link.

### Generating a Preset from an Existing Link

The easiest way to create a preset is to configure a Link in-game, then export it:

1. Set up the Link manually — assign filters, tag containers, configure Whoosh
2. Open MCM → Mod Author page
3. Select your Link from the dropdown
4. Click "Generate Preset INI"
5. Find `SLID_GEN_{name}.ini` in `Data/SKSE/Plugins/SLID/`
6. Rename it to `YourMod_SLID.ini` and add a `RequirePlugin` line

The generated file contains all four preset sections with inline comments for readability.

### Preset INI Structure

```ini
[Preset:My Player Home]
RequirePlugin = MyMod.esp
Master = MyMod.esp|0x001234  ; Main Storage Chest

[Preset:My Player Home:Filters]
unique_items = Keep
enchanted_items = Keep
weapons = MyMod.esp|0x001235  ; Weapons Rack
armor = MyMod.esp|0x001236    ; Armor Chest
valuables = MyMod.esp|0x001237  ; Safe
CatchAll = Keep

[Preset:My Player Home:Tags]
MyMod.esp|0x001234 = Main Storage Chest
MyMod.esp|0x001235 = Weapons Rack
MyMod.esp|0x001236 = Armor Chest
MyMod.esp|0x001237 = Safe

[Preset:My Player Home:Whoosh]
weapons = true
armor = true
consumables = true
crafting_materials = true
```

- **RequirePlugin**: Preset is silently skipped if plugin isn't loaded
- **Filter values**: `Keep` (stays in master), `Pass` (filter skipped), or `Plugin.esp|0xLocalID`
- **Tags**: Display names for the container picker
- **Whoosh**: Family root IDs only (not children)
- **Filter order** in the INI determines pipeline priority (top = highest)

Users see the preset as "Available" on the MCM Link page and can activate it with one click.

### Simple Networks and Containers

For lighter integration, you can ship just a network or container tags:

#### Networks

```ini
[Network:MyMod Player Home]
Master = MyMod.esp|0x001234
```

Creates a network with the specified container as master. Users configure filters themselves.

#### Tagged Containers

```ini
[TaggedContainers]
MyMod.esp|0x001234|Main Storage = true
MyMod.esp|0x001235|Weapons Rack = true
MyMod.esp|0x001236|Alchemy Supplies = true
```

Gives containers friendly display names in SLID's picker.

---

## Container Lists

Container lists make named containers from your mod available in SLID's picker for **any** Link. Unlike presets, they don't create a network — they just make containers accessible. Users decide which filters to assign.

This is ideal for mods with organized storage in a different cell from the player's master (e.g., LOTD Safehouse containers that cell scan can't discover).

### Container List INI Structure

```ini
[ContainerList:LOTD Safehouse]
RequirePlugin = LegacyoftheDragonborn.esm
Description = Safehouse and Quality Armory containers from Legacy of the Dragonborn.

[ContainerList:LOTD Safehouse:Containers]
LegacyoftheDragonborn.esm|0x614C0E = Ingots and Ore
LegacyoftheDragonborn.esm|0x614C07 = Leathercraft
LegacyoftheDragonborn.esm|0x0AECD9 = Soul Gems
```

### Root Section

| Field | Description |
|-------|-------------|
| `RequirePlugin` | Plugin filename. Container list is silently skipped if not loaded. Multiple allowed. |
| `Description` | Shown in MCM info text when the user highlights the container list. |

### Containers Section

Each line is `key = value` where:
- **key** is a container reference (`Plugin.esp|0xLocalFormID`) — must be a persistent REFR, not a base CONT
- **value** is the display name override (shown in the picker instead of the base object name)

If the display name is empty, SLID falls back to the container's base object name.

### Behavior

- Containers appear in the picker alongside tagged, scanned, and follower containers, grouped under a section header matching the container list name
- Plugin-gated: containers only appear when `RequirePlugin` is installed
- Non-destructive: removing the INI removes containers from the picker. Any filter assignments that used those containers show as unavailable (greyed out)
- No cosave involvement: container lists are derived from INI at load time

### Example: Player Home Storage

```ini
; MyHome_SLID.ini
[ContainerList:My Awesome Home]
RequirePlugin = MyHome.esp
Description = Storage containers in My Awesome Home.

[ContainerList:My Awesome Home:Containers]
MyHome.esp|0x001234 = Weapon Rack
MyHome.esp|0x001235 = Armor Chest
MyHome.esp|0x001236 = Alchemy Supplies
MyHome.esp|0x001237 = Gem Safe
MyHome.esp|0x001238 = Pantry
```

---

## Best Practices

1. **Use a unique prefix** for your filter IDs (`yourmod_`) to avoid collisions.

2. **Always use `RequirePlugin`** so your filter only appears when your mod is installed.

3. **Keep Display names short** — they appear in dropdown menus.

4. **Use `DefaultExclude = true`** for filters containing valuable/unique items users probably don't want auto-sorted.

5. **Test with SLID's MCM** — the Mod Author page has a "Dump Filters" button that logs all loaded filters for debugging.

---

## Troubleshooting

**Filter doesn't appear:**
- Check the filename matches `*SLID*.ini`
- Verify `RequirePlugin` ESP is actually loaded
- Check SLID.log for parsing errors

**Wrong items matching:**
- Use SLID's Mod Author > Dump Filters to see how your filter was parsed
- Verify keyword/FormList EditorIDs are correct
- Check for typos in trait names

---

## Questions?

For integration support, open an issue at: https://github.com/ohfor/slid
