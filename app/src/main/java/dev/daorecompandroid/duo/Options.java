package dev.daorecompandroid.duo;

import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.Locale;

/**
 * The game's options (PLAN 7.7, Phase 9, 10.1): each one a file in the files directory
 * that the native side reads as the game starts. The live ones are also taken at once
 * when the options panel changes them ({@link NativeBridge#applyOption}); the others from
 * the next start.
 */
final class Options {

    private static final String TAG = "Warden";

    /** One option: its file, its title, and its values with what each is called. */
    static final class Option {
        final String file;
        final String title;
        final String[] values;
        /** The value as the panel's row shows it. */
        final String[] labels;
        /** A sentence on each value, under the panel's list. */
        final String[] details;
        /** The value without a file. */
        final String defaultValue;
        /** Taken while the game runs; otherwise from the next start. */
        final boolean live;

        Option(String file, String title, String defaultValue, boolean live,
               String[] values, String[] labels, String[] details) {
            this.file = file;
            this.title = title;
            this.defaultValue = defaultValue;
            this.live = live;
            this.values = values;
            this.labels = labels;
            this.details = details;
        }

        int indexOf(String value) {
            for (int i = 0; i < values.length; i++) {
                if (values[i].equals(value)) {
                    return i;
                }
            }
            return 0;
        }
    }

