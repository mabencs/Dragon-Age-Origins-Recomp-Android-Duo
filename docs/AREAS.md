# The areas' tour (PLAN 10.11)

Every major area loaded through the game's own debug menus and profiled the same way,
uncapped (Options: Frame rate 120, Frame generation off, FSR 3.1 Performance, Shader math
Exact, texture filtering 16x, post-processing and shadows as the owner has them), on the
Thor's panel at 1080p. The method is in docs/TESTING.md ("The areas' tour"); the list is
`tools/areas/tour.txt` (the areas done commented out, so `tour.sh` goes on from the first
live line); the screenshots are
`build/areas/<label>-{arrival,walk1,walk2,turn1,turn2,walk3}.png` (never committed: game
imagery), with contact sheets `build/areas/sheet-*.png` from `tools/areas/sheets.py`.

**How to read the table:** `standing` is the frame rate over about 10 s on arrival,
`walk` while the left stick is held forward for 6 s, `turn`-`walk2` after a 4 s camera
turn, from the status lines' own 5 s counts; `busy` the GPU's busy percentage (99 means the
GPU is the limit); `talk` the number of presses it took to end an arrival conversation.
`File` is the last area file the game read during the probe, which can be a neighbour's
(the jump's own file is in the notes where it differs). The King's Camp (the heaviest
scene known before the tour) runs at 37 fps this way.

Done: session 38 (the night of 2026-10-31) toured the Circle, Lothering, the Dalish camp,
Denerim, Orzammar's city and the Urn's first four; the computer crashed at 01:40 and
session 39 wrapped it up. Session 40 (2026-11-01) fixed BUG-084 first, then toured the
rest of the main campaign (the prelude, the origins, Brecilian, the Deep Roads, Redcliffe,
the party camp, the epilogue - with a second pass for the menus the first could not read)
and all the downloadable content: the add-ins (Warden's Keep, Return to Ostagar, The
Stone Prisoner), Awakening, The Darkspawn Chronicles, Leliana's Song, The Golems of
Amgarrak and Witch Hunt - 118 areas in all. The tour's screenshots (over 700) stay
on the machine that took them, in `build/areas/`, out of the repository.

## Results

| Area | File | Standing | Walk | Walk 2 | GPU busy | Notes |
|---|---|---|---|---|---|---|
| cir-tower1 | `cir200ar_tower_level_1` | 54.4 | 55.0 | 67.8 | 98,99,99,99 | arrival talk (87 presses) |
| cir-tower2 | `cir210ar_tower_level_2` | 55.3 | 56.7 | 60.4 | 98,99,99,99 |  |
| cir-tower3 | `cir220ar_tower_level_3` | 67.1 | 67.8 | 56.0 | 99,99,99,99 |  |
| cir-tower4 | `cir230ar_tower_level_4` | 62.2 | 59.3 | 51.5 | 99,99,99,99 |  |
| cir-harrowing | `cir240ar_tower_harrowing` | 62.3 | 62.5 | 60.9 | 99,99,99,99 | Uldred's fight: the test character dies at once; the death screen measured (120 presses) |
| cir-lake-calenhad | `cir100ar_docks` | 57.6 | 62.7 | 57.6 | 99,99,99,99 |  |
| cir-fade-raw | `cir300ar_fade` | 55.9 | 55.1 | 54.1 | 99,99,99,99 |  |
| cir-fade-weisshaupt | `cir350ar_fade_weisshaupt` | 56.4 | 57.4 | 63.9 | 99,99,99,99 |  |
| cir-fade-burning-tower | `cir310ar_fade_burning_to` | 56.3 | 56.5 | 61.1 | 99,99,99,99 |  |
| cir-fade-mage-asunder | `cir330ar_fade_mage_asund` | 56.3 | 55.7 | 60.5 | 96,99,99,99 |  |
| cir-fade-templar-nightmare | `cir340ar_fade_templar_ni` | 45.6 | 46.5 | 62.9 | 99,99,99,99 |  |
| cir-fade-sloth | `cir360ar_sloth_demon` | 54.7 | 55.3 | 62.9 | 99,99,99,99 |  |
| lot-outdoors | `lot100ar_lothering` | 45.1 | 45.9 | 58.1 | 99,99,99,99 | arrival talk (62 presses); the party of four; the probe's last read was `pre200ar_korcari_wilds` |
| lot-chantry | `lot110ar_chantry` | 43.6 | 43.9 | 57.5 | 96,99,99,99 | light shafts through the windows |
| lot-danes-refuge | `lot120ar_danes_refuge` | 51.9 | 56.0 | 53.3 | 99,99,99,99 | arrival talk (119 presses) |
| ntb-dalish-camp | `ntb01al_dalish_camp` | 56.7 | 53.5 | 56.1 | 96,99,99,99 | Zathrian's conversation never ended (120 presses, the cap; its replies loop): the numbers and shots are its cameras, not explore |
| den-market | `den02al_den_market` | 44.5 | 47.7 | 69.4 | 99,99,99,99 |  |
| den-alienage | `den03al_den_elven_aliena` | 47.3 | 47.1 | 64.4 | 99,99,99,99 |  |
| den-fort-drakon | `den400ar_fort` | 62.7 | 62.2 | 63.4 | 99,99,99,99 | arrival talk (41 presses); the cell, the player stripped - the game's own capture scene |
| den-howe-estate | `den510ar_arl_estate` | 86.6 | 84.9 | 80.6 | 99,98,99,99 | **black: only the player and a few lit props (BUG-085)**; the rate is of nothing drawn. The jump's item was "Outside of the estate" |
| orz-entrance | `orz01al_orzammar_entranc` | 54.9 | 55.4 | 56.7 | 99,99,99,99 | the snowy pass |
| orz-commons | `orz02al_orzammar_commons` | 47.7 | 48.0 | 68.7 | 99,99,99,99 |  |
| orz-nobles-quarter | `orz03al_orzammar_nobles` | 58.2 | 57.4 | 61.4 | 99,99,99,99 | the first load died (BUG-084, 01:22); the second, after a reboot, loaded |
| orz-slums | `orz04al_orzammar_slums` | 49.7 | 51.4 | 55.4 | 99,99,99,99 | the first load died (BUG-084, 01:27); the second loaded |
| urn-cultist-village | `urn01al_ruined_village` | 49.9 | 71.5 | 55.6 | 99,99,99,99 | the last walk ended against a snowbank, the camera inside it (not a bug) |
| urn-ruined-temple | `urn200ar_ruined_temple` | 40.3 | 42.5 | 58.1 | 99,99,99,99 | the heaviest of the tour so far |
| urn-wyrmling-lair | `urn210ar_wyrmlings_lair` | 63.9 | 63.1 | 70.2 | 99,99,99,99 |  |
| urn-mountain-top | `urn220ar_mountain_top` | 61.2 | 60.0 | 64.0 | 98,99,99,99 | session 40 (BUG-084 fixed first): the bridge and the peaks |
| urn-gauntlet | `urn230ar_the_gauntlet` | 63.6 | 63.9 | 67.2 | 99,99,99,99 | the torch-lit hall, the spirit |
| pre-korcari-wilds | `pre200ar_korcari_wilds` | 45.9 | 46.3 | 58.4 | 99,99,99,99 | the party of four |
| pre-night-camp | `pre200ar_korcari_wilds` | 46.4 | 47.1 | 61.0 | 99,99,99,99 | "Start in the night version of the King's camp (after Wilds)" leaves the party at the Wilds' end - measured three times (46-47); the night camp proper was reached by the "Flemeth's hut (after Signal Tower)" item: `pre04al_kings_camp_night` 54.8 / 60.3 / 60.3, its own debug menu open on arrival |
| pre-ishal-level-3 | `pre430ar_tower_level_3` | 55.8 | 54.5 | 61.8 | 99,99,99,99 | the Tower of Ishal's third level |
| pre-kings-camp-day | `pre01al_kings_camp` | 37.5 | 39.3 | 52.5 | 99,99,99,99 | landed on by the night camp's menu ((MORE) -> 4 from its four-item menu): the camp's known 37 uncapped |
| camp-party-camp | `cam104ar_camp_arch1` | 52.7 | 53.1 | 56.8 | 99,99,99,99 | the party camp at night |
| bdn-intro | `bdn120ar_royal_palace` | 63.2 | 62.5 | 62.6 | 99,99,99,99 | Dwarf Noble: the royal palace; arrival talk (40 presses) |
| bdc-proving | `bdc02al_slums` | 43.8 | 49.2 | 61.1 | 99,99,99,99 | Dwarf Commoner: Dust Town (through "Entering the Proving" -> "jump to get Leske") |
| bhn-start | `bhn01al_castle_cousland` | 57.7 | 60.0 | 58.5 | 98,99,99,99 | Human Noble: Castle Cousland |
| bhn-siege | `bhn02al_castle_after_sie` | 67.1 | 65.3 | 65.3 | 99,99,99,99 | the castle during the siege |
| bec-home | `bec01al_alienage` | 70.3 | 69.9 | 70.1 | 99,99,99,99 | City Elf: the home; arrival talk (25 presses) |
| bec-alienage | `bec01al_alienage` | 44.0 | 45.4 | 55.5 | 99,99,99,99 | the alienage outdoors |
| bec-palace-exterior | `bec02al_estate_exterior` | 51.5 | 54.1 | 56.3 | 99,99,99,99 | the Arl of Denerim's estate at night, in the origin's own state: walls, towers, bushes all drawn (against BUG-085's black `den510ar_arl_estate`) |
| bec-palace | `bec03al_estate_interior` | 64.5 | 63.5 | 71.4 | 99,99,99,99 | the estate's interior |
| bed-ruins | `bed110ar_elven_ruins` | 60.9 | 59.3 | 70.7 | 99,99,99,99 | Dalish Elf: the elven ruins |
| bed-forest-clearing | `bed100ar_forest_clearing` | 54.8 | 57.1 | 61.6 | 99,99,95,99 |  |
| bed-dalish-camp | `bed200ar_dalish_camp` | 52.9 | 53.9 | 49.5 | 99,99,99,99 | arrival talk (86 presses) |
| ntb-forest | `ntb02al_brecilian_forest` | 46.5 | 45.3 | 57.2 | 99,99,99,99 | Brecilian forest |
| ntb-ruins-upper | `ntb310ar_top_level` | 70.7 | 70.1 | 64.6 | 99,99,99,99 | the ruins' upper level: the lightest of the tour |
| ntb-ruins-undead | `ntb330ar_lair_of_the_und` | 54.1 | 53.5 | 69.6 | 99,99,99,99 |  |
| ntb-ruins-werewolves | `ntb340ar_lair_of_werewol` | 61.4 | 61.2 | 63.0 | 99,99,99,99 | light shafts through the roof |
| orz-caridins-cross | `orz06al_orzammar_mines` | 49.7 | 49.3 | 63.6 | 99,99,99,99 | the Deep Roads' columned halls ("Where in?" -> Enter) |
| orz-ortan-thaig | `orz520ar_aeducan_thaig` | 63.1 | 63.7 | 66.9 | 99,99,99,99 | Ortan Thaig's "Where in?" -> Deeproad Outskirts (Dace): Aeducan Thaig (BUG-051's area, loads) |
| den-eamon-estate | `den07al_eamon` | 51.8 | 50.7 | 51.1 | 99,99,99,99 | Eamon's conversation never ended (120 presses, the cap): the dialogue's cameras |
| arl-plot-start | `arl100ar_redcliffe_villa` | 51.9 | 51.3 | 58.6 | 95,99,99,99 | Redcliffe village |
| arl-castle-point | `arl02al_redcliffe_castle` | 66.0 | 64.7 | 67.2 | 99,99,99,99 | Redcliffe castle (the dungeon) |
| epi-start | `epi300ar_post_coronation` | 45.7 | 48.5 | 63.3 | 98,99,99,99 | the epilogue's hall; arrival talk (89 presses) |

## The downloadable content (session 40)

The DLC modules keep their scripts as `.ncs` resources (ERF V2.0 archives, which
`tools/dao_erf.py` reads now), so their debug scripts are run through the engine's own
RunScript as the player's character (`files/route-call` holding `!zz_gwb_debug` and the
like; `warden_game.cc`). The add-ins of the main campaign - Warden's Keep, Return to
Ostagar, The Stone Prisoner - were toured in a game started on the `probe-dlc` route
(New Game with the DLC visible, the chooser's "Dragon Age: Origins"), the list
`tools/areas/tour-dlc.txt`. The probe's `File` column is empty for them: their area
files are read through another path than the main campaign's. Several arrivals are
fights the level-one test character loses, so those rows are measured during the fight
or on the death screen (said in the notes); the scripts never press on at a death screen
(its A is "Load Last Save").

| Area | Standing | Walk | Walk 2 | GPU busy | Notes |
|---|---|---|---|---|---|
| gwb-soldiers-peak | 48.5 | 48.1 | 60.7 | 94,98,99,94 | Warden's Keep: Soldier's Peak - snow, towers, the bridge; arrival talk (24 presses) |
| gwb-keep-first-floor | 56.1 | 55.5 | 53.9 | 92,98,99,97 | the keep's first floor |
| gwb-keep-second-floor | 65.5 | 66.5 | 59.8 | 96,98,99,94 | the keep's second floor |
| gwb-bridge | - | - | - | - | the player dies on arrival (its fight); skipped |
| gwb-soldiers-peak-repaired | 53.5 | 54.1 | 57.7 | 97,98,99,98 | Soldier's Peak, repaired |
| kco-offer-area | 52.5 | 52.7 | 52.6 | 95,99,99,99 | Return to Ostagar: the offer area; arrival talk (23 presses) |
| kcc-ostagar | 32.6 | 31.1 | 33.4 | 95,99,99,99 | Ostagar in the snow: renders; the character dies in the arrival fight, the numbers are the death screen's over the scene |
| kcc-battlefield | 36.3 | 36.4 | 37.0 | 98,99,99,99 | the battlefield: the same, measured in the fight and at the death screen |
| kcc-tower-of-ishal | 54.7 | 56.1 | 54.6 | 90,98,99,99 | the Tower of Ishal (Return to Ostagar's) |
| shl-honnleath | 45.1 | 45.0 | 61.2 | 97,98,96,98 | The Stone Prisoner: Honnleath |
| shl-cadash-thaig | 67.0 | 64.0 | 53.5 | 93,95,97,93 | Cadash Thaig |
| shl-more-1 | 63.2 | 61.9 | 64.8 | 83,97,92,95 | the warp list's MORE page, first item: the cellar (torch-lit) |
| shl-more-2 | 68.6 | 65.2 | 66.4 | 95,96,96,96 | the MORE page's second item: Wilhelm's house |

**Awakening** (the `probe-awakening` route: New Game, the chooser's second item, with
`gxa_module_start.ncs` hidden - the expansion's default start area, a dark stone hall -
then its ten modules' debug scripts; `tools/areas/tour-awakening.txt`, 28 areas loaded
of 33 lines):

| Area | File | Standing | Walk | Walk 2 | GPU busy | Notes |
|---|---|---|---|---|---|---|
| vgk-keep-proper | `vgk100ar_exterior` | 55.7 | 55.9 | 52.7 | 96,97,99,99 | Vigil's Keep's courtyard ("Where?" -> VGK100 Exterior); arrival talk (17 presses) |
| vgk-basements | `vgk210ar_throne_room` | 49.9 | 51.2 | 64.4 | 98,99,98,98 | the "VGK Basements" item's "Where?" landed in the throne room; arrival talk (47) |
| vgk-siege | - | - | - | - | - | "VGK Siege" asks a further choice; not reached |
| coa-amaranthine | `coa100ar_city` | 35.1 | 40.2 | 61.9 | 93,96,95,99 | the City of Amaranthine's gate: the heaviest Awakening scene standing |
| coa-inn | `coa110ar_inn` | 49.6 | 49.7 | 60.3 | 92,98,99,98 |  |
| coa-smugglers-cove | `coa130ar_smugglers_cove` | 62.3 | 62.7 | 72.4 | 94,97,96,97 |  |
| coa-chantry | `coa140ar_chantry` | 45.7 | 48.4 | 64.7 | 95,98,97,98 |  |
| coa-more-1 | `coa100ar_city` | 62.9 | 58.9 | 46.4 | 91,95,96,96 | the list's MORE page, item 1 (the city again, another spot) |
| coa-more-2 | `coa160ar_anders_warehous` | 63.2 | 61.2 | 65.7 | 92,96,97,98 | Anders' warehouse |
| coa-more-3 | `coa100ar_city` | 35.2 | 39.6 | 62.5 | 88,97,98,99 | the city again |
| aoa-amaranthine | (the city) | 64.2 | 57.1 | 32.7 | 99,99,96,97 | the siege module's Amaranthine |
| aoa-siege-stage-1 | `aoa100ar_city_siege` | 33.6 | 32.8 | 36.2 | 76,94,87,96 | the siege's first stage: the battle at the gate, measured in the fight (the character dies after) |
| aoa-siege-stage-2 | `aoa100ar_city_siege` | 39.9 | 39.8 | 38.9 | 92,97,94,96 | the second stage, in the fight |
| aoa-chantry-siege | `aoa140ar_chantry` | 41.2 | 45.4 | 63.2 | 97,95,98,99 | the chantry under siege |
| trp-wending-wood | `trp100ar_wending_wood` | 47.4 | 48.0 | 56.0 | 95,99,98,99 | arrival talk (17) |
| trp-silverite-mine | `trp200ar_silverite_mine` | 58.3 | 54.1 | 68.2 | 90,93,98,96 |  |
| stb-blackmarsh | `stb100ar_blackmarsh` | 43.4 | 41.4 | 58.6 | 99,95,99,92 |  |
| stb-blackmarsh-fade | `stb200ar_blackmarsh_fade` | 55.7 | 54.5 | 54.7 | 96,99,98,97 | the Fade's Blackmarsh (the character dies after) |
| stb-crypt-fade | `stb210ar_fade_crypt` | 47.2 | 45.9 | 65.0 | 97,99,99,99 | (dies after) |
| ltl-deep-road-entrance | `ltl100ar_deep_road_entra` | 44.4 | 44.9 | 63.3 | 97,99,98,97 |  |
| ltl-kal-hirol-entrance | `ltl200ar_kal_hirol_entra` | 42.6 | 45.8 | 65.5 | 99,97,99,99 |  |
| ltl-kal-hirol-upper | `ltl300ar_kal_hirol_upper` | 45.4 | 47.8 | 65.8 | 92,97,94,98 |  |
| ltl-kal-hirol-middle | `ltl400ar_kal_hirol_smith` | 41.5 | 40.7 | 56.9 | 82,97,98,98 | the smithy level; arrival talk (16) |
| ltl-kal-hirol-lower | `ltl500ar_kal_hirol_lower` | 59.9 | 55.8 | 59.2 | 97,99,99,99 |  |
| ltm-dragonbone-wastes | `ltm100ar_wastes` | 52.0 | 54.0 | 67.7 | 98,99,99,99 |  |
| ltm-drakes-fall | `ltm200ar_drakes_fall` | 53.3 | 51.1 | 67.7 | 94,97,97,98 |  |
| ltm-nest | `ltm300ar_nest` | 59.0 | 57.6 | 74.6 | 96,97,98,97 | the Mother's nest |
| rxa-* | - | - | - | - | - | the countryside's encounters (Beach, Canyon, Farm, Forest): each item sets up a travel and closes the menu with no load; not reached |
| int-keep-exterior | `int100ar_exterior` | 34.8 | 38.4 | 47.7 | 96,95,95,98 | the introduction's Vigil's Keep under attack: the siege battle |
| int-keep-interior | `int200ar_interior` | 48.9 | 52.6 | 45.8 | 88,99,90,97 | arrival talk (21) |

**The other campaigns** (each on its own route: `probe-drk`, `probe-lel`, `probe-gib`,
`probe-str` - New Game, the chooser's third to sixth items; the Golems and Witch Hunt open
on character creation, which `tools/areas/chargen_quick.sh` accepts with defaults and
Quick Play). The Darkspawn Chronicles' and the Golems' jump scripts are console scripts
that take the place as their value (`runscript zz_drk market`,
`runscript zz_gib_bdebug warp_gib100ar_smithy`), which `files/route-call` passes as
RUNSCRIPT_VAR (`!zz_drk market`); Leliana's Song and Witch Hunt have debug menus.
Lists: `tools/areas/tour-drk.txt`, `tour-lel.txt`, `tour-gib.txt`, `tour-str.txt`.

| Area | File | Standing | Walk | Walk 2 | GPU busy | Notes |
|---|---|---|---|---|---|---|
| drk-gates | `drk100ar_gates` | 48.6 | 48.5 | 62.0 | 99,99,99,99 | The Darkspawn Chronicles: Denerim's gates burning, the hurlock vanguard (the campaign's start) |
| drk-market | `drk200ar_market` | 43.0 | 49.8 | 68.4 | 99,99,99,99 | `zz_drk market` |
| drk-alienage | `drk300ar_alienage` | 43.9 | 45.3 | 62.3 | 95,99,99,99 |  |
| drk-palace | `drk400ar_palace` | 39.8 | 41.2 | 61.2 | 97,99,89,99 |  |
| drk-fort | `drk500ar_fort` | 55.3 | 54.2 | 62.8 | 98,98,99,99 | Fort Drakon |
| drk-fort-roof | `drk510ar_fort_roof` | 63.9 | 61.7 | 55.0 | 95,98,99,86 | the fort's roof |
| lel-estate-exterior | `lel200ar_estate_ext` | 59.5 | 56.0 | 58.6 | 99,99,99,99 | Leliana's Song (its own opening was the market); arrival talk (46) |
| lel-estate-interior | `lel210ar_estate_int` | 64.8 | 66.6 | 69.8 | 99,99,99,99 | arrival talk (29) |
| lel-hideout | `lel300ar_hideout` | 60.1 | 58.7 | 69.3 | 99,99,99,99 |  |
| lel-return-to-estate | (the estate) | 63.5 | 67.2 | 62.6 | 99,99,99,99 |  |
| lel-prison | `lel220ar_estate_dun` | 67.8 | 74.7 | 65.0 | 99,99,99,99 | the estate's dungeon |
| lel-chantry | `lel400ar_chantry` | 54.0 | 56.1 | 71.6 | 97,99,99,99 | arrival talk (73) |
| lel-shore-1 | `lel500ar_shore_1` | 53.9 | 57.8 | 57.8 | 99,99,99,99 | arrival talk (16) |
| lel-final-battle | - | - | - | - | - | "Before the final battle": the character dies on arrival and the retry set no load; not reached |
| gib-intro | `gib000ar_intro` | 63.3 | 62.4 | 58.2 | 99,99,99,95 | The Golems of Amgarrak: the campaign's start (Jerrik Dace's opening conversation loops on its first replies - its last, "Let's just move on", ends it) |
| gib-smithy | `gib100ar_smithy` | 40.9 | 43.9 | 54.5 | 83,96,92,99 | Amgarrak's smithy; `warp_finalfight` is a spot inside it (no load) |
| str-hut-exterior | `str100ar_hut_exterior` | 50.8 | 54.6 | 56.5 | 98,99,99,99 | Witch Hunt: Flemeth's hut, the dog in the party (the campaign's start) |
| str-elven-ruins | `str300ar_elven_ruins` | 59.1 | 59.3 | 60.2 | 99,99,99,99 | the Scrying Fight's mirror room is inside it (no load) |

