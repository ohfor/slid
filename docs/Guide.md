# SLID User Guide

This is the complete guide to **SLID — Skyrim Linked Item Distribution**: what the pieces are, how the sorting engine actually decides where each item goes, how to read the config menu, and what every action does.

If you just want to get sorting in five minutes, jump to [Setting up](#5-setting-up). If your filter is showing `0` items and you want to know why, read [How sorting works](#2-how-sorting-works) — that section is the heart of SLID.

---

## Contents

1. [Core concepts](#1-core-concepts)
2. [How sorting works](#2-how-sorting-works)
3. [Reading the config menu](#3-reading-the-config-menu)
4. [The actions](#4-the-actions)
5. [Setting up](#5-setting-up)
6. [The MCM](#6-the-mcm)
7. [Troubleshooting & FAQ](#7-troubleshooting--faq)

---

## 1. Core concepts

SLID has a small vocabulary. Learn these six words and everything else follows.

| Term | What it is |
|------|------------|
| **Link** | A named group of containers that work together. You can have as many Links as you like — one per house, one per playthrough phase, whatever suits. (Internally a Link is called a *network*; the words are interchangeable.) |
| **Master** | The one container per Link where you dump loot. Everything flows *out* of the master when you Sort, and back *into* it when you Sweep. |
| **Linked container** | Any other container you've added to the Link to receive sorted items — your weapon rack, alchemy shelf, gem safe, and so on. |
| **Filter** | A rule that recognises a kind of item ("Weapons", "Ingredients", "Soul Gems"). SLID ships with 52 filters in 12 families. Each filter can be pointed at a container. |
| **Family** | Filters are grouped into families. *Weapons* is a family; *Ranged*, *One-Handed*, *Two-Handed*, and *Staves* are its sub-filters. You can route a whole family to one place, or expand it and send each sub-type somewhere different. |
| **Catch-all** | The final filter. Anything no other filter claimed lands here — or stays in the master if you leave the catch-all unset. |

The single power **SLID** is your way in. Cast it while looking at a container, at your master, or at nothing at all, and it shows you the actions that make sense for what you're looking at. Everything in this guide is reached through that one power (and the SkyUI MCM for settings).

---

## 2. How sorting works

This is the part worth understanding properly. Once the model clicks, the config menu stops being mysterious and you can predict exactly where every item will end up.

### Priority and first-match-wins

A Link is an **ordered list of filters**, top to bottom. That order is the priority order, and you set it yourself by dragging rows in the config menu.

When SLID sorts, it takes each item and walks down the list:

> The **first filter that both matches the item and has a destination** claims it. The walk stops there. No lower filter gets a look at that item.

This is *first-match-wins*. It is the single rule that governs all sorting. Priority isn't a tie-breaker for edge cases — it is the whole mechanism.

**Worked example.** Suppose your list is, in order:

1. **Enchanted Weapons** → Display Case
2. **Weapons** → Weapon Rack

An enchanted sword matches *both* filters. Because Enchanted Weapons sits higher, it claims the sword first and the sword goes to the Display Case. The Weapons filter never sees it. Reverse the two rows and the same sword goes to the Weapon Rack instead, because now Weapons matches first. Nothing about the sword changed — only the priority did.

### The three destinations: Container, Keep, Pass

Every filter row has exactly one destination, chosen in the picker. There are three kinds:

| Destination | Meaning |
|-------------|---------|
| **A container** | Matched items are routed to that container. |
| **Keep** | The filter matches and *claims* the item, but the item stays in the master. Use Keep when you want certain items to remain in the master and to stop lower filters from grabbing them. |
| **Pass** | The filter is switched off. It claims nothing and is **invisible to the sort** — items it would have matched fall straight through to the filters below it. |

The distinction between **Keep** and **Pass** trips people up, so it's worth stating plainly:

- **Keep** is active. It says *"these items are mine, and mine is the master."* It blocks lower filters.
- **Pass** is inactive. It says *"ignore me entirely."* Lower filters are free to claim the items.

A row set to **Pass** behaves as if it weren't in the list at all.

### Contest — why a filter shows `0`

A filter showing `0` is ambiguous on its own. Does it match nothing in your inventory? Or does it match plenty, but everything it would claim is already being taken by a higher-priority filter? Those are very different situations, and SLID tells them apart with the **contest count**.

When the config menu predicts a sort, each row shows how many items it *would claim*. Alongside that, if a higher filter is scooping items this row also matches, you'll see an **amber `+N`** — the contest count. It means: *"N more items would have come here, but filters above me claimed them first."*

So:

- `0` with **no** amber number → this filter genuinely matches nothing in your pool. It's idle.
- `0` with an amber `+N` → this filter matches plenty, but everything is being claimed above it. It's *contested into silence*.

If a contested filter is one you care about, raise its priority (drag it above the filter stealing from it) and watch the amber number move into its real count. When you rest the cursor on a row, the rows above that are stealing from it are tinted, so you can see exactly who the competition is.

This is why the config menu insists on showing live numbers: every count answers one question — **"what happens if I press Sort right now?"**

### The catch-all

The **catch-all** is always the last row. It's the safety net: any item that reached the bottom of the list unclaimed lands in the catch-all's container.

- Point the catch-all at a "miscellaneous" chest and stray items collect there.
- Leave the catch-all on **Keep** (the default) and unclaimed items simply stay in the master.

The catch-all never uses **Pass** — an unset catch-all means Keep, not "switch off the safety net".

### What "Sort" actually does

Sort is more than a one-way push. When you Sort a Link, SLID runs three phases:

1. **Gather.** Every linked container is emptied back into the master. The Link is pooled.
2. **Pool.** SLID reads the master's full contents as the working set.
3. **Distribute.** The first-match-wins pipeline runs over that pool and moves each item to its destination.

This is why Sort *reshuffles the entire network* rather than only filing away what's new. If you change a filter's destination and Sort again, items already sitting in the old container are gathered back and re-routed to the new one. Sorting is idempotent: Sort twice in a row and the second pass moves nothing.

### When a container isn't available

Containers can become temporarily unreachable — a mod-added home that isn't loaded, a container that was removed. SLID handles this gracefully and never strands your items:

- A normal filter pointing at an unavailable container behaves as **Pass** for that sort (its items fall through to lower filters / the catch-all).
- The **catch-all** pointing at an unavailable container falls back to **Keep** (items stay in the master) rather than vanishing.

Nothing is ever moved into a container SLID can't reach.

---

## 3. Reading the config menu

Open the config menu by casting **SLID** at your master and choosing **Configure**. It's a custom interface built for exactly this job. The whole menu is a live answer to *"what happens if I Sort now?"*

**Each row is a filter** — a family, or a sub-filter when a family is expanded. From left to right a row shows:

- the filter's **name**;
- its current **destination** (a container name, **Keep**, or **Pass**), colour-coded so Keep and Pass stand out from real containers;
- the **count** it would claim on a Sort;
- an amber **`+N` contest count**, when higher filters are stealing matches from it (see [Contest](#contest--why-a-filter-shows-0)).

**The container picker.** Select a row's destination to open the picker. It lists **Keep** and **Pass** at the top, then every container SLID can offer for this Link — nearby containers, tagged containers, follower storage, preset and container-list entries, grouped under headings. Pick one and the row's count updates immediately.

**Families expand and collapse.** A family row (e.g. *Crafting Materials*) can be expanded to reveal its sub-filters (*Ingots*, *Ores*, *Ingredients*, …). Assign the family as a whole and everything in it follows that destination; expand it and you can send each sub-type to a different container. A collapsed family shows the **combined** count and contest of itself and all its children, so you don't have to expand it to see whether it's doing anything.

**Drag to reorder.** Lift a row and drag it up or down to change its priority. Because priority *is* the sort, the counts re-predict as you move — you're watching the consequences of the reorder before you commit it. (Gamepad reorder updates the predictions the same way.)

**The catch-all row** sits at the bottom and works like any other destination, except its options are **Keep** or a container — never Pass.

---

## 4. The actions

All of these are reached by casting the **SLID** power and choosing from the context menu. Which actions appear depends on what you're looking at.

### Sort

Reshuffle the whole Link. Gathers every linked container back to the master, then redistributes the entire pool by your filter priority. See [What "Sort" actually does](#what-sort-actually-does).

### Sweep

The reverse of a distribute: pull **everything** from the linked containers back into the master, and leave it there. Sweep is the "undo" for a layout — empty the network into one place so you can rethink it, or move house.

### Whoosh

Quick-deposit from **your own inventory** into the master. Configure which categories of item get whooshed (tap to run with your saved selection; **hold** to open the configurator). Whoosh is the "I'm home, take the loot" button — it fills the master, ready for a Sort.

> **Equipped gear is protected.** Anything you're currently wearing stays on you and is never whooshed away — including items from frameworks that equip gear in unusual ways (transmog and slotless-jewellery mods). You won't lose your rings.

### Restock

The mirror of Whoosh: pull supplies **from storage back to you**, up to quantities you set per category. Roughly 55 categories across 15 families — restore potions, resist and fortify potions, poisons, food and drink, arrows and bolts, soul gems, torches, firewood, and more.

- **Per-category limits.** "5 Restore Health, 3 Resist Fire, 20 Arrows" — set each individually. Restock tops you up to the target; it won't pull what you already carry.
- **Quality first.** It restocks the best items first — strongest potions, highest-damage arrows, largest soul gems.
- **Effect-based.** Items are classified by their magic effects, not by hardcoded lists, so Restock works with vanilla potions, alchemy overhauls, and items you've brewed yourself.

Tap to run with your saved loadout; **hold** to reconfigure. The first time you use Restock it opens the configurator so you can set your loadout.

### Whoosh & Restock

Both in one action: dump your inventory into the master, then pull your essentials back. One button to reset your loadout between adventures.

### Sell

Designate a container as your **sell chest** (cast SLID at it → **Set as Sell**), drop in what you don't want, then **sleep or wait**. SLID sells the contents automatically and deposits the gold. Review what happened in the **Sell Overview** popup, which keeps a transaction history.

> **Vendor wholesale.** Talk to a whitelisted merchant and choose the wholesale option to register them. Registered vendors visit your sell container on their own schedule and buy from it, and vendors you've invested in pay a 5% bonus. Requires the **Investor** perk and 5,000 gold. 39 vanilla vendors are whitelisted out of the box; mod authors can add more.

### Remote access — cast at nothing

Cast **SLID** with no target and you can operate a Link from wherever you stand — no need to travel to the container. Cycle to the Link you want, then choose any of:

- **Open** — opens the Link's master container directly, on the spot, so you can rummage through its contents from anywhere.
- **Whoosh**, **Restock**, **Whoosh & Restock** — deposit and pull supplies remotely.
- **Sort**, **Sweep**, **Configure**, **Detect** — the full suite, run from where you are.

Mid-dungeon and overloaded? Cast at nothing, Open or Whoosh, and carry on — no fast travel, no loading screens, and nothing conjured into the world.

### Detect

Cast **SLID** into empty air to highlight nearby containers by role:

| Colour | Meaning |
|--------|---------|
| **White** | Master container |
| **Blue** | Tagged or filter-assigned container |
| **Orange** | Sell container |
| **Green** | A persistent container available for use, not yet claimed by SLID |

Some mod-added containers won't glow because of how their meshes are built — they still work as SLID targets, they just don't light up.

---

## 5. Setting up

### Requirements

- Skyrim SE/AE (1.5.97 or later)
- [SKSE64](https://skse.silverlock.org/) matching your game version
- [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) (for the MCM)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)

Install with your mod manager. You're granted the **SLID** power automatically.

### Quick start

1. **Create the Link.** Cast **SLID** at the container you want as your master and choose **Create Link**. Name it (it defaults to the cell name).
2. **Add containers.** Walk to each container you want to use, cast **SLID** at it, and choose **Add to Link**. Give it a name if you like ("Weapon Rack", "Alchemy Shelf").
3. **Assign filters.** Cast **SLID** at your master → **Configure**. For each filter, pick the container that should receive it. Drag rows to set priority — higher filters claim items first.
4. **Sort.** Drop loot in the master, cast **SLID** at it → **Sort**. Items flow to where they belong.

### A shortcut: unlinked containers

Step 2 is optional. Turn on **Include Unlinked Containers** in *MCM → Settings* and nearby containers appear in the config menu's picker automatically — no need to add each one by hand first. Handy for filing into a room full of chests you've never tagged.

### Presets

A **Link Preset** is a ready-made configuration for a particular player home — containers tagged and filters assigned, all in one import. Open *MCM → Presets* to import one. SLID ships with presets for [Eli's Breezehome](https://www.nexusmods.com/skyrimspecialedition/mods/2829) and Leaf Rest, and mod authors can create presets for any home (see the [Mod Author Guide](ModAuthorGuide.md)).

### Multiple Links

Each Link has its own master, its own containers, and its own filter priority. Make a separate Link per house, or per phase of a playthrough. Switch between them from the *MCM → Link* page, or cycle Links directly in the context menu.

---

## 6. The MCM

Separate from the in-world config menu ([§3](#3-reading-the-config-menu)), the **MCM** is SLID's SkyUI settings interface — open it from the pause menu under *Mod Configuration → SLID*. It's where you manage Links, tune selling, and check compatibility, all without going in-world. Seven pages:

### Settings

General behaviour:

- **Include Unlinked Containers** — surface nearby untagged containers in the config-menu picker automatically (the shortcut described under [Setting up](#a-shortcut-unlinked-containers)).
- **Include SCIE Containers** — let [SCIE](#4-the-actions) containers appear in the picker. Available only when SCIE is installed.
- **Intercept Activation** — when on, *opening* a master or sell container pops a SLID action menu instead of the container itself. Off by default — the context power is the normal way in.

### Link

Manage and run your existing Links from the menu:

- **Select Link** — cycle between your Links. A Link whose master can't be found is marked unavailable and its actions are greyed.
- **Run Sort** / **Run Sweep** — run the action on the selected Link without travelling to it.
- **Destroy Link** — remove the selected Link. Containers and their contents are left untouched; only the Link itself is deleted.
- The right column lists the selected Link's containers (read-only).

### Presets

Import ready-made Links, export your own, and toggle container lists:

- **Your Links → Export** — write any of your Links out to a preset INI in your SLID user folder, ready to share or re-import elsewhere.
- **Your Presets** / **Presets** — activate a preset you exported, or one shipped by a mod. A preset whose required mod isn't installed is greyed and marked *Unavailable*.
- **Container Lists** — enable or disable each declared list (e.g. LOTD Safehouse, General Stores), controlling whether its containers show up in the picker.

See also the [Presets](#presets) note under Setting up for what a preset actually is.

### Sales Chest

Tune automatic selling and wholesale (see [Sell](#sell)):

- **General Sales Vendor** — sell price percentage, how many items clear per cycle, and how often sleeping or waiting triggers a sale. **Defaults** restores them.
- **Wholesale Arrangements** — your registered vendors, each showing their store, what they buy, and whether you've invested (for the price bonus).
- **Wholesale Vendor Settings** — price percentage, batch size, visit interval, and the gold cost to register a wholesale vendor.

### Compatibility

What SLID has detected on your load order:

- **SCIE** — detected or not, with the integration toggle.
- **Detected Mods** — e.g. TCC (Legacy of the Dragonborn); when present, the Museum Needed filter becomes available.

### Maintenance

- **Mod Enabled** / **Debug Logging** — the master switch and verbose logging (turn logging on only when chasing a problem).
- **Grant Powers** — re-grant the SLID power if it ever goes missing.
- **Show Welcome Guide** — re-show the first-time tutorial. **Font Test** opens the language/font preview.
- **Danger Zone → Reset All Data** — wipe every Link, tag, sell container, and vendor arrangement. Run this before uninstalling (see Uninstallation on the mod page).

### About

Version and author, links (Patreon, GitHub, ohfor.zone), and credits.

---

## 7. Troubleshooting & FAQ

**A filter shows `0` — is it broken?**
Check for an amber `+N` beside it. No amber number means it genuinely matches nothing in your current pool. An amber number means a higher-priority filter is claiming everything it would match — raise this filter's priority to take those items back. See [Contest](#contest--why-a-filter-shows-0).

**My items went to the wrong container.**
Priority is first-match-wins, top to bottom. The item matched a filter *above* the one you expected. Open Configure, find which higher row also matches that item (rest the cursor on the row you wanted — its thieves are tinted), and either reorder the rows or set the higher one to **Pass**.

**What's the difference between Keep and Pass again?**
**Keep** claims the item and holds it in the master, blocking lower filters. **Pass** switches the filter off entirely, letting lower filters claim the item. Keep is active; Pass is invisible.

**Where do items go if nothing matches?**
To the **catch-all** container, or — if the catch-all is set to Keep — they stay in the master. They're never lost.

**Does SLID move items automatically as I loot?**
No. Sorting is always something you trigger — Sort, Sweep, Whoosh, or Restock. This is deliberate, for performance and so items never move when you don't expect them to.

**Can I have more than one Link?**
Yes — as many as you like, each with its own master and filters. See [Multiple Links](#multiple-links).

**I assigned a container but it's not in the picker.**
Make sure it's reachable (in a loaded cell), or turn on **Include Unlinked Containers** in the MCM to surface nearby containers automatically. A container in an unloaded cell is treated as unavailable for that sort and skipped safely.

**I sorted twice and the second Sort moved nothing.**
That's expected. Sort is idempotent — once everything is in its correct place, a second Sort has nothing to do.

**How does Restock decide which potions to give me?**
By magic effect, not by name, so it works with any potion mod. It restocks the highest-quality items first and only tops you up to the per-category limit you set.

---

*For integrating your own mod with SLID — custom filters, presets, container lists, vendor whitelists — see the [Mod Author Guide](ModAuthorGuide.md). For the plugin messaging API, see [API.md](API.md).*
