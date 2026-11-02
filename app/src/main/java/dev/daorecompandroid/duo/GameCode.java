package dev.daorecompandroid.duo;

import android.content.Context;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Iterator;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * PLAN 12.5: the game's code, made on this device from the player's own disc.
 *
 * <p>The published APK holds none of the game. Its code - libdaogame.so, which the runtime
 * loads at boot (warden_game_library.cc) - is made here, once: XenonRecomp (in the APK as
 * libxenonrecomp.so) translates the disc's executable with the title update into C++, and
 * clang and lld (libllvm.so) compile that and link it against the runtime, with the
 * headers and link inputs of the APK's assets/gamecode/toolchain.zip and the recipe beside
 * it (assets/gamecode/: hooks.h, shim.cpp, recipe.json, sources.json -
 * tools/gen_gamecode_recipe.py). On the Thor about ten minutes, most of it one file.
 * Resumable: every compiled file is kept until the library is linked.
 *
 * <p>{@link GameCodeService} runs it in a process of its own, in the foreground, so it goes
 * on when the app is put away; {@code files/gamecode/state.txt} says how far it is, for the
 * launcher ({@link #state}).
 */
final class GameCode {
    private static final String TAG = "WardenGameCode";

    // The inputs the code is translated from (tools/recompile.py's, DAORecomp's deps.py).
    private static final String XEX_SHA1 = "532c0ec164f44bc8784cc0e8f6f67f72c1ed3751";
    private static final String TU6_XEXP_SHA1 = "4141b608e44c4ed35eb4e24ba2c05844a2f57a54";
    private static final String TU7_XEXP_SHA1 = "dd5b493f4fe2fb4c99e6ef7ed6923ddddeee8291";
    // The slowest files first, so the long one starts at once (12.1: ppc_recomp.17 is 521 s
    // of the Thor's 522).
    private static final List<String> SLOWEST = Arrays.asList(
            "ppc_recomp.17.cpp", "ppc_recomp.26.cpp");
    private static final int JOBS = 6;

    private GameCode() {
    }

    static File dir(Context context) {
        return new File(context.getFilesDir(), "gamecode");
    }

    static File library(Context context) {
        return new File(dir(context), "libdaogame.so");
    }

    private static File manifest(Context context) {
        return new File(dir(context), "manifest.json");
    }

    private static File stateFile(Context context) {
        return new File(dir(context), "state.txt");
    }

    /** PLAN 12.8: when the making's state was last written (0 when it never was). */
    static long stateTime(Context context) {
        return stateFile(context).lastModified();
    }

    /** The ABI the app's runtime loads (assets/gamecode/recipe.json). */
    static String abi(Context context) {
        try {
            return recipe(context).getString("abi");
        } catch (Exception e) {
            return "";
        }
    }

    /**
     * Whether the game's code in place was made for this app (its runtime's ABI) from this
     * title update - else it is made again.
     */
    static boolean current(Context context, int titleUpdate) {
        try {
            JSONObject made = new JSONObject(readText(manifest(context)));
            return library(context).isFile() && made.getString("abi").equals(abi(context))
                    && made.getInt("tu") == titleUpdate;
        } catch (Exception e) {
            return false;
        }
    }

    /** The owner (2026-11-02), for reports: the title update the game's code was made from. */
    static String madeFrom(Context context) {
        try {
            JSONObject made = new JSONObject(readText(manifest(context)));
            return "made from Title Update " + made.getInt("tu") + " ("
                    + made.optString("made", "?") + ")"
                    + (made.getString("abi").equals(abi(context)) ? ""
                            : ", for another app version");
        } catch (Exception e) {
            return library(context).isFile() ? "present, no manifest" : "not made";
        }
    }

    /** How far the making is: [phase, done, total, message] - see {@link Progress}. */
    static String[] state(Context context) {
        try {
            String text = readText(stateFile(context)).trim();
            String[] parts = text.split("\t", 4);
            return parts.length == 4 ? parts : null;
        } catch (IOException e) {
            return null;
        }
    }

    /** The making is going on: its state says so, and its heartbeat is under a minute old. */
    static boolean running(Context context) {
        String[] state = state(context);
        return state != null && Progress.MAKING.equals(state[0])
                && System.currentTimeMillis() - stateFile(context).lastModified() < 60_000;
    }

    /** Written as the making goes, read by the launcher in the app's own process. */
    static final class Progress {
        static final String MAKING = "making";
        static final String DONE = "done";
        static final String FAILED = "failed";

        private final File file;

        Progress(Context context) {
            this.file = stateFile(context);
            File folder = file.getParentFile();
            if (folder != null && !folder.isDirectory() && !folder.mkdirs()) {
                Log.w(TAG, "could not make " + folder);
            }
        }

        /** The heartbeat (GameCodeService): alive, however long one step takes. */
        void touch() {
            if (!file.setLastModified(System.currentTimeMillis())) {
                Log.w(TAG, "could not touch " + file);
            }
        }

        void report(String phase, long done, long total, String message) {
            try {
                File part = new File(file.getPath() + ".part");
                try (OutputStream out = new FileOutputStream(part)) {
                    out.write((phase + "\t" + done + "\t" + total + "\t" + message + "\n")
                            .getBytes(StandardCharsets.UTF_8));
                }
                if (!part.renameTo(file)) {
                    Log.w(TAG, "could not write " + file);
                }
            } catch (IOException e) {
                Log.w(TAG, "could not write " + file, e);
            }
        }
    }

    /** Making the code failed, with the reason in words for the launcher. */
    static final class Failure extends Exception {
        Failure(String message) {
            super(message);
        }
    }

    /**
     * Makes the game's code: the toolchain unpacked, the executable and the title update's
     * patch checked, XenonRecomp's translation checked file by file, every file compiled,
     * the library linked and put in place, read-only, with what it was made from.
     */
    static void make(Context context, Progress progress) throws Failure {
        GameData.TitleUpdate update = GameData.titleUpdate(context);
        if (update == null || update.number < 6) {
            throw new Failure("No title update in place: choose the Ultimate Edition's disc 2,"
                    + " or Title Update 7.");
        }
        File base = dir(context);
        File work = new File(base, "work");
        File toolchain = new File(base, "toolchain");
        String libraryDir = context.getApplicationInfo().nativeLibraryDir;
        File llvm = new File(libraryDir, "libllvm.so");
        File xenonrecomp = new File(libraryDir, "libxenonrecomp.so");
        if (!llvm.canExecute() || !xenonrecomp.canExecute()) {
            throw new Failure("This build of the app cannot make the game's code (no compiler"
                    + " in it): install the published one, or put the game's code there from"
                    + " the computer (tools/push_gamecode.py).");
        }
        try {
            progress.report(Progress.MAKING, 0, 1, "Unpacking the compiler's files");
            unpackToolchain(context, toolchain);

            progress.report(Progress.MAKING, 0, 1, "Checking the disc's executable");
            File game = new File(work, "game");
            File title = new File(work, "title_update");
            File config = new File(work, "config");
            File source = new File(work, "src");
            File objects = new File(work, "obj");
            File logs = new File(work, "log");
            for (File folder : new File[] {game, title, config, source, objects, logs}) {
                mkdirs(folder);
            }
            File xex = new File(game, "default.xex");
            copy(new File(GameData.destination(context), "default.xex"), xex);
            if (!XEX_SHA1.equals(digest("SHA-1", xex))) {
                throw new Failure("The extracted disc's executable is not the supported one"
                        + " (media 186A1115).");
            }
            File xexp = new File(title, "default.xexp");
            if (update.file.endsWith("default.xexp")) {
                copy(update.path, xexp);
            } else if (!NativeBridge.titleUpdatePatch(update.path.getAbsolutePath(),
                    xexp.getAbsolutePath())) {
                throw new Failure("The title update's patch could not be read from "
                        + update.file + ".");
            }
            String patch = digest("SHA-1", xexp);
            int tu = TU7_XEXP_SHA1.equals(patch) ? 7 : TU6_XEXP_SHA1.equals(patch) ? 6 : 0;
            if (tu != update.number) {
                throw new Failure("The title update is not Title Update 6 or 7 as its name"
                        + " says (" + update.file + ").");
            }

            progress.report(Progress.MAKING, 0, 1, "Translating the game's program");
            File daorecomp = new File(toolchain, "daorecomp");
            File toml = new File(config, "DAO_tu6.toml");
            writeText(toml, redirect(readText(new File(daorecomp, "DAO_tu6.toml"))));
            copy(new File(daorecomp, "DAO_tu6_switch_tables.toml"),
                    new File(config, "DAO_tu6_switch_tables.toml"));
            JSONObject expected = sources(context).getJSONObject("tu").getJSONObject(
                    String.valueOf(tu));
            if (!translated(source, expected)) {
                run(Arrays.asList(xenonrecomp.getAbsolutePath(), toml.getAbsolutePath(),
                        new File(toolchain, "xenonrecomp/ppc_context.h").getAbsolutePath(),
                        "tu6"), config, new File(logs, "xenonrecomp.txt"));
                progress.report(Progress.MAKING, 0, 1, "Checking the translation");
                for (Iterator<String> names = expected.keys(); names.hasNext(); ) {
                    String name = names.next();
                    File file = new File(source, name);
                    if (!file.isFile() || !expected.getString(name).equals(
                            digest("SHA-256", file))) {
                        throw new Failure("The translation of the game's program differs from"
                                + " the expected (" + name + ").");
                    }
                }
            }

            JSONObject recipe = recipe(context);
            File hooks = new File(work, "hooks.h");
            File shim = new File(work, "shim.cpp");
            copyAsset(context, "gamecode/hooks.h", hooks);
            copyAsset(context, "gamecode/shim.cpp", shim);
            List<File> units = new ArrayList<>();
            File[] generated = source.listFiles((folder, name) -> name.endsWith(".cpp"));
            if (generated != null) {
                units.addAll(Arrays.asList(generated));
            }
            units.sort((a, b) -> {
                int ra = SLOWEST.indexOf(a.getName()), rb = SLOWEST.indexOf(b.getName());
                ra = ra < 0 ? Integer.MAX_VALUE : ra;
                rb = rb < 0 ? Integer.MAX_VALUE : rb;
                return ra != rb ? Integer.compare(ra, rb) : Long.compare(b.length(), a.length());
            });
            units.add(shim);
            compile(units, objects, logs, llvm, recipe.getJSONArray("compile"), toolchain,
                    source, hooks, progress);

            progress.report(Progress.MAKING, units.size(), units.size(), "Linking the game's code");
            File part = new File(base, "libdaogame.so.part");
            if (part.exists() && !part.delete()) {
                throw new Failure("Could not replace " + part);
            }
            List<String> command = new ArrayList<>();
            command.add(llvm.getAbsolutePath());
            command.add("ld.lld");
            JSONArray link = recipe.getJSONArray("link");
            File lib = new File(toolchain, "lib");
            for (int i = 0; i < link.length(); i++) {
                String argument = link.getString(i);
                if (argument.equals("{objects}")) {
                    for (File unit : units) {
                        command.add(objectOf(objects, unit).getAbsolutePath());
                    }
                    continue;
                }
                command.add(argument
                        .replace("{sysroot}", new File(toolchain, "sysroot").getAbsolutePath())
                        .replace("{lib}", lib.getAbsolutePath())
                        .replace("{output}", part.getAbsolutePath())
                        .replace("{libwarden}", new File(libraryDir, "libwarden.so").getAbsolutePath())
                        .replace("{builtins}", new File(lib,
                                "libclang_rt.builtins-aarch64-android.a").getAbsolutePath())
                        .replace("{unwind}", new File(lib, "libunwind.a").getAbsolutePath()));
            }
            run(command, work, new File(logs, "link.txt"));
            if (!part.setReadOnly()) {
                throw new Failure("Could not make the game's code read-only.");
            }
            File library = library(context);
            if (library.exists() && !library.delete()) {
                throw new Failure("Could not replace the game's code in place.");
            }
            if (!part.renameTo(library)) {
                throw new Failure("Could not put the game's code in place.");
            }
            JSONObject made = new JSONObject();
            made.put("abi", recipe.getString("abi"));
            made.put("tu", tu);
            made.put("made", "device");
            made.put("time", System.currentTimeMillis());
            writeText(manifest(context), made.toString(1) + "\n");
            deleteTree(work);
            progress.report(Progress.DONE, units.size(), units.size(),
                    "The game's code is made (Title Update " + tu + ")");
            Log.i(TAG, "made " + library + " from Title Update " + tu);
        } catch (Failure failure) {
            throw failure;
        } catch (Exception e) {
            Log.e(TAG, "making the game's code failed", e);
            throw new Failure("Making the game's code failed: " + e.getMessage());
        }
    }

    /** DAORecomp's TOML with its inputs and output where the work folder has them. */
    private static String redirect(String toml) {
        StringBuilder out = new StringBuilder();
        for (String line : toml.split("\n", -1)) {
            String trimmed = line.trim();
            if (trimmed.startsWith("file_path") && trimmed.contains("=")) {
                line = "file_path = \"../game/default.xex\"";
            } else if (trimmed.startsWith("patch_file_path") && trimmed.contains("=")) {
                line = "patch_file_path = \"../title_update/default.xexp\"";
            } else if (trimmed.startsWith("out_directory_path") && trimmed.contains("=")) {
                line = "out_directory_path = \"../src\"";
            }
            out.append(line).append('\n');
        }
        return out.substring(0, out.length() - 1);
    }

    /** All of the expected files there already, as expected (an earlier, interrupted run). */
    private static boolean translated(File source, JSONObject expected) throws Exception {
        for (Iterator<String> names = expected.keys(); names.hasNext(); ) {
            String name = names.next();
            File file = new File(source, name);
            if (!file.isFile() || !expected.getString(name).equals(digest("SHA-256", file))) {
                return false;
            }
        }
        return true;
    }

    private static File objectOf(File objects, File unit) {
        return new File(objects, unit.getName() + ".o");
    }

    private static void compile(List<File> units, File objects, File logs, File llvm,
                                JSONArray flags, File toolchain, File source, File hooks,
                                Progress progress) throws Exception {
        List<String> base = new ArrayList<>();
        base.add(llvm.getAbsolutePath());
        base.add("clang");
        for (int i = 0; i < flags.length(); i++) {
            base.add(flags.getString(i)
                    .replace("{sysroot}", new File(toolchain, "sysroot").getAbsolutePath())
                    .replace("{resource_dir}", new File(toolchain, "clang").getAbsolutePath())
                    .replace("{include}", source.getAbsolutePath())
                    .replace("{simde}", new File(toolchain, "simde").getAbsolutePath())
                    .replace("{hooks}", hooks.getAbsolutePath()));
        }
        AtomicInteger done = new AtomicInteger();
        AtomicLong bytes = new AtomicLong();
        long total = 0;
        for (File unit : units) {
            total += unit.length();
            if (objectOf(objects, unit).isFile()) {
                done.incrementAndGet();
                bytes.addAndGet(unit.length());
            }
        }
        final long all = total;
        progress.report(Progress.MAKING, done.get(), units.size(), "Compiling the game's code");
        ExecutorService pool = Executors.newFixedThreadPool(JOBS);
        List<Future<?>> jobs = new ArrayList<>();
        for (File unit : units) {
            File object = objectOf(objects, unit);
            if (object.isFile()) {
                continue;
            }
            jobs.add(pool.submit(() -> {
                File part = new File(object.getPath() + ".part");
                List<String> command = new ArrayList<>(base);
                command.addAll(Arrays.asList("-c", unit.getAbsolutePath(), "-o",
                        part.getAbsolutePath()));
                run(command, objects, new File(logs, unit.getName() + ".txt"));
                if (!part.renameTo(object)) {
                    throw new Failure("Could not keep " + object.getName());
                }
                int count = done.incrementAndGet();
                long size = bytes.addAndGet(unit.length());
                progress.report(Progress.MAKING, size, all, "Compiling the game's code: "
                        + count + " of " + units.size() + " files");
                return null;
            }));
        }
        pool.shutdown();
        try {
            for (Future<?> job : jobs) {
                job.get();
            }
        } catch (java.util.concurrent.ExecutionException e) {
            pool.shutdownNow();
            Throwable cause = e.getCause();
            if (cause instanceof Failure) {
                throw (Failure) cause;
            }
            throw new Failure("Compiling the game's code failed: " + cause);
        }
        pool.awaitTermination(1, TimeUnit.MINUTES);
    }

    /** Runs a program, its output to `log`; a Failure naming its last lines if it fails. */
    private static void run(List<String> command, File directory, File log) throws Exception {
        Process process = new ProcessBuilder(command).directory(directory)
                .redirectErrorStream(true).redirectOutput(log).start();
        int code = process.waitFor();
        if (code != 0) {
            String text = log.isFile() ? readText(log) : "";
            String tail = text.length() > 600 ? text.substring(text.length() - 600) : text;
            Log.e(TAG, command.get(1) + " exited " + code + ": " + tail);
            throw new Failure(new File(command.get(0)).getName() + " " + command.get(1)
                    + " failed (" + code + "): " + tail.trim());
        }
    }

    /** toolchain.zip unpacked into `toolchain`, again when its VERSION changes. */
    private static void unpackToolchain(Context context, File toolchain) throws Exception {
        String version;
        try (InputStream in = context.getAssets().open("gamecode/toolchain.version")) {
            version = new String(readAll(in), StandardCharsets.UTF_8).trim();
        }
        File stamp = new File(toolchain, "VERSION");
        if (stamp.isFile() && readText(stamp).trim().equals(version)) {
            return;
        }
        deleteTree(toolchain);
        mkdirs(toolchain);
        byte[] buffer = new byte[1 << 16];
        String root = toolchain.getCanonicalPath() + File.separator;
        try (ZipInputStream zip = new ZipInputStream(
                context.getAssets().open("gamecode/toolchain.zip"))) {
            for (ZipEntry entry; (entry = zip.getNextEntry()) != null; ) {
                File file = new File(toolchain, entry.getName());
                if (!file.getCanonicalPath().startsWith(root)) {
                    throw new Failure("A bad entry in the compiler's files: " + entry.getName());
                }
                if (entry.isDirectory()) {
                    mkdirs(file);
                    continue;
                }
                mkdirs(file.getParentFile());
                try (OutputStream out = new FileOutputStream(file)) {
                    for (int n; (n = zip.read(buffer)) > 0; ) {
                        out.write(buffer, 0, n);
                    }
                }
            }
        }
        if (!readText(stamp).trim().equals(version)) {
            throw new Failure("The compiler's files are not this app's.");
        }
    }

    private static JSONObject recipe(Context context) throws Exception {
        try (InputStream in = context.getAssets().open("gamecode/recipe.json")) {
            return new JSONObject(new String(readAll(in), StandardCharsets.UTF_8));
        }
    }

    private static JSONObject sources(Context context) throws Exception {
        try (InputStream in = context.getAssets().open("gamecode/sources.json")) {
            return new JSONObject(new String(readAll(in), StandardCharsets.UTF_8));
        }
    }

    private static void copyAsset(Context context, String asset, File to) throws IOException {
        try (InputStream in = context.getAssets().open(asset);
             OutputStream out = new FileOutputStream(to)) {
            out.write(readAll(in));
        }
    }

    private static void copy(File from, File to) throws IOException {
        File part = new File(to.getPath() + ".part");
        byte[] buffer = new byte[1 << 16];
        try (InputStream in = new FileInputStream(from);
             OutputStream out = new FileOutputStream(part)) {
            for (int n; (n = in.read(buffer)) > 0; ) {
                out.write(buffer, 0, n);
            }
        }
        if (!part.renameTo(to)) {
            throw new IOException("could not write " + to);
        }
    }

    private static String digest(String algorithm, File file) throws Exception {
        MessageDigest digest = MessageDigest.getInstance(algorithm);
        byte[] buffer = new byte[1 << 16];
        try (InputStream in = new FileInputStream(file)) {
            for (int n; (n = in.read(buffer)) > 0; ) {
                digest.update(buffer, 0, n);
            }
        }
        StringBuilder hex = new StringBuilder();
        for (byte b : digest.digest()) {
            hex.append(String.format(java.util.Locale.ROOT, "%02x", b));
        }
        return hex.toString();
    }

    private static byte[] readAll(InputStream in) throws IOException {
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        byte[] buffer = new byte[1 << 16];
        for (int n; (n = in.read(buffer)) > 0; ) {
            out.write(buffer, 0, n);
        }
        return out.toByteArray();
    }

    private static String readText(File file) throws IOException {
        StringBuilder text = new StringBuilder();
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                new FileInputStream(file), StandardCharsets.UTF_8))) {
            char[] buffer = new char[8192];
            for (int n; (n = reader.read(buffer)) > 0; ) {
                text.append(buffer, 0, n);
            }
        }
        return text.toString();
    }

    private static void writeText(File file, String text) throws IOException {
        File part = new File(file.getPath() + ".part");
        try (OutputStream out = new FileOutputStream(part)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
        if (!part.renameTo(file)) {
            throw new IOException("could not write " + file);
        }
    }

    private static void mkdirs(File folder) throws IOException {
        if (!folder.isDirectory() && !folder.mkdirs()) {
            throw new IOException("could not make " + folder);
        }
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        if (file.exists() && !file.delete()) {
            Log.w(TAG, "could not delete " + file);
        }
    }
}