## Visual bugs seen (to fix later)

- **BUG-085 - the Arl of Denerim's estate is black: the game's own.** The debug jump
  ("Jump to Arl of Denerim's Estate") places the player outside the estate's level -
  black, a row of doors floating, the player unable to move; DAORecomp's Windows build
  shows the very same with the same jump (session 40, `build/reference/howe2/`).
- **BUG-086 - Awakening's Smuggler's Cove is black on arrival: the game's own.** The
  player arrives at the tunnel's mouth facing out of the area, where nothing is modelled;
  the reference renders the same from the same save (session 40, `build/reference/cove2/`).
- Nothing else in the 118 areas looked at, the main campaign's and every DLC's: every
  contact sheet (`build/areas/sheet-*.png`) shows the scene, the HUD and the conversation
  wheel as they should be. The City Elf origin's own estate exterior (`bec02al`) draws
  fully, so BUG-085 is that one area file's (`den510ar`) state or content.
- BUG-084 (docs/BUGS.md): the native crash in the log's ring buffer struck three times in
  the night, each during a load - Flemeth's hut after a world-map travel (00:20),
  Orzammar's Noble's Quarter (01:22) and Slums (01:27) from the debug menu. The tour saw
  "still-loading", rebooted and the second try loaded. **Fixed in session 40** before the
  tour went on: a report of the port's longer than the logger's 64 KiB thread buffer was
  copied past the buffer's end (the length `fmt::format_to_n` reports is the whole
  line's); the logger clamps it now and the reports are logged in pieces.

