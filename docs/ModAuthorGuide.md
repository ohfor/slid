# SLID Mod Author Integration Guide

This guide explains how mod authors can integrate their mods with SLID (Skyrim Linked Item Distribution) to provide custom filters for item sorting.

## Overview

SLID automatically discovers INI files matching `*SLID*.ini` in the SKSE plugins folder. By shipping a simple INI file with your mod, you can:

- Add custom filters that appear in SLID's config menu
- Filter items by keyword, FormList membership, or other traits
- Gate filters behind your mod's ESP (only appears if your mod is installed)

No DLL required. No API calls. Just data.

---

## Quick Start

Create a file named `YourModName_SLID.ini` in `Data/SKSE/Plugins/`:

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

## Pre-configured Networks and Containers

Beyond filters, you can also ship pre-configured networks and tagged containers:

### Networks

```ini
[Network:MyMod Player Home]
Master = MyMod.esp|0x001234
```

Creates a network with the specified container as master. Users can then configure filters.

### Tagged Containers

```ini
[TaggedContainers]
MyMod.esp|0x001234|Main Storage = true
MyMod.esp|0x001235|Weapons Rack = true
MyMod.esp|0x001236|Alchemy Supplies = true
```

Gives containers friendly display names in SLID's picker.

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
