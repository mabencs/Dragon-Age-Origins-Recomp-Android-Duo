# Dragon Age Origins Recomp Android Duo

*Disclaimer, all the work done by "me" in this repo, was done by Claude.*

**Version 0.1 — Open Alpha** · Dragon Age: Origins for the Xbox 360, natively recompiled for
Android, made for the **AYN Thor** and its two screens.

Should also be playable on other handheld gaming devices and phones as there is an on-screen controller overlay available. They are however untested.

![The game on the AYN Thor's two screens: Orzammar's Royal Palace with the Dwarf Noble and Gorim on
the main screen, and the companion deck's Party tab - their portraits, health and stamina -
on the second](docs/images/thor-both-screens.jpg)

*The Dwarf Noble origin in Orzammar's Royal Palace on the main screen; the companion deck's
Party tab on the second.*

This is not an emulator. The game's PowerPC program is translated to C++ ahead of time
(static recompilation, built on [DAORecomp](https://github.com/96dot/Dragon-Age-Origins-Recomp)),
compiled for 64-bit ARM, and drawn through Xenia's Vulkan GPU backend. On the Thor it runs
as a native Android app, with the lower screen as a companion display.

> **Your own game.** Neither this repository nor the app holds any part of Dragon Age:
> Origins: no game files, no game code, no art. You give the app your own Xbox 360 discs,
> and it makes the game's program from your disc **on your device**, once, when you choose
> **Build** (about eight minutes on the Thor): the compiler it carries translates the disc's
> executable for this device. The game's pictures, sounds and words are read from your disc image as you play.
>
> Dragon Age: Origins belongs to BioWare and Electronic Arts. This project is not made,
> endorsed or supported by them.

## Contents

- [What it does](#what-it-does)
- [Status: 0.1 Open Alpha](#status-01-open-alpha)
- [What you need](#what-you-need)
- [Installing the app](#installing-the-app)
- [Setting up on the device](#setting-up-on-the-device)
- [Playing](#playing)
- [Settings reference](#settings-reference)
- [Recommended settings](#recommended-settings)
- [The second screen](#the-second-screen)
- [Saves](#saves)
- [Mods](#mods)
- [Debugging Mode and reporting bugs](#debugging-mode-and-reporting-bugs)
- [For developers](#for-developers) (and [building from source](#building-from-source))
- [Licence and credits](#licence-and-credits)

---

## What it does

- **The whole game.** The main campaign, plus Awakening and every DLC from the Ultimate
  Edition's second disc: Leliana's Song, The Golems of Amgarrak, Witch Hunt, The Darkspawn
  Chronicles, Return to Ostagar, Warden's Keep, The Stone Prisoner and the item packs.
- **Up to 120 fps** on the panel. The game's own logic runs as on the console, and the frame
  rate is a setting.
- **The PC recomp's picture options:**
  - FSR 3.1 upscaling, with FSR 3.1's own frame generation;
  - Lossless Scaling frame generation (through lsfg-vk, with your own `Lossless.dll`);
  - HDR10 and wide-gamut colour;
  - rendering resolutions from 960×540 to 2560×1440, field of view, and anisotropic
    filtering;
  - motion blur and cinematic depth of field.
- **A companion deck on the second screen.** It shows your party's portraits, health and
  mana, a quickbar you can tap to use, quests, the area map, tactics, the codex and journal,
  your items, and performance numbers.
- **Button prompts for the controller in your hands:** Xbox, Nintendo or PlayStation. Only the
  drawn labels change, never what the buttons do.
- **Saves you can name**, with their thumbnails. You can export saves to a .zip, import them
  from one, and restore saves the game wrote over. An optional autosave runs whenever you put
  the app away.
- **PC mods:**
  - import mods from .zip, .7z, .rar or .dazip files, or from a folder;
  - convert PC textures and add-ins to the Xbox 360 version's formats in the app;
  - turn mods on and off, and set their load order.
- **Photo mode**, an **on-screen touch controller**, the developers' own **cheats**, and a
  **Debugging Mode** that packs a bug report into one file.
- **Updates from the launcher.** Each time the launcher opens it asks GitHub whether a newer
  version exists, and can download and install it when you press the button. That is the
  app's only use of the internet: the game itself never connects to anything, and nothing
  about you or your game is sent. GitHub sees an ordinary request from your connection.

---

## Status: 0.1 Open Alpha

The game is playable from character creation onwards. Each piece below has been tested on
the device:
- character creation;
- the game's program made on the device from either title update (disc 2's 6, or 7);
- the Mage and Dwarf Noble origins, played by hand;
- Ostagar and the Korcari Wilds, with combat and spells;
- Orzammar;
- saving and loading;
- Awakening and the DLC campaigns, started from New Game;
- every one of the game's 118 areas, loaded through its own debug menus and profiled
  ([`docs/AREAS.md`](docs/AREAS.md)).

Nobody has played the whole campaign through on this port yet. Expect the rough edges of an
alpha.

**Tested on one device:** the AYN Thor:
- Snapdragon 8 Gen 2, Adreno 740 GPU;
- Android 13;
- a 1920×1080 main screen and a 1240×1080 second screen.

Other 64-bit Android 13+ devices with a Vulkan driver of that class may work, but none have
been tried. The companion deck needs a second display; without one the game plays on the one
screen.

**Performance on the Thor.** The GPU is the limit:
- measured uncapped with FSR 3.1 Performance, the 118 areas run at about 40–70 fps, most of
  them 45–60 ([`docs/AREAS.md`](docs/AREAS.md));
- the heaviest scene known, Ostagar's King's Camp, runs at 37 fps that way, and at 44 at
  the default settings;
- so the default 60 fps cap is held in the lighter scenes only.

FSR 3.1 and frame generation trade sharpness or latency for smoothness. See
[Recommended settings](#recommended-settings).

**Known issues**
- **First visits stutter.** The first time an area loads, its shaders are compiled, so the
  load is slower and there can be a hitch or two after arriving. Compiled shaders are kept,
  so later visits are smooth.
- **Rare freezes and crashes.** Very rarely the game's frames stop (BUG-042), or the process
  ends on a malformed GPU command (BUG-047). Save often; Debugging Mode's report helps.
- **Android can close the game in the background.** While the app is put away, Android may
  close it, and anything since the last save is lost. Turn on **Autosave when put away** to
  protect against it.
- **A short hitch at the start of conversations.** About 0.1 s; this is the game's own work
  for the first frame of a conversation.
- **Some CPU use while held.** The app keeps one CPU core about 11% busy even while put away
  (BUG-058).
- **Character limit.** The game itself limits how many characters can have saves; past that,
  New Game says "Character limit reached".

The full list, with causes, is [`docs/BUGS.md`](docs/BUGS.md).

---

## What you need

### Your game files

| File | Needed | What it is |
|---|---|---|
| **Dragon Age: Origins disc image** (.iso) | **Yes** | A full image of the Xbox 360 game disc, media ID **`186A1115`** (the Redump name: *Dragon Age - Origins (World) (En,Es,It)*, 7,838,695,424 bytes). Other presses of the game have other executables and **will not work**: the app checks the executable inside the image. |
| **Ultimate Edition disc 2** (.iso) | **Yes** (with Title Update 7 the game runs without it, but with no Awakening or DLC) | *Dragon Age: Origins - Ultimate Edition, Disc 2 (Additional Content Packs Install Disc)*. It carries the game's **Title Update 6**, which the app takes from it, and Awakening and every DLC, which the app installs (about 4 GB). |
| **Title Update 7** | Optional, recommended | The game's last title update, the file `TU_12K2260_0000004000000.00000000001O1` (3,506,176 bytes), from your console's storage once it has downloaded the update (`Content/0000000000000000/454108C0/000B0000/`). When it is there the game is made from it rather than from disc 2's Title Update 6. **Needed for bug reports** ([below](#debugging-mode-and-reporting-bugs)). |
| **`Lossless.dll`** | Optional | From your own copy of **Lossless Scaling** (a paid app on Steam), in its install folder: `<Steam>\steamapps\common\Lossless Scaling\Lossless.dll`. Needed only for the *Lossless* frame generation choices; the app reads the frame generation shaders from it. |

Dump the disc images from discs you own.

### A device

| | |
|---|---|
| **Android** | 13 or newer, 64-bit ARM (arm64-v8a) |
| **GPU** | Vulkan 1.1 or newer, of the Snapdragon 8 Gen 2 class (Adreno 740 tested) |
| **Free storage** | about 7 GB for the game, 4 GB more for Awakening and the DLC, and about 1 GB for the app and the game's program it makes |
| **Controller** | the handheld's own, or the on-screen controller |

No computer is needed: everything happens on the device.

---

## Installing the app

1. Download the APK from this repository's [**Releases**](https://github.com/mabencs/Dragon-Age-Origins-Recomp-Android-Duo/releases) page, on the device or onto it.
2. Open it, allowing installs from where you opened it when Android asks, and install.

**Updating:** install the new APK over the old one. Never uninstall the app to update:
uninstalling deletes the extracted game, the installed DLC and **your saves**. Export your
saves first if you ever do ([Saves](#saves)). After some updates the game's program must be
made again: Build then says **Build again**; choose it before launching.

---

## Setting up on the device

1. **Copy your files to the device.** Put the disc images and, if you have them, Title
   Update 7 and `Lossless.dll` anywhere Android's file picker can reach: the Download folder
   over USB, or an SD card.
2. **Open the app.** It starts on the launcher. Pick a row by touch, or with the D-pad and the
   confirm button.
3. **Disc image (.iso).** Choose your disc image in the file picker. The app checks it is the
   supported disc, then extracts the game's files into its own storage: 9,450 files, about
   6.9 GB, about eight minutes on the Thor, with the progress on the row. If the extraction
   is interrupted, it carries on where it stopped the next time the app starts.
4. **Ultimate Edition disc 2 (.iso).** Choose disc 2. Its Title Update 6 is put in place,
   and Awakening and the DLC are installed from it, about 4 GB; the row counts them (**11
   installed**). New Game then offers the campaigns.
5. **Title update 7 (optional, recommended).** Choose the `TU_12K2260_...` file if you have
   it; the row then says **In place**, and otherwise **disc 2's TU6 instead**.
6. **Lossless Scaling (Lossless.dll)** (optional). Choose the file; it is copied into the app's
   storage, for **Frame generation → Lossless 2x**.
7. **Build.** Once all your files are in, choose **Build**, the row under Launch. The app
   makes the game's program from your disc and the title update in place, once: about
   eight minutes on the Thor, with **building N%** on the row and **Done** when it is
   finished. Nothing is built until you choose it, so add Title Update 7 first if you have
   it: the program is made from the title update in place. Build is offered once the disc
   image is extracted and disc 2 is installed (or Title Update 7 chosen). Keep the device
   charging; you can put the app away meanwhile.
8. **Launch Dragon Age.**

If you choose another title update later, or an app update needs it, Build says **Build
again** and Launch waits until you choose it. If building fails, the row says **failed**
with the reason; choose Build to try again.

Once the game is extracted and the DLC installed, the app no longer needs the .iso files,
and you can delete them from the device; keep your own backups elsewhere.

On the first start, the game shows its notice about downloadable content. The storage
question the console would ask is answered by the app (the "hard drive"). Then you are at
the main menu.

---

## Playing

### Controls

- **The game.** The handheld's own controller, exactly as an Xbox 360 controller. The
  game's controls are the game's.
- **The options panel.** Press **Select + Start** together during play. Without the
  on-screen controller, a **three-finger tap** on the main screen opens it too; with it, the
  controller's own panel button does. The game pauses while the panel is open.
- **In the panel.** Up and down choose a row. Left, right, or the confirm button change its
  value. In the game, B, Back, Select or Start closes the panel; in the launcher, Start
  launches the game. By touch, tap a row, then the left or right half of its value.
- **Photo mode.** Press **L3 + R3** together, or use the panel's *Photo mode* row. The world
  pauses, the HUD hides and the camera is free:
  - the sticks move and turn the camera;
  - the triggers lower and raise it;
  - the D-pad zooms and rolls;
  - the confirm button saves a photo to `Download/Dragon Age Origins/`.
- **Putting the game away.** Home, locking the device or closing the lid pause the game where
  it is; both screens go home together. The game comes back where you left it. Android may
  still close it in the background; see **Autosave when put away**.

### Updating

When a newer version is out, a row appears under **Launch Dragon Age**. **Download & install**
fetches it, checks it against the checksum GitHub publishes, and hands it to Android, which asks
you to confirm; your saves, game data and settings stay. The first time, Android also asks you
to allow installs from this app. The app closes as it installs: open it again. Without a
connection there is simply no row, and the game plays as ever.

A row that says **Manual update** opens the release's page instead: that version has to be
installed by hand, and its notes say why.

### The launcher

Opening the app shows the launcher: the rows below, then every setting.

| Row | What it does |
|---|---|
| **Launch Dragon Age** | Starts the game with the settings below it. Ready once the disc image is extracted, a title update is in place and the game's program is built (**Build**, below). It also tells you if the last session ended badly (a crash, or Android closing the app). |
| **Build** | Makes the game's program from your disc and title update, once, when you choose it ([Setting up](#setting-up-on-the-device), step 7): **Start**, then **building N%**, then **Done**; **Build again** when you choose another title update, or an app update needs it. |
| **Disc image (.iso)** | Chooses your disc image and extracts it ([Setting up](#setting-up-on-the-device)). |
| **Ultimate Edition disc 2 (.iso)** | Installs Awakening and the DLC from disc 2, and puts its Title Update 6 in place. |
| **Title update 7 (optional, recommended)** | Copies in Title Update 7; the game's program is then made from it instead of disc 2's Title Update 6. |
| **Lossless Scaling (Lossless.dll)** | Copies in your `Lossless.dll` for Lossless frame generation. |
| **Saves - export, import, restore** | The [Saves](#saves) page. |
| **Mods** | The [Mods](#mods) page. |
| *(the settings)* | Every setting below; those marked \* take effect when the game starts. |
| **About and licences** | The version, what the app is, and every project built into it with its licence. |
| **Close** | Closes the app. |

In the game, the same panel (Select + Start) has the settings and these rows:

| Row | What it does |
|---|---|
| **Photo mode** | The free camera above. |
| **Exit and export save/logs** | Only with Debugging Mode on: makes a fresh save, ends the game, and puts a bug report in Downloads ([below](#debugging-mode-and-reporting-bugs)). |
| **About and licences** | As in the launcher. |
| **Quit the game** | Ends the game; progress since the last save is lost. |

---

## Settings reference

Settings marked **\*** take effect the next time the game starts. Changed while playing,
they wait for the next start; the panel shows a • after a value waiting like that. Every
other setting changes at once. The panel's last line under the list says the same, and each
setting's description in the panel matches the table.

| Setting | Choices (default in bold) | What it does |
|---|---|---|
| Frame rate | 30 fps · 45 fps · **60 fps** · 120 fps | *30 fps:* The game's own: each frame held for two of its 60 Hz vblanks. With frame generation, the game at 15 and a frame made after each.<br>*45 fps:* Each frame shown as soon as it is finished, the game held to 45 a second. With frame generation, the game at 22.5 and a frame made after each.<br>*60 fps:* A frame at every vblank where the GPU keeps up. With frame generation, the game at its own 30 and a frame made after each.<br>*120 fps:* Each frame shown as soon as it is finished, up to 120 a second where the GPU keeps up. With frame generation, the game at 60 and a frame made after each. |
| Upscaling | **Bilinear** · FXAA · FSR 1 · CAS | *Bilinear:* The game's 1280x720 stretched to the panel: the softest.<br>*FXAA:* Bilinear with the edges smoothed (NVIDIA FXAA 3.11).<br>*FSR 1:* AMD FidelityFX Super Resolution 1: the sharpest, a little GPU.<br>*CAS:* AMD FidelityFX CAS: stretched and sharpened. |
| Rendering resolution \* | 960x540 · **1280x720** · 1360x768 · 1600x900 · 1920x1080 · 2560x1440 | *960x540:* Rendered at 960x540 and scaled to the panel: the fastest, the softest (the King's Camp uncapped: 58.5 fps).<br>*1280x720:* The game's own size, scaled to the panel as Upscaling says (the King's Camp uncapped: 44.0 fps).<br>*1360x768:* Rendered at 1360x768, a little sharper (the King's Camp: about 43 fps).<br>*1600x900:* Rendered at 1600x900 (the King's Camp on the Thor: 29.4 fps).<br>*1920x1080:* Rendered at the panel's own 1920x1080: the sharpest picture the panel shows (the King's Camp on the Thor: 23.6 fps).<br>*2560x1440:* Rendered at 2560x1440 and scaled down to the panel: for faster devices (the King's Camp on the Thor: 15.1 fps). |
| Field of view | 80° · 85° · **91° (the game's)** · 95° · 100° · 105° · 110° · 115° · 120° | In play - exploring, fighting, the menus over the world. 91° is the game's own; lower is closer, higher shows more around you (the edges stretched a little). |
| Field of view in conversations | 80° · 85° · **91° (the game's)** · 95° · 100° · 105° · 110° · 115° · 120° | In conversations and cutscenes, each shot widened or narrowed by as much. 91° is the game's own; lower is closer, higher shows more around you (the edges stretched a little). |
| FSR 3.1 \* | **Off** · Quality · Balanced · Performance · Ultra performance | *Off:* The game's 1280x720, scaled to the panel as Upscaling says (the King's Camp: 31 fps).<br>*Quality:* Rendered at 1280x720, rebuilt to 1920x1080 from several frames: the sharpest, and the slowest (the King's Camp: 23 fps).<br>*Balanced:* Rendered at 1120x630, rebuilt to 1920x1080 (the King's Camp: 26 fps).<br>*Performance:* Rendered at 960x540, rebuilt to 1920x1080: the HUD a little softer (the King's Camp: 29 fps).<br>*Ultra performance:* Rendered at 640x360, rebuilt to 1920x1080: the fastest, the picture and the HUD noticeably softer (the King's Camp uncapped: 46.6 fps against Performance's 37). |
| Motion blur | **Off** · Low · High | *Off:* No motion blur.<br>*Low:* The picture blurred along the camera's motion, as a quarter of the frame's exposure would: turning and walking soften a little.<br>*High:* As Low, half the frame's exposure: a stronger blur. |
| Depth of field in conversations | **Off** · On | *Off:* Conversations and cutscenes as the game draws them.<br>*On:* In conversations and cutscenes the people in the shot sharp and the scene behind them blurred, as a film camera's lens does. |
| FSR 3.1 sharpening \* | **On** · Off | *On:* FSR 3.1's own sharpening of the rebuilt picture.<br>*Off:* Without it: a softer picture, about 2% more frame rate with FSR 3.1 on. |
| Frame generation \* | **Off** · Lossless 2x · Lossless 2x, balanced · Lossless 2x, fast · FSR 3.1 2x | *Off:* Only the game's own frames.<br>*Lossless 2x:* Lossless Scaling's frame generation: a frame made between each two of the game's, a little later; the Frame rate is then what the screen shows and the game runs at half of it (60: the game at its own 30). Needs Lossless.dll from your copy of Lossless Scaling in the files folder. Its quality mode, the motion found at full size: the dearest (the King's Camp with FSR 3.1 Performance: 14 ms of the GPU a frame, the game at 22 fps).<br>*Lossless 2x, balanced:* As Lossless 2x, the motion found at half the size: about 6 ms of the GPU a frame (the King's Camp with FSR 3.1 Performance: the game at 27 fps).<br>*Lossless 2x, fast:* As Lossless 2x, its performance mode: the motion found at half the size and a lighter network - about 3 ms (the King's Camp with FSR 3.1 Performance: the game at 29 fps), a little rougher around moving edges.<br>*FSR 3.1 2x:* FSR 3.1's own frame generation: the frame between made from the upscaled scene with the game's depth and motion, the HUD left as it is. Needs FSR 3.1 on (off without it). The King's Camp with FSR 3.1 Performance: the game at 26 fps, about 52 on the screen (Balanced 22, Quality 19). |
| Colour | **Accurate** · Vivid | *Accurate:* The game's own colours, in 10 bits.<br>*Vivid:* The game's colours stretched to the panel's wider Display-P3 gamut: more saturated than the game was made. |
| HDR | **Off** · On | *Off:* Standard dynamic range: the picture as the game made it.<br>*On:* HDR10: the game's highlights brightened toward the panel's peak, the rest at the paper white. Vivid colour applies too. |
| HDR paper white | 150 nits · **200 nits** · 250 nits · 300 nits | *150 nits:* With HDR on: white at 150 nits - dimmer, the most room for highlights.<br>*200 nits:* With HDR on: white at 200 nits, about the reference for graphics in HDR.<br>*250 nits:* With HDR on: white at 250 nits - brighter.<br>*300 nits:* With HDR on: white at 300 nits - the brightest, the least room for highlights. |
| Dynamic shadows | **On** · Off | *On:* The game's own.<br>*Off:* Faster, the most at 60 fps: at the King's Camp 31.6 -> 36.5 fps. |
| Post-processing | **On** · Off | *On:* The game's own.<br>*Off:* Faster, but darker and flatter. |
| Shader math \* | **Exact** · Fast | *Exact:* The Xbox 360's own arithmetic: zero times anything is zero.<br>*Fast:* The handheld GPU's own multiplication: much less work in the heaviest shaders (the King's Camp with Lossless 2x, balanced: 27 -> 30 fps, its cap; uncapped without frame generation 37 -> 42). Not exact - where the game multiplies zero by infinity, a pixel can come out wrong. |
| Texture filtering \* | The game's own · 4x · 8x · **16x** | *The game's own:* The game's own filtering: the ground blurs as it recedes.<br>*4x:* 4x anisotropic filtering.<br>*8x:* 8x anisotropic filtering.<br>*16x:* 16x anisotropic filtering: the receding ground stays sharp, at no measurable cost. |
| Button prompts \* | **Xbox** · Nintendo · PlayStation | *Xbox:* A at the bottom, B on the right.<br>*Nintendo:* B at the bottom, A on the right.<br>*PlayStation:* ✕ at the bottom, ○ on the right. |
| Intro logos \* | **Shown** · Skipped | *Shown:* The EA, BioWare and Edge of Reality logos as the game starts.<br>*Skipped:* Straight to the title screen. |
| Autosave when put away | **Off** · On | *Off:* Put away, the game holds where it is; if Android closes it in the background, progress since the last save is lost.<br>*On:* Put away while exploring, the game makes its own autosave first (about 2.5 s), then holds. |
| Heat guard | **Off** · On | *Off:* The frame rate stays as set, however warm the handheld gets.<br>*On:* The frame rate a step lower while the GPU is hot (90 C, or the system's own limit), back after a minute cooler; 30 in battery saver. |
| Headphones | **Stereo** · Surround | *Stereo:* The speakers' mixdown.<br>*Surround:* The game's five speakers placed around you - on headphones only. |
| Volume boost | **Off** · +6 dB · +12 dB · +18 dB | *Off:* The game's sound as it is mixed.<br>*+6 dB:* Louder; the loudest moments held just under full volume.<br>*+12 dB:* Much louder - for the handheld's speakers; the loudest moments held just under full volume.<br>*+18 dB:* Loudest; the louder moments of combat held down more often. |
| Second screen \* | **Companion deck** · Off | *Companion deck:* The lower screen shows the party, the quests and more from the game while you play - read-only, by touch.<br>*Off:* The lower screen is left to Android while you play. |
| Performance tab/overlay | **Off** · On | *Off:* No performance numbers: the second screen's Perf tab is hidden.<br>*On:* The game's and the screen's frames a second, frame time, 1% low, power, frame generation, upscaling and resolutions - on the second screen's Perf tab when it shows, otherwise at the top of the main screen. |
| Debugging Mode \* | **Off** · On | *Off:* The game logs only what it always does, to Android's own log.<br>*On:* From the next start the game records everything to a file, and the options in the game offer "Exit and export save/logs": one .zip in Downloads to send with a bug report. Uses more storage. |
| On-screen controller | **Off** · On | *Off:* Only the controller in your hands.<br>*On:* An Xbox controller over the game, played by touch: sticks, D-pad, A B X Y, bumpers, triggers, Back, Start and a button for this panel. Turning it on sets the Button prompts to Xbox. |
| Language \* | **English** · Español · Italiano | *English:* The game's text in English.<br>*Español:* El texto del juego en español (las voces, en inglés).<br>*Italiano:* Il testo del gioco in italiano (le voci in inglese). |
| Cheat: immortal party | **Off** · On | *Off:* The party takes damage as the game decides.<br>*On:* No one in the party can die (the developers' pc_immortal, for each member). Set again after loading a save. |
| Cheat: pass every skill check | **Off** · On | *Off:* Persuade, intimidate and the like as the game decides.<br>*On:* Every skill check in conversation passes (zz_override_skills). |
| Cheat: kill all hostiles | **Do it** | Every hostile creature in the area dies (killallhostiles). Press the confirm button or tap the name to do it. |
| Cheat: add gold | +1 gold · +10 gold · **+100 gold** · +1,000 gold | Choose the amount; the confirm button or a tap on the name gives it. |
| Cheat: add experience | +1,000 XP · **+5,000 XP** · +20,000 XP · +100,000 XP | Choose the amount; the confirm button or a tap on the name gives it. |

Some rows depend on the device or the moment:
- **HDR** and **HDR paper white** are offered only where the display takes HDR10 (the Thor's
  panel does).
- **Second screen** is offered only where there is a second display.
- The **cheats** work only in a game in progress (they are greyed in the launcher).
- **Motion blur** and **Depth of field in conversations** need **FSR 3.1** on.
- **Upscaling** and **Rendering resolution** are greyed while FSR 3.1 is on: FSR 3.1's
  profile picks the render size and does the upscaling.
- **Frame generation → FSR 3.1 2x** needs FSR 3.1 on.
- **Frame generation → Lossless** needs `Lossless.dll` in place.

---

## Recommended settings

The defaults are close to the console's picture:
- 60 fps where the GPU keeps up;
- the game's own 1280×720, stretched to the panel (Bilinear);
- FSR 3.1 and frame generation off;
- 16× texture filtering, which costs nothing measurable.

On the Thor, these are good starting points:

| You want | Settings |
|---|---|
| **The console's picture, steady** | Frame rate 30 fps; everything else default. |
| **Smoother motion** | Frame rate 60 fps · FSR 3.1 Performance · Frame generation Lossless 2x, balanced (needs `Lossless.dll`): the game at up to 30 fps and 60 on the screen (the King's Camp: the game at 27). Add Shader math Fast to hold 30 there. |
| **The sharpest picture** | FSR 3.1 Quality, or Rendering resolution 1920×1080 with FSR 3.1 off. Both cost frame rate: about 23 fps at the King's Camp. |
| **The longest battery** | Frame rate 30 fps · Dynamic shadows off · Heat guard on. |
| **Louder speakers** | Volume boost +12 dB. |

The **Performance tab/overlay** setting shows the frame rates, frame times and power drawn
while you try settings.

---

## The second screen

With **Second screen → Companion deck**, the lower screen shows the game's state while you
play. It is read-only, apart from the quickbar's taps, and it never takes the controller from
the game. Tap the tabs along the top.

| Tab | What it shows |
|---|---|
| **Party** | Each member's portrait, name, class, health, and mana or stamina, with level-up, poison and paralysis marked. |
| **Quickbar** | The selected character's quick slots, large enough for a thumb. A tap uses the slot, as its button would (in exploration and combat). |
| **Quests** | The journal's two pages, current and completed quests, in the game's groups. A tap unfolds a group, and a quest for its text. |
| **Map** | The area: the party, quest givers and the area's notes, north up. |
| **Tactics** | Each member's tactics slots: condition, command and target. |
| **Codex** | The codex entries you have found, by category: a tap unfolds a category, and an entry to read it. The journal's conversations below. |
| **Items** | What each member wears, the backpack, and the purse. A tap shows an item's stats, properties, requirements and text. **Equip**, **Use** and **Unequip** act for the character selected in the game (in exploration and combat). |
| **Perf** | With *Performance tab/overlay* on: frames a second, frame times, the 1% low, power, battery and the GPU. |

On AYN devices the game's sound follows the media volume through the app itself, because
the firmware does not apply it to the game's sound; Volume boost is applied first.

The deck runs in its own process, so a fault in it never stops the game. It leaves the second
screen when you put the game away, and comes back with it.

---

## Saves

- **Saving** works as in the game: from its pause menu, along with the game's own
  autosaves.
- **Naming a save.** After each manual save, the app asks for its name with the system
  keyboard ("Name this save"), the game's name already typed in. Type a new name and press
  OK, or press Cancel to keep the game's. Up to 40 characters; accented letters are dropped.
- **Thumbnails** show the moment of each save in the game's save lists.
- **Autosave when put away** (off by default; it's a setting). When the app is put away while
  you are exploring, the game first makes its own autosave (about 2.5 seconds), and only
  then pauses. Use it if Android tends to close the app in the background.
- **Safe writes.** Every save is written in full before it replaces the old one. If the app is
  closed mid-save, the previous save is kept as it was.

**The Saves page** (*Saves - export, import, restore* in the launcher):

- **Export all saves** or **Export one save** writes a .zip to `Download/Dragon Age Origins/`.
  Use it to keep a backup or move saves to another device.
- **Import saves from a .zip** takes a .zip made by Export, or one you made from save
  folders. Only this game's Xbox 360 saves are accepted; PC saves are not. If a save's slot is
  taken, you choose: **Keep both**, **Replace** (the old one is kept as a backup) or
  **Skip**.
- **Restore an earlier save** brings back a save the game wrote over. The app keeps the
  previous copy each time a save is overwritten.

The saves live in the app's own storage. **Uninstalling the app deletes them**, so export
them first.

---

## Mods

The app takes PC mods the way the PC recomp does. Each mod is a folder laid over the game's
files, in a load order you choose, and mods made for the PC version can be converted to the
Xbox 360 version's formats inside the app. Mods are managed from the launcher, before the
game starts, and they apply from the next start.

Bug reports from a game with mods loaded might not be investigated: check a bug with every
mod turned off before reporting it ([Reporting bugs](#debugging-mode-and-reporting-bugs)).

### Adding a mod

1. Copy the mod's file to the device: the .zip, .7z or .rar as downloaded (from Nexus Mods,
   for example), or a .dazip.
2. In the launcher, open **Mods** and choose **+ Add a mod from a file** (.zip, .7z, .rar,
   .dazip), or **+ Add a mod from a folder** for one that is already unpacked.
3. The app sorts it into place:
   - `packages/`, `core/` and `override/` folders go where they belong;
   - loose files go into `packages/core/override/<mod>/`;
   - read-mes stay with the mod;
   - a DAModder `Manifest.xml` becomes the mod's description.

   If the mod has optional parts, the app says so; read the mod's read-me to choose.
4. If the mod is listed **[PC version]**, select it and choose **Convert for this version**.
   This converts:
   - textures (.dds → the 360's .xds, with the smaller copies the game uses for distant
     models);
   - models and other GFF files, to the 360's byte order and layouts;
   - archives, which are unpacked.

   **Add-ins** (.dazip, or a mod's `addins` folder) are installed as downloadable content,
   which is how the Xbox 360 version loads them. The page lists what was converted and what
   could not be. The PC originals are kept in `mods/.originals/`.
5. Make sure the mod is **[on]**, then launch.

For each mod you can choose:
- **About this mod**: its name, version, author, description, file count and warnings, and
  other mods that change the same files;
- **Turn off** or **on**;
- **Move up** or **down**: lower in the list wins, where two mods change the same file;
- **Delete**.

A mod's add-ins go with it: turning it off hides them, and deleting it uninstalls them.
**Saves made with an add-in need it**, as on the PC.

The mods are in the app's storage at `Android/data/prod.daorecompandroid.duo/files/mods/`
(`dev.daorecompandroid.duo` for a debug build made from source), one folder each,
mirroring the game's folders.

### Which mods work

| Kind of mod | State |
|---|---|
| **PC texture mods** (.dds) | **Works** after **Convert** (tested). |
| **PC add-ins** (.dazip, or a mod's `addins/` folder): new items, scripts, plots, tables, talk tables | **Works** after **Convert**, installed as downloadable content (tested: **No Helmet Hack** 1.6, and 1.5a for Awakening). |
| **Files already in the Xbox 360 version's formats** (a patched .tlk, an .xds) | **Works** as they are (tested). |
| **PC models, meshes and other GFF files** (.mmh, .msh and the like) | **Should work** after **Convert** (the PC recomp's own converter, unchanged); not yet tested in a game. |
| **PC archives** (.erf, .rim) | **Should work**: unpacked and converted; not yet tested in a game. |
| **Add-ins with their own areas or campaigns** | **Should work**: the same mechanism the official DLC campaigns use; not yet tested. |
| **Game tables** (.gda) that replace a whole table | **Use with care**: converted like other GFF files, but a replaced table takes the place of the Xbox 360 version's own, which can differ from the PC's. |
| **Interface mods** (.gfx) and **sound mods** for the PC | **Unknown**: the Xbox 360 version has its own menus and sound banks; untested. |
| **Programs**: script extenders, .exe or .dll tools, patches to the game's executable | **No**. |

Tested in full: **No Helmet Hack** ([Nexus Mods, mod 202](https://www.nexusmods.com/dragonage/mods/202)).
Its rule book shows and hides helmets as described. Reports of other mods, working or not,
are welcome.

---

## Debugging Mode and reporting bugs

> **Reports are investigated only if the bug happens with Title Update 7 installed.**
> Before reporting, choose Title Update 7 in the launcher, choose **Build** again, and
> reproduce the bug on that build (with the Build row selected, the launcher says the
> game's program is made from Title Update 7). Bugs seen only with disc 2's Title Update 6
> are not looked into.
>
> **Reports from a game with mods loaded might not be investigated either.** A mod can
> change the game in ways the port cannot account for. Before reporting, turn every mod off
> on the [Mods](#mods) page and check that the bug still happens; say in the report if it
> happens only with a mod. The report and its log say which mods were loaded.

1. Turn on **Debugging Mode** in the settings (it takes effect when the game starts).
2. From then on, the game records everything to a file and keeps a copy of each save it
   loads.
3. When something goes wrong, open the panel and choose **Exit and export save/logs**. The
   app makes a fresh save, ends the game, and writes one .zip to `Download/Dragon Age Origins/`
   holding:
   - this run's log and the one before;
   - the save you loaded and a fresh one;
   - how earlier runs ended (crash reports);
   - a screenshot;
   - a summary (`readme.txt`): the app's version, the device, your settings, the title
     update the game's program was made from, and **which mods were loaded** (or that none
     were).

   The log says the same at each start: its `MODS LOADED` lines name every mod and add-in
   in use, or say `MODS LOADED: none`.
4. Attach the .zip to a bug report on this repository's [issue tracker](https://github.com/mabencs/Dragon-Age-Origins-Recomp-Android-Duo/issues), with what you were
   doing. It holds only what is listed above: the device's model and Android version in
   the log, the names of your mods, and in the saves your characters' names.

Turn Debugging Mode off again afterwards; it uses more storage.

---

## For developers

How the port works:
- The Xbox 360 executable, with Title Update 6 or 7 applied, is statically recompiled to
  C++ by XenonRecomp, using DAORecomp's function database and patches.
- The C++ is compiled for arm64 into the game's own library, `libdaogame.so` - on the
  player's device, by the app (`GameCode.java`, with clang and lld from LLVM 19.1.7 built for
  Android), or on a developer's computer with the NDK. It is never in the APK.
- Xenia's Vulkan backend is grafted onto DAORecomp's Xenos GPU frontend, through recorded,
  re-applicable edits (`tools/assemble_runtime.py`).
- The console's kernel and system calls the game makes are implemented by this project
  (`app/src/main/cpp/warden_*.cc`).
- The Android app (`app/src/main/java/`) is the launcher, the options panel, the deck and the
  pages.

| Document | What's in it |
|---|---|
| [`STATUS.md`](STATUS.md) | Where the work stands, verified facts, and the session log |
| [`RESUMING.md`](RESUMING.md) | Setting up a development machine, the device loop, and how work is done here |
| [`docs/PLAN.md`](docs/PLAN.md) | Every phase and task, with what was done and measured |
| [`docs/BUGS.md`](docs/BUGS.md) | Every bug found: ours, upstream's and the driver's, with what each one causes |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | How the pieces fit together, and why |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | Decisions taken, with their reasoning |
| [`docs/FEATURES.md`](docs/FEATURES.md) | The feature matrix |
| [`docs/AREAS.md`](docs/AREAS.md) | Every area loaded and profiled on the Thor |
| [`docs/SECOND-SCREEN.md`](docs/SECOND-SCREEN.md) | The companion deck's design |
| [`docs/GLYPHS.md`](docs/GLYPHS.md) | The button prompts' design |
| [`docs/ASSETS.md`](docs/ASSETS.md) | The discs, their formats, and the extraction |
| [`docs/IMPORTS.md`](docs/IMPORTS.md) | The kernel and system imports the game calls |
| [`docs/TESTING.md`](docs/TESTING.md) | The device loop, test routes, and how things were measured |
| [`docs/TOOLCHAIN.md`](docs/TOOLCHAIN.md) | The build environment |
| [`docs/SIGNING.md`](docs/SIGNING.md) | Debug and release signing |
| [`device/thor-profile.md`](device/thor-profile.md) | The AYN Thor, measured |
| [`THIRD_PARTY.md`](THIRD_PARTY.md) | Every upstream source, pinned to exact commits, with its licence |

### Building from source

The build runs on a Windows PC. It needs your disc image and Title Update 7 beside the
source, for the developer's own build of the game's program; the APK it makes holds none of
it. Exact versions and alternatives: [`docs/TOOLCHAIN.md`](docs/TOOLCHAIN.md).

```powershell
winget install Python.Python.3.14        # 3.11 or newer
winget install Git.Git
winget install Kitware.CMake
winget install Ninja-build.Ninja
winget install LLVM.LLVM                 # clang-cl, found in C:\Program Files\LLVM\bin
winget install EclipseAdoptium.Temurin.17.JDK
winget install Google.AndroidCLI         # the `android` command, for the SDK
android sdk install platforms/android-36 build-tools/36.1.0 cmake/3.31.6 ndk/28.2.13676358 platform-tools
```

Set `ANDROID_HOME`, `ANDROID_SDK_ROOT`, `ANDROID_NDK_HOME` and `JAVA_HOME` for your user (the
JDK's folder is whichever version winget installed), put `%ANDROID_HOME%\platform-tools` on
`PATH`, and open a new terminal. Then:

```bash
git clone <this repository's URL> DragonAgeOriginsAndroid
cd DragonAgeOriginsAndroid
# copy your disc image and the title update file into this folder (they are never committed)
python tools/verify_assets.py .
python tools/bootstrap.py --check                  # what is there and what is missing
python tools/bootstrap.py --accept-msvc-license    # everything, up to the APK
```

The bootstrap fetches the pinned third-party sources ([`THIRD_PARTY.md`](THIRD_PARTY.md)),
builds the recompiler and translates the game's program, assembles the Vulkan backend,
builds the device's compiler and XenonRecomp for Android (about 25 minutes the first time),
and builds the APK. `--accept-msvc-license` accepts Microsoft's licence for the Visual C++
runtime and Windows SDK headers the recompiler is built against. Each step is skipped when
its output is already there.

The debug APK is `app/build/outputs/apk/debug/app-debug.apk`; `python tools/push_gamecode.py`
puts the game's program built here on the device, so it need not be made there. A release
build, signed with a key of your own, and its checks are in [`docs/SIGNING.md`](docs/SIGNING.md).

Tools for looking inside the disc and its archives:

```bash
python tools/dao_iso.py info "<disc>.iso"                                   # media id, title id
python tools/dao_iso.py ls "<disc>.iso" packages/core/data                  # list the disc
python tools/dao_erf.py ls guiexport.erf --filter tutorial                  # list an ERF V2.1 archive
```

The Xbox 360 release uses **ERF V2.1** archives (zlib), which PC Dragon Age tools do not read.

---

## Licence and credits

This project is free software under the **GNU General Public License, version 3 or any
later version** ([`LICENSE`](LICENSE)). It compiles in lsfg-vk (GPL-3.0), so the app as a
whole is GPL-3.0. Everything else built in is under a licence that allows that: MIT, BSD,
ISC, Apache-2.0, BSL-1.0, LGPL-2.1, the Unlicense or public domain.

The app's **About and licences** page lists every project built into it, each with its full
licence text, and [`THIRD_PARTY.md`](THIRD_PARTY.md) has the same with the pinned versions.

| Project | Role | Licence |
|---|---|---|
| [96dot/Dragon-Age-Origins-Recomp](https://github.com/96dot/Dragon-Age-Origins-Recomp) (DAORecomp) | The recompilation's configuration, function database, game fixes, enhancements and mod converter: **what this port is built on** | MIT |
| [hedge-dev/XenonRecomp](https://github.com/hedge-dev/XenonRecomp) | PowerPC → C++ | MIT |
| [Xenia](https://github.com/xenia-canary/xenia-canary) | The Xenos GPU frontend and Vulkan backend, kernel parts | BSD 3-Clause |
| [SIMD Everywhere](https://github.com/simd-everywhere/simde) | The 360's vector instructions on ARM | MIT |
| [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk) | Frame generation with Lossless Scaling's shaders (from your own `Lossless.dll`) | GPL-3.0 |
| [AMD FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) | FSR 3.1 upscaling and frame generation | MIT |
| [FFmpeg](https://github.com/has207/FFmpeg) | The game's XMA audio | LGPL-2.1 |
| [libarchive](https://github.com/libarchive/libarchive), [7-Zip's C decoder](https://github.com/ip7z/7zip) | .rar and .7z mod imports | BSD 2-Clause, public domain |

| [LLVM](https://github.com/llvm/llvm-project) | clang and lld, which make the game's program on the device | Apache-2.0 WITH LLVM-exception |

…and the rest of the 28 in [`THIRD_PARTY.md`](THIRD_PARTY.md).

Dragon Age: Origins © Electronic Arts. Lossless Scaling, AMD FidelityFX and FSR belong to
their owners. This project is not affiliated with any of them.