## Performance discrepancies (to look at later)

- Uncapped, the King's Camp's 37 fps was the floor of the main campaign; the DLC goes
  lower in a few battles and open cities: Return to Ostagar's Ostagar and battlefield
  (31-36, measured on the death screen over the scene), Awakening's Amaranthine siege
  (33-40, in the fight), the introduction's Vigil's Keep under attack (35), Amaranthine's
  gate (35 standing, 40 walking). With the owner's settings (30 fps, Lossless 2x to 60)
  every area holds the game's 30 with a margin but those, which sit at it.
- The heaviest of the main campaign: the Urn's ruined temple (40 standing), Lothering's
  chantry (44), Denerim's market (44-48), Dust Town (44). The lightest: the estate that
  draws nothing (87), the Brecilian ruins' upper level (71), the City Elf's home (70).
- The second walk is often faster than the first (Denerim's market 48 -> 69, Orzammar's
  commons 48 -> 69): after the 4 s turn the camera faces away from the open view, so the
  numbers depend on the direction as much as on the area.

## Not reached, and what to do about it

Everything the debug menus could reach was toured by the end of session 40: the main
campaign's 58 areas, the add-ins' 13, Awakening's 28 and the four campaigns' 19. Left:

- **Plot-gated:** the prelude's signal tower and its top ("the red shirts are required";
  even from the night camp proper the item answered the same), Warden's Keep's bridge and
  Leliana's final battle (the test character dies on arrival), Return to Ostagar's
  Ostagar and battlefield (measured on the death screen).
- **Menus that route elsewhere:** Orzammar's Dead Trenches and Anvil of the Void (their
  "Where in?" pages land in Caridin's Cross and at Orzammar's gate), Vigil's Keep's siege
  (one more choice), Awakening's countryside encounters (they set up a world-map travel
  instead of loading), Haven's summon circle (its jump sets state in place), the Dwarf
  Commoner's "Beginning" and Witch Hunt's mirror room (both inside an area already toured).
- **No menu:** the climax (`zz_cli_debug` opens nothing outside its module) and the
  Golems' final fight (a spot in the smithy).
- To reach these, a save from a real playthrough near them is the way, not the debug menus.
