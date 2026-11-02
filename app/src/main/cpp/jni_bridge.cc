// The JNI entry points. Task 4.10.
//
// This file is the whole native side of the Java boundary, and is meant to stay that
// way: every function here is short, does no work of its own, and immediately hands
// over to Runtime. The reason is debuggability - a crash in native code gives a
// stack trace that may or may not be symbolised, so the less logic lives at the
// boundary, the fewer places a bad argument from Java can become an unreadable abort.
//
// Registration is dynamic, through JNI_OnLoad, rather than by relying on mangled
// Java_dev_daorecompandroid_duo_* names. Two reasons: a typo in a mangled name is a crash at
// first call instead of an error at load, and renaming the Java package would
// otherwise silently break every entry point.

#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <unistd.h>

#include <cerrno>
#include <string>
#include <vector>

#include "xenia/base/logging.h"

#include "warden_audio.h"
#include "warden_deck.h"
#include "warden_boot.h"
#include "warden_dlc.h"
#include "warden_extract.h"
#include "warden_gpu.h"
#include "warden_input.h"
#include "warden_photo_mode.h"
#include "dao_archive_android.h"
#include "dao_convert_android.h"
#include "warden_keyboard.h"
#include "warden_lifecycle.h"
#include "warden_title.h"
#include "warden_runtime.h"