    static final Option[] ALL = {
        // PLAN 9.1, 10.1: the title's own 30; 60 at every vblank; 45 and 120 each frame
        // shown as it is finished, the title held to the rate (warden_frame_rate.cc).
        // 60 unless chosen otherwise (the owner, 10.1). PLAN 9.14: the rate on the
        // screen - with frame generation the game runs at half of it.
        new Option("frame-rate", "Frame rate", "60", true,
                new String[] {"30", "45", "60", "120"},
                new String[] {"30 fps", "45 fps", "60 fps", "120 fps"},
                new String[] {
                    "The game's own: each frame held for two of its 60 Hz vblanks. With"
                            + " frame generation, the game at 15 and a frame made after each.",
                    "Each frame shown as soon as it is finished, the game held to 45 a second."
                            + " With frame generation, the game at 22.5 and a frame made after each.",
                    "A frame at every vblank where the GPU keeps up. With frame generation,"
                            + " the game at its own 30 and a frame made after each.",
                    "Each frame shown as soon as it is finished, up to 120 a second where"
                            + " the GPU keeps up. With frame generation, the game at 60 and a frame"
                            + " made after each.",
                }),
        // PLAN 9.5, 9.7: how the title's 1280x720 reaches the panel (warden_gpu.cc,
        // ApplyPaintSettings).
        new Option("upscaling", "Upscaling", "bilinear", true,
                new String[] {"bilinear", "fxaa", "fsr", "cas"},
                new String[] {"Bilinear", "FXAA", "FSR 1", "CAS"},
                new String[] {
                    "The game's 1280x720 stretched to the panel: the softest.",
                    "Bilinear with the edges smoothed (NVIDIA FXAA 3.11).",
                    "AMD FidelityFX Super Resolution 1: the sharpest, a little GPU.",
                    "AMD FidelityFX CAS: stretched and sharpened.",
                }),
        // PLAN 9.16 (the owner, 2026-11-01): the size the game renders at without FSR 3.1,
        // from the next start, Upscaling filling the panel. Up to 1360x768 the game's device
        // is made at the size (warden_fsr3.cc's RenderSize); above it the game's own layout
        // of the EDRAM does not fit (its depth at 0 and its colour at 1024 in every pass), so
        // the device is made at half the size and Xenia renders every target at twice it
        // (RenderScale) - the pixels are the full size's. 1280x720, the game's own, stays
        // the default (the owner's rule: fidelity trades are options); the larger sizes
        // are for devices with more GPU than the Thor (the owner, 2026-11-01).
        new Option("resolution", "Rendering resolution", "1280x720", false,
                new String[] {"960x540", "1280x720", "1360x768", "1600x900", "1920x1080",
                    "2560x1440"},
                new String[] {"960x540", "1280x720", "1360x768", "1600x900", "1920x1080",
                    "2560x1440"},
                new String[] {
                    "Rendered at 960x540 and scaled to the panel: the fastest, the softest"
                            + " (the King's Camp uncapped: 58.5 fps).",
                    "The game's own size, scaled to the panel as Upscaling says (the King's"
                            + " Camp uncapped: 44.0 fps).",
                    "Rendered at 1360x768, a little sharper (the King's Camp: about 43 fps).",
                    "Rendered at 1600x900 (the King's Camp on the Thor: 29.4 fps).",
                    "Rendered at the panel's own 1920x1080: the sharpest picture the panel"
                            + " shows (the King's Camp on the Thor: 23.6 fps).",
                    "Rendered at 2560x1440 and scaled down to the panel: for faster devices"
                            + " (the King's Camp on the Thor: 15.1 fps).",
                }),
        // PLAN 9.17 (the owner picked it, 2026-11-01): the field of view, DAORecomp's way
        // (warden_camera_fov.cc) - the camera's angle scaled, the picture's proportions
        // kept. Horizontal degrees at 16:9; 91 is the game's own. Live.
        new Option("fov", "Field of view", "91", true,
                new String[] {"80", "85", "91", "95", "100", "105", "110", "115", "120"},
                new String[] {"80°", "85°", "91° (the game's)", "95°", "100°", "105°", "110°",
                    "115°", "120°"},
                fovDetails("In play - exploring, fighting, the menus over the world.")),
        new Option("fov-conversation", "Field of view in conversations", "91", true,
                new String[] {"80", "85", "91", "95", "100", "105", "110", "115", "120"},
                new String[] {"80°", "85°", "91° (the game's)", "95°", "100°", "105°", "110°",
                    "115°", "120°"},
                fovDetails("In conversations and cutscenes, each shot widened or narrowed"
                        + " by as much.")),
        // PLAN 9.4: AMD FidelityFX Super Resolution 3.1 - the game rendered at a size of
        // its own and rebuilt to the panel's 1080p from several frames, with the camera's
        // motion (warden_upscale.cc). The game's device is made at the size
        // (warden_game.cc), so from the next start. Off unless chosen.
        new Option("fsr3", "FSR 3.1", "off", false,
                new String[] {"off", "quality", "balanced", "performance", "ultra"},
                new String[] {"Off", "Quality", "Balanced", "Performance", "Ultra performance"},
                new String[] {
                    "The game's 1280x720, scaled to the panel as Upscaling says (the King's"
                            + " Camp: 31 fps).",
                    "Rendered at 1280x720, rebuilt to 1920x1080 from several frames: the"
                            + " sharpest, and the slowest (the King's Camp: 23 fps).",
                    "Rendered at 1120x630, rebuilt to 1920x1080 (the King's Camp: 26 fps).",
                    "Rendered at 960x540, rebuilt to 1920x1080: the HUD a little softer (the"
                            + " King's Camp: 29 fps).",
                    "Rendered at 640x360, rebuilt to 1920x1080: the fastest, the picture and"
                            + " the HUD noticeably softer (the King's Camp uncapped:"
                            + " 46.6 fps against Performance's 37).",
                }),
        // PLAN 9.18 (the owner picked it, 2026-11-01): motion blur, DAORecomp's - along the
        // camera's motion, from FSR 3.1's motion vectors (warden_upscale.cc), so only with
        // FSR 3.1 on. Off by default, as upstream. Live.
        new Option("motion-blur", "Motion blur", "off", true,
                new String[] {"off", "low", "high"},
                new String[] {"Off", "Low", "High"},
                new String[] {
                    "No motion blur.",
                    "The picture blurred along the camera's motion, as a quarter of the"
                            + " frame's exposure would: turning and walking soften a little.",
                    "As Low, half the frame's exposure: a stronger blur.",
                }),
        // PLAN 9.19 (the owner picked it, 2026-11-01): DAORecomp's cinematic depth of field
        // in conversations and cutscenes - the people in the shot sharp, the scene behind
        // them blurred - on FSR 3.1's upscaled scene, so only with FSR 3.1 on. Off by
        // default, the owner's decision (2026-11-01; about 2.1 ms a frame). Live.
        new Option("dof", "Depth of field in conversations", "off", true,
                new String[] {"off", "on"},
                new String[] {"Off", "On"},
                new String[] {
                    "Conversations and cutscenes as the game draws them.",
                    "In conversations and cutscenes the people in the shot sharp and the"
                            + " scene behind them blurred, as a film camera's lens does.",
                }),
        // PLAN 10.9: FSR 3.1's sharpening pass (RCAS) as a switch - about 0.5 ms of the GPU a
        // frame; on unless chosen (the owner's rule: speed that costs the picture is an
        // option).
        new Option("fsr3-sharpen", "FSR 3.1 sharpening", "on", false,
                new String[] {"on", "off"},
                new String[] {"On", "Off"},
                new String[] {
                    "FSR 3.1's own sharpening of the rebuilt picture.",
                    "Without it: a softer picture, about 2% more frame rate with FSR 3.1 on.",
                }),
        // PLAN 9.14: Lossless Scaling's frame generation (lsfg-vk on the game's device,
        // warden_frame_generation.cc) - a frame made between each two of the game's.
        // Needs the owner's Lossless.dll in the files folder; off without it. FSR 3.1's own
        // (warden_upscale.cc) needs FSR 3.1 on; off without it.
        new Option("frame-generation", "Frame generation", "off", false,
                new String[] {"off", "lossless", "lossless-balanced", "lossless-performance",
                    "fsr3"},
                new String[] {"Off", "Lossless 2x", "Lossless 2x, balanced", "Lossless 2x, fast",
                    "FSR 3.1 2x"},
                new String[] {
                    "Only the game's own frames.",
                    "Lossless Scaling's frame generation: a frame made between each two of"
                            + " the game's, a little later; the Frame rate is then what the"
                            + " screen shows and the game runs at half of it (60: the game at"
                            + " its own 30). Needs Lossless.dll from your copy of Lossless"
                            + " Scaling in the files folder. Its quality mode, the motion found"
                            + " at full size: the dearest (the King's Camp with FSR 3.1"
                            + " Performance: 14 ms of the GPU a frame, the game at 22 fps).",
                    "As Lossless 2x, the motion found at half the size: about 6 ms of the GPU"
                            + " a frame (the King's Camp with FSR 3.1 Performance: the game at"
                            + " 27 fps).",
                    "As Lossless 2x, its performance mode: the motion found at half the"
                            + " size and a lighter network - about 3 ms (the King's Camp with"
                            + " FSR 3.1 Performance: the game at 29 fps), a little rougher"
                            + " around moving edges.",
                    "FSR 3.1's own frame generation: the frame between made from the"
                            + " upscaled scene with the game's depth and motion, the HUD"
                            + " left as it is. Needs FSR 3.1 on (off without it). The"
                            + " King's Camp with FSR 3.1 Performance: the game at 26 fps,"
                            + " about 52 on the screen (Balanced 22, Quality 19).",
                }),
        // PLAN 9.3: the panel runs in Display-P3. Accurate keeps the game's own colours
        // (10-bit, converted by Android); Vivid stretches them to the panel's wider gamut
        // (warden_gpu.cc, warden_surface_format). Off unless chosen (the owner, 10.1).
        new Option("colour", "Colour", "accurate", true,
                new String[] {"accurate", "vivid"},
                new String[] {"Accurate", "Vivid"},
                new String[] {
                    "The game's own colours, in 10 bits.",
                    "The game's colours stretched to the panel's wider Display-P3 gamut:"
                            + " more saturated than the game was made.",
                }),
        // PLAN 9.2: HDR10 where the display takes it (Display.HdrCapabilities; the Thor's
        // panel: HDR10, 420 nits). The presenter's last pass encodes the game's picture
        // for it (warden_hdr_output.glsli): the highlights brightened toward the panel's
        // peak, white at the paper white below. Off unless chosen (the owner, 10.1).
        new Option("hdr", "HDR", "off", true,
                new String[] {"off", "on"},
                new String[] {"Off", "On"},
                new String[] {
                    "Standard dynamic range: the picture as the game made it.",
                    "HDR10: the game's highlights brightened toward the panel's peak, the"
                            + " rest at the paper white. Vivid colour applies too.",
                }),
        new Option("hdr-white", "HDR paper white", "200", true,
                new String[] {"150", "200", "250", "300"},
                new String[] {"150 nits", "200 nits", "250 nits", "300 nits"},
                new String[] {
                    "With HDR on: white at 150 nits - dimmer, the most room for highlights.",
                    "With HDR on: white at 200 nits, about the reference for graphics in HDR.",
                    "With HDR on: white at 250 nits - brighter.",
                    "With HDR on: white at 300 nits - the brightest, the least room for"
                            + " highlights.",
                }),
        // PLAN 9.8: the engine's dynamic shadows and post-processing - the PC version's
        // switches, left in the 360 engine (warden_game.cc). Off, frames are cheaper: at
        // the King's Camp at 60 fps, 31.6 -> 36.5 fps with the shadows off, 41.5 with both.
        new Option("shadows", "Dynamic shadows", "on", true,
                new String[] {"on", "off"},
                new String[] {"On", "Off"},
                new String[] {
                    "The game's own.",
                    "Faster, the most at 60 fps: at the King's Camp 31.6 -> 36.5 fps.",
                }),
        new Option("post-processing", "Post-processing", "on", true,
                new String[] {"on", "off"},
                new String[] {"On", "Off"},
                new String[] {
                    "The game's own.",
                    "Faster, but darker and flatter.",
                }),
        // PLAN 10.9: the Xenos rule that zero times anything is zero, or the GPU's own IEEE
        // multiplication (warden_gpu.cc) - about 40% of the heaviest shaders' arithmetic is
        // the rule. Exact unless chosen (the owner, 2026-10-31); from the next start.
        new Option("shader-math", "Shader math", "exact", false,
                new String[] {"exact", "fast"},
                new String[] {"Exact", "Fast"},
                new String[] {
                    "The Xbox 360's own arithmetic: zero times anything is zero.",
                    "The handheld GPU's own multiplication: much less work in the heaviest"
                            + " shaders (the King's Camp with Lossless 2x, balanced: 27 -> 30"
                            + " fps, its cap; uncapped without frame generation 37 -> 42). Not"
                            + " exact - where the game multiplies zero by infinity, a pixel can"
                            + " come out wrong.",
                }),
        // PLAN 9.6: anisotropic filtering for every mipmapped texture (warden_gpu.cc),
        // 16x unless chosen otherwise: free at the King's Camp, and the ground stays sharp.
        new Option("anisotropic", "Texture filtering", "16", false,
                new String[] {"off", "4", "8", "16"},
                new String[] {"The game's own", "4x", "8x", "16x"},
                new String[] {
                    "The game's own filtering: the ground blurs as it recedes.",
                    "4x anisotropic filtering.",
                    "8x anisotropic filtering.",
                    "16x anisotropic filtering: the receding ground stays sharp, at no"
                            + " measurable cost.",
                }),
        // Phase 7: which controller's labels the prompts show (warden_glyphs.cc); what
        // the buttons do never changes, only how they are drawn. The game takes them as it
        // starts - changed under it, its next load crashed (BUG-061).
        new Option("glyphs", "Button prompts", "xbox", false,
                new String[] {"xbox", "nintendo", "playstation"},
                new String[] {"Xbox", "Nintendo", "PlayStation"},
                new String[] {
                    "A at the bottom, B on the right.",
                    "B at the bottom, A on the right.",
                    "✕ at the bottom, ○ on the right.",
                }),
        // PLAN 9.11: the EA, BioWare and Edge of Reality logo movies at the start
        // (warden_fs.cc hides them; the title then skips them).
        new Option("skip_intro_logos", "Intro logos", "show", false,
                new String[] {"show", "skip"},
                new String[] {"Shown", "Skipped"},
                new String[] {
                    "The EA, BioWare and Edge of Reality logos as the game starts.",
                    "Straight to the title screen.",
                }),
        // PLAN 9.13: put away while exploring, the game makes its own autosave first
        // (DoAutoSave, warden_game.cc) and only then holds - against Android ending the
        // process in the background with everything since the last save. Off unless
        // chosen (the owner, 10.1).
        new Option("autosave-on-pause", "Autosave when put away", "off", true,
                new String[] {"off", "on"},
                new String[] {"Off", "On"},
                new String[] {
                    "Put away, the game holds where it is; if Android closes it in the"
                            + " background, progress since the last save is lost.",
                    "Put away while exploring, the game makes its own autosave first"
                            + " (about 2.5 s), then holds.",
                }),
        // PLAN 10.2: the heat guard (warden_thermal.cc) - the frame rate held a step lower
        // while the GPU is hot (the system's own cap on it, or 90 C) and back after a minute
        // cooler, and 30 in battery saver. Off unless chosen (the owner, 2026-10-31); the
        // Thor did not throttle in 14 minutes at the camp at the heaviest settings (run 475).
        new Option("thermal-guard", "Heat guard", "off", true,
                new String[] {"off", "on"},
                new String[] {"Off", "On"},
                new String[] {
                    "The frame rate stays as set, however warm the handheld gets.",
                    "The frame rate a step lower while the GPU is hot (90 C, or the"
                            + " system's own limit), back after a minute cooler; 30 in"
                            + " battery saver.",
                }),
        // PLAN 9.12: on headphones, the speakers' mixdown, or the game's five speakers
        // placed around the listener (warden_audio_mix.cc). The speakers always get the
        // mixdown. Stereo unless chosen (the owner, 10.1).
        new Option("headphones", "Headphones", "stereo", true,
                new String[] {"stereo", "surround"},
                new String[] {"Stereo", "Surround"},
                new String[] {
                    "The speakers' mixdown.",
                    "The game's five speakers placed around you - on headphones only.",
                }),
        // The owner (2026-11-02): the sound very low at the handheld's full volume - the
        // mix keeps the headroom combat's peaks need, so the rest of the game sits near
        // -20 dBFS. Louder, with the peaks limited (warden_audio_mix.h's Boost); a change
        // to the sound, so an option, off unless chosen (the owner's rule for fidelity).
        new Option("volume-boost", "Volume boost", "off", true,
                new String[] {"off", "6", "12", "18"},
                new String[] {"Off", "+6 dB", "+12 dB", "+18 dB"},
                new String[] {
                    "The game's sound as it is mixed.",
                    "Louder; the loudest moments held just under full volume.",
                    "Much louder - for the handheld's speakers; the loudest moments held"
                            + " just under full volume.",
                    "Loudest; the louder moments of combat held down more often.",
                }),
        // PLAN 8: the companion deck on the handheld's second screen while the game runs
        // (DeckActivity) - the party, the quests and the rest, read-only. Offered where
        // there is a second display; on unless chosen otherwise.
        new Option("deck", "Second screen", "on", false,
                new String[] {"on", "off"},
                new String[] {"Companion deck", "Off"},
                new String[] {
                    "The lower screen shows the party, the quests and more from the game"
                            + " while you play - read-only, by touch.",
                    "The lower screen is left to Android while you play.",
                }),
        // The owner, 2026-11-01: the performance numbers - frames a second, frame time,
        // 1% low, power drawn (PerfStats) - on the second screen's Perf tab while the deck
        // shows, else as an overlay on the main screen; off hides both. Taken at once
        // (GameActivity's overlay and the deck read it twice a second).
        new Option("perf", "Performance tab/overlay", "off", true,
                new String[] {"off", "on"},
                new String[] {"Off", "On"},
                new String[] {
                    "No performance numbers: the second screen's Perf tab is hidden.",
                    "The game's and the screen's frames a second, frame time, 1% low, power,"
                            + " frame generation, upscaling and resolutions - on the second"
                            + " screen's Perf tab when it shows, otherwise at the top of the"
                            + " main screen.",
                }),
        // PLAN 11.1: Debugging Mode (DebugReport, warden_debug_mode.cc) - the next run's
        // whole log to a file, the save it loads kept, and in the game a row that exports
        // a report to Downloads. Off by default; from the next start.
        new Option(DebugReport.OPTION, "Debugging Mode", "off", false,
                new String[] {"off", "on"},
                new String[] {"Off", "On"},
                new String[] {
                    "The game logs only what it always does, to Android's own log.",
                    "From the next start the game records everything to a file, and the"
                            + " options in the game offer \"Exit and export save/logs\": one"
                            + " .zip in Downloads to send with a bug report. Uses more storage.",
                }),
        // PLAN 10.17 (the owner, 2026-11-01): an Xbox controller drawn over the game and
        // played by touch, merged into the first pad (TouchControls, warden_input.cc).
        // Taken at once. Turned on, the Button prompts become Xbox's too (OptionsPanel):
        // the buttons on the screen are Xbox's.
        new Option("touch-controls", "On-screen controller", "off", true,
                new String[] {"off", "on"},
                new String[] {"Off", "On"},
                new String[] {
                    "Only the controller in your hands.",
                    "An Xbox controller over the game, played by touch: sticks, D-pad,"
                            + " A B X Y, bumpers, triggers, Back, Start and a button for this"
                            + " panel. Turning it on sets the Button prompts to Xbox.",
                }),
        // PLAN 10.16 (the owner picked it, 2026-11-01): the game's language, as the console
        // would say it (warden_imports_kernel.cc's ExGetXConfigSetting). The disc has the
        // text in English, Spanish and Italian; the voices are English in all three. From
        // the next start.
        new Option("language", "Language", "en", false,
                new String[] {"en", "es", "it"},
                new String[] {"English", "Español", "Italiano"},
                new String[] {
                    "The game's text in English.",
                    "El texto del juego en español (las voces, en inglés).",
                    "Il testo del gioco in italiano (le voci in inglese).",
                }),
        // PLAN 10.14 (the owner picked it, 2026-11-01): upstream's cheats - the developers'
        // own debug scripts, run at the next frame of play (warden_game.cc's RunCheats). In
        // the game only; each session starts with them off (GameActivity). The last three
        // are actions: the value chooses, A (or a tap on the name) does it (OptionsPanel).
        new Option("cheat-immortal", "Cheat: immortal party", "off", true,
                new String[] {"off", "on"},
                new String[] {"Off", "On"},
                new String[] {
                    "The party takes damage as the game decides.",
                    "No one in the party can die (the developers' pc_immortal, for each"
                            + " member). Set again after loading a save.",
                }),
        new Option("cheat-skills", "Cheat: pass every skill check", "off", true,
                new String[] {"off", "on"},
                new String[] {"Off", "On"},
                new String[] {
                    "Persuade, intimidate and the like as the game decides.",
                    "Every skill check in conversation passes (zz_override_skills).",
                }),
        new Option("cheat-kill", "Cheat: kill all hostiles", "now", true,
                new String[] {"now"},
                new String[] {"Do it"},
                new String[] {
                    "Every hostile creature in the area dies (killallhostiles). Press "
                            + "the confirm button or tap the name to do it.",
                }),
        new Option("cheat-gold", "Cheat: add gold", "100", true,
                new String[] {"1", "10", "100", "1000"},
                new String[] {"+1 gold", "+10 gold", "+100 gold", "+1,000 gold"},
                new String[] {
                    "Choose the amount; the confirm button or a tap on the name gives it.",
                    "Choose the amount; the confirm button or a tap on the name gives it.",
                    "Choose the amount; the confirm button or a tap on the name gives it.",
                    "Choose the amount; the confirm button or a tap on the name gives it.",
                }),
        new Option("cheat-xp", "Cheat: add experience", "5000", true,
                new String[] {"1000", "5000", "20000", "100000"},
                new String[] {"+1,000 XP", "+5,000 XP", "+20,000 XP", "+100,000 XP"},
                new String[] {
                    "Choose the amount; the confirm button or a tap on the name gives it.",
                    "Choose the amount; the confirm button or a tap on the name gives it.",
                    "Choose the amount; the confirm button or a tap on the name gives it.",
                    "Choose the amount; the confirm button or a tap on the name gives it.",
                }),
    };

