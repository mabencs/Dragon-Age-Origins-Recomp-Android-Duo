package dev.daorecompandroid.duo;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * PLAN 10.12, PC mods the way upstream (DAORecomp's mods/) has them: each mod is a folder
 * under {@code <app storage>/mods/} mirroring the game folder, laid over the disc in load
 * order, the last winning (warden_fs.cc's mod roots, read from
 * {@code files/mods-enabled.txt} as the game starts). An optional {@code mod.toml}
 * names it. A mod made for the PC version - PC GFF files, .dds textures, PC archives -
 * would crash the 360 engine, so it is marked and not loaded until converted (10.13).
 *
 * <p>Adding one: a .zip (a .dazip too) or a folder picked through Android's file picker,
 * unpacked into {@code mods/.adding/} and sorted as upstream sorts it (mod_install.cpp):
 * single wrapping folders unwrapped, {@code packages/}, {@code core/} and
 * {@code override/} folders put where they belong, read-mes and {@code mod.toml} kept
 * on top, the rest into {@code packages/core/override/<mod>/} (or {@code override/}
 * itself for a DAModder CoreOverrideAddIn); a DAModder Manifest.xml names it.
 */
final class Mods {

    private static final String TAG = "Warden";
    private static final String STATE = "mods-state.txt";
    private static final String ENABLED = "mods-enabled.txt";
    private static final String DLC_OFF = "dlc-off";
    private static final String ADDINS = "addins.txt";
    private static final String[][] ANCHORS = {
        {"packages", "packages"},
        {"core", "packages/core"},
        {"override", "packages/core/override"},
    };
    private static final int ANCHOR_DEPTH = 3;
    private static final String[] GENERIC = {"data", "files", "main", "main files", "mod",
        "mods", "content", "contents", "install"};
    private static final String[] DOCUMENTS = {".txt", ".md", ".pdf", ".htm", ".html",
        ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".url", ".rtf", ".doc", ".docx"};
    /** The most an import may unpack (bytes), and a file's longest path in it. */
    private static final long MAX_UNPACKED = 8L << 30;

    /** One mod folder, as the Mods page shows it. */
    static final class Mod {
        String folder;
        String name;
        String version = "";
        String author = "";
        String description = "";
        boolean enabled = true;
        int files;
        /** Files in the PC version's formats, and one of them. */
        int pcFiles;
        String pcExample = "";
        final List<String> warnings = new ArrayList<>();
        /**
         * PLAN 10.19: its add-ins installed as downloadable content - {package folder,
         * title} each, from the mod's addins.txt (dao_addin_android.cc writes it).
         */
        final List<String[]> addins = new ArrayList<>();

        boolean isForPc() {
            return pcFiles > 0;
        }

        boolean isLoaded() {
            return enabled && !isForPc();
        }
    }

    private Mods() {
    }

    static File root(Context context) {
        return new File(context.getExternalFilesDir(null), "mods");
    }

    // ---- the list, its order and what the game loads ----

    /** The mods in the saved load order; new folders appended, enabled. */
    static List<Mod> scan(Context context) {
        File root = root(context);
        Map<String, Boolean> saved = new java.util.LinkedHashMap<>();
        for (String line : readLines(new File(context.getFilesDir(), STATE))) {
            int tab = line.indexOf('\t');
            if (tab > 0) {
                saved.put(line.substring(0, tab), line.substring(tab + 1).equals("1"));
            }
        }
        List<Mod> mods = new ArrayList<>();
        File[] folders = root.listFiles();
        List<String> present = new ArrayList<>();
        if (folders != null) {
            for (File f : folders) {
                if (f.isDirectory() && !f.getName().startsWith(".")) {
                    present.add(f.getName());
                }
            }
        }
        for (Map.Entry<String, Boolean> entry : saved.entrySet()) {
            if (present.remove(entry.getKey())) {
                Mod mod = describe(new File(root, entry.getKey()));
                mod.enabled = entry.getValue();
                mods.add(mod);
            }
        }
        java.util.Collections.sort(present, String.CASE_INSENSITIVE_ORDER);
        for (String folder : present) {
            mods.add(describe(new File(root, folder)));
        }
        return mods;
    }

    /** The order and the switches saved, and the game's list written (the next start). */
    static void save(Context context, List<Mod> mods) {
        StringBuilder state = new StringBuilder();
        StringBuilder enabled = new StringBuilder();
        File root = root(context);
        for (Mod mod : mods) {
            state.append(mod.folder).append('\t').append(mod.enabled ? "1" : "0").append('\n');
        }
        // The game's list: the loaded mods, the one that wins first (the last in order).
        for (int i = mods.size() - 1; i >= 0; i--) {
            if (mods.get(i).isLoaded()) {
                enabled.append(new File(root, mods.get(i).folder).getAbsolutePath()).append('\n');
            }
        }
        // PLAN 10.19: the add-ins of mods turned off, hidden from the game's content list.
        StringBuilder off = new StringBuilder();
        for (Mod mod : mods) {
            if (!mod.enabled) {
                for (String[] addin : mod.addins) {
                    off.append(addin[0]).append('\n');
                }
            }
        }
        writeText(new File(context.getFilesDir(), STATE), state.toString());
        writeText(new File(context.getFilesDir(), ENABLED), enabled.toString());
        writeText(new File(context.getFilesDir(), DLC_OFF), off.toString());
        Log.i(TAG, "mods: " + mods.size() + " in the list, the game's list written");
    }

    /**
     * The other loaded mods changing the same files: those under an override folder by
     * name (the game finds them by name), the others by path. A sentence, or "".
     */
    static String overlaps(Context context, List<Mod> mods, Mod which) {
        if (!which.isLoaded()) {
            return "";
        }
        File root = root(context);
        java.util.Set<String> mine = changes(new File(root, which.folder));
        List<String> others = new ArrayList<>();
        for (Mod other : mods) {
            if (other == which || !other.isLoaded()) {
                continue;
            }
            java.util.Set<String> theirs = changes(new File(root, other.folder));
            theirs.retainAll(mine);
            if (!theirs.isEmpty()) {
                others.add(other.name);
            }
        }
        return others.isEmpty() ? "" : "Changes files that " + String.join(", ", others)
                + " change too: the one lower in the list wins.";
    }

    private static java.util.Set<String> changes(File folder) {
        java.util.Set<String> out = new java.util.HashSet<>();
        walk(folder, folder, (file, relative) -> {
            String lower = relative.toLowerCase(Locale.ROOT);
            if (isDocument(file.getName()) || lower.equals("mod.toml")
                    || lower.equals("manifest.xml")) {
                return;
            }
            out.add(lower.contains("/override/") ? file.getName().toLowerCase(Locale.ROOT)
                    : lower);
        });
        return out;
    }

    // ---- what a mod is ----

    static Mod describe(File folder) {
        Mod mod = new Mod();
        mod.folder = folder.getName();
        mod.name = folder.getName();
        File toml = new File(folder, "mod.toml");
        for (String line : readLines(toml)) {
            int eq = line.indexOf('=');
            if (eq < 0) {
                continue;
            }
            String key = line.substring(0, eq).trim();
            String value = line.substring(eq + 1).trim();
            if (value.length() >= 2 && value.startsWith("\"") && value.endsWith("\"")) {
                value = value.substring(1, value.length() - 1).replace("\\\"", "\"");
            }
            switch (key) {
                case "name": mod.name = value; break;
                case "version": mod.version = value; break;
                case "author": mod.author = value; break;
                case "description": mod.description = value; break;
                default: break;
            }
        }
        final boolean[] addin = {false};
        walk(folder, folder, (file, relative) -> {
            String lower = relative.toLowerCase(Locale.ROOT);
            if (isDocument(file.getName()) || lower.equals("mod.toml")
                    || lower.equals("manifest.xml")) {
                return;
            }
            mod.files++;
            if (isPcFile(file)) {
                if (mod.pcFiles++ == 0) {
                    mod.pcExample = relative;
                }
            }
            if (lower.matches(".*addins/[^/]+/module/.*")) {
                addin[0] = true;
            }
        });
        for (String line : readLines(new File(folder, ADDINS))) {
            int tab = line.indexOf('\t');
            if (tab > 0) {
                mod.addins.add(new String[] {line.substring(0, tab), line.substring(tab + 1)});
            }
        }
        if (mod.isForPc()) {
            mod.warnings.add("Made for the PC version (" + mod.pcFiles
                    + (mod.pcFiles == 1 ? " file: " : " files such as ")
                    + mod.pcExample + "): not loaded until converted.");
        }
        if (addin[0]) {
            mod.warnings.add("Has an add-in (its own scripts, items or areas): converting"
                    + " installs it as downloadable content.");
        }
        if (mod.files == 0 && mod.addins.isEmpty()) {
            mod.warnings.add("Has no files for the game.");
        }
        return mod;
    }

    /** A file the 360 version cannot read: a PC GFF, a .dds, a PC archive. */
    static boolean isPcFile(File file) {
        String name = file.getName().toLowerCase(Locale.ROOT);
        if (name.endsWith(".dds") || name.endsWith(".dazip")) {
            return true;
        }
        byte[] head = new byte[12];
        try (FileInputStream in = new FileInputStream(file)) {
            int n = in.read(head);
            if (n < 12) {
                return false;
            }
        } catch (Exception e) {
            return false;
        }
        // GFF V4.0 then the platform: "PC  " (the 360's say "X360").
        if (head[0] == 'G' && head[1] == 'F' && head[2] == 'F' && head[3] == ' ') {
            return head[8] == 'P' && head[9] == 'C';
        }
        // A PC ERF: its header in UTF-16 ("E\0R\0F\0"); the 360's are ASCII.
        return head[0] == 'E' && head[1] == 0 && head[2] == 'R' && head[3] == 0
                && head[4] == 'F' && head[5] == 0;
    }

    static boolean isDocument(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        for (String ext : DOCUMENTS) {
            if (lower.endsWith(ext)) {
                return true;
            }
        }
        return false;
    }

    // ---- adding a mod ----

    /** The result of an import: the mod's folder, or why not, and notes for the player. */
    static final class Added {
        String folder;
        String error;
        final List<String> notes = new ArrayList<>();
    }

    /** A picked .zip (or .dazip) or folder as a new mod. Not on the UI thread. */
    static Added add(Context context, Uri uri, boolean tree) {
        Added added = new Added();
        File root = root(context);
        File work = new File(root, ".adding/" + System.currentTimeMillis());
        File raw = new File(work, "raw");
        File out = new File(work, "out");
        try {
            raw.mkdirs();
            String name;
            if (tree) {
                name = copyTree(context, uri, raw);
            } else {
                name = displayName(context, uri);
                unpack(context, uri, name, work, raw);
            }
            name = modName(stripExtension(name));
            String[] nameBox = {name};
            organize(raw, out, nameBox, added.notes);
            if (countFiles(out) == 0) {
                added.error = "It has no files for the game.";
                return added;
            }
            String folder = nameBox[0];
            File target = new File(root, folder);
            for (int n = 2; target.exists(); n++) {
                target = new File(root, nameBox[0] + " (" + n + ")");
            }
            if (!out.renameTo(target)) {
                added.error = "It could not be put in the mods folder.";
                return added;
            }
            added.folder = target.getName();
            Log.i(TAG, "mods: added " + added.folder);
        } catch (Exception e) {
            Log.w(TAG, "mods: not added", e);
            added.error = e.getMessage() != null ? e.getMessage() : e.toString();
        } finally {
            deleteTree(work);
        }
        return added;
    }

    /** Sorts one import's files (in raw) into a mod folder (out), as upstream does. */
    static void organize(File raw, File out, String[] name, List<String> notes) throws Exception {
        // Unwrap single folders around everything (not one saying where it goes).
        for (;;) {
            File[] entries = raw.listFiles();
            if (entries == null || entries.length != 1 || !entries[0].isDirectory()
                    || anchorPlace(entries[0]) != null) {
                break;
            }
            raw = entries[0];
            if (!isGeneric(raw.getName())) {
                name[0] = modName(raw.getName());
            }
        }
        String optional = findOptional(raw, 0);
        if (optional != null) {
            notes.add("It has optional parts (\"" + optional + "\"), all added: its readme may"
                    + " say which to keep.");
        }
        out.mkdirs();
        String manifestType = "";
        File[] top = raw.listFiles();
        if (top != null) {
            for (File f : top) {
                if (!f.isFile()) {
                    continue;
                }
                String lower = f.getName().toLowerCase(Locale.ROOT);
                if (lower.equals("manifest.xml")) {
                    String xml = new String(readAll(f, 1 << 20), StandardCharsets.UTF_8);
                    String manifestName = xmlAttribute(xml, "<Name", "Value");
                    if (manifestName.isEmpty()) {
                        manifestName = xmlAttribute(xml, "<Title", "DefaultText");
                    }
                    if (!manifestName.isEmpty()) {
                        name[0] = modName(manifestName);
                    }
                    manifestType = xmlAttribute(xml, "<AddInItem", "Type");
                    writeToml(new File(out, "mod.toml"), name[0],
                            xmlAttribute(xml, "<Version", "Value"),
                            xmlAttribute(xml, "<Author", "Value"),
                            xmlAttribute(xml, "<Description", "DefaultText"));
                }
                if (lower.equals("manifest.xml") || lower.equals("mod.toml") || isDocument(lower)) {
                    move(f, new File(out, f.getName()));
                }
            }
        }
        for (Object[] anchor : findAnchors(raw)) {
            File folder = (File) anchor[0];
            moveInto(folder, new File(out, (String) anchor[1]));
            deleteTree(folder);
        }
        File override = new File(out, "packages/core/override");
        moveInto(raw, manifestType.equals("CoreOverrideAddIn") ? override
                : new File(override, name[0]));
        removeEmptyFolders(out);
    }

    private static String anchorPlace(File folder) {
        String name = folder.getName().toLowerCase(Locale.ROOT);
        for (String[] anchor : ANCHORS) {
            if (anchor[0].equals(name)) {
                return anchor[1];
            }
        }
        return null;
    }

    /** The anchor folders nearest the top: all those at the shallowest depth with any. */
    private static List<Object[]> findAnchors(File raw) {
        List<File> level = new ArrayList<>();
        level.add(raw);
        for (int depth = 0; depth < ANCHOR_DEPTH && !level.isEmpty(); depth++) {
            List<Object[]> found = new ArrayList<>();
            List<File> next = new ArrayList<>();
            for (File dir : level) {
                File[] entries = dir.listFiles();
                if (entries == null) {
                    continue;
                }
                for (File entry : entries) {
                    if (!entry.isDirectory()) {
                        continue;
                    }
                    String place = anchorPlace(entry);
                    if (place != null) {
                        found.add(new Object[] {entry, place});
                    } else {
                        next.add(entry);
                    }
                }
            }
            if (!found.isEmpty()) {
                return found;
            }
            level = next;
        }
        return new ArrayList<>();
    }

    private static String findOptional(File dir, int depth) {
        File[] entries = dir.listFiles();
        if (entries == null) {
            return null;
        }
        for (File entry : entries) {
            if (!entry.isDirectory()) {
                continue;
            }
            String lower = entry.getName().toLowerCase(Locale.ROOT);
            for (String word : new String[] {"optional", "alternat", "variant", "choose"}) {
                if (lower.contains(word)) {
                    return entry.getName();
                }
            }
            if (depth + 1 < ANCHOR_DEPTH) {
                String found = findOptional(entry, depth + 1);
                if (found != null) {
                    return found;
                }
            }
        }
        return null;
    }

    private static boolean isGeneric(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        for (String generic : GENERIC) {
            if (generic.equals(lower)) {
                return true;
            }
        }
        return false;
    }

    /** A folder name from a mod's: Nexus's "-id-version-time" ending removed, safe. */
    static String modName(String name) {
        String trimmed = name.replaceAll("-\\d+(-[\\d-]+)?(-\\d{9,})$", "");
        trimmed = trimmed.replaceAll("[\\\\/:*?\"<>|\\x00-\\x1F]", "").trim();
        if (trimmed.startsWith(".")) {
            trimmed = trimmed.substring(1);
        }
        if (trimmed.isEmpty()) {
            trimmed = "New mod";
        }
        return trimmed.length() > 80 ? trimmed.substring(0, 80).trim() : trimmed;
    }

    private static String stripExtension(String name) {
        int dot = name.lastIndexOf('.');
        return dot > 0 ? name.substring(0, dot) : name;
    }

    private static String xmlAttribute(String xml, String element, String attribute) {
        int at = xml.indexOf(element);
        if (at < 0) {
            return "";
        }
        int end = xml.indexOf('>', at);
        String tag = end > at ? xml.substring(at, end) : xml.substring(at);
        int a = tag.indexOf(attribute + "=\"");
        if (a < 0) {
            return "";
        }
        a += attribute.length() + 2;
        int b = tag.indexOf('"', a);
        String value = b > a ? tag.substring(a, b) : "";
        return value.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">")
                .replace("&quot;", "\"").replace("&apos;", "'");
    }

    private static void writeToml(File file, String name, String version, String author,
                                  String description) {
        StringBuilder text = new StringBuilder();
        text.append("name = \"").append(escape(name)).append("\"\n");
        if (!version.isEmpty()) {
            text.append("version = \"").append(escape(version)).append("\"\n");
        }
        if (!author.isEmpty()) {
            text.append("author = \"").append(escape(author)).append("\"\n");
        }
        if (!description.isEmpty()) {
            text.append("description = \"").append(escape(description)).append("\"\n");
        }
        writeText(file, text.toString());
    }

    private static String escape(String text) {
        return text.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", " ");
    }

    // ---- unpacking and copying ----

    /**
     * A picked file into raw: a .zip unpacked here; a .dazip kept whole (its add-ins are
     * installed when the mod is converted - PLAN 10.19); a .7z or .rar unpacked natively
     * (NativeBridge.unpackArchive, as upstream unpacks them with tar).
     */
    private static void unpack(Context context, Uri uri, String name, File work, File raw)
            throws Exception {
        ContentResolver resolver = context.getContentResolver();
        byte[] magic = new byte[6];
        int got = 0;
        try (InputStream in = resolver.openInputStream(uri)) {
            int n;
            while (got < magic.length && (n = in.read(magic, got, magic.length - got)) > 0) {
                got += n;
            }
        }
        String lower = name.toLowerCase(Locale.ROOT);
        boolean zip = got >= 4 && magic[0] == 'P' && magic[1] == 'K';
        boolean rar = got >= 6 && magic[0] == 'R' && magic[1] == 'a' && magic[2] == 'r'
                && magic[3] == '!';
        boolean sevenZip = got >= 6 && magic[0] == '7' && magic[1] == 'z'
                && (magic[2] & 0xFF) == 0xBC && (magic[3] & 0xFF) == 0xAF;
        if (zip && lower.endsWith(".dazip")) {
            copyUri(resolver, uri, new File(raw, new File(name).getName()));
        } else if (zip) {
            unzip(resolver, uri, raw);
        } else if (rar || sevenZip) {
            File archive = new File(work, rar ? "archive.rar" : "archive.7z");
            copyUri(resolver, uri, archive);
            String error = NativeBridge.unpackArchive(archive.getAbsolutePath(),
                    raw.getAbsolutePath());
            archive.delete();
            if (!error.isEmpty()) {
                throw new Exception(error);
            }
        } else {
            throw new Exception("It is not a .zip, .7z, .rar or .dazip archive.");
        }
    }

    private static void copyUri(ContentResolver resolver, Uri uri, File to) throws Exception {
        to.getParentFile().mkdirs();
        long total = 0;
        byte[] buffer = new byte[1 << 16];
        try (InputStream in = resolver.openInputStream(uri);
             OutputStream out = new FileOutputStream(to)) {
            int n;
            while ((n = in.read(buffer)) > 0) {
                total += n;
                if (total > MAX_UNPACKED) {
                    throw new Exception("It is larger than the space a mod may take.");
                }
                out.write(buffer, 0, n);
            }
        }
    }

    private static void unzip(ContentResolver resolver, Uri uri, File to) throws Exception {
        long total = 0;
        String canonicalRoot = to.getCanonicalPath() + File.separator;
        try (InputStream in = resolver.openInputStream(uri);
             ZipInputStream zip = new ZipInputStream(in)) {
            ZipEntry entry;
            byte[] buffer = new byte[1 << 16];
            while ((entry = zip.getNextEntry()) != null) {
                String path = entry.getName().replace('\\', '/');
                if (path.startsWith("/") || path.contains("../") || path.equals("..")) {
                    continue;  // outside the mod
                }
                File target = new File(to, path);
                if (!target.getCanonicalPath().startsWith(canonicalRoot)) {
                    continue;
                }
                if (entry.isDirectory()) {
                    target.mkdirs();
                    continue;
                }
                target.getParentFile().mkdirs();
                try (OutputStream out = new FileOutputStream(target)) {
                    int n;
                    while ((n = zip.read(buffer)) > 0) {
                        total += n;
                        if (total > MAX_UNPACKED) {
                            throw new Exception("It is larger than the space a mod may take.");
                        }
                        out.write(buffer, 0, n);
                    }
                }
            }
        }
    }

    /** A picked folder (a document tree) copied; its name returned. */
    private static String copyTree(Context context, Uri tree, File to) throws Exception {
        String rootId = DocumentsContract.getTreeDocumentId(tree);
        Uri rootDoc = DocumentsContract.buildDocumentUriUsingTree(tree, rootId);
        String name = queryName(context, rootDoc);
        copyChildren(context, tree, rootId, to);
        return name != null ? name : "New mod";
    }

    private static void copyChildren(Context context, Uri tree, String parentId, File to)
            throws Exception {
        to.mkdirs();
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        ContentResolver resolver = context.getContentResolver();
        try (Cursor cursor = resolver.query(children, new String[] {
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null)) {
            if (cursor == null) {
                return;
            }
            while (cursor.moveToNext()) {
                String id = cursor.getString(0);
                String name = cursor.getString(1).replace("/", "_");
                String mime = cursor.getString(2);
                if (name.equals(".") || name.equals("..")) {
                    continue;
                }
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    copyChildren(context, tree, id, new File(to, name));
                } else {
                    Uri doc = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                    try (InputStream in = resolver.openInputStream(doc);
                         OutputStream out = new FileOutputStream(new File(to, name))) {
                        copy(in, out);
                    }
                }
            }
        }
    }

    private static String displayName(Context context, Uri uri) {
        String name = queryName(context, uri);
        return name != null ? name : "New mod";
    }

    private static String queryName(Context context, Uri uri) {
        try (Cursor cursor = context.getContentResolver().query(uri,
                new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                return cursor.getString(0);
            }
        } catch (Exception e) {
            // The name is a nicety.
        }
        return null;
    }

    // ---- files ----

    interface Visitor {
        void visit(File file, String relative);
    }

    static void walk(File root, File dir, Visitor visitor) {
        File[] entries = dir.listFiles();
        if (entries == null) {
            return;
        }
        for (File entry : entries) {
            if (entry.isDirectory()) {
                walk(root, entry, visitor);
            } else {
                String relative = root.toURI().relativize(entry.toURI()).getPath();
                visitor.visit(entry, relative);
            }
        }
    }

    static int countFiles(File dir) {
        final int[] count = {0};
        walk(dir, dir, (f, r) -> count[0]++);
        return count[0];
    }

    private static void move(File from, File to) throws Exception {
        to.getParentFile().mkdirs();
        if (to.exists()) {
            deleteTree(to);
        }
        if (!from.renameTo(to)) {
            throw new Exception("could not move " + from.getName());
        }
    }

    /** Everything in `from` into `to`, merging folders. */
    private static void moveInto(File from, File to) throws Exception {
        File[] entries = from.listFiles();
        if (entries == null) {
            return;
        }
        to.mkdirs();
        for (File entry : entries) {
            File target = new File(to, entry.getName());
            if (entry.isDirectory() && target.isDirectory()) {
                moveInto(entry, target);
                deleteTree(entry);
            } else {
                move(entry, target);
            }
        }
    }

    private static void removeEmptyFolders(File dir) {
        File[] entries = dir.listFiles();
        if (entries == null) {
            return;
        }
        for (File entry : entries) {
            if (entry.isDirectory()) {
                removeEmptyFolders(entry);
                String[] left = entry.list();
                if (left != null && left.length == 0) {
                    entry.delete();
                }
            }
        }
    }

    /** A mod deleted, with its add-ins' packages (PLAN 10.19). Not on the UI thread ideally. */
    static void delete(Context context, Mod mod) {
        File content = GameData.dlcFolder(context);
        for (String[] addin : mod.addins) {
            if (addin[0].startsWith("mod-") && !addin[0].contains("/")) {
                deleteTree(new File(content, addin[0]));
                new File(content, addin[0] + ".name").delete();
                Log.i(TAG, "mods: add-in " + addin[0] + " uninstalled");
            }
        }
        deleteTree(new File(root(context), mod.folder));
        // Its PC files kept by converting (mods/.originals/<folder>/) go with it.
        deleteTree(new File(root(context), ".originals/" + mod.folder));
    }

    static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        file.delete();
    }

    private static void copy(InputStream in, OutputStream out) throws Exception {
        byte[] buffer = new byte[1 << 16];
        int n;
        while ((n = in.read(buffer)) > 0) {
            out.write(buffer, 0, n);
        }
    }

    private static byte[] readAll(File file, int max) throws Exception {
        try (FileInputStream in = new FileInputStream(file)) {
            byte[] data = new byte[(int) Math.min(file.length(), max)];
            int read = 0;
            while (read < data.length) {
                int n = in.read(data, read, data.length - read);
                if (n < 0) {
                    break;
                }
                read += n;
            }
            return data;
        }
    }

    private static List<String> readLines(File file) {
        List<String> lines = new ArrayList<>();
        if (!file.isFile()) {
            return lines;
        }
        try {
            String text = new String(readAll(file, 1 << 20), StandardCharsets.UTF_8);
            for (String line : text.split("\n")) {
                String trimmed = line.replace("\r", "");
                if (!trimmed.isEmpty()) {
                    lines.add(trimmed);
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "mods: " + file + " not read", e);
        }
        return lines;
    }

    private static void writeText(File file, String text) {
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        } catch (Exception e) {
            Log.w(TAG, "mods: " + file + " not written", e);
        }
    }
}