namespace {

constexpr const char* kTag = "Warden";
constexpr const char* kBridgeClass = "dev/daorecompandroid/duo/NativeBridge";

std::string ToStdString(JNIEnv* env, jstring value) {
  if (!value) {
    return {};
  }
  const char* chars = env->GetStringUTFChars(value, nullptr);
  std::string out = chars ? chars : "";
  if (chars) {
    env->ReleaseStringUTFChars(value, chars);
  }
  return out;
}

jboolean Startup(JNIEnv* env, jclass, jstring data_dir) {
  return warden::Runtime::Get().Startup(ToStdString(env, data_dir)) ? JNI_TRUE
                                                                    : JNI_FALSE;
}

void Shutdown(JNIEnv*, jclass) { warden::Runtime::Get().Shutdown(); }

void SurfaceCreated(JNIEnv* env, jclass, jobject surface) {
  // ANativeWindow_fromSurface takes its own reference, which Runtime adopts and
  // releases. Passing nullptr through would be a Java-side bug, so say so loudly
  // rather than quietly doing nothing.
  ANativeWindow* window =
      surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
  if (!window) {
    __android_log_print(ANDROID_LOG_ERROR, kTag,
                        "surfaceCreated: no ANativeWindow from the Surface");
    return;
  }
  warden::Runtime::Get().SurfaceCreated(window);
  // Runtime acquired its own reference; drop ours.
  ANativeWindow_release(window);
}

void SurfaceChanged(JNIEnv*, jclass, jint width, jint height) {
  if (width <= 0 || height <= 0) {
    return;
  }
  warden::Runtime::Get().SurfaceChanged(uint32_t(width), uint32_t(height));
}

void SurfaceDestroyed(JNIEnv*, jclass) {
  warden::Runtime::Get().SurfaceDestroyed();
}

void SetPaused(JNIEnv*, jclass, jboolean paused) {
  warden::Runtime::Get().SetPaused(paused == JNI_TRUE);
}

// ---- the display (PLAN 9.2) ----

void DisplayHdr(JNIEnv*, jclass, jfloat max_nits) { warden::gpu::SetDisplayHdrPeak(max_nits); }

// ---- the options panel (PLAN 10.1) ----

// PLAN 11.1: a bug report's fresh save - 1 written, 0 none possible now, -1 not in time.
jint SaveForReport(JNIEnv*, jclass) { return warden::Runtime::Get().SaveForReport(); }

void HoldGame(JNIEnv*, jclass, jboolean held) {
  warden::Runtime::Get().SetHeld(held == JNI_TRUE);
}

jboolean ApplyOption(JNIEnv* env, jclass, jstring name, jstring value) {
  const char* name_chars = name ? env->GetStringUTFChars(name, nullptr) : nullptr;
  const char* value_chars = value ? env->GetStringUTFChars(value, nullptr) : nullptr;
  const bool live = name_chars && value_chars &&
                    warden::Runtime::Get().ApplyOption(name_chars, value_chars);
  if (name_chars) {
    env->ReleaseStringUTFChars(name, name_chars);
  }
  if (value_chars) {
    env->ReleaseStringUTFChars(value, value_chars);
  }
  return live ? JNI_TRUE : JNI_FALSE;
}

jstring DeviceReport(JNIEnv* env, jclass) {
  return env->NewStringUTF(warden::Runtime::Get().Report().c_str());
}

// ---- controllers (task 4.6) ----

void PadAttached(JNIEnv* env, jclass, jint slot, jint device_id, jint vendor_id,
                 jint product_id, jstring name, jstring descriptor,
                 jboolean has_analog_triggers, jboolean has_hat) {
  warden::Input::Get().PadAttached(slot, device_id, vendor_id, product_id,
                                   ToStdString(env, name),
                                   ToStdString(env, descriptor),
                                   has_analog_triggers == JNI_TRUE,
                                   has_hat == JNI_TRUE);
}

void PadDetached(JNIEnv*, jclass, jint slot) {
  warden::Input::Get().PadDetached(slot);
}

jboolean PadKey(JNIEnv*, jclass, jint slot, jint key_code, jboolean down) {
  return warden::Input::Get().PadKey(slot, key_code, down == JNI_TRUE) ? JNI_TRUE
                                                                       : JNI_FALSE;
}

void PadAxes(JNIEnv*, jclass, jint slot, jfloat left_x, jfloat left_y, jfloat right_x,
             jfloat right_y, jfloat left_trigger, jfloat right_trigger, jfloat hat_x,
             jfloat hat_y) {
  warden::Input::Get().PadAxes(slot, left_x, left_y, right_x, right_y, left_trigger,
                               right_trigger, hat_x, hat_y);
}

jstring PadReport(JNIEnv* env, jclass) {
  return env->NewStringUTF(warden::Input::Get().Report().c_str());
}

// ---- game data extraction (task 4.4) ----

jboolean ExtractBegin(JNIEnv* env, jclass, jint iso_fd, jstring destination) {
  if (iso_fd < 0) {
    return JNI_FALSE;
  }
  return warden::Extractor::Get().Begin(iso_fd, ToStdString(env, destination))
             ? JNI_TRUE
             : JNI_FALSE;
}

// PLAN 10.10: a downloadable content package, or the packages in a disc image.
jstring InstallDlc(JNIEnv* env, jclass, jint fd, jstring content_dir, jstring title_update) {
  return env->NewStringUTF(warden::dlc::Install(fd, ToStdString(env, content_dir),
                                                ToStdString(env, title_update))
                               .c_str());
}

// PLAN 12.5: a title update package's default.xexp, for making the game's code.
jboolean TitleUpdatePatch(JNIEnv* env, jclass, jstring package, jstring out) {
  return warden::ExtractTitleUpdatePatch(ToStdString(env, package), ToStdString(env, out))
             ? JNI_TRUE
             : JNI_FALSE;
}

// PLAN 10.10: the DLC install's progress - bytes done and in all.
jlongArray DlcProgress(JNIEnv* env, jclass) {
  uint64_t done = 0, total = 0;
  warden::dlc::Progress(done, total);
  const jlong values[2] = {jlong(done), jlong(total)};
  jlongArray array = env->NewLongArray(2);
  if (array) {
    env->SetLongArrayRegion(array, 0, 2, values);
  }
  return array;
}

jlongArray ExtractProgress(JNIEnv* env, jclass) {
  const warden::Extractor::Progress progress = warden::Extractor::Get().Poll();
  const jlong values[6] = {
      jlong(progress.state),        jlong(progress.bytes_done),
      jlong(progress.bytes_total),  jlong(progress.files_done),
      jlong(progress.files_total),  jlong(progress.files_skipped),
  };
  jlongArray array = env->NewLongArray(6);
  if (array) {
    env->SetLongArrayRegion(array, 0, 6, values);
  }
  return array;
}

void ExtractCancel(JNIEnv*, jclass) { warden::Extractor::Get().Cancel(); }

jboolean ExtractIsComplete(JNIEnv* env, jclass, jstring destination) {
  std::string detail;
  return warden::Extractor::IsComplete(ToStdString(env, destination), detail) ? JNI_TRUE
                                                                               : JNI_FALSE;
}

// Loads the title from the extracted game data, once per process: the button prompts'
// set, the intro logos and the title update are taken now. The activity calls it as the
// game is launched (GameActivity.bootWhenReady) - not at startup, so what the pre-launch
// settings chose is what the game starts with.
void LoadTitle(JNIEnv* env, jclass, jstring game_data_dir) {
  warden::Runtime::Get().LoadTitle(ToStdString(env, game_data_dir));
}

// ---- the second screen's deck (PLAN 8, warden_deck.h) ----

// The state mirror's latest snapshot: "path<tab>value" lines. Called from DeckService's
// binder threads, never the game's; it never waits on the game.
jstring DeckSnapshot(JNIEnv* env, jclass) {
  return env->NewStringUTF(warden::deck::Snapshot().c_str());
}

// PLAN 8.8: a tap on the deck's quickbar - "" when queued, else why not.
jstring DeckQuickslot(JNIEnv* env, jclass, jint slot) {
  return env->NewStringUTF(warden::deck::QueueQuickslot(int(slot)).c_str());
}

// The Items tab's buttons - "" when queued, else why not.
jstring DeckItem(JNIEnv* env, jclass, jint action, jint item, jint ability) {
  return env->NewStringUTF(
      warden::deck::QueueItemAction(int(action), int32_t(item), int32_t(ability)).c_str());
}

// What the deck has open: its read patterns, one a line.
void DeckWatch(JNIEnv* env, jclass, jstring patterns) {
  const char* text = patterns ? env->GetStringUTFChars(patterns, nullptr) : nullptr;
  warden::deck::SetWatches(text ? text : "");
  if (text) {
    env->ReleaseStringUTFChars(patterns, text);
  }
}

// {now ns, the game's swaps so far, presents so far, then the game's last frame
// intervals in ns, oldest first}.
jlongArray DeckFrames(JNIEnv* env, jclass) {
  const warden::deck::FrameStats stats = warden::deck::Frames();
  std::vector<jlong> values = {jlong(stats.now_nanos), jlong(stats.guest_swaps),
                               jlong(stats.presents)};
  for (int64_t interval : stats.intervals) {
    values.push_back(jlong(interval));
  }
  jlongArray array = env->NewLongArray(jsize(values.size()));
  if (array) {
    env->SetLongArrayRegion(array, 0, jsize(values.size()), values.data());
  }
  return array;
}

// PLAN 9.20: photo mode - toggled at the game's next frame; its state for the overlay.
void PhotoToggle(JNIEnv*, jclass) { warden::photo::Toggle(); }

jfloatArray PhotoState(JNIEnv* env, jclass) {
  float state[6];
  warden::photo::State(state);
  jfloatArray array = env->NewFloatArray(6);
  if (array) {
    env->SetFloatArrayRegion(array, 0, 6, state);
  }
  return array;
}

// PLAN 10.13: a PC mod converted to the 360's formats - its report (dao_convert_android.h).
// PLAN 10.19: its add-ins installed as downloadable content in content_dir.
jstring ConvertMod(JNIEnv* env, jclass, jstring mod_folder, jstring game_root,
                   jstring content_dir) {
  return env->NewStringUTF(warden::ConvertMod(ToStdString(env, mod_folder),
                                              ToStdString(env, game_root),
                                              ToStdString(env, content_dir))
                               .c_str());
}

// PLAN 10.19: a .7z or .rar mod archive unpacked - "" or why not (dao_archive_android.h).
jstring UnpackArchive(JNIEnv* env, jclass, jstring archive, jstring into) {
  return env->NewStringUTF(
      warden::archives::Unpack(ToStdString(env, archive), ToStdString(env, into)).c_str());
}

// PLAN 10.17: the on-screen controller's state, in the 360's terms.
void TouchPad(JNIEnv*, jclass, jboolean active, jint buttons, jfloat left_x, jfloat left_y,
              jfloat right_x, jfloat right_y, jfloat left_trigger, jfloat right_trigger) {
  warden::SetTouchPad(active, uint16_t(buttons), left_x, left_y, right_x, right_y,
                      left_trigger, right_trigger);
}

// What the frames go through as the game runs: "Render.<name>\t<value>" lines.
jstring DeckRender(JNIEnv* env, jclass) {
  return env->NewStringUTF(warden::deck::RenderReport().c_str());
}

// The deck's faces: each party slot's face generation (0 none yet).
jintArray DeckPortraitGenerations(JNIEnv* env, jclass) {
  const std::vector<uint32_t> generations = warden::deck::PortraitGenerations();
  jintArray array = env->NewIntArray(jsize(generations.size()));
  if (array) {
    std::vector<jint> values(generations.begin(), generations.end());
    env->SetIntArrayRegion(array, 0, jsize(values.size()), values.data());
  }
  return array;
}

// A party slot's face, 146x146 RGBA, or null.
jbyteArray DeckPortrait(JNIEnv* env, jclass, jint slot) {
  const std::vector<uint8_t> rgba = warden::deck::Portrait(int(slot));
  if (rgba.empty()) {
    return nullptr;
  }
  jbyteArray array = env->NewByteArray(jsize(rgba.size()));
  if (array) {
    env->SetByteArrayRegion(array, 0, jsize(rgba.size()),
                            reinterpret_cast<const jbyte*>(rgba.data()));
  }
  return array;
}

jstring ExtractReport(JNIEnv* env, jclass) {
  return env->NewStringUTF(warden::Extractor::Get().Report().c_str());
}

// Identifies a disc image. Takes ownership of the descriptor, like every other fd
// crossing this boundary - one rule, so there is never a question of who closes it.
//
// Returns {valid, titleId, mediaId, rank}. The rank is what the caller sorts on, so
// the knowledge of which ids mean what stays in warden_title.h and is not duplicated
// in Java: 2 is Origins, 1 another Dragon Age disc, 0 something else.
jintArray ImageIdentity(JNIEnv* env, jclass, jint fd) {
  warden::TitleInfo info;
  if (fd >= 0) {
    warden::Xdvdfs iso([fd](uint64_t offset, void* out, size_t size) {
      uint8_t* cursor = static_cast<uint8_t*>(out);
      size_t remaining = size;
      while (remaining) {
        const ssize_t got = pread(fd, cursor, remaining, off_t(offset));
        if (got <= 0) {
          if (got < 0 && errno == EINTR) {
            continue;
          }
          return false;
        }
        cursor += got;
        offset += uint64_t(got);
        remaining -= size_t(got);
      }
      return true;
    });
    if (iso.Open()) {
      info = warden::ReadTitleInfo(iso);
    }
    close(fd);
  }
  const jint rank = !info.is_dragon_age() ? 0 : (info.is_origins() ? 2 : 1);
  if (info.valid) {
    XELOGI("image identified: {} (rank {})", info.Describe(), int(rank));
  } else {
    XELOGW("image could not be identified");
  }
  const jint values[4] = {info.valid ? 1 : 0, jint(info.title_id),
                          jint(info.media_id), rank};
  jintArray array = env->NewIntArray(4);
  if (array) {
    env->SetIntArrayRegion(array, 0, 4, values);
  }
  return array;
}

// Starts guest code: the game. Separate from startup(): the activity calls it once the
// game data is here and the surface exists (GameActivity.bootWhenReady).
jboolean BootGuest(JNIEnv*, jclass) {
  return warden::Runtime::Get().BootGuest() ? JNI_TRUE : JNI_FALSE;
}

jstring BootReport(JNIEnv* env, jclass) {
  return env->NewStringUTF(warden::Runtime::Get().BootReport().c_str());
}

// ---- the title's on-screen keyboard (warden_keyboard.h) ----

jstring NewUtf16String(JNIEnv* env, const std::u16string& text) {
  return env->NewString(reinterpret_cast<const jchar*>(text.data()), jsize(text.size()));
}

// The waiting prompt as {title, description, default text, max characters}, or null.
jobjectArray KeyboardRequest(JNIEnv* env, jclass) {
  warden::KeyboardPrompt prompt;
  if (!warden::PendingKeyboardPrompt(&prompt)) {
    return nullptr;
  }
  jclass string_class = env->FindClass("java/lang/String");
  jobjectArray array = env->NewObjectArray(4, string_class, nullptr);
  if (!array) {
    return nullptr;
  }
  env->SetObjectArrayElement(array, 0, NewUtf16String(env, prompt.title));
  env->SetObjectArrayElement(array, 1, NewUtf16String(env, prompt.description));
  env->SetObjectArrayElement(array, 2, NewUtf16String(env, prompt.default_text));
  env->SetObjectArrayElement(
      array, 3, env->NewStringUTF(std::to_string(prompt.max_chars).c_str()));
  return array;
}

void KeyboardAnswer(JNIEnv* env, jclass, jstring text, jboolean accepted) {
  std::u16string value;
  if (text) {
    const jsize length = env->GetStringLength(text);
    const jchar* chars = env->GetStringChars(text, nullptr);
    if (chars) {
      value.assign(reinterpret_cast<const char16_t*>(chars), size_t(length));
      env->ReleaseStringChars(text, chars);
    }
  }
  warden::AnswerKeyboardPrompt(value, accepted == JNI_TRUE);
}

// ---- the app's end (task 4.9), polled with the keyboard ----

jstring ExitRequest(JNIEnv* env, jclass) {
  return env->NewStringUTF(warden::ExitRequest().c_str());
}

void RequestQuit(JNIEnv* env, jclass, jstring why) {
  const char* chars = why ? env->GetStringUTFChars(why, nullptr) : nullptr;
  warden::RequestExit(chars ? chars : "the player quit", false);
  if (chars) {
    env->ReleaseStringUTFChars(why, chars);
  }
}

// ---- the audio route (PLAN 9.12) ----

void AudioHeadphones(JNIEnv* env, jclass, jintArray ids) {
  std::vector<int32_t> devices;
  if (ids) {
    const jsize count = env->GetArrayLength(ids);
    devices.resize(size_t(count));
    if (count > 0) {
      env->GetIntArrayRegion(ids, 0, count, reinterpret_cast<jint*>(devices.data()));
    }
  }
  warden::SetHeadphoneDevices(devices);
}

void AudioMediaVolume(JNIEnv*, jclass, jfloat gain) { warden::SetMediaVolume(float(gain)); }

const JNINativeMethod kMethods[] = {
    {"startup", "(Ljava/lang/String;)Z", reinterpret_cast<void*>(Startup)},
    {"shutdown", "()V", reinterpret_cast<void*>(Shutdown)},
    {"surfaceCreated", "(Landroid/view/Surface;)V",
     reinterpret_cast<void*>(SurfaceCreated)},
    {"surfaceChanged", "(II)V", reinterpret_cast<void*>(SurfaceChanged)},
    {"surfaceDestroyed", "()V", reinterpret_cast<void*>(SurfaceDestroyed)},
    {"setPaused", "(Z)V", reinterpret_cast<void*>(SetPaused)},
    {"holdGame", "(Z)V", reinterpret_cast<void*>(HoldGame)},
    {"saveForReport", "()I", reinterpret_cast<void*>(SaveForReport)},
    {"displayHdr", "(F)V", reinterpret_cast<void*>(DisplayHdr)},
    {"applyOption", "(Ljava/lang/String;Ljava/lang/String;)Z",
     reinterpret_cast<void*>(ApplyOption)},
    {"deviceReport", "()Ljava/lang/String;",
     reinterpret_cast<void*>(DeviceReport)},
    {"padAttached", "(IIIILjava/lang/String;Ljava/lang/String;ZZ)V",
     reinterpret_cast<void*>(PadAttached)},
    {"padDetached", "(I)V", reinterpret_cast<void*>(PadDetached)},
    {"padKey", "(IIZ)Z", reinterpret_cast<void*>(PadKey)},
    {"padAxes", "(IFFFFFFFF)V", reinterpret_cast<void*>(PadAxes)},
    {"padReport", "()Ljava/lang/String;", reinterpret_cast<void*>(PadReport)},
    {"extractBegin", "(ILjava/lang/String;)Z", reinterpret_cast<void*>(ExtractBegin)},
    {"extractProgress", "()[J", reinterpret_cast<void*>(ExtractProgress)},
    {"installDlc", "(ILjava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
     reinterpret_cast<void*>(InstallDlc)},
    {"dlcProgress", "()[J", reinterpret_cast<void*>(DlcProgress)},
    {"titleUpdatePatch", "(Ljava/lang/String;Ljava/lang/String;)Z",
     reinterpret_cast<void*>(TitleUpdatePatch)},
    {"extractCancel", "()V", reinterpret_cast<void*>(ExtractCancel)},
    {"extractIsComplete", "(Ljava/lang/String;)Z",
     reinterpret_cast<void*>(ExtractIsComplete)},
    {"extractReport", "()Ljava/lang/String;", reinterpret_cast<void*>(ExtractReport)},
    {"loadTitle", "(Ljava/lang/String;)V", reinterpret_cast<void*>(LoadTitle)},
    {"deckSnapshot", "()Ljava/lang/String;", reinterpret_cast<void*>(DeckSnapshot)},
    {"deckFrames", "()[J", reinterpret_cast<void*>(DeckFrames)},
    {"deckQuickslot", "(I)Ljava/lang/String;", reinterpret_cast<void*>(DeckQuickslot)},
    {"deckWatch", "(Ljava/lang/String;)V", reinterpret_cast<void*>(DeckWatch)},
    {"deckItem", "(III)Ljava/lang/String;", reinterpret_cast<void*>(DeckItem)},
    {"deckRender", "()Ljava/lang/String;", reinterpret_cast<void*>(DeckRender)},
    {"touchPad", "(ZIFFFFFF)V", reinterpret_cast<void*>(TouchPad)},
    {"photoToggle", "()V", reinterpret_cast<void*>(PhotoToggle)},
    {"convertMod",
     "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
     reinterpret_cast<void*>(ConvertMod)},
    {"unpackArchive", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
     reinterpret_cast<void*>(UnpackArchive)},
    {"photoState", "()[F", reinterpret_cast<void*>(PhotoState)},
    {"deckPortraitGenerations", "()[I", reinterpret_cast<void*>(DeckPortraitGenerations)},
    {"deckPortrait", "(I)[B", reinterpret_cast<void*>(DeckPortrait)},
    {"imageIdentity", "(I)[I", reinterpret_cast<void*>(ImageIdentity)},
    {"bootGuest", "()Z", reinterpret_cast<void*>(BootGuest)},
    {"bootReport", "()Ljava/lang/String;", reinterpret_cast<void*>(BootReport)},
    {"keyboardRequest", "()[Ljava/lang/String;", reinterpret_cast<void*>(KeyboardRequest)},
    {"keyboardAnswer", "(Ljava/lang/String;Z)V", reinterpret_cast<void*>(KeyboardAnswer)},
    {"exitRequest", "()Ljava/lang/String;", reinterpret_cast<void*>(ExitRequest)},
    {"requestQuit", "(Ljava/lang/String;)V", reinterpret_cast<void*>(RequestQuit)},
    {"audioHeadphones", "([I)V", reinterpret_cast<void*>(AudioHeadphones)},
    {"audioMediaVolume", "(F)V", reinterpret_cast<void*>(AudioMediaVolume)},
};

}  // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "JNI_OnLoad: GetEnv failed");
    return JNI_ERR;
  }
  jclass bridge = env->FindClass(kBridgeClass);
  if (!bridge) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "JNI_OnLoad: %s not found",
                        kBridgeClass);
    return JNI_ERR;
  }
  const int count = int(sizeof(kMethods) / sizeof(kMethods[0]));
  if (env->RegisterNatives(bridge, kMethods, count) != 0) {
    __android_log_print(ANDROID_LOG_ERROR, kTag,
                        "JNI_OnLoad: RegisterNatives failed");
    return JNI_ERR;
  }
  __android_log_print(ANDROID_LOG_INFO, kTag, "JNI_OnLoad: %d methods registered",
                      count);
  return JNI_VERSION_1_6;
}