    /** PLAN 10.14: a cheat that is done on demand, not a setting (OptionsPanel). */
    static boolean isCheatAction(String file) {
        return file.equals("cheat-kill") || file.equals("cheat-gold") || file.equals("cheat-xp");
    }

    /** PLAN 9.17: the field of view rows' words, one per value. */
    private static String[] fovDetails(String where) {
        String[] details = new String[9];
        for (int i = 0; i < details.length; i++) {
            details[i] = where + (i == 2 ? " The game's own view."
                    : i < 2 ? " Narrower than the game's: closer." : " Wider than the game's: more"
                            + " around you, the edges stretched a little.");
        }
        return details;
    }

    /** PLAN 8: the handheld has a second display for the deck (GameActivity, Deck). */
    static boolean deckAvailable;

    /** PLAN 9.2: the display takes HDR10 (GameActivity, from Display.HdrCapabilities). */
    static boolean hdrAvailable;

    private Options() {
    }

    /** The options this device offers: HDR's only where the display takes HDR10. */
    static Option[] available() {
        java.util.ArrayList<Option> offered = new java.util.ArrayList<>();
        for (Option option : ALL) {
            if (option.file.startsWith("hdr") && !hdrAvailable) {
                continue;
            }
            if (option.file.equals("deck") && !deckAvailable) {
                continue;
            }
            offered.add(option);
        }
        return offered.toArray(new Option[0]);
    }

    /** The option written to {@code file}. */
    static Option byFile(String file) {
        for (Option option : ALL) {
            if (option.file.equals(file)) {
                return option;
            }
        }
        throw new IllegalArgumentException(file);
    }

    /** The option's file, or its default when there is none or it says something else. */
    static String read(File dir, Option option) {
        File file = new File(dir, option.file);
        try (BufferedReader in = new BufferedReader(new FileReader(file))) {
            String line = in.readLine();
            String value = line == null ? "" : line.trim().toLowerCase(Locale.ROOT);
            for (String known : option.values) {
                if (known.equals(value)) {
                    return value;
                }
            }
        } catch (IOException e) {
            // None yet: the default.
        }
        return option.defaultValue;
    }

    static boolean write(File dir, Option option, String value) {
        File file = new File(dir, option.file);
        try (FileWriter out = new FileWriter(file)) {
            out.write(value + "\n");
            Log.i(TAG, "options: " + option.file + " = " + value);
            return true;
        } catch (IOException e) {
            Log.e(TAG, "options: cannot write " + file, e);
            return false;
        }
    }
}
