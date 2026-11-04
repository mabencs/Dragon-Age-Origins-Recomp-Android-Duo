#!/usr/bin/env python3
"""
assemble_runtime.py - build the grafted Xenia source tree for the Vulkan port.

Phase 3.1. DAORecomp vendors a stripped Xenia subset containing the Xenos frontend
and *only* the Direct3D 12 backend. This assembles a tree that has the Vulkan backend
instead, from three inputs:

  1. DAORecomp's vendored Xenia subset          (the frontend; the valuable part)
  2. Xenia Canary's Vulkan files                (58 files, added verbatim)
  3. the edits in EDITS below                   (our actual changes)

Nothing is committed to the repository except this script: the output tree is
reproducible from two pinned upstream checkouts plus the edits here, so our real diff
against upstream stays small and legible instead of being buried in 60,000 lines of
copied third-party code.

### Why edits rather than patch files

Upstream is pinned (see THIRD_PARTY.md), so these edits are stable. Expressing them as
explicit find/replace pairs with assertions means that if a pin is ever moved and the
surrounding code has changed, assembly fails loudly and names the edit, rather than a
patch applying at the wrong offset or being silently skipped.

### What the edits do

They sever the Vulkan backend's dependency on the Xbox 360 *emulator*. Xenia's backend
reaches into the kernel, audio system and emulator object; DAORecomp already solved
this for D3D12 by replacing those with `xenia/dao_host.h`, a 28-line interface of four
functions. The same substitution applied to the Vulkan backend turns out to need only
the handful of edits below, because the emulator usage is confined to a constructor
parameter and a cosmetic window title.

Usage:
  python tools/assemble_runtime.py
  python tools/assemble_runtime.py --check     report what would change
  python tools/assemble_runtime.py --clean
"""
import argparse
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DAO_XENIA = ROOT / "third_party" / "DAORecomp" / "xenia" / "src"
CANARY = ROOT / "third_party" / "xenia-canary" / "src"
OUT = ROOT / "build" / "runtime-tree" / "src"

# Files to take from Canary. Demos and standalone tools are excluded: they are what
# drags in the emulator's input and window stack, and we build neither.
GRAFT_DIRS = ["xenia/gpu/vulkan", "xenia/ui/vulkan"]
GRAFT_GLOBS = [("xenia/gpu", "spirv_shader_translator*"),
               ("xenia/gpu", "spirv_builder*"),
               ("xenia/gpu", "spirv_shader.*"),
               ("xenia/gpu", "spirv_compatibility.h")]
EXCLUDE = re.compile(r"window_demo|trace_viewer_main|trace_dump_main|_main\.cc$")
# .inc matters: xenia/ui/vulkan/functions/*.inc are the Vulkan entry-point tables the
# loader x-macros over, and they include an Android surface list already.
SRC_EXT = (".cc", ".h", ".inc")

VK_API_H = "xenia/ui/vulkan/vulkan_api.h"
# A DAORecomp file, not an upstream one: edits are verified against whichever
# source actually provides the file.
DAO_RESOLVE = "xenia/gpu/shaders/dao_resolve_direct.xesli"
DAO_MUTEX = "xenia/base/mutex.cc"
DAO_GFX_SYSTEM = "xenia/gpu/graphics_system.cc"
ANDROID_WINDOW = "xenia/ui/window_android.cc"
WINDOW_C = "xenia/ui/window.cc"
THREADING_POSIX = "xenia/base/threading_posix.cc"
ANDROID_FILESYSTEM = "xenia/base/filesystem_android.cc"
VKP_H = "xenia/ui/vulkan/vulkan_presenter.h"
VKP_C = "xenia/ui/vulkan/vulkan_presenter.cc"
VKI_H = "xenia/ui/vulkan/vulkan_instance.h"
VKI_C = "xenia/ui/vulkan/vulkan_instance.cc"
GCT_H = "xenia/ui/gpu_completion_timeline.h"
VCP_H = "xenia/gpu/vulkan/vulkan_command_processor.h"
VCP_C = "xenia/gpu/vulkan/vulkan_command_processor.cc"
CP_C = "xenia/gpu/command_processor.cc"
VGS_H = "xenia/gpu/vulkan/vulkan_graphics_system.h"
VGS_C = "xenia/gpu/vulkan/vulkan_graphics_system.cc"
PM4_H = "xenia/gpu/pm4_command_processor_implement.h"
VRTC_C = "xenia/gpu/vulkan/vulkan_render_target_cache.cc"
RTC_C = "xenia/gpu/render_target_cache.cc"
VRTC_H = "xenia/gpu/vulkan/vulkan_render_target_cache.h"
TC_C = "xenia/gpu/texture_cache.cc"
TU_C = "xenia/gpu/texture_util.cc"
SS_H = "xenia/gpu/shader_storage.h"
VPC_H = "xenia/gpu/vulkan/vulkan_pipeline_cache.h"
VSM_C = "xenia/gpu/vulkan/vulkan_shared_memory.cc"
VTC_C = "xenia/gpu/vulkan/vulkan_texture_cache.cc"
VPC_C = "xenia/gpu/vulkan/vulkan_pipeline_cache.cc"
LOGGING_C = "xenia/base/logging.cc"
DFT_H = "xenia/gpu/d3d12/dao_frame_trace.h"
SST_C = "xenia/gpu/spirv_shader_translator.cc"
VPP_C = "xenia/gpu/vulkan/vulkan_primitive_processor.cc"
SST_H = "xenia/gpu/spirv_shader_translator.h"
VS_C = "xenia/gpu/vulkan/vulkan_shader.cc"
SST_RB = "xenia/gpu/spirv_shader_translator_rb.cc"
DCB_C = "xenia/gpu/vulkan/deferred_command_buffer.cc"
DCB_H = "xenia/gpu/vulkan/deferred_command_buffer.h"
VDF_INC = "xenia/ui/vulkan/functions/device_1_0.inc"
SST_FETCH = "xenia/gpu/spirv_shader_translator_fetch.cc"
TC_H = "xenia/gpu/texture_cache.h"
SST_ALU = "xenia/gpu/spirv_shader_translator_alu.cc"
VKD_H = "xenia/ui/vulkan/vulkan_device.h"
VKD_C = "xenia/ui/vulkan/vulkan_device.cc"
LOG_H = "xenia/base/logging.h"

# (file, description, find, replace). Every one must match exactly once.
EDITS = [
    # A WAIT_REG_MEM the command processor sits in for a second is waiting on
    # something that is not coming - a sync word the title's interrupt handler never
    # wrote, a register nothing sets. Bringing the GPU up (task 5) was blind to it:
    # the command processor thread simply slept. Once a second, what it is waiting on.
    (PM4_H, "WAIT_REG_MEM: a poll counter",
     "  bool matched = false;\n",
     "  bool matched = false;\n"
     "  uint32_t stalled_polls = 0;  // DAO/Warden: see below\n"),
    # DAORecomp's per-frame draw trace (dao_frame_trace.h) is fed by its Direct3D 12
    # command processor and not by Xenia's Vulkan one. Three calls make the Vulkan
    # backend report the same: the swap boundary, each issued draw, each resolve.
    # Without the first, the per-swap draw counts in the GPU status line stay at
    # "GPU swaps 0" however many frames are presented.
    (VCP_C, "frame trace: the swap boundary",
     "  SCOPE_profile_cpu_f(\"gpu\");\n"
     "  EndZPDFrame();\n",
     "  SCOPE_profile_cpu_f(\"gpu\");\n"
     "  EndZPDFrame();\n"
     "  // DAO/Warden: the frame trace's swap boundary (dao_frame_trace.h), which the\n"
     "  // Direct3D 12 backend has and this one did not; the per-swap draw counts in\n"
     "  // the GPU status line and the one-frame trace depend on it.\n"
     "  dao_trace::OnSwap(frontbuffer_ptr);\n"
     "  // DAO/Warden (3.8): the traced frame's GPU time per render pass, once its\n"
     "  // submission has completed - each timestamp with its availability word.\n"
     "  if (dao_trace::timing_pending && !dao_trace::Active() && dao_trace::timing_submission &&\n"
     "      GetCompletedSubmission() >= dao_trace::timing_submission) {\n"
     "    dao_trace::timing_pending = false;\n"
     "    const ui::vulkan::VulkanDevice* timing_device = GetVulkanDevice();\n"
     "    const uint32_t stamp_count = dao_trace::pass_count * 2;\n"
     "    std::vector<uint64_t> stamps(size_t(stamp_count) * 2, 0);\n"
     "    timing_device->functions().vkGetQueryPoolResults(\n"
     "        timing_device->device(), reinterpret_cast<VkQueryPool>(dao_trace::timestamp_pool), 0,\n"
     "        stamp_count, stamps.size() * sizeof(uint64_t), stamps.data(), 2 * sizeof(uint64_t),\n"
     "        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);\n"
     "    const double ms_per_tick = double(dao_trace::timestamp_period_ns) / 1.0e6;\n"
     "    const char* timing_dir = std::getenv(\"WARDEN_FILES_DIR\");\n"
     "    FILE* timing_file =\n"
     "        timing_dir ? std::fopen((std::string(timing_dir) + \"/gpu-timing.txt\").c_str(), \"a\")\n"
     "                   : nullptr;\n"
     "    double total = 0.0;\n"
     "    double gaps = 0.0;\n"
     "    uint64_t previous_end = 0;\n"
     "    for (uint32_t pass = 0; pass < dao_trace::pass_count; ++pass) {\n"
     "      const uint64_t begin = stamps[pass * 4], begin_ok = stamps[pass * 4 + 1];\n"
     "      const uint64_t end = stamps[pass * 4 + 2], end_ok = stamps[pass * 4 + 3];\n"
     "      const dao_trace::PassTiming& timing = dao_trace::pass_timings[pass];\n"
     "      if (!begin_ok || !end_ok || end < begin) {\n"
     "        XELOGI(\"FT gpu pass {}: no timing ({}x{}, {} draws)\", pass, timing.width,\n"
     "               timing.height, timing.draws);\n"
     "        continue;\n"
     "      }\n"
     "      const double ms = double(end - begin) * ms_per_tick;\n"
     "      const double gap = previous_end ? double(begin - previous_end) * ms_per_tick : 0.0;\n"
     "      total += ms;\n"
     "      gaps += gap;\n"
     "      previous_end = end;\n"
     "      const std::string pass_line = fmt::format(\n"
     "          \"FT gpu pass {}: {:.3f} ms, {}x{}, {} draws from draw {} (VS {:016X} PS {:016X}), \"\n"
     "          \"{:.3f} ms before it\",\n"
     "          pass, ms, timing.width, timing.height, timing.draws, timing.first_draw,\n"
     "          timing.first_vs, timing.first_ps, gap);\n"
     "      XELOGI(\"{}\", pass_line);\n"
     "      if (timing_file) {\n"
     "        std::fprintf(timing_file, \"%s\\n\", pass_line.c_str());\n"
     "      }\n"
     "    }\n"
     "    const std::string frame_line = fmt::format(\n"
     "        \"FT gpu frame: {:.3f} ms in {} render passes, {:.3f} ms between them, \"\n"
     "        \"{} draws (timestamp period {} ns)\", total, dao_trace::pass_count, gaps,\n"
     "        dao_trace::draws_in_frame, dao_trace::timestamp_period_ns);\n"
     "    XELOGI(\"{}\", frame_line);\n"
     "    if (timing_file) {\n"
     "      std::fprintf(timing_file, \"%s\\n\\n\", frame_line.c_str());\n"
     "      std::fclose(timing_file);\n"
     "    }\n"
     "  }\n"
     "  // A traced frame starting here counts its passes and draws from zero.\n"
     "  if (dao_trace::Active() && !dao_trace::timing_pending) {\n"
     "    dao_trace::pass_count = 0;\n"
     "    dao_trace::draws_in_frame = 0;\n"
     "  }\n"),
    (VCP_C, "frame trace: each issued draw",
     "  texture_cache_->RequestTextures(used_texture_mask);\n",
     "  texture_cache_->RequestTextures(used_texture_mask);\n"
     "  // DAO/Warden: as the Direct3D 12 backend logs it - counts the draw as issued,\n"
     "  // and under DAO_GPU_FRAME_TRACE writes its shaders, targets and textures.\n"
     "  dao_trace::LogDraw(\n"
     "      regs, vertex_shader->ucode_data_hash(),\n"
     "      pixel_shader ? pixel_shader->ucode_data_hash() : 0, normalized_color_mask,\n"
     "      pixel_shader ? pixel_shader->GetUsedTextureMaskAfterTranslation() : 0,\n"
     "      vertex_shader->GetUsedTextureMaskAfterTranslation(), prim_type,\n"
     "      index_count);\n"
     "  // DAO/Warden (BUG-052): kept for the resolve that follows (dao_frame_trace.h).\n"
     "  dao_trace::RecordDraw(regs, vertex_shader->ucode_data_hash(),\n"
     "                        pixel_shader ? pixel_shader->ucode_data_hash() : 0,\n"
     "                        pixel_shader ? pixel_shader->GetUsedTextureMaskAfterTranslation() : 0,\n"
     "                        uint32_t(prim_type), index_count,\n"
     "                        pixel_shader ? pixel_shader->constant_register_map().float_bitmap\n"
     "                                     : nullptr,\n"
     "                        vertex_shader->constant_register_map().float_bitmap);\n"
     "  // DAO/Warden (BUG-049): DAO_HACK_LOG_BINDINGS_PS=<pixel shader hash> - the texture\n"
     "  // bindings of that shader's draws are logged as a traced frame's are.\n"
     "  {\n"
     "    static const char* dao_log_ps_text = std::getenv(\"DAO_HACK_LOG_BINDINGS_PS\");\n"
     "    static const uint64_t dao_log_ps =\n"
     "        dao_log_ps_text ? uint64_t(std::strtoull(dao_log_ps_text, nullptr, 16)) : 0;\n"
     "    dao_trace::log_bindings = dao_log_ps && pixel_shader &&\n"
     "                              pixel_shader->ucode_data_hash() == dao_log_ps;\n"
     "  }\n"
     "  // DAO/Warden (BUG-052): the skinned head's draws (this vertex shader is the\n"
     "  // portraits' and the scene's) - how many VS constants are zero vectors, the first\n"
     "  // 400, and which whenever that set changes: when the face's bones are posed. Only\n"
     "  // while the portraits are investigated (DAO_HACK_RESOLVE_DUMP_PITCH set).\n"
     "  static const bool dao_log_heads =\n"
     "      std::getenv(\"DAO_HACK_RESOLVE_DUMP_PITCH\") != nullptr;\n"
     "  if (dao_log_heads &&\n"
     "      vertex_shader->ucode_data_hash() == UINT64_C(0x0CA594B641A10C7E)) {\n"
     "    static uint32_t head_draws_logged = 0;\n"
     "    static std::string head_last_zero;\n"
     "    if (head_draws_logged < 400) {\n"
     "      ++head_draws_logged;\n"
     "      const dao_trace::RecentDraw& head =\n"
     "          dao_trace::recent_draws[(dao_trace::recent_draw_count - 1) % 64];\n"
     "      std::string zero = dao_trace::BitRanges(head.vs_zero, 256);\n"
     "      const bool same = zero == head_last_zero;\n"
     "      XELOGI(\"GPU: head draw {} (draw {}, {} indices): {} of {} VS constants zero{}{}\",\n"
     "             head_draws_logged, head.number, index_count, head.vs_float_zero,\n"
     "             head.vs_float_count, same ? \" (as before)\" : \": \", same ? \"\" : zero);\n"
     "      head_last_zero = std::move(zero);\n"
     "    }\n"
     "    // The first portrait head draw (into the 160-pitch target) whose face\n"
     "    // bones are set: find the array they are copied from, and watch it.\n"
     "    static bool dao_face_searched = false;\n"
     "    const uint32_t* dao_c186 =\n"
     "        &regs.values[XE_GPU_REG_SHADER_CONSTANT_000_X + 186 * 4];\n"
     "    const dao_trace::RecentDraw& dao_head =\n"
     "        dao_trace::recent_draws[(dao_trace::recent_draw_count - 1) % 64];\n"
     "    if (!dao_face_searched && (dao_head.surface_info & 0x3FFF) == 160 &&\n"
     "        ((dao_c186[0] | dao_c186[1] | dao_c186[2] | dao_c186[3]) & 0x7FFFFFFF)) {\n"
     "      dao_face_searched = true;\n"
     "      warden::DaoFindAndWatch(dao_c186, 16);\n"
     "    }\n"
     "    // Each portrait head draw, the first eight: interrupt every guest thread - the\n"
     "    // one recording the portraits may still be in that code (call chain and the\n"
     "    // functions it entered last).\n"
     "    static uint32_t dao_portrait_samples = 0;\n"
     "    if ((dao_head.surface_info & 0x3FFF) == 160 && dao_portrait_samples < 8) {\n"
     "      ++dao_portrait_samples;\n"
     "      warden::SampleGuestThreadsNow(\"a portrait's head draw reached the GPU\");\n"
     "    }\n"
     "  }\n"
     "  if (dao_log_heads) {\n"
     "    warden::DaoWatchPoll();  // BUG-052: what the write watch saw\n"
     "  }\n"),
    (VCP_C, "frame trace: each resolve",
     "  if (!render_target_cache_->Resolve(*memory_, *shared_memory_, *texture_cache_,\n"
     "                                     written_address, written_length,\n"
     "                                     &copy_dest_info, &is_scaled)) {\n"
     "    return false;\n"
     "  }\n"
     "\n"
     "  // CPU readback resolve path (if not disabled).\n",
     "  if (!render_target_cache_->Resolve(*memory_, *shared_memory_, *texture_cache_,\n"
     "                                     written_address, written_length,\n"
     "                                     &copy_dest_info, &is_scaled)) {\n"
     "    return false;\n"
     "  }\n"
     "  // DAO/Warden: the one-frame trace's resolve line, as the Direct3D 12 backend,\n"
     "  // with what was written - a resolve that wrote nothing is a black frame.\n"
     "  dao_trace::LogResolve(*register_file_);\n"
     "  // DAO/Warden (BUG-052): each distinct resolve destination, the first 256 of the\n"
     "  // boot whether traced or not - what writes the memory the HUD's textures are in.\n"
     "  {\n"
     "    static uint64_t dao_resolves_seen[256];\n"
     "    static uint32_t dao_resolves_seen_count = 0;\n"
     "    const uint64_t dao_key = (uint64_t(written_address) << 32) | written_length;\n"
     "    bool dao_new = dao_resolves_seen_count < 256;\n"
     "    for (uint32_t i = 0; dao_new && i < dao_resolves_seen_count; ++i) {\n"
     "      dao_new = dao_resolves_seen[i] != dao_key;\n"
     "    }\n"
     "    if (dao_new) {\n"
     "      dao_resolves_seen[dao_resolves_seen_count++] = dao_key;\n"
     "      XELOGI(\"GPU: resolve to {:08X}+{:X}, dest info {:08X} pitch {:08X} (distinct {})\",\n"
     "             written_address, written_length,\n"
     "             register_file_->values[XE_GPU_REG_RB_COPY_DEST_INFO],\n"
     "             register_file_->values[XE_GPU_REG_RB_COPY_DEST_PITCH],\n"
     "             dao_resolves_seen_count);\n"
     "    }\n"
     "  }\n"
     "  if (dao_trace::Active()) {\n"
     "    XELOGI(\"FT resolve wrote {:08X}+{:X}, dest endian {}\", written_address,\n"
     "           written_length,\n"
     "           uint32_t(register_file_->Get<reg::RB_COPY_DEST_INFO>().copy_dest_endian));\n"
     "  }\n"
     "\n"
     "  // CPU readback resolve path (if not disabled).\n"),
    (TC_C, "frame trace: the trace header",
     "#include \"xenia/gpu/shared_memory.h\"\n",
     "#include \"xenia/gpu/shared_memory.h\"\n"
     "#include \"xenia/gpu/d3d12/dao_frame_trace.h\"\n"),
    (TC_C, "frame trace: textures taken as up to date",
     "  if (!texture.base_outdated_lockless() && !texture.mips_outdated_lockless()) {\n"
     "    return true;\n"
     "  }\n",
     "  if (!texture.base_outdated_lockless() && !texture.mips_outdated_lockless()) {\n"
     "    // DAO/Warden: in the traced frame, the textures a draw took as up to date - a\n"
     "    // texture the title rewrites every frame that is never reloaded has lost its\n"
     "    // write watch (BUG-039).\n"
     "    if (dao_trace::Active()) {\n"
     "      const TextureKey& k = texture.key();\n"
     "      XELOGI(\"FT tex @{:08X}+{:X} fmt {} {}x{}: up to date\", k.base_page << 12,\n"
     "             texture.GetGuestBaseSize(), uint32_t(k.format), k.GetWidth(),\n"
     "             k.GetHeight());\n"
     "    }\n"
     "    return true;\n"
     "  }\n"),
    (TC_C, "frame trace: textures loaded",
     "  texture.LogAction(\"Loaded\");\n"
     "\n"
     "  return true;\n"
     "}\n",
     "  texture.LogAction(\"Loaded\");\n"
     "  // DAO/Warden: in the traced frame, each texture (re)loaded from guest memory - and\n"
     "  // (BUG-049) those at DAO_HACK_RESOLVE_DUMP_ADDRESS, through an event gate.\n"
     "  static uint32_t loads_logged = 0;  // the first 64, traced or not\n"
     "  static const char* dao_watch_text = std::getenv(\"DAO_HACK_RESOLVE_DUMP_ADDRESS\");\n"
     "  static const uint32_t dao_watch_page =\n"
     "      dao_watch_text ? (uint32_t(std::strtoul(dao_watch_text, nullptr, 16)) >> 12) : 0;\n"
     "  static dao_trace::EventGate dao_watch_gate;\n"
     "  if (dao_trace::Active() || loads_logged++ < 64 ||\n"
     "      (dao_watch_page && texture_key.base_page == dao_watch_page &&\n"
     "       dao_watch_gate.Pass())) {\n"
     "    XELOGI(\"FT tex @{:08X}+{:X} fmt {} {}x{}: loaded (base {}, mips {}; {} so far)\",\n"
     "           texture_key.base_page << 12, texture.GetGuestBaseSize(),\n"
     "           uint32_t(texture_key.format), texture_key.GetWidth(),\n"
     "           texture_key.GetHeight(), base_outdated, mips_outdated,\n"
     "           dao_watch_gate.count);\n"
     "  }\n"
     "  if (dao_watch_page && texture_key.base_page == dao_watch_page) {\n"
     "    dao_trace::RecordTextureEvent(\n"
     "        &texture, 0,\n"
     "        dao_trace::TextureKeyBits(texture_key.tiled, uint32_t(texture_key.endianness),\n"
     "                                  texture_key.signed_separate,\n"
     "                                  texture_key.scaled_resolve, texture_key.pitch,\n"
     "                                  uint32_t(texture_key.format)));\n"
     "  }\n"
     "  return true;\n"
     "}\n"),
    (VSM_C, "frame trace: the trace header",
     "#include \"xenia/gpu/vulkan/vulkan_shared_memory.h\"\n",
     "#include \"xenia/gpu/vulkan/vulkan_shared_memory.h\"\n"
     "#include \"xenia/gpu/d3d12/dao_frame_trace.h\"\n"),
    (VSM_C, "frame trace: what each upload reads from guest memory",
     "      MakeRangeValid(upload_range_start << page_size_log2(),\n"
     "                     uint32_t(upload_buffer_size), false);\n",
     "      MakeRangeValid(upload_range_start << page_size_log2(),\n"
     "                     uint32_t(upload_buffer_size), false);\n"
     "      // DAO/Warden: in the traced frame, what each upload reads from guest memory\n"
     "      // - a texture that samples zeros while guest memory holds data was either\n"
     "      // never uploaded or uploaded from somewhere else (BUG-039).\n"
     "      static uint32_t uploads_logged = 0;  // the first 64, traced or not\n"
     "      if (dao_trace::Active() || uploads_logged++ < 64 ||\n"
     "          dao_trace::UploadLogged(upload_range_start << page_size_log2(),\n"
     "                                  uint32_t(upload_buffer_size))) {\n"
     "        const uint8_t* src =\n"
     "            memory().TranslatePhysical(upload_range_start << page_size_log2());\n"
     "        const uint32_t n = uint32_t(std::min<VkDeviceSize>(upload_buffer_size, 16u << 20));\n"
     "        uint32_t nonzero = 0;\n"
     "        for (uint32_t i = 0; i < n; ++i) {\n"
     "          nonzero += src[i] != 0;\n"
     "        }\n"
     "        XELOGI(\"FT upload @{:08X}+{:X}: {} non-zero bytes\",\n"
     "               upload_range_start << page_size_log2(), n, nonzero);\n"
     "      }\n"),
    (VTC_C, "frame trace: the trace header",
     "#include \"xenia/gpu/vulkan/vulkan_texture_cache.h\"\n",
     "#include \"xenia/gpu/vulkan/vulkan_texture_cache.h\"\n"
     "#include \"xenia/gpu/d3d12/dao_frame_trace.h\"\n"),
    (VTC_C, "frame trace: what each fetch constant is bound to",
     "  if (image_view != VK_NULL_HANDLE) {\n"
     "    return image_view;\n"
     "  }\n"
     "  switch (dimension) {\n"
     "    case xenos::FetchOpDimension::k3DOrStacked:\n"
     "      return null_image_view_3d_;\n",
     "  // DAO/Warden: in the traced frame, what each fetch constant is bound to - a draw\n"
     "  // whose textures are the null image samples zeros (BUG-039) - and for the draws\n"
     "  // of DAO_HACK_LOG_BINDINGS_PS's pixel shader (BUG-049), with the texture's state.\n"
     "  if (dao_trace::Active() || dao_trace::log_bindings) {\n"
     "    XELOGI(\"FT bind tf{} dim {}: {}{}\", fetch_constant_index, uint32_t(dimension),\n"
     "           image_view != VK_NULL_HANDLE ? \"image view\" : \"NULL IMAGE\",\n"
     "           binding ? (binding->texture ? \" (binding with texture)\"\n"
     "                                       : \" (binding without texture)\")\n"
     "                   : \" (no valid binding)\");\n"
     "    // DAO/Warden (BUG-052): which texture the binding is - its guest address,\n"
     "    // format and size - to tell a HUD draw's textures apart.\n"
     "    if (binding) {\n"
     "      XELOGI(\"FT bind tf{} key @{:08X} mips @{:08X} fmt {} {}x{} pitch {} tiled {} dim {}\",\n"
     "             fetch_constant_index, uint32_t(binding->key.base_page) << 12,\n"
     "             uint32_t(binding->key.mip_page) << 12, uint32_t(binding->key.format),\n"
     "             uint32_t(binding->key.width_minus_1) + 1,\n"
     "             uint32_t(binding->key.height_minus_1) + 1, uint32_t(binding->key.pitch),\n"
     "             uint32_t(binding->key.tiled), uint32_t(binding->key.dimension));\n"
     "      if (binding->texture) {\n"
     "        XELOGI(\"FT bind tf{} texture: scaled resolve {}, base outdated {}, mips \"\n"
     "               \"outdated {}, guest base {} bytes, last used in submission {} (now {})\",\n"
     "               fetch_constant_index, uint32_t(binding->key.scaled_resolve),\n"
     "               binding->texture->base_outdated_lockless(),\n"
     "               binding->texture->mips_outdated_lockless(),\n"
     "               binding->texture->GetGuestBaseSize(),\n"
     "               binding->texture->last_usage_submission_index(),\n"
     "               command_processor_.GetCurrentSubmission());\n"
     "        // BUG-049: which texture object, its key's distinguishing bits, and - once a\n"
     "        // draw - the history of the textures at the watched address.\n"
     "        XELOGI(\"FT bind tf{} texture object {} key {:08X}\", fetch_constant_index,\n"
     "               fmt::ptr(binding->texture),\n"
     "               dao_trace::TextureKeyBits(binding->key.tiled,\n"
     "                                         uint32_t(binding->key.endianness),\n"
     "                                         binding->key.signed_separate,\n"
     "                                         binding->key.scaled_resolve,\n"
     "                                         binding->key.pitch,\n"
     "                                         uint32_t(binding->key.format)));\n"
     "        static uint32_t dao_events_logged_for = UINT32_MAX;\n"
     "        if (dao_trace::log_bindings &&\n"
     "            dao_events_logged_for != dao_trace::recent_draw_count) {\n"
     "          dao_events_logged_for = dao_trace::recent_draw_count;\n"
     "          dao_trace::LogTextureEvents(binding->texture);\n"
     "        }\n"
     "      }\n"
     "    }\n"
     "  }\n"
     "  if (image_view != VK_NULL_HANDLE) {\n"
     "    return image_view;\n"
     "  }\n"
     "  switch (dimension) {\n"
     "    case xenos::FetchOpDimension::k3DOrStacked:\n"
     "      return null_image_view_3d_;\n"),
    (DFT_H, "frame trace: the draw's colour mask and blend state",
     "  line += fmt::format(\" pitch {} msaa {}\", surface.surface_pitch,\n"
     "                      uint32_t(surface.msaa_samples));\n",
     "  line += fmt::format(\" pitch {} msaa {}\", surface.surface_pitch,\n"
     "                      uint32_t(surface.msaa_samples));\n"
     "  // DAO/Warden: the colour mask, blend control and colour control - a draw whose\n"
     "  // RGB is masked or blended away leaves the clear colour behind (BUG-039).\n"
     "  line += fmt::format(\" mask {:X} blend0 {:08X} cc {:08X}\",\n"
     "                      regs.values[XE_GPU_REG_RB_COLOR_MASK],\n"
     "                      regs.values[XE_GPU_REG_RB_BLENDCONTROL0],\n"
     "                      regs.values[XE_GPU_REG_RB_COLORCONTROL]);\n"
     "  // DAO/Warden: the raw depth control and stencil reference/mask registers - a\n"
     "  // stencil test that fails everywhere leaves nothing behind either (BUG-045).\n"
     "  line += fmt::format(\" dc {:08X} sref {:08X} sref_bf {:08X}\",\n"
     "                      regs.values[XE_GPU_REG_RB_DEPTHCONTROL],\n"
     "                      regs.values[XE_GPU_REG_RB_STENCILREFMASK],\n"
     "                      regs.values[XE_GPU_REG_RB_STENCILREFMASK_BF]);\n"),
    (VCP_C, "frame trace: the viewport, the vertex shader and its constants",
     "  draw_util::GetHostViewportInfo(&gviargs, viewport_info);\n",
     "  draw_util::GetHostViewportInfo(&gviargs, viewport_info);\n"
     "  // DAO/Warden: in the traced frame, the vertex side of the draw - the host\n"
     "  // viewport the guest registers came to, the vertex shader's microcode (once)\n"
     "  // and its first constants (BUG-039: a full-screen quad covering a 1280x90 band).\n"
     "  if (dao_trace::Active()) {\n"
     "    auto f = [&](uint32_t r) {\n"
     "      float v;\n"
     "      std::memcpy(&v, &regs.values[r], sizeof(v));\n"
     "      return v;\n"
     "    };\n"
     "    XELOGI(\"FT viewport host xy_offset ({} {}) xy_extent ({} {}) z {:.3f}..{:.3f} \"\n"
     "           \"ndc_scale ({:.4f} {:.4f} {:.4f}) ndc_offset ({:.4f} {:.4f} {:.4f}) | guest \"\n"
     "           \"xs {:.2f} xo {:.2f} ys {:.2f} yo {:.2f} zs {:.4f} zo {:.4f} vte {:08X} \"\n"
     "           \"window_scissor {:08X} {:08X} screen_scissor {:08X} {:08X} window_offset \"\n"
     "           \"{:08X} su_sc_mode {:08X}\",\n"
     "           viewport_info.xy_offset[0], viewport_info.xy_offset[1],\n"
     "           viewport_info.xy_extent[0], viewport_info.xy_extent[1], viewport_info.z_min,\n"
     "           viewport_info.z_max, viewport_info.ndc_scale[0], viewport_info.ndc_scale[1],\n"
     "           viewport_info.ndc_scale[2], viewport_info.ndc_offset[0],\n"
     "           viewport_info.ndc_offset[1], viewport_info.ndc_offset[2],\n"
     "           f(XE_GPU_REG_PA_CL_VPORT_XSCALE), f(XE_GPU_REG_PA_CL_VPORT_XOFFSET),\n"
     "           f(XE_GPU_REG_PA_CL_VPORT_YSCALE), f(XE_GPU_REG_PA_CL_VPORT_YOFFSET),\n"
     "           f(XE_GPU_REG_PA_CL_VPORT_ZSCALE), f(XE_GPU_REG_PA_CL_VPORT_ZOFFSET),\n"
     "           regs.values[XE_GPU_REG_PA_CL_VTE_CNTL],\n"
     "           regs.values[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL],\n"
     "           regs.values[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR],\n"
     "           regs.values[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL],\n"
     "           regs.values[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR],\n"
     "           regs.values[XE_GPU_REG_PA_SC_WINDOW_OFFSET],\n"
     "           regs.values[XE_GPU_REG_PA_SU_SC_MODE_CNTL]);\n"
     "    static std::vector<uint64_t> vs_disassembled;\n"
     "    const uint64_t vs_hash = vertex_shader->ucode_data_hash();\n"
     "    if (std::find(vs_disassembled.begin(), vs_disassembled.end(), vs_hash) ==\n"
     "        vs_disassembled.end()) {\n"
     "      vs_disassembled.push_back(vs_hash);\n"
     "      XELOGI(\"FT VS {:016X} microcode:\\n{}\", vs_hash, vertex_shader->ucode_disassembly());\n"
     "    }\n"
     "    std::string constants;\n"
     "    for (uint32_t c = 0; c < 8; ++c) {\n"
     "      constants += fmt::format(\" c{}=({:.4g} {:.4g} {:.4g} {:.4g})\", c,\n"
     "                               f(XE_GPU_REG_SHADER_CONSTANT_000_X + 4 * c),\n"
     "                               f(XE_GPU_REG_SHADER_CONSTANT_000_X + 4 * c + 1),\n"
     "                               f(XE_GPU_REG_SHADER_CONSTANT_000_X + 4 * c + 2),\n"
     "                               f(XE_GPU_REG_SHADER_CONSTANT_000_X + 4 * c + 3));\n"
     "    }\n"
     "    XELOGI(\"FT draw constants VS{}\", constants);\n"
     "    // DAO/Warden (BUG-045): a skinned vertex shader reads its bones with a0-relative\n"
     "    // addressing - how many of the 256 VS constants are non-zero, and the first bones\n"
     "    // and the unpack scales it uses.\n"
     "    if (vertex_shader->constant_register_map().float_dynamic_addressing) {\n"
     "      uint32_t nonzero = 0;\n"
     "      for (uint32_t c = 0; c < 256 * 4; ++c) {\n"
     "        nonzero += regs.values[XE_GPU_REG_SHADER_CONSTANT_000_X + c] != 0;\n"
     "      }\n"
     "      std::string bones;\n"
     "      for (uint32_t c : {32u, 33u, 34u, 35u, 36u, 37u, 38u, 39u, 40u, 41u, 42u, 43u, 44u, 45u,\n"
     "                         46u, 47u, 253u, 254u, 255u}) {\n"
     "        bones += fmt::format(\" c{}=({:.4g} {:.4g} {:.4g} {:.4g})\", c,\n"
     "                             f(XE_GPU_REG_SHADER_CONSTANT_000_X + 4 * c),\n"
     "                             f(XE_GPU_REG_SHADER_CONSTANT_000_X + 4 * c + 1),\n"
     "                             f(XE_GPU_REG_SHADER_CONSTANT_000_X + 4 * c + 2),\n"
     "                             f(XE_GPU_REG_SHADER_CONSTANT_000_X + 4 * c + 3));\n"
     "      }\n"
     "      XELOGI(\"FT draw VS dynamic constants: {} of 1024 VS constant words non-zero;{}\",\n"
     "             nonzero, bones);\n"
     "    }\n"
     "    // DAO/Warden (BUG-045): the same vertex shader run on the CPU with Xenia's\n"
     "    // ShaderInterpreter for vertices 0..2 - what oPos should be, to compare with what\n"
     "    // the GPU draws.\n"
     "    if (ShaderInterpreter::CanInterpretShader(*vertex_shader)) {\n"
     "      struct PositionSink : ShaderInterpreter::ExportSink {\n"
     "        float position[4] = {};\n"
     "        bool exported = false;\n"
     "        void Export(ucode::ExportRegister export_register, const float* value,\n"
     "                    uint32_t value_mask) override {\n"
     "          if (export_register == ucode::ExportRegister::kVSPosition) {\n"
     "            for (uint32_t i = 0; i < 4; ++i) {\n"
     "              if (value_mask & (1u << i)) {\n"
     "                position[i] = value[i];\n"
     "              }\n"
     "            }\n"
     "            exported = true;\n"
     "          }\n"
     "        }\n"
     "      };\n"
     "      ShaderInterpreter interpreter(*register_file_, *memory_);\n"
     "      interpreter.SetShader(*vertex_shader);\n"
     "      PositionSink sink;\n"
     "      interpreter.SetExportSink(&sink);\n"
     "      std::string positions;\n"
     "      for (uint32_t v = 0; v < 3; ++v) {\n"
     "        sink.exported = false;\n"
     "        interpreter.temp_registers()[0] = float(v);\n"
     "        interpreter.Execute();\n"
     "        positions += fmt::format(\" v{}=({:.4g} {:.4g} {:.4g} {:.4g}){}\", v, sink.position[0],\n"
     "                                 sink.position[1], sink.position[2], sink.position[3],\n"
     "                                 sink.exported ? \"\" : \" (no export)\");\n"
     "      }\n"
     "      XELOGI(\"FT draw VS on the CPU {:016X}: oPos{}\", vertex_shader->ucode_data_hash(),\n"
     "             positions);\n"
     "    }\n"
     "  }\n"),
    (VCP_C, "frame trace: the vertex buffers",
     "    if (vfetch_current_queued) {\n"
     "      // Pre-acquire the critical region so we're not repeatedly re-acquiring\n",
     "    if (vfetch_current_queued) {\n"
     "      // DAO/Warden: in the traced frame, each vertex buffer's address, size and\n"
     "      // first words as the GPU sees them (byte-swapped) - the quad's positions and\n"
     "      // texture coordinates (BUG-039).\n"
     "      if (dao_trace::Active()) {\n"
     "        for (uint32_t i = 0; i < vfetch_current_queued; ++i) {\n"
     "          const uint32_t address = vfetch_addresses[i] << 2;\n"
     "          const uint32_t size = vfetch_sizes[i] << 2;\n"
     "          const uint8_t* bytes = memory_->TranslatePhysical(address);\n"
     "          std::string words;\n"
     "          const uint32_t count = std::min<uint32_t>(size / 4, 32);\n"
     "          for (uint32_t w = 0; w < count; ++w) {\n"
     "            uint32_t value;\n"
     "            std::memcpy(&value, bytes + 4 * w, sizeof(value));\n"
     "            value = xe::byte_swap(value);\n"
     "            float as_float;\n"
     "            std::memcpy(&as_float, &value, sizeof(as_float));\n"
     "            words += fmt::format(\" {:08X}({:.4g})\", value, as_float);\n"
     "          }\n"
     "          XELOGI(\"FT vfetch {} @{:08X}+{:X}:{}\", i, address, size, words);\n"
     "        }\n"
     "      }\n"
     "      // Pre-acquire the critical region so we're not repeatedly re-acquiring\n"),
    (VRTC_C, "frame trace: the trace header",
     "#include \"xenia/gpu/vulkan/vulkan_render_target_cache.h\"\n",
     "#include \"xenia/gpu/vulkan/vulkan_render_target_cache.h\"\n"
     "#include \"xenia/gpu/d3d12/dao_frame_trace.h\"\n"),
    (VRTC_C, "frame trace: the resolve's rectangle, dump and dispatch",
     "    assert_true(copy_group_count_x && copy_group_count_y);\n",
     "    assert_true(copy_group_count_x && copy_group_count_y);\n"
     "    // DAO/Warden: in the traced frame, the resolve's rectangle, the EDRAM dump\n"
     "    // and the copy dispatch (BUG-039: the front buffer got a 1280x90 band).\n"
     "    if (dao_trace::Active()) {\n"
     "      XELOGI(\"FT resolve rect {}x{} px, EDRAM offset ({} {}) /8, dest pitch {} height \"\n"
     "             \"{} offset ({} {}) /8, extent {:08X}+{:X}, dump base {} rows {} \"\n"
     "             \"row_length {} pitch {}, copy shader {} groups {}x{}\",\n"
     "             uint32_t(resolve_info.coordinate_info.width_div_8) << 3,\n"
     "             uint32_t(resolve_info.height_div_8) << 3,\n"
     "             uint32_t(resolve_info.coordinate_info.edram_offset_x_div_8),\n"
     "             uint32_t(resolve_info.coordinate_info.edram_offset_y_div_8),\n"
     "             uint32_t(resolve_info.copy_dest_coordinate_info.pitch_aligned_div_32) << 5,\n"
     "             uint32_t(resolve_info.copy_dest_coordinate_info.height_aligned_div_32) << 5,\n"
     "             uint32_t(resolve_info.copy_dest_coordinate_info.offset_x_div_8),\n"
     "             uint32_t(resolve_info.copy_dest_coordinate_info.offset_y_div_8),\n"
     "             uint32_t(resolve_info.copy_dest_extent_start),\n"
     "             uint32_t(resolve_info.copy_dest_extent_length), dump_base, dump_rows,\n"
     "             dump_row_length_used, dump_pitch, uint32_t(copy_shader),\n"
     "             copy_group_count_x, copy_group_count_y);\n"
     "    }\n"),
    (VCP_C, "frame trace: the pixel shader's float constants as uploaded",
     "      buffer_info.range = VkDeviceSize(float_constants_size);\n"
     "      for (uint32_t i = 0; i < 4; ++i) {\n"
     "        uint64_t float_constant_map_entry =\n"
     "            current_float_constant_map_pixel_[i];\n"
     "        uint32_t float_constant_index;\n"
     "        while (xe::bit_scan_forward(float_constant_map_entry,\n"
     "                                    &float_constant_index)) {\n"
     "          float_constant_map_entry &= ~(1ull << float_constant_index);\n"
     "          std::memcpy(mapping,\n"
     "                      &regs[XE_GPU_REG_SHADER_CONSTANT_256_X + (i << 8) +\n"
     "                            (float_constant_index << 2)],\n"
     "                      sizeof(float) * 4);\n"
     "          mapping += sizeof(float) * 4;\n",
     "      buffer_info.range = VkDeviceSize(float_constants_size);\n"
     "      uint32_t dao_trace_slot = 0;  // DAO/Warden: the traced frame's constant log\n"
     "      for (uint32_t i = 0; i < 4; ++i) {\n"
     "        uint64_t float_constant_map_entry =\n"
     "            current_float_constant_map_pixel_[i];\n"
     "        uint32_t float_constant_index;\n"
     "        while (xe::bit_scan_forward(float_constant_map_entry,\n"
     "                                    &float_constant_index)) {\n"
     "          float_constant_map_entry &= ~(1ull << float_constant_index);\n"
     "          std::memcpy(mapping,\n"
     "                      &regs[XE_GPU_REG_SHADER_CONSTANT_256_X + (i << 8) +\n"
     "                            (float_constant_index << 2)],\n"
     "                      sizeof(float) * 4);\n"
     "          // DAO/Warden: in the traced frame, what the pixel shader will read\n"
     "          // (BUG-039: colour zero, alpha from c0 - c253..255 suspected unread).\n"
     "          if (dao_trace::Active()) {\n"
     "            float v[4];\n"
     "            std::memcpy(v, mapping, sizeof(v));\n"
     "            XELOGI(\"FT PS float constant c{} -> slot {} of {} ({} bytes): ({:.4g} {:.4g} \"\n"
     "                   \"{:.4g} {:.4g})\", (i << 6) + float_constant_index, dao_trace_slot,\n"
     "                   float_constant_count_pixel, float_constants_size, v[0], v[1], v[2],\n"
     "                   v[3]);\n"
     "          }\n"
     "          ++dao_trace_slot;\n"
     "          mapping += sizeof(float) * 4;\n"),
    (VCP_C, "say when a draw is dropped for want of its pipeline",
     "    if (current_pipeline == VK_NULL_HANDLE) {\n"
     "      // Still not ready - something is wrong.\n"
     "      return false;\n",
     "    if (current_pipeline == VK_NULL_HANDLE) {\n"
     "      // Still not ready - something is wrong.\n"
     "      // DAO/Warden: a dropped draw is otherwise silent (BUG-039).\n"
     "      static uint32_t dropped_logged = 0;\n"
     "      if (dropped_logged++ < 20) {\n"
     "        XELOGE(\"Draw dropped: the graphics pipeline was not created (VS {:016X} PS \"\n"
     "               \"{:016X})\", vertex_shader->ucode_data_hash(),\n"
     "               pixel_shader ? pixel_shader->ucode_data_hash() : 0);\n"
     "      }\n"
     "      return false;\n"),
    (VPC_C, "dump_shaders: write the translated SPIR-V too",
     "  if (translation.GetOrCreateShaderModule() == VK_NULL_HANDLE) {\n"
     "    return false;\n"
     "  }\n"
     "\n"
     "  // TODO(Triang3l): Log that the shader has been successfully translated in\n",
     "  if (translation.GetOrCreateShaderModule() == VK_NULL_HANDLE) {\n"
     "    return false;\n"
     "  }\n"
     "\n"
     "  // DAO/Warden: the Direct3D 12 pipeline cache dumps its translations under\n"
     "  // dump_shaders; this one did not. The SPIR-V the driver compiles, on disk.\n"
     "  if (!cvars::dump_shaders.empty()) {\n"
     "    translation.Dump(cvars::dump_shaders, \"vk\");\n"
     "  }\n"
     "\n"
     "  // TODO(Triang3l): Log that the shader has been successfully translated in\n"),
    (VPC_C, "frame trace: the trace header",
     "#include \"xenia/gpu/gpu_flags.h\"\n",
     "#include \"xenia/gpu/gpu_flags.h\"\n"
     "#include \"xenia/gpu/d3d12/dao_frame_trace.h\"\n"),
    (VPC_C, "frame trace: each colour target's write mask and blend state",
     "  render_target_out.color_write_mask = write_mask;\n"
     "}\n",
     "  render_target_out.color_write_mask = write_mask;\n"
     "  // DAO/Warden: in the traced frame, what the pipeline will do with the colour\n"
     "  // (BUG-039: alpha reaches the target, the colour does not).\n"
     "  if (dao_trace::Active()) {\n"
     "    XELOGI(\"FT pipeline RT write mask {:X}, blend colour src {} dst {} op {}, alpha \"\n"
     "           \"src {} dst {} op {}\", write_mask,\n"
     "           uint32_t(render_target_out.src_color_blend_factor),\n"
     "           uint32_t(render_target_out.dst_color_blend_factor),\n"
     "           uint32_t(render_target_out.color_blend_op),\n"
     "           uint32_t(render_target_out.src_alpha_blend_factor),\n"
     "           uint32_t(render_target_out.dst_alpha_blend_factor),\n"
     "           uint32_t(render_target_out.alpha_blend_op));\n"
     "  }\n"
     "}\n"),
    (VCP_C, "frame trace: the draw's render pass key",
     "  VulkanRenderTargetCache::RenderPassKey render_pass_key =\n"
     "      render_target_cache_->last_update_render_pass_key();\n",
     "  VulkanRenderTargetCache::RenderPassKey render_pass_key =\n"
     "      render_target_cache_->last_update_render_pass_key();\n"
     "  // DAO/Warden: in the traced frame, the render pass the draw goes into.\n"
     "  if (dao_trace::Active()) {\n"
     "    XELOGI(\"FT render pass: attachments used {:X}, transfer formats {}, msaa {}, \"\n"
     "           \"color formats {} {} {} {}\", uint32_t(render_pass_key.depth_and_color_used),\n"
     "           uint32_t(render_pass_key.color_rts_use_transfer_formats),\n"
     "           uint32_t(render_pass_key.msaa_samples), uint32_t(render_pass_key.color_0_view_format),\n"
     "           uint32_t(render_pass_key.color_1_view_format),\n"
     "           uint32_t(render_pass_key.color_2_view_format),\n"
     "           uint32_t(render_pass_key.color_3_view_format));\n"
     "  }\n"),
    (VRTC_C, "say which images a new framebuffer is made of",
     "  // Creates at a persistent location - safe to use pointers.\n"
     "  return &framebuffers_\n",
     "  // DAO/Warden: the first framebuffers' attachments - which render target image\n"
     "  // each draw goes into (BUG-039: the dumped image never held a draw).\n"
     "  {\n"
     "    static uint32_t framebuffers_logged = 0;\n"
     "    if (framebuffers_logged++ < 32) {\n"
     "      uint32_t rts_remaining = render_pass_key.depth_and_color_used;\n"
     "      uint32_t index;\n"
     "      while (xe::bit_scan_forward(rts_remaining, &index)) {\n"
     "        rts_remaining &= ~(uint32_t(1) << index);\n"
     "        const auto& vulkan_rt = *static_cast<const VulkanRenderTarget*>(\n"
     "            depth_and_color_render_targets[index]);\n"
     "        const RenderTargetKey k = vulkan_rt.key();\n"
     "        XELOGI(\"GPU: framebuffer {} ({}x{}) attachment {}: RT base {} pitch_tiles {} msaa {} \"\n"
     "               \"{} {} image {} transfer_formats {}\",\n"
     "               fmt::ptr(framebuffer), host_extent.width, host_extent.height, index,\n"
     "               k.base_tiles, k.pitch_tiles_at_32bpp, uint32_t(k.msaa_samples),\n"
     "               k.is_depth ? \"depth\" : \"color\",\n"
     "               k.is_depth ? xenos::GetDepthRenderTargetFormatName(k.GetDepthFormat())\n"
     "                          : xenos::GetColorRenderTargetFormatName(k.GetColorFormat()),\n"
     "               fmt::ptr(vulkan_rt.image()),\n"
     "               uint32_t(render_pass_key.color_rts_use_transfer_formats));\n"
     "      }\n"
     "    }\n"
     "  }\n"
     "  // Creates at a persistent location - safe to use pointers.\n"
     "  return &framebuffers_\n"),
    (VRTC_C, "frame trace: the ownership transfers before a draw",
     "  bool resolve_clear_needed =\n"
     "      render_target_resolve_clear_values && resolve_clear_rectangle;\n",
     "  // DAO/Warden: in the traced frame, the EDRAM ownership transfers into each\n"
     "  // render target before the draw, and a resolve clear if there is one.\n"
     "  if (dao_trace::Active()) {\n"
     "    for (uint32_t i = 0; i < render_target_count; ++i) {\n"
     "      if (!render_targets[i]) {\n"
     "        continue;\n"
     "      }\n"
     "      const auto& vulkan_rt = *static_cast<const VulkanRenderTarget*>(render_targets[i]);\n"
     "      const RenderTargetKey k = vulkan_rt.key();\n"
     "      XELOGI(\"FT transfers: RT {} base {} pitch_tiles {} msaa {} {} image {}: {} transfers, \"\n"
     "             \"resolve clear {}\",\n"
     "             i, k.base_tiles, k.pitch_tiles_at_32bpp, uint32_t(k.msaa_samples),\n"
     "             k.is_depth ? \"depth\" : \"color\", fmt::ptr(vulkan_rt.image()),\n"
     "             render_target_transfers ? render_target_transfers[i].size() : 0,\n"
     "             render_target_resolve_clear_values && resolve_clear_rectangle ? \"yes\" : \"no\");\n"
     "    }\n"
     "  }\n"
     "  bool resolve_clear_needed =\n"
     "      render_target_resolve_clear_values && resolve_clear_rectangle;\n"),
    (VCP_C, "frame trace: the framebuffer of each render pass",
     "  current_render_pass_ = render_pass;\n"
     "  current_framebuffer_ = framebuffer;\n",
     "  current_render_pass_ = render_pass;\n"
     "  current_framebuffer_ = framebuffer;\n"
     "  // DAO/Warden: in the traced frame, which framebuffer the pass draws into.\n"
     "  if (dao_trace::Active()) {\n"
     "    XELOGI(\"FT render pass begin: framebuffer {} ({}x{})\", fmt::ptr(framebuffer->framebuffer),\n"
     "           framebuffer->host_extent.width, framebuffer->host_extent.height);\n"
     "  }\n"),
    (VRTC_C, "frame trace: what the update hands the draw",
     "      last_update_framebuffer_ = framebuffer;\n"
     "\n"
     "      // Transition the used render targets.\n",
     "      last_update_framebuffer_ = framebuffer;\n"
     "      // DAO/Warden: in the traced frame, the pass and framebuffer the draw gets.\n"
     "      if (dao_trace::Active()) {\n"
     "        XELOGI(\"FT update: key {:08X} (transfer formats {}) pass {} framebuffer {} ({})\",\n"
     "               render_pass_key.key, uint32_t(render_pass_key.color_rts_use_transfer_formats),\n"
     "               fmt::ptr(render_pass), fmt::ptr(framebuffer),\n"
     "               fmt::ptr(framebuffer->framebuffer));\n"
     "      }\n"
     "\n"
     "      // Transition the used render targets.\n"),
    (VCP_C, "frame trace: staying in the current render pass",
     "  if (current_render_pass_ == render_pass &&\n"
     "      current_framebuffer_ == framebuffer) {\n"
     "    return;\n"
     "  }\n",
     "  if (current_render_pass_ == render_pass &&\n"
     "      current_framebuffer_ == framebuffer) {\n"
     "    // DAO/Warden: in the traced frame, say so (BUG-039).\n"
     "    if (dao_trace::Active()) {\n"
     "      XELOGI(\"FT render pass: staying in pass {} framebuffer {} ({})\", fmt::ptr(render_pass),\n"
     "             fmt::ptr(framebuffer), fmt::ptr(framebuffer->framebuffer));\n"
     "    }\n"
     "    return;\n"
     "  }\n"),
    (VCP_C, "frame trace: the Vulkan viewport and scissor set for the draw",
     "  scissor_rect.extent.height = scissor.extent[1];\n"
     "  SetScissor(scissor_rect);\n",
     "  scissor_rect.extent.height = scissor.extent[1];\n"
     "  SetScissor(scissor_rect);\n"
     "  // DAO/Warden: in the traced frame, the host viewport and scissor as set.\n"
     "  if (dao_trace::Active()) {\n"
     "    XELOGI(\"FT dynamic state: viewport x {} y {} w {} h {} z {}..{}, scissor ({} {}) {}x{}\",\n"
     "           viewport.x, viewport.y, viewport.width, viewport.height, viewport.minDepth,\n"
     "           viewport.maxDepth, scissor_rect.offset.x, scissor_rect.offset.y,\n"
     "           scissor_rect.extent.width, scissor_rect.extent.height);\n"
     "  }\n"),
    (VCP_C, "frame trace: the draw command",
     "    deferred_command_buffer_.CmdVkDraw(\n"
     "        primitive_processing_result.host_draw_vertex_count, 1, 0, 0);\n",
     "    deferred_command_buffer_.CmdVkDraw(\n"
     "        primitive_processing_result.host_draw_vertex_count, 1, 0, 0);\n"
     "    // DAO/Warden: in the traced frame, the draw as recorded.\n"
     "    if (dao_trace::Active()) {\n"
     "      XELOGI(\"FT vkCmdDraw: {} vertices, host primitive type {}\",\n"
     "             primitive_processing_result.host_draw_vertex_count,\n"
     "             uint32_t(primitive_processing_result.host_primitive_type));\n"
     "    }\n"),
    (VPC_C, "experiment: DAO_HACK_NO_CULL draws both faces (BUG-045)",
     "  rasterization_state.frontFace = description.front_face_clockwise\n"
     "                                      ? VK_FRONT_FACE_CLOCKWISE\n"
     "                                      : VK_FRONT_FACE_COUNTER_CLOCKWISE;\n",
     "  rasterization_state.frontFace = description.front_face_clockwise\n"
     "                                      ? VK_FRONT_FACE_CLOCKWISE\n"
     "                                      : VK_FRONT_FACE_COUNTER_CLOCKWISE;\n"
     "  // DAO/Warden experiment (BUG-045: the world's geometry leaves no fragments):\n"
     "  // DAO_HACK_NO_CULL draws both faces of every triangle.\n"
     "  {\n"
     "    static const bool no_cull = getenv(\"DAO_HACK_NO_CULL\") != nullptr;\n"
     "    if (no_cull) {\n"
     "      rasterization_state.cullMode = VK_CULL_MODE_NONE;\n"
     "    }\n"
     "  }\n"),
    (VPC_C, "experiment: DAO_HACK_NO_DEPTH_TEST / DAO_HACK_DEPTH_ALWAYS (BUG-045)",
     "    if (description.depth_write_enable ||\n"
     "        description.depth_compare_op != xenos::CompareFunction::kAlways) {\n"
     "      depth_stencil_state.depthTestEnable = VK_TRUE;\n"
     "      depth_stencil_state.depthWriteEnable =\n"
     "          description.depth_write_enable ? VK_TRUE : VK_FALSE;\n"
     "      depth_stencil_state.depthCompareOp =\n"
     "          VkCompareOp(uint32_t(VK_COMPARE_OP_NEVER) +\n"
     "                      uint32_t(description.depth_compare_op));\n"
     "    }\n",
     "    // DAO/Warden experiment (BUG-045): DAO_HACK_NO_DEPTH_TEST leaves the depth test\n"
     "    // off, so every fragment that is rasterized reaches the colour target;\n"
     "    // DAO_HACK_DEPTH_ALWAYS keeps the test on with the comparison always passing,\n"
     "    // so depth is still written and can be read back.\n"
     "    static const bool no_depth_test = getenv(\"DAO_HACK_NO_DEPTH_TEST\") != nullptr;\n"
     "    static const bool depth_always = getenv(\"DAO_HACK_DEPTH_ALWAYS\") != nullptr;\n"
     "    if (!no_depth_test && (description.depth_write_enable ||\n"
     "        description.depth_compare_op != xenos::CompareFunction::kAlways)) {\n"
     "      depth_stencil_state.depthTestEnable = VK_TRUE;\n"
     "      depth_stencil_state.depthWriteEnable =\n"
     "          description.depth_write_enable ? VK_TRUE : VK_FALSE;\n"
     "      depth_stencil_state.depthCompareOp =\n"
     "          depth_always ? VK_COMPARE_OP_ALWAYS\n"
     "                       : VkCompareOp(uint32_t(VK_COMPARE_OP_NEVER) +\n"
     "                                     uint32_t(description.depth_compare_op));\n"
     "    }\n"),
    (SST_RB, "experiment: <cstdlib> for DAO_HACK_PS_RED",
     "#include <cstdint>\n",
     "#include <cstdint>\n"
     "#include <cstdlib>  // DAO/Warden: std::getenv, for DAO_HACK_PS_RED\n"),
    (SST_RB, "experiment: DAO_HACK_PS_RED=<hash> paints one pixel shader red (BUG-045)",
     "        spv::Id color = builder_->createLoad(var_color, spv::NoPrecision);\n",
     "        spv::Id color = builder_->createLoad(var_color, spv::NoPrecision);\n"
     "        // DAO/Warden experiment (BUG-045): DAO_HACK_PS_RED=<ucode hash, hex> makes\n"
     "        // that pixel shader's colour outputs solid red - a red silhouette says the\n"
     "        // geometry reaches the target and the shader's own output is what is lost.\n"
     "        {\n"
     "          static const uint64_t red_hash = [] {\n"
     "            const char* env = std::getenv(\"DAO_HACK_PS_RED\");\n"
     "            return env ? uint64_t(std::strtoull(env, nullptr, 16)) : uint64_t(0);\n"
     "          }();\n"
     "          if (red_hash && current_shader().ucode_data_hash() == red_hash) {\n"
     "            id_vector_temp_.clear();\n"
     "            id_vector_temp_.push_back(builder_->makeFloatConstant(1.0f));\n"
     "            id_vector_temp_.push_back(const_float_0_);\n"
     "            id_vector_temp_.push_back(const_float_0_);\n"
     "            id_vector_temp_.push_back(builder_->makeFloatConstant(1.0f));\n"
     "            color = builder_->makeCompositeConstant(type_float4_, id_vector_temp_);\n"
     "          }\n"
     "        }\n"
     "        // DAO/Warden experiment (BUG-045): DAO_HACK_PS_PARAMGEN - colour output 0 of\n"
     "        // the listed pixel shaders shows the raw gl_FragCoord, (|x| / 2048,\n"
     "        // |y| / 1024, fract(|x|), 1), or magenta when it has no gl_FragCoord.\n"
     "        if (color_target_index == 0 && dao_ps_param_gen_debug_) {\n"
     "          if (input_fragment_coordinates_ != spv::NoResult) {\n"
     "            id_vector_temp_.clear();\n"
     "            id_vector_temp_.push_back(const_int_0_);\n"
     "            spv::Id frag_x = builder_->createUnaryBuiltinCall(\n"
     "                type_float_, ext_inst_glsl_std_450_, GLSLstd450FAbs,\n"
     "                builder_->createLoad(\n"
     "                    builder_->createAccessChain(spv::StorageClassInput,\n"
     "                                                input_fragment_coordinates_,\n"
     "                                                id_vector_temp_),\n"
     "                    spv::NoPrecision));\n"
     "            id_vector_temp_.clear();\n"
     "            id_vector_temp_.push_back(builder_->makeIntConstant(1));\n"
     "            spv::Id frag_y = builder_->createUnaryBuiltinCall(\n"
     "                type_float_, ext_inst_glsl_std_450_, GLSLstd450FAbs,\n"
     "                builder_->createLoad(\n"
     "                    builder_->createAccessChain(spv::StorageClassInput,\n"
     "                                                input_fragment_coordinates_,\n"
     "                                                id_vector_temp_),\n"
     "                    spv::NoPrecision));\n"
     "            spv::Id coarse_frag_x =\n"
     "                builder_->createBinOp(spv::OpFMul, type_float_, frag_x,\n"
     "                                      builder_->makeFloatConstant(1.0f / 2048.0f));\n"
     "            spv::Id coarse_frag_y =\n"
     "                builder_->createBinOp(spv::OpFMul, type_float_, frag_y,\n"
     "                                      builder_->makeFloatConstant(1.0f / 1024.0f));\n"
     "            spv::Id frac_frag_x = builder_->createUnaryBuiltinCall(\n"
     "                type_float_, ext_inst_glsl_std_450_, GLSLstd450Fract, frag_x);\n"
     "            (void)coarse_frag_x;\n"
     "            (void)coarse_frag_y;\n"
     "            // The whole vector loaded, then the components extracted.\n"
     "            spv::Id frag_whole =\n"
     "                builder_->createLoad(input_fragment_coordinates_, spv::NoPrecision);\n"
     "            spv::Id whole_x = builder_->createUnaryBuiltinCall(\n"
     "                type_float_, ext_inst_glsl_std_450_, GLSLstd450Fract,\n"
     "                builder_->createCompositeExtract(frag_whole, type_float_, 0));\n"
     "            spv::Id whole_y = builder_->createBinOp(\n"
     "                spv::OpFMul, type_float_,\n"
     "                builder_->createCompositeExtract(frag_whole, type_float_, 1),\n"
     "                builder_->makeFloatConstant(1.0f / 1024.0f));\n"
     "            id_vector_temp_.clear();\n"
     "            id_vector_temp_.push_back(whole_x);\n"
     "            id_vector_temp_.push_back(whole_y);\n"
     "            id_vector_temp_.push_back(frac_frag_x);\n"
     "            id_vector_temp_.push_back(builder_->makeFloatConstant(1.0f));\n"
     "            color = builder_->createCompositeConstruct(type_float4_, id_vector_temp_);\n"
     "          } else {\n"
     "            id_vector_temp_.clear();\n"
     "            id_vector_temp_.push_back(builder_->makeFloatConstant(1.0f));\n"
     "            id_vector_temp_.push_back(const_float_0_);\n"
     "            id_vector_temp_.push_back(builder_->makeFloatConstant(1.0f));\n"
     "            id_vector_temp_.push_back(builder_->makeFloatConstant(1.0f));\n"
     "            color = builder_->makeCompositeConstant(type_float4_, id_vector_temp_);\n"
     "          }\n"
     "        }\n"),
    (SST_C, "experiment: DAO_HACK_NO_A0 reads a0-relative float constants absolutely (BUG-045)",
     "    case InstructionStorageAddressingMode::kAddressRegisterRelative:\n"
     "      base_pointer = var_main_address_register_;\n"
     "      break;\n",
     "    case InstructionStorageAddressingMode::kAddressRegisterRelative: {\n"
     "      // DAO/Warden experiment (BUG-045): DAO_HACK_NO_A0 ignores the address\n"
     "      // register for float constants - every skinned vertex then reads the first\n"
     "      // bone, so a character that appears (rigid) says the index path is at fault.\n"
     "      static const bool no_a0 = std::getenv(\"DAO_HACK_NO_A0\") != nullptr;\n"
     "      if (no_a0 && is_float_constant) {\n"
     "        return builder_->makeIntConstant(int(storage_index));\n"
     "      }\n"
     "      base_pointer = var_main_address_register_;\n"
     "      break;\n"
     "    }\n"),
    (VCP_C, "BUG-045: log writes to VS constants c252..c255",
     "void VulkanCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {\n"
     "  CommandProcessor::WriteRegister(index, value);\n",
     "void VulkanCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {\n"
     "  CommandProcessor::WriteRegister(index, value);\n"
     "  // DAO/Warden (BUG-045): the vertex shaders' literal constants c252..c255 read as\n"
     "  // zero on the device - every non-zero write to them, the first few hundred.\n"
     "  if (value && index >= XE_GPU_REG_SHADER_CONSTANT_000_X + 252 * 4 &&\n"
     "      index < XE_GPU_REG_SHADER_CONSTANT_000_X + 256 * 4) {\n"
     "    static std::atomic<int> logged{0};\n"
     "    if (logged.fetch_add(1, std::memory_order_relaxed) < 400) {\n"
     "      const uint32_t word = index - XE_GPU_REG_SHADER_CONSTANT_000_X;\n"
     "      float as_float;\n"
     "      std::memcpy(&as_float, &value, sizeof(as_float));\n"
     "      XELOGI(\"VS constant c{}.{} <- {:08X} ({:.4g})\", word / 4, \"xyzw\"[word % 4], value,\n"
     "             as_float);\n"
     "    }\n"
     "  }\n"),
    (PM4_H, "BUG-045: log LOAD_ALU_CONSTANT loads that reach VS c252..c255",
     "    case 0:  // ALU\n"
     "      trace_writer_.WriteMemoryRead(CpuToGpu(address), size_dwords * 4);\n",
     "    case 0:  // ALU\n"
     "      // DAO/Warden (BUG-045): loads that reach VS constants c252..c255, with where\n"
     "      // they read from.\n"
     "      if (index < 256 * 4 && index + size_dwords > 252 * 4) {\n"
     "        static std::atomic<int> logged{0};\n"
     "        if (dao_trace::Active() ||\n"
     "            logged.fetch_add(1, std::memory_order_relaxed) < 20) {\n"
     "          std::string words;\n"
     "          for (uint32_t w = 0; w < size_dwords && w < 16; ++w) {\n"
     "            words += fmt::format(\" {:08X}\", xe::load_and_swap<uint32_t>(xlat_address + w));\n"
     "          }\n"
     "          XELOGI(\"LOAD_ALU_CONSTANT: words {}..{} (c{}..c{}) from {:08X}:{}\", index,\n"
     "                 index + size_dwords - 1, index / 4, (index + size_dwords - 1) / 4,\n"
     "                 address, words);\n"
     "        }\n"
     "      }\n"
     "      // DAO/Warden (BUG-052): every ALU constant load, for the resolve log.\n"
     "      {\n"
     "        dao_trace::RecentConstantLoad& dao_load = dao_trace::recent_constant_loads\n"
     "            [dao_trace::recent_constant_load_count++ % 64];\n"
     "        dao_load.address = address;\n"
     "        dao_load.first_word = index;\n"
     "        dao_load.word_count = size_dwords;\n"
     "        dao_load.draw_count = dao_trace::recent_draw_count;\n"
     "      }\n"
     "      trace_writer_.WriteMemoryRead(CpuToGpu(address), size_dwords * 4);\n"),
    (VCP_C, "BUG-045: the shader interpreter, for the CPU run of traced vertex shaders",
     "#include \"xenia/gpu/vulkan/vulkan_command_processor.h\"\n",
     "#include \"xenia/gpu/vulkan/vulkan_command_processor.h\"\n"
     "#include \"xenia/gpu/shader_interpreter.h\"  // DAO/Warden: BUG-045\n"),
    (VS_C, "experiment: <cstdlib>, <cstring>, <vector> for DAO_HACK_NO_NMIN",
     "#include <cstdint>\n",
     "#include <cstdint>\n"
     "#include <cstdlib>  // DAO/Warden: DAO_HACK_NO_NMIN\n"
     "#include <cstring>\n"
     "#include <vector>\n"),
    (VS_C, "experiment: DAO_HACK_NO_NMIN turns NMin/NMax/NClamp into FMin/FMax/FClamp (BUG-045)",
     "  shader_module_create_info.codeSize = translated_binary().size();\n"
     "  shader_module_create_info.pCode =\n"
     "      reinterpret_cast<const uint32_t*>(translated_binary().data());\n",
     "  shader_module_create_info.codeSize = translated_binary().size();\n"
     "  shader_module_create_info.pCode =\n"
     "      reinterpret_cast<const uint32_t*>(translated_binary().data());\n"
     "  // DAO/Warden experiment (BUG-045): DAO_HACK_NO_NMIN replaces the NaN-aware\n"
     "  // GLSL.std.450 NMin (79), NMax (80) and NClamp (81) with FMin (37), FMax (40) and\n"
     "  // FClamp (43) in the SPIR-V the driver gets - a test of the Adreno driver's\n"
     "  // handling of the rarely used NaN-aware forms.\n"
     "  static const bool no_nmin = std::getenv(\"DAO_HACK_NO_NMIN\") != nullptr;\n"
     "  std::vector<uint32_t> patched;\n"
     "  if (no_nmin && translated_binary().size() >= 20 &&\n"
     "      !(translated_binary().size() & 3)) {\n"
     "    patched.resize(translated_binary().size() / 4);\n"
     "    std::memcpy(patched.data(), translated_binary().data(), translated_binary().size());\n"
     "    uint32_t glsl_set = UINT32_MAX;\n"
     "    for (size_t i = 5; i < patched.size();) {\n"
     "      const uint32_t word_count = patched[i] >> 16;\n"
     "      const uint32_t opcode = patched[i] & 0xFFFF;\n"
     "      if (!word_count || i + word_count > patched.size()) {\n"
     "        break;\n"
     "      }\n"
     "      if (opcode == 11 && word_count >= 3 &&\n"
     "          !std::memcmp(&patched[i + 2], \"GLSL.std.450\", 12)) {\n"
     "        glsl_set = patched[i + 1];\n"
     "      } else if (opcode == 12 && word_count >= 5 && patched[i + 3] == glsl_set) {\n"
     "        uint32_t& instruction = patched[i + 4];\n"
     "        if (instruction == 79) {\n"
     "          instruction = 37;\n"
     "        } else if (instruction == 80) {\n"
     "          instruction = 40;\n"
     "        } else if (instruction == 81) {\n"
     "          instruction = 43;\n"
     "        }\n"
     "      }\n"
     "      i += word_count;\n"
     "    }\n"
     "    shader_module_create_info.pCode = patched.data();\n"
     "  }\n"),
    (SST_C, "experiment: DAO_HACK_VS_PROBE, the probed vertex shader's hash (BUG-045)",
     "#include \"xenia/gpu/xenos_zpd_report.h\"\n",
     "#include \"xenia/gpu/xenos_zpd_report.h\"\n"
     "\n"
     "#include <cstdlib>  // DAO/Warden: std::getenv, std::strtoull\n"
     "\n"
     "// DAO/Warden experiment (BUG-045): DAO_HACK_VS_PROBE=<ucode hashes, hex, comma or\n"
     "// space separated, up to 8> makes those vertex shaders store the guest position\n"
     "// (as the guest shader wrote it) of guest vertex indices 0..4095 at guest physical\n"
     "// 0x1FF80000, 8 dwords per vertex - read back by the command processor for the\n"
     "// probed draws of a traced frame and logged, the GPU's answer for where the mesh\n"
     "// lands.\n"
     "namespace {\n"
     "bool DaoVsProbeMatches(uint64_t ucode_hash) {\n"
     "  struct Hashes {\n"
     "    uint64_t hash[8] = {};\n"
     "    Hashes() {\n"
     "      const char* env = std::getenv(\"DAO_HACK_VS_PROBE\");\n"
     "      for (int i = 0; env && *env && i < 8;) {\n"
     "        char* end = nullptr;\n"
     "        const uint64_t value = std::strtoull(env, &end, 16);\n"
     "        if (end == env) {\n"
     "          ++env;\n"
     "          continue;\n"
     "        }\n"
     "        hash[i++] = value;\n"
     "        env = end;\n"
     "      }\n"
     "    }\n"
     "  };\n"
     "  static const Hashes hashes;\n"
     "  for (uint64_t hash : hashes.hash) {\n"
     "    if (hash && hash == ucode_hash) {\n"
     "      return true;\n"
     "    }\n"
     "  }\n"
     "  return false;\n"
     "}\n"
     "// DAO/Warden experiment (BUG-045): DAO_HACK_PS_PARAMGEN=<ucode hashes, hex, comma\n"
     "// separated, up to 8> - those pixel shaders get gl_FragCoord and show it as colour.\n"
     "bool DaoPsDebugMatches(uint64_t ucode_hash) {\n"
     "  struct Hashes {\n"
     "    uint64_t hash[8] = {};\n"
     "    Hashes() {\n"
     "      const char* env = std::getenv(\"DAO_HACK_PS_PARAMGEN\");\n"
     "      for (int i = 0; env && *env && i < 8;) {\n"
     "        char* end = nullptr;\n"
     "        const uint64_t value = std::strtoull(env, &end, 16);\n"
     "        if (end == env) {\n"
     "          ++env;\n"
     "          continue;\n"
     "        }\n"
     "        hash[i++] = value;\n"
     "        env = end;\n"
     "      }\n"
     "    }\n"
     "  };\n"
     "  static const Hashes hashes;\n"
     "  for (uint64_t hash : hashes.hash) {\n"
     "    if (hash && hash == ucode_hash) {\n"
     "      return true;\n"
     "    }\n"
     "  }\n"
     "  return false;\n"
     "}\n"
     "// The function variable holding the guest vertex index, while a probed vertex\n"
     "// shader is being translated.\n"
     "thread_local spv::Id dao_vs_probe_index_var = spv::NoResult;\n"
     "}  // namespace\n"),
    (SST_C, "experiment: DAO_HACK_VS_PROBE leaves the probed shader's shared memory writable",
     "    if (!memexport_used) {\n"
     "      builder_->addMemberDecoration(type_shared_memory, 0,\n"
     "                                    spv::DecorationNonWritable);\n"
     "    }\n",
     "    if (!memexport_used && !(IsSpirvVertexShader() &&\n"
     "      DaoVsProbeMatches(current_shader().ucode_data_hash()) &&\n"
     "      features_.vertex_pipeline_stores_and_atomics)) {\n"
     "      builder_->addMemberDecoration(type_shared_memory, 0,\n"
     "                                    spv::DecorationNonWritable);\n"
     "    }\n"),
    (SST_C, "experiment: DAO_HACK_VS_PROBE keeps the guest vertex index",
     "      builder_->createStore(\n"
     "          builder_->createUnaryOp(spv::OpConvertSToF, type_float_,\n"
     "                                  vertex_index),\n"
     "          builder_->createAccessChain(spv::StorageClassFunction,\n"
     "                                      var_main_registers_, id_vector_temp_));\n"
     "    }\n"
     "  }\n"
     "}\n",
     "      builder_->createStore(\n"
     "          builder_->createUnaryOp(spv::OpConvertSToF, type_float_,\n"
     "                                  vertex_index),\n"
     "          builder_->createAccessChain(spv::StorageClassFunction,\n"
     "                                      var_main_registers_, id_vector_temp_));\n"
     "      // DAO/Warden experiment (BUG-045): the vertex position probe.\n"
     "      dao_vs_probe_index_var = spv::NoResult;\n"
     "      if (IsSpirvVertexShader() &&\n"
     "      DaoVsProbeMatches(current_shader().ucode_data_hash()) &&\n"
     "      features_.vertex_pipeline_stores_and_atomics) {\n"
     "        dao_vs_probe_index_var = builder_->createVariable(\n"
     "            spv::NoPrecision, spv::StorageClassFunction, type_int_,\n"
     "            \"xe_var_dao_vs_probe_index\");\n"
     "        builder_->createStore(vertex_index, dao_vs_probe_index_var);\n"
     "      }\n"
     "    }\n"
     "  }\n"
     "}\n"),
    (SST_C, "experiment: DAO_HACK_VS_PROBE stores the positions",
     "      position = composite_construct_op->getResultId();\n"
     "      builder_->getBuildPoint()->addInstruction(\n"
     "          std::move(composite_construct_op));\n"
     "    }\n"
     "    builder_->createStore(position, position_ptr);\n"
     "  }\n"
     "}\n",
     "      position = composite_construct_op->getResultId();\n"
     "      builder_->getBuildPoint()->addInstruction(\n"
     "          std::move(composite_construct_op));\n"
     "    }\n"
     "    builder_->createStore(position, position_ptr);\n"
     "    // DAO/Warden experiment (BUG-045): the vertex position probe - the guest\n"
     "    // position, gl_VertexIndex and a marker for guest vertex indices 0..4095.\n"
     "    if (dao_vs_probe_index_var != spv::NoResult) {\n"
     "      spv::Id probe_index =\n"
     "          builder_->createLoad(dao_vs_probe_index_var, spv::NoPrecision);\n"
     "      dao_vs_probe_index_var = spv::NoResult;\n"
     "      spv::Id probe_in_range = builder_->createBinOp(\n"
     "          spv::OpULessThan, type_bool_,\n"
     "          builder_->createUnaryOp(spv::OpBitcast, type_uint_, probe_index),\n"
     "          builder_->makeUintConstant(4096));\n"
     "      SpirvBuilder::IfBuilder probe_if(\n"
     "          probe_in_range, spv::SelectionControlDontFlattenMask, *builder_);\n"
     "      {\n"
     "        spv::Id probe_base = builder_->createBinOp(\n"
     "            spv::OpIAdd, type_int_,\n"
     "            builder_->makeIntConstant(int(0x1FF80000 >> 2)),\n"
     "            builder_->createBinOp(spv::OpIMul, type_int_, probe_index,\n"
     "                                  builder_->makeIntConstant(8)));\n"
     "        auto probe_store = [&](int word, spv::Id value) {\n"
     "          StoreUint32ToSharedMemory(\n"
     "              value, builder_->createBinOp(spv::OpIAdd, type_int_, probe_base,\n"
     "                                           builder_->makeIntConstant(word)));\n"
     "        };\n"
     "        for (int i = 0; i < 4; ++i) {\n"
     "          probe_store(i, builder_->createUnaryOp(\n"
     "                             spv::OpBitcast, type_uint_,\n"
     "                             builder_->createCompositeExtract(\n"
     "                                 guest_position, type_float_, i)));\n"
     "        }\n"
     "        probe_store(4, builder_->createUnaryOp(\n"
     "                           spv::OpBitcast, type_uint_,\n"
     "                           builder_->createLoad(input_vertex_index_,\n"
     "                                                spv::NoPrecision)));\n"
     "        probe_store(5, builder_->makeUintConstant(0xDA0DA0u));\n"
     "      }\n"
     "      probe_if.makeEndIf();\n"
     "    }\n"
     "  }\n"
     "}\n"),
    (VCP_C, "experiment: DAO_HACK_VS_PROBE, the probe range and the logged readback (BUG-045)",
     "  if (vertex_shader->memexport_eM_written() != 0 &&\n"
     "      device_properties.vertexPipelineStoresAndAtomics) {\n"
     "    draw_util::AddMemExportRanges(regs, *vertex_shader, memexport_ranges_);\n"
     "  }\n",
     "  if (vertex_shader->memexport_eM_written() != 0 &&\n"
     "      device_properties.vertexPipelineStoresAndAtomics) {\n"
     "    draw_util::AddMemExportRanges(regs, *vertex_shader, memexport_ranges_);\n"
     "  }\n"
     "  // DAO/Warden experiment (BUG-045): DAO_HACK_VS_PROBE=<ucode hashes, hex> - the\n"
     "  // translator makes those vertex shaders store their positions at guest physical\n"
     "  // 0x1FF80000; up to 8 probed draws of a traced frame get that range zeroed and\n"
     "  // as a memexport range, a copy to their own slot of the readback buffer after\n"
     "  // the draw, and the slots are logged once their submission completes.\n"
     "  struct DaoVsProbe {\n"
     "    uint64_t hashes[8] = {};\n"
     "    struct Slot {\n"
     "      uint64_t hash;\n"
     "      uint32_t draw;\n"
     "      uint32_t index_count;\n"
     "    } slots[8] = {};\n"
     "    uint32_t used = 0;\n"
     "    uint64_t submission = 0;\n"
     "    bool logged = false;\n"
     "    DaoVsProbe() {\n"
     "      const char* env = std::getenv(\"DAO_HACK_VS_PROBE\");\n"
     "      for (int i = 0; env && *env && i < 8;) {\n"
     "        char* end = nullptr;\n"
     "        const uint64_t value = std::strtoull(env, &end, 16);\n"
     "        if (end == env) {\n"
     "          ++env;\n"
     "          continue;\n"
     "        }\n"
     "        hashes[i++] = value;\n"
     "        env = end;\n"
     "      }\n"
     "    }\n"
     "    bool Matches(uint64_t hash) const {\n"
     "      for (uint64_t h : hashes) {\n"
     "        if (h && h == hash) {\n"
     "          return true;\n"
     "        }\n"
     "      }\n"
     "      return false;\n"
     "    }\n"
     "  };\n"
     "  static DaoVsProbe dao_vs_probe;\n"
     "  bool dao_vs_probe_draw = false;\n"
     "  if (dao_vs_probe.submission && !dao_vs_probe.logged &&\n"
     "      GetCurrentSubmission() > dao_vs_probe.submission) {\n"
     "    CheckSubmissionCompletionAndDeviceLoss(dao_vs_probe.submission);\n"
     "    if (GetCompletedSubmission() >= dao_vs_probe.submission) {\n"
     "      dao_vs_probe.logged = true;\n"
     "      const ui::vulkan::VulkanDevice* const probe_device = GetVulkanDevice();\n"
     "      void* mapped = nullptr;\n"
     "      if (probe_device->functions().vkMapMemory(\n"
     "              probe_device->device(), memexport_readback_buffer_memory_, 0,\n"
     "              8 * (4096 * 8 * 4), 0, &mapped) == VK_SUCCESS &&\n"
     "          mapped) {\n"
     "        for (uint32_t s = 0; s < dao_vs_probe.used; ++s) {\n"
     "          const DaoVsProbe::Slot& slot = dao_vs_probe.slots[s];\n"
     "          const uint32_t* words =\n"
     "              static_cast<const uint32_t*>(mapped) + s * ((4096 * 8 * 4) / 4);\n"
     "          uint32_t written = 0, non_finite = 0, w_not_positive = 0;\n"
     "          uint32_t rows[10] = {}, columns[10] = {};\n"
     "          float min_x = 1e30f, max_x = -1e30f, min_y = 1e30f, max_y = -1e30f;\n"
     "          float min_z = 1e30f, max_z = -1e30f;\n"
     "          std::string first;\n"
     "          for (uint32_t v = 0; v < 4096; ++v) {\n"
     "            const uint32_t* w = words + v * 8;\n"
     "            if (w[5] != 0xDA0DA0u) {\n"
     "              continue;\n"
     "            }\n"
     "            ++written;\n"
     "            float f[4];\n"
     "            std::memcpy(f, w, sizeof(f));\n"
     "            if (!std::isfinite(f[0]) || !std::isfinite(f[1]) ||\n"
     "                !std::isfinite(f[2]) || !std::isfinite(f[3])) {\n"
     "              ++non_finite;\n"
     "              continue;\n"
     "            }\n"
     "            if (first.size() < 400) {\n"
     "              first += fmt::format(\" v{}=({:.4g} {:.4g} {:.4g} {:.4g})\", v, f[0],\n"
     "                                   f[1], f[2], f[3]);\n"
     "            }\n"
     "            if (f[3] <= 0.0f) {\n"
     "              ++w_not_positive;\n"
     "              continue;\n"
     "            }\n"
     "            // The guest screen position with this title's viewport (1280x720,\n"
     "            // y down).\n"
     "            const float x = 640.0f + 640.0f * f[0] / f[3];\n"
     "            const float y = 360.0f - 360.0f * f[1] / f[3];\n"
     "            const float z = f[2] / f[3];\n"
     "            min_x = std::min(min_x, x);\n"
     "            max_x = std::max(max_x, x);\n"
     "            min_y = std::min(min_y, y);\n"
     "            max_y = std::max(max_y, y);\n"
     "            min_z = std::min(min_z, z);\n"
     "            max_z = std::max(max_z, z);\n"
     "            // 0: above/left of the screen, 1..8: eighths, 9: below/right.\n"
     "            const int row = y < 0.0f ? 0 : y >= 720.0f ? 9 : 1 + int(y / 90.0f);\n"
     "            const int column =\n"
     "                x < 0.0f ? 0 : x >= 1280.0f ? 9 : 1 + int(x / 160.0f);\n"
     "            ++rows[row];\n"
     "            ++columns[column];\n"
     "          }\n"
     "          XELOGI(\n"
     "              \"FT VS probe {:016X} (draw ~{}, {} indices): {} vertices written, {} \"\n"
     "              \"not finite, {} with w <= 0; screen x {:.1f}..{:.1f} y {:.1f}..{:.1f} \"\n"
     "              \"z {:.4f}..{:.4f}; rows above,0-90..630-720,below {} {} {} {} {} {} {} \"\n"
     "              \"{} {} {}; columns left,0-160..,right {} {} {} {} {} {} {} {} {} {}\",\n"
     "              slot.hash, slot.draw, slot.index_count, written, non_finite,\n"
     "              w_not_positive, min_x, max_x, min_y, max_y, min_z, max_z, rows[0],\n"
     "              rows[1], rows[2], rows[3], rows[4], rows[5], rows[6], rows[7], rows[8],\n"
     "              rows[9], columns[0], columns[1], columns[2], columns[3], columns[4],\n"
     "              columns[5], columns[6], columns[7], columns[8], columns[9]);\n"
     "          XELOGI(\"FT VS probe {:016X} (draw ~{}) first:{}\", slot.hash, slot.draw,\n"
     "                 first);\n"
     "        }\n"
     "        probe_device->functions().vkUnmapMemory(\n"
     "            probe_device->device(), memexport_readback_buffer_memory_);\n"
     "      } else {\n"
     "        XELOGE(\"FT VS probe: the readback buffer could not be mapped\");\n"
     "      }\n"
     "    }\n"
     "  }\n"
     "  if (dao_vs_probe.used < 8 && !dao_vs_probe.submission &&\n"
     "      dao_trace::Active() && dao_vs_probe.Matches(vertex_shader->ucode_data_hash())) {\n"
     "    if (device_properties.vertexPipelineStoresAndAtomics) {\n"
     "      dao_vs_probe_draw = true;\n"
     "      memexport_ranges_.emplace_back(0x1FF80000 >> 2, (4096 * 8 * 4));\n"
     "    } else {\n"
     "      XELOGE(\"FT VS probe: vertexPipelineStoresAndAtomics is not supported\");\n"
     "    }\n"
     "  }\n"),
    (VCP_C, "experiment: DAO_HACK_VS_PROBE zeroes the probe range before the draw",
     "  // Insert the shared memory barrier if needed.\n",
     "  // DAO/Warden (BUG-052): each distinct memory export range, the first 256 of the\n"
     "  // boot - what shaders write straight into guest memory.\n"
     "  {\n"
     "    static uint64_t dao_exports_seen[256];\n"
     "    static uint32_t dao_exports_seen_count = 0;\n"
     "    for (const draw_util::MemExportRange& dao_range : memexport_ranges_) {\n"
     "      const uint64_t dao_key =\n"
     "          (uint64_t(dao_range.base_address_dwords) << 32) | dao_range.size_bytes;\n"
     "      bool dao_new = dao_exports_seen_count < 256;\n"
     "      for (uint32_t i = 0; dao_new && i < dao_exports_seen_count; ++i) {\n"
     "        dao_new = dao_exports_seen[i] != dao_key;\n"
     "      }\n"
     "      if (dao_new) {\n"
     "        dao_exports_seen[dao_exports_seen_count++] = dao_key;\n"
     "        XELOGI(\"GPU: memory export to {:08X}+{:X} by VS {:016X} PS {:016X} (distinct {})\",\n"
     "               dao_range.base_address_dwords << 2, dao_range.size_bytes,\n"
     "               vertex_shader->ucode_data_hash(),\n"
     "               pixel_shader ? pixel_shader->ucode_data_hash() : 0,\n"
     "               dao_exports_seen_count);\n"
     "      }\n"
     "    }\n"
     "  }\n"
     "  // DAO/Warden experiment (BUG-045): the vertex position probe's range starts\n"
     "  // zeroed for each probed draw, so the marker says which vertices it wrote.\n"
     "  if (dao_vs_probe_draw) {\n"
     "    shared_memory_->Use(VulkanSharedMemory::Usage::kTransferDestination,\n"
     "                        std::make_pair(uint32_t(0x1FF80000), uint32_t(4096 * 8 * 4)));\n"
     "    SubmitBarriers(true);\n"
     "    deferred_command_buffer_.CmdVkFillBuffer(shared_memory_->buffer(), 0x1FF80000,\n"
     "                                             (4096 * 8 * 4), 0);\n"
     "  }\n"
     "\n"
     "  // Insert the shared memory barrier if needed.\n"),
    (VCP_C, "experiment: DAO_HACK_VS_PROBE copies the probe range after the draw",
     "  // CPU readback for memexport data (if enabled).\n",
     "  // DAO/Warden experiment (BUG-045): the vertex position probe's copy to the\n"
     "  // draw's slot, logged by a later draw once the submission completes (the\n"
     "  // frame's probed draws are all in it, the trace ending the submission).\n"
     "  if (dao_vs_probe_draw) {\n"
     "    VkBuffer probe_buffer = RequestReadbackBuffer(8 * (4096 * 8 * 4));\n"
     "    if (probe_buffer != VK_NULL_HANDLE) {\n"
     "      shared_memory_->Use(VulkanSharedMemory::Usage::kRead);\n"
     "      SubmitBarriers(true);\n"
     "      VkBufferCopy probe_copy = {};\n"
     "      probe_copy.srcOffset = 0x1FF80000;\n"
     "      probe_copy.dstOffset = VkDeviceSize(dao_vs_probe.used) * (4096 * 8 * 4);\n"
     "      probe_copy.size = (4096 * 8 * 4);\n"
     "      deferred_command_buffer_.CmdVkCopyBuffer(shared_memory_->buffer(),\n"
     "                                               probe_buffer, 1, &probe_copy);\n"
     "      DaoVsProbe::Slot& slot = dao_vs_probe.slots[dao_vs_probe.used++];\n"
     "      slot.hash = vertex_shader->ucode_data_hash();\n"
     "      slot.draw = dao_trace::DrawIndex();\n"
     "      slot.index_count = index_count;\n"
     "      XELOGI(\"FT VS probe {:016X}: draw ~{} copied to slot {}\", slot.hash,\n"
     "             slot.draw, dao_vs_probe.used - 1);\n"
     "    } else {\n"
     "      XELOGE(\"FT VS probe: no readback buffer\");\n"
     "    }\n"
     "  }\n"
     "  // The traced frame over, the probed draws are logged when its submission\n"
     "  // completes.\n"
     "  if (dao_vs_probe.used && !dao_vs_probe.submission && !dao_trace::Active()) {\n"
     "    dao_vs_probe.submission = GetCurrentSubmission();\n"
     "  }\n"
     "\n"
     "  // CPU readback for memexport data (if enabled).\n"),
    (VCP_C, "experiment: DAO_HACK_SKIP_VS skips the draws of the listed vertex shaders (BUG-045)",
     "  pipeline_cache_->AnalyzeShaderUcode(*vertex_shader);\n",
     "  pipeline_cache_->AnalyzeShaderUcode(*vertex_shader);\n"
     "  // DAO/Warden experiment (BUG-045): DAO_HACK_SKIP_VS=<ucode hashes, hex, comma\n"
     "  // separated, up to 8> - the draws of those vertex shaders are dropped, to see\n"
     "  // what they cover.\n"
     "  {\n"
     "    struct SkipList {\n"
     "      uint64_t hashes[8] = {};\n"
     "      SkipList() {\n"
     "        const char* env = std::getenv(\"DAO_HACK_SKIP_VS\");\n"
     "        for (int i = 0; env && *env && i < 8;) {\n"
     "          char* end = nullptr;\n"
     "          const uint64_t value = std::strtoull(env, &end, 16);\n"
     "          if (end == env) {\n"
     "            ++env;\n"
     "            continue;\n"
     "          }\n"
     "          hashes[i++] = value;\n"
     "          env = end;\n"
     "        }\n"
     "      }\n"
     "    };\n"
     "    static const SkipList skip_list;\n"
     "    for (uint64_t hash : skip_list.hashes) {\n"
     "      if (hash && hash == vertex_shader->ucode_data_hash()) {\n"
     "        return true;\n"
     "      }\n"
     "    }\n"
     "  }\n"),
    (VCP_C, "experiment: DAO_HACK_RESOLVE_DUMP writes the traced frame's resolves to files (BUG-045)",
     "  // CPU readback resolve path (if not disabled).\n"
     "  ReadbackResolveMode readback_mode = GetReadbackResolveMode();\n",
     "  // DAO/Warden experiment (BUG-045): DAO_HACK_RESOLVE_DUMP=<dir> - in the traced\n"
     "  // frame, each resolve's output (up to 48) is copied out of the shared memory into a\n"
     "  // dedicated host-visible buffer, waited for, and written to\n"
     "  // <dir>/resolve-<n>-<address>.raw in the guest layout - what each pass of the frame\n"
     "  // left behind, independent of the guest memory and its allocator.\n"
     "  {\n"
     "    static const char* dump_dir = std::getenv(\"DAO_HACK_RESOLVE_DUMP\");\n"
     "    static VkBuffer dump_buffer = VK_NULL_HANDLE;\n"
     "    static VkDeviceMemory dump_memory = VK_NULL_HANDLE;\n"
     "    static uint32_t dump_count = 0;\n"
     "    // BUG-052: DAO_HACK_RESOLVE_DUMP_PITCH=<RB_COPY_DEST_PITCH, hex> - resolves with\n"
     "    // that destination pitch are dumped in any frame, not only the traced one.\n"
     "    static const char* dump_pitch_text = std::getenv(\"DAO_HACK_RESOLVE_DUMP_PITCH\");\n"
     "    static const uint32_t dump_pitch =\n"
     "        dump_pitch_text ? uint32_t(std::strtoul(dump_pitch_text, nullptr, 16)) : 0;\n"
     "    // BUG-049: DAO_HACK_RESOLVE_DUMP_ADDRESS=<hex> - with each resolve dumped for its\n"
     "    // pitch, the guest memory from that address as it is then, in a second file:\n"
     "    // the scene copy the save thumbnail's quad sampled.\n"
     "    static const char* dump_address_text = std::getenv(\"DAO_HACK_RESOLVE_DUMP_ADDRESS\");\n"
     "    static const uint32_t dump_address =\n"
     "        dump_address_text ? uint32_t(std::strtoul(dump_address_text, nullptr, 16)) : 0;\n"
     "    // BUG-052: what was drawn since the last resolve with that pitch (up to the last\n"
     "    // 64 draws), logged whether or not the frame is traced.\n"
     "    if (dump_pitch &&\n"
     "        register_file_->values[XE_GPU_REG_RB_COPY_DEST_PITCH] == dump_pitch) {\n"
     "      static uint32_t draws_logged = 0;\n"
     "      const uint32_t total = dao_trace::recent_draw_count;\n"
     "      uint32_t first = draws_logged;\n"
     "      if (total > 64 && first < total - 64) {\n"
     "        first = total - 64;\n"
     "      }\n"
     "      for (uint32_t i = first; i < total; ++i) {\n"
     "        const dao_trace::RecentDraw& d = dao_trace::recent_draws[i % 64];\n"
     "        XELOGI(\"GPU: drawn before the resolve to {:08X}: VS {:016X} PS {:016X} prim {} \"\n"
     "               \"count {} surface {:08X} color {:08X} depth {:08X} dc {:08X} mask {:X} \"\n"
     "               \"tex [{:08X} {:08X} {:08X}] [{:08X} {:08X} {:08X}] [{:08X} {:08X} {:08X}] \"\n"
     "               \"[{:08X} {:08X} {:08X}] [{:08X} {:08X} {:08X}] [{:08X} {:08X} {:08X}]\",\n"
     "               written_address, d.vs_hash, d.ps_hash, d.prim_type, d.index_count,\n"
     "               d.surface_info, d.color_info, d.depth_info, d.depth_control, d.color_mask,\n"
     "               d.texture[0][0], d.texture[0][1], d.texture[0][2], d.texture[1][0],\n"
     "               d.texture[1][1], d.texture[1][2], d.texture[2][0], d.texture[2][1],\n"
     "               d.texture[2][2], d.texture[3][0], d.texture[3][1], d.texture[3][2],\n"
     "               d.texture[4][0], d.texture[4][1], d.texture[4][2], d.texture[5][0],\n"
     "               d.texture[5][1], d.texture[5][2]);\n"
     "        std::string dao_constants;\n"
     "        for (uint32_t c = 0; c < 8 && d.ps_index[c] != 0xFFFF; ++c) {\n"
     "          dao_constants += fmt::format(\" c{}=({:08X} {:08X} {:08X} {:08X})\", d.ps_index[c],\n"
     "                                       d.ps_value[c][0], d.ps_value[c][1], d.ps_value[c][2],\n"
     "                                       d.ps_value[c][3]);\n"
     "        }\n"
     "        XELOGI(\"GPU: drawn before the resolve to {:08X}: VS constants {} read, {} zero, \"\n"
     "               \"{} non-finite, sum {:08X}; PS constants {} read, {} zero:{}\",\n"
     "               written_address, d.vs_float_count, d.vs_float_zero, d.vs_float_bad,\n"
     "               d.vs_float_sum, d.ps_float_count, d.ps_float_zero, dao_constants);\n"
     "        // BUG-052: which VS constants were zero vectors, and the draw's number.\n"
     "        XELOGI(\"GPU: drawn before the resolve to {:08X}: draw {} VS zero: {}\",\n"
     "               written_address, d.number, dao_trace::BitRanges(d.vs_zero, 256));\n"
     "        // BUG-049: where it lands.\n"
     "        auto dao_float = [](uint32_t bits) {\n"
     "          float value;\n"
     "          std::memcpy(&value, &bits, sizeof(value));\n"
     "          return value;\n"
     "        };\n"
     "        XELOGI(\"GPU: drawn before the resolve to {:08X}: draw {} lands at viewport ({} {}) \"\n"
     "               \"{}x{}, scissor ({} {}) {}x{}; guest window offset {:08X}, window scissor \"\n"
     "               \"{:08X} {:08X}, screen scissor {:08X} {:08X}, VTE {:08X}, SC mode {:08X}, \"\n"
     "               \"viewport x {}*+{} y {}*+{}\",\n"
     "               written_address, d.number, d.host_viewport[0], d.host_viewport[1],\n"
     "               d.host_viewport[2], d.host_viewport[3], d.host_scissor[0],\n"
     "               d.host_scissor[1], d.host_scissor[2], d.host_scissor[3], d.window_offset,\n"
     "               d.window_scissor[0], d.window_scissor[1], d.screen_scissor[0],\n"
     "               d.screen_scissor[1], d.vte_cntl, d.su_sc_mode, dao_float(d.viewport[0]),\n"
     "               dao_float(d.viewport[1]), dao_float(d.viewport[2]),\n"
     "               dao_float(d.viewport[3]));\n"
     "        // BUG-049: stream 0's first 20 words as floats (big-endian), as they are now\n"
     "        // - a quad's four vertices of position and texture coordinates.\n"
     "        if ((d.vfetch[0] & 3) == 3 && memory_) {\n"
     "          const uint32_t stream = d.vfetch[0] & 0x1FFFFFFC;\n"
     "          const uint8_t* bytes = memory_->TranslatePhysical<const uint8_t*>(stream);\n"
     "          std::string floats;\n"
     "          for (uint32_t k = 0; bytes && k < 20; ++k) {\n"
     "            uint32_t word;\n"
     "            std::memcpy(&word, bytes + 4 * k, sizeof(word));\n"
     "            floats += fmt::format(\"{}{}\", k ? \" \" : \"\", dao_float(xe::byte_swap(word)));\n"
     "          }\n"
     "          XELOGI(\"GPU: drawn before the resolve to {:08X}: draw {} stream 0 {:08X} {:08X} \"\n"
     "                 \"at {:08X}: {}\",\n"
     "                 written_address, d.number, d.vfetch[0], d.vfetch[1], stream, floats);\n"
     "        }\n"
     "        if (d.texture[0][0]) {\n"
     "          XELOGI(\"GPU: drawn before the resolve to {:08X}: draw {} texture 0 fetch \"\n"
     "                 \"constant {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}\",\n"
     "                 written_address, d.number, d.texture[0][0], d.texture[0][1],\n"
     "                 d.texture[0][2], d.texture0_high[0], d.texture0_high[1],\n"
     "                 d.texture0_high[2]);\n"
     "        }\n"
     "      }\n"
     "      draws_logged = total;\n"
     "      // BUG-052: the ALU constant loads since the last such resolve.\n"
     "      static uint32_t loads_logged = 0;\n"
     "      const uint32_t loads_total = dao_trace::recent_constant_load_count;\n"
     "      uint32_t first_load = loads_logged;\n"
     "      if (loads_total > 64 && first_load < loads_total - 64) {\n"
     "        first_load = loads_total - 64;\n"
     "      }\n"
     "      for (uint32_t i = first_load; i < loads_total; ++i) {\n"
     "        const dao_trace::RecentConstantLoad& l = dao_trace::recent_constant_loads[i % 64];\n"
     "        XELOGI(\"GPU: constants loaded before the resolve to {:08X}: {} c{}..c{} from \"\n"
     "               \"{:08X} before draw {}\",\n"
     "               written_address, l.first_word >= 1024 ? \"PS\" : \"VS\",\n"
     "               (l.first_word & 1023) / 4, ((l.first_word + l.word_count - 1) & 1023) / 4,\n"
     "               l.address, l.draw_count);\n"
     "      }\n"
     "      loads_logged = loads_total;\n"
     "    }\n"
     "    constexpr uint32_t kDumpBufferSize = 8 * 1024 * 1024;\n"
     "    if (dump_dir &&\n"
     "        (dao_trace::Active() ||\n"
     "         (dump_pitch &&\n"
     "          register_file_->values[XE_GPU_REG_RB_COPY_DEST_PITCH] == dump_pitch)) &&\n"
     "        written_length && !is_scaled &&\n"
     "        written_length <= kDumpBufferSize && dump_count < 48) {\n"
     "      const ui::vulkan::VulkanDevice* const dump_device = GetVulkanDevice();\n"
     "      if (dump_buffer == VK_NULL_HANDLE) {\n"
     "        ui::vulkan::util::CreateDedicatedAllocationBuffer(\n"
     "            dump_device, kDumpBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,\n"
     "            ui::vulkan::util::MemoryPurpose::kReadback, dump_buffer, dump_memory);\n"
     "      }\n"
     "      if (dump_buffer != VK_NULL_HANDLE) {\n"
     "        shared_memory_->Use(VulkanSharedMemory::Usage::kRead);\n"
     "        SubmitBarriers(true);\n"
     "        VkBufferCopy dump_region = {};\n"
     "        dump_region.srcOffset = written_address;\n"
     "        dump_region.dstOffset = 0;\n"
     "        dump_region.size = written_length;\n"
     "        deferred_command_buffer_.CmdVkCopyBuffer(shared_memory_->buffer(), dump_buffer,\n"
     "                                                 1, &dump_region);\n"
     "        // Ends the submission and waits for it.\n"
     "        CheckSubmissionCompletionAndDeviceLoss(GetCurrentSubmission());\n"
     "        void* mapping = nullptr;\n"
     "        if (dump_device->functions().vkMapMemory(dump_device->device(), dump_memory, 0,\n"
     "                                                 VK_WHOLE_SIZE, 0,\n"
     "                                                 &mapping) == VK_SUCCESS) {\n"
     "          VkMappedMemoryRange range = {};\n"
     "          range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;\n"
     "          range.memory = dump_memory;\n"
     "          range.size = VK_WHOLE_SIZE;\n"
     "          dump_device->functions().vkInvalidateMappedMemoryRanges(dump_device->device(),\n"
     "                                                                  1, &range);\n"
     "          const std::string path = fmt::format(\"{}/resolve-{:02}-{:08X}.raw\", dump_dir,\n"
     "                                               dump_count, written_address);\n"
     "          if (FILE* file = std::fopen(path.c_str(), \"wb\")) {\n"
     "            std::fwrite(mapping, 1, written_length, file);\n"
     "            std::fclose(file);\n"
     "          }\n"
     "          uint32_t dump_nonzero = 0;\n"
     "          for (uint32_t i = 0; i < written_length; ++i) {\n"
     "            dump_nonzero += static_cast<const uint8_t*>(mapping)[i] != 0;\n"
     "          }\n"
     "          dump_device->functions().vkUnmapMemory(dump_device->device(), dump_memory);\n"
     "          XELOGI(\"FT resolve dump {}: {:08X}+{:X} -> {} ({} non-zero bytes, pitch {:08X})\",\n"
     "                 dump_count, written_address, written_length, path, dump_nonzero,\n"
     "                 register_file_->values[XE_GPU_REG_RB_COPY_DEST_PITCH]);\n"
     "        }\n"
     "        // The wait above ended the submission: a new one for the second copy (run\n"
     "        // 303 recorded into the ended one, and the GPU command thread trapped).\n"
     "        if (dump_address && dump_pitch &&\n"
     "            register_file_->values[XE_GPU_REG_RB_COPY_DEST_PITCH] == dump_pitch &&\n"
     "            BeginSubmission(true)) {\n"
     "          constexpr uint32_t kSourceLength = 0x398000;  // 1280x736, 32bpp\n"
     "          shared_memory_->Use(VulkanSharedMemory::Usage::kRead);\n"
     "          SubmitBarriers(true);\n"
     "          VkBufferCopy source_region = {};\n"
     "          source_region.srcOffset = dump_address;\n"
     "          source_region.dstOffset = 0;\n"
     "          source_region.size = kSourceLength;\n"
     "          deferred_command_buffer_.CmdVkCopyBuffer(shared_memory_->buffer(), dump_buffer,\n"
     "                                                   1, &source_region);\n"
     "          CheckSubmissionCompletionAndDeviceLoss(GetCurrentSubmission());\n"
     "          void* source_mapping = nullptr;\n"
     "          if (dump_device->functions().vkMapMemory(dump_device->device(), dump_memory,\n"
     "                                                   0, VK_WHOLE_SIZE, 0,\n"
     "                                                   &source_mapping) == VK_SUCCESS) {\n"
     "            VkMappedMemoryRange source_range = {};\n"
     "            source_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;\n"
     "            source_range.memory = dump_memory;\n"
     "            source_range.size = VK_WHOLE_SIZE;\n"
     "            dump_device->functions().vkInvalidateMappedMemoryRanges(dump_device->device(),\n"
     "                                                                    1, &source_range);\n"
     "            const std::string source_path =\n"
     "                fmt::format(\"{}/resolve-{:02}-{:08X}-source-{:08X}.raw\", dump_dir,\n"
     "                            dump_count, written_address, dump_address);\n"
     "            if (FILE* file = std::fopen(source_path.c_str(), \"wb\")) {\n"
     "              std::fwrite(source_mapping, 1, kSourceLength, file);\n"
     "              std::fclose(file);\n"
     "            }\n"
     "            uint32_t source_nonzero = 0;\n"
     "            for (uint32_t i = 0; i < kSourceLength; ++i) {\n"
     "              source_nonzero += static_cast<const uint8_t*>(source_mapping)[i] != 0;\n"
     "            }\n"
     "            dump_device->functions().vkUnmapMemory(dump_device->device(), dump_memory);\n"
     "            XELOGI(\"FT resolve dump {}: the memory at {:08X}+{:X} then -> {} ({} non-zero \"\n"
     "                   \"bytes)\",\n"
     "                   dump_count, dump_address, kSourceLength, source_path, source_nonzero);\n"
     "          }\n"
     "        }\n"
     "        ++dump_count;\n"
     "      }\n"
     "    }\n"
     "  }\n"
     "\n"
     "  // CPU readback resolve path (if not disabled).\n"
     "  ReadbackResolveMode readback_mode = GetReadbackResolveMode();\n"),
    (SST_H, "experiment: DAO_HACK_PS_PARAMGEN state (BUG-045)",
     "  spv::Id buffers_shared_memory_;\n",
     "  spv::Id buffers_shared_memory_;\n"
     "  // DAO/Warden experiment (BUG-045): DAO_HACK_PS_PARAMGEN - whether the pixel\n"
     "  // shader being translated is the one to show its PsParamGen, and the function\n"
     "  // variable holding that value.\n"
     "  bool dao_ps_param_gen_debug_ = false;\n"
     "  spv::Id dao_ps_param_gen_var_ = spv::NoResult;\n"),
    (SST_C, "experiment: DAO_HACK_PS_PARAMGEN selects the pixel shader",
     "  uint32_t param_gen_interpolator = GetPsParamGenInterpolator();\n",
     "  // DAO/Warden experiment (BUG-045): DAO_HACK_PS_PARAMGEN=<ucode hash, hex>.\n"
     "  dao_ps_param_gen_debug_ = DaoPsDebugMatches(current_shader().ucode_data_hash());\n"
     "  dao_ps_param_gen_var_ = spv::NoResult;\n"
     "  uint32_t param_gen_interpolator = GetPsParamGenInterpolator();\n"),
    (SST_C, "experiment: DAO_HACK_PS_PARAMGEN keeps the PsParamGen value",
     "    builder_->createStore(param_gen, builder_->createAccessChain(\n"
     "                                         spv::StorageClassFunction,\n"
     "                                         var_main_registers_, id_vector_temp_));\n"
     "  }\n"
     "}\n",
     "    builder_->createStore(param_gen, builder_->createAccessChain(\n"
     "                                         spv::StorageClassFunction,\n"
     "                                         var_main_registers_, id_vector_temp_));\n"
     "    // DAO/Warden experiment (BUG-045): DAO_HACK_PS_PARAMGEN keeps the value for\n"
     "    // the colour output.\n"
     "    if (dao_ps_param_gen_debug_) {\n"
     "      dao_ps_param_gen_var_ = builder_->createVariable(\n"
     "          spv::NoPrecision, spv::StorageClassFunction, type_float4_,\n"
     "          \"xe_var_dao_param_gen\");\n"
     "      builder_->createStore(param_gen, dao_ps_param_gen_var_);\n"
     "    }\n"
     "  }\n"
     "}\n"),
    (VPP_C, "experiment: <cstdlib> for DAO_HACK_RECT_IN_VS",
     "#include \"xenia/gpu/vulkan/vulkan_primitive_processor.h\"\n",
     "#include \"xenia/gpu/vulkan/vulkan_primitive_processor.h\"\n"
     "\n"
     "#include <cstdlib>  // DAO/Warden: std::getenv, for DAO_HACK_RECT_IN_VS\n"),
    (VPP_C, "experiment: DAO_HACK_RECT_IN_VS expands rectangle lists in the vertex shader (BUG-045)",
     "  if (!InitializeCommon(\n"
     "          device_properties.fullDrawIndexUint32, device_properties.triangleFans,\n"
     "          false, device_properties.geometryShader,\n"
     "          device_properties.geometryShader, device_properties.geometryShader)) {\n",
     "  // DAO/Warden experiment (BUG-045): DAO_HACK_RECT_IN_VS - rectangle lists are\n"
     "  // expanded in the vertex shader (the fallback for devices without geometry\n"
     "  // shaders) instead of by the geometry shader, to test the geometry shader path.\n"
     "  const bool rectangles_in_gs =\n"
     "      device_properties.geometryShader && !std::getenv(\"DAO_HACK_RECT_IN_VS\");\n"
     "  if (!InitializeCommon(\n"
     "          device_properties.fullDrawIndexUint32, device_properties.triangleFans,\n"
     "          false, device_properties.geometryShader,\n"
     "          device_properties.geometryShader, rectangles_in_gs)) {\n"),
    (SST_C, "experiment: DAO_HACK_PS_PARAMGEN declares gl_FragCoord for the listed shaders",
     "  bool need_frag_coord =\n"
     "      edram_fragment_shader_interlock_ || param_gen_needed || IsSampleRate() ||\n",
     "  // DAO/Warden experiment (BUG-045): DAO_HACK_PS_PARAMGEN shaders read it.\n"
     "  bool need_frag_coord =\n"
     "      (!is_depth_only_fragment_shader_ &&\n"
     "       DaoPsDebugMatches(current_shader().ucode_data_hash())) ||\n"
     "      edram_fragment_shader_interlock_ || param_gen_needed || IsSampleRate() ||\n"),
    (SST_C, "BUG-045: no DenormFlushToZero on Qualcomm (a zero gl_FragCoord); DAO_HACK_NO_FLOAT_CONTROLS drops more",
     "          vulkan_device->properties().shaderDemoteToHelperInvocation) {\n",
     "          vulkan_device->properties().shaderDemoteToHelperInvocation) {\n"
     "  // DAO/Warden (BUG-045): no DenormFlushToZero execution mode on Qualcomm GPUs.\n"
     "  // With it, the Adreno driver gives fragment shaders a zero gl_FragCoord - the\n"
     "  // PsParamGen position is zero, and Dragon Age's EDRAM restore pass (a pixel\n"
     "  // shader fetching the resolved scene at its own position) copies one texel over\n"
     "  // the whole scene: the character creation scene, the character in it, all 3D\n"
     "  // went black behind the post effects. Without the mode the driver's default\n"
     "  // denormal handling applies, as on devices that lack the feature.\n"
     "  if (vulkan_device->properties().vendorID == 0x5143) {\n"
     "    denorm_flush_to_zero_float32 = false;\n"
     "  }\n"
     "  // DAO/Warden experiment (BUG-045): DAO_HACK_NO_FLOAT_CONTROLS drops float\n"
     "  // controls execution modes from every shader, to bisect a driver's handling.\n"
     "  // The value says which: n (NaN/Inf/signed zero preservation), d (denormal\n"
     "  // flush), r (round to nearest even); without any of those letters, all three.\n"
     "  if (const char* drop = std::getenv(\"DAO_HACK_NO_FLOAT_CONTROLS\")) {\n"
     "    const std::string modes(drop);\n"
     "    const bool all = modes.find_first_of(\"ndr\") == std::string::npos;\n"
     "    if (all || modes.find('n') != std::string::npos) {\n"
     "      signed_zero_inf_nan_preserve_float32 = false;\n"
     "    }\n"
     "    if (all || modes.find('d') != std::string::npos) {\n"
     "      denorm_flush_to_zero_float32 = false;\n"
     "    }\n"
     "    if (all || modes.find('r') != std::string::npos) {\n"
     "      rounding_mode_rte_float32 = false;\n"
     "    }\n"
     "  }\n"),
    (VCP_C, "experiment: DAO_HACK_TINY_SCISSOR clamps the scissor to 8x8 (performance)",
     "void VulkanCommandProcessor::SetScissor(const VkRect2D& scissor) {\n"
     "  if (!dynamic_scissor_update_needed_) {\n",
     "void VulkanCommandProcessor::SetScissor(const VkRect2D& scissor_in) {\n"
     "  // DAO/Warden experiment (performance): DAO_HACK_TINY_SCISSOR clamps every\n"
     "  // draw's scissor to 8x8 - the vertex work stays and almost no pixels are\n"
     "  // shaded, so the frame rate says whether pixels or geometry are the cost.\n"
     "  // DAO_HACK_TINY_SCISSOR=<first>:<last> limits it to those draws of the traced frame\n"
     "  // (counted from 0; the draw being set up is the last one counted), 3.8.\n"
     "  static const char* tiny_text = std::getenv(\"DAO_HACK_TINY_SCISSOR\");\n"
     "  static uint32_t tiny_first = 0, tiny_last = UINT32_MAX;\n"
     "  static bool tiny_ranged = false, tiny_parsed = false;\n"
     "  if (!tiny_parsed) {\n"
     "    tiny_parsed = true;\n"
     "    if (tiny_text && std::strchr(tiny_text, ':')) {\n"
     "      char* end = nullptr;\n"
     "      tiny_first = uint32_t(std::strtoul(tiny_text, &end, 10));\n"
     "      tiny_last = uint32_t(std::strtoul(end + 1, nullptr, 10));\n"
     "      tiny_ranged = true;\n"
     "    }\n"
     "  }\n"
     "  const bool tiny_scissor =\n"
     "      tiny_text && (!tiny_ranged || (dao_trace::Active() && dao_trace::draws_in_frame &&\n"
     "                                     dao_trace::draws_in_frame - 1 >= tiny_first &&\n"
     "                                     dao_trace::draws_in_frame - 1 <= tiny_last));\n"
     "  VkRect2D scissor = scissor_in;\n"
     "  if (tiny_scissor) {\n"
     "    scissor.extent.width = std::min(scissor.extent.width, uint32_t(8));\n"
     "    scissor.extent.height = std::min(scissor.extent.height, uint32_t(8));\n"
     "  }\n"
     "  if (!dynamic_scissor_update_needed_) {\n"),
    (SST_C, "experiment: DAO_HACK_NO_SAMPLE_MASK leaves gl_SampleMask out (performance)",
     "  output_fragment_sample_mask_ = spv::NoResult;\n"
     "  if (!edram_fragment_shader_interlock_ && !is_depth_only_fragment_shader_) {\n",
     "  output_fragment_sample_mask_ = spv::NoResult;\n"
     "  // DAO/Warden experiment (performance): DAO_HACK_NO_SAMPLE_MASK leaves the output\n"
     "  // out - no full-coverage write and no alpha-to-coverage path with its discard -\n"
     "  // to measure what they cost the Adreno (early depth rejection).\n"
     "  static const bool no_sample_mask = std::getenv(\"DAO_HACK_NO_SAMPLE_MASK\") != nullptr;\n"
     "  if (!edram_fragment_shader_interlock_ && !is_depth_only_fragment_shader_ &&\n"
     "      !no_sample_mask) {\n"),
    (VPC_C, "frame trace: the pipeline description of the draw",
     "          zpd_total, viz_survey, description)) {\n"
     "    return false;\n"
     "  }\n"
     "  if (last_pipeline_ && last_pipeline_->first == description) {\n",
     "          zpd_total, viz_survey, description)) {\n"
     "    return false;\n"
     "  }\n"
     "  // DAO/Warden: in the traced frame, the whole pipeline description (BUG-039).\n"
     "  if (dao_trace::Active()) {\n"
     "    XELOGI(\"FT pipeline: VS {:016X}/{:016X} PS {:016X}/{:016X} pass key {:08X} geometry {} \"\n"
     "           \"tess {}/{} topology {} restart {} depth_clamp {} polygon {} cull {}{} cw {} \"\n"
     "           \"depth write {} cmp {} stencil {} zpd {} viz {} RT0 mask {:X}\",\n"
     "           description.vertex_shader_hash, description.vertex_shader_modification,\n"
     "           description.pixel_shader_hash, description.pixel_shader_modification,\n"
     "           description.render_pass_key.key, uint32_t(description.geometry_shader),\n"
     "           uint32_t(description.tessellation_mode),\n"
     "           uint32_t(description.tessellation_patch),\n"
     "           uint32_t(description.primitive_topology), uint32_t(description.primitive_restart),\n"
     "           uint32_t(description.depth_clamp_enable), uint32_t(description.polygon_mode),\n"
     "           description.cull_front ? \"F\" : \"\", description.cull_back ? \"B\" : \"\",\n"
     "           uint32_t(description.front_face_clockwise),\n"
     "           uint32_t(description.depth_write_enable), uint32_t(description.depth_compare_op),\n"
     "           uint32_t(description.stencil_test_enable), uint32_t(description.zpd_total),\n"
     "           uint32_t(description.viz_survey),\n"
     "           uint32_t(description.render_targets[0].color_write_mask));\n"
     "  }\n"
     "  if (last_pipeline_ && last_pipeline_->first == description) {\n"),
    (DCB_H, "render target readback: the image-to-buffer copy command (enum)",
     "    kVkCopyBufferToImage,\n",
     "    kVkCopyBufferToImage,\n"
     "    kVkCopyImageToBuffer,\n"),
    (DCB_H, "render target readback: the image-to-buffer copy command (arguments)",
     "  struct ArgsVkCopyBufferToImage {\n",
     "  struct ArgsVkCopyImageToBuffer {\n"
     "    VkImage src_image;\n"
     "    VkImageLayout src_image_layout;\n"
     "    VkBuffer dst_buffer;\n"
     "    uint32_t region_count;\n"
     "    // Followed by aligned VkBufferImageCopy[].\n"
     "  };\n"
     "  struct ArgsVkCopyBufferToImage {\n"),
    (DCB_H, "render target readback: the image-to-buffer copy command (recording)",
     "  void CmdVkFillBuffer(VkBuffer dst_buffer, VkDeviceSize dst_offset,\n",
     "  void CmdVkCopyImageToBuffer(VkImage src_image, VkImageLayout src_image_layout,\n"
     "                              VkBuffer dst_buffer, uint32_t region_count,\n"
     "                              const VkBufferImageCopy* regions) {\n"
     "    const size_t header_size =\n"
     "        xe::align(sizeof(ArgsVkCopyImageToBuffer), alignof(VkBufferImageCopy));\n"
     "    uint8_t* args_ptr = reinterpret_cast<uint8_t*>(\n"
     "        WriteCommand(Command::kVkCopyImageToBuffer,\n"
     "                     header_size + sizeof(VkBufferImageCopy) * region_count));\n"
     "    auto& args = *reinterpret_cast<ArgsVkCopyImageToBuffer*>(args_ptr);\n"
     "    args.src_image = src_image;\n"
     "    args.src_image_layout = src_image_layout;\n"
     "    args.dst_buffer = dst_buffer;\n"
     "    args.region_count = region_count;\n"
     "    std::memcpy(args_ptr + header_size, regions,\n"
     "                sizeof(VkBufferImageCopy) * region_count);\n"
     "  }\n"
     "\n"
     "  void CmdVkFillBuffer(VkBuffer dst_buffer, VkDeviceSize dst_offset,\n"),
    (DCB_C, "render target readback: the image-to-buffer copy command (execution)",
     "      case Command::kVkCopyBufferToImage: {\n",
     "      case Command::kVkCopyImageToBuffer: {\n"
     "        auto& args = *reinterpret_cast<const ArgsVkCopyImageToBuffer*>(stream);\n"
     "        dfn.vkCmdCopyImageToBuffer(\n"
     "            command_buffer, args.src_image, args.src_image_layout,\n"
     "            args.dst_buffer, args.region_count,\n"
     "            reinterpret_cast<const VkBufferImageCopy*>(\n"
     "                reinterpret_cast<const uint8_t*>(stream) +\n"
     "                xe::align(sizeof(ArgsVkCopyImageToBuffer),\n"
     "                          alignof(VkBufferImageCopy))));\n"
     "      } break;\n"
     "\n"
     "      case Command::kVkCopyBufferToImage: {\n"),
    (VCP_H, "render target readback: the completion check is public",
     "  void CheckSubmissionCompletionAndDeviceLoss(uint64_t await_submission);\n",
     " public:  // DAO/Warden: the render target readback instrument waits on a submission.\n"
     "  void CheckSubmissionCompletionAndDeviceLoss(uint64_t await_submission);\n"
     " private:\n"),
    (VRTC_C, "render target readback: the headers",
     "#include <cstddef>\n",
     "#include <cstddef>\n"
     "#include <cstdio>\n"
     "#include <cstdlib>\n"),
    (VRTC_C, "render target readback: the Vulkan helpers header",
     "#include \"xenia/gpu/xenos.h\"\n",
     "#include \"xenia/gpu/xenos.h\"\n"
     "#include \"xenia/ui/vulkan/vulkan_util.h\"\n"),
    (VCP_C, "frame trace: each pixel shader's microcode, the draw's constants and the system constants",
     "  // Update the graphics pipeline, and if the new graphics pipeline has a\n"
     "  // different layout, invalidate incompatible descriptor sets before updating\n",
     "  // DAO/Warden: in the traced frame, each pixel shader's microcode once and the\n"
     "  // first pixel-shader float constants of every draw - what a draw that comes out\n"
     "  // black multiplies by (BUG-039).\n"
     "  if (dao_trace::Active() && pixel_shader) {\n"
     "    static std::vector<uint64_t> disassembled;\n"
     "    const uint64_t hash = pixel_shader->ucode_data_hash();\n"
     "    if (std::find(disassembled.begin(), disassembled.end(), hash) ==\n"
     "        disassembled.end()) {\n"
     "      disassembled.push_back(hash);\n"
     "      XELOGI(\"FT PS {:016X} microcode:\\n{}\", hash, pixel_shader->ucode_disassembly());\n"
     "    }\n"
     "    std::string constants;\n"
     "    for (uint32_t c : {0u, 1u, 2u, 3u, 250u, 251u, 252u, 253u, 254u, 255u}) {\n"
     "      const float* v = reinterpret_cast<const float*>(\n"
     "          &regs.values[XE_GPU_REG_SHADER_CONSTANT_256_X + 4 * c]);\n"
     "      constants += fmt::format(\" c{}=({:.3f} {:.3f} {:.3f} {:.3f})\", c, v[0], v[1],\n"
     "                               v[2], v[3]);\n"
     "    }\n"
     "    XELOGI(\"FT draw constants PS{}\", constants);\n"
     "    XELOGI(\"FT system constants: flags {:08X}, color_exp_bias ({} {} {} {}), ndc_scale \"\n"
     "           \"({:.5f} {:.5f} {:.5f}) ndc_offset ({:.5f} {:.5f} {:.5f}), alpha_test_ref {}\",\n"
     "           system_constants_.flags, system_constants_.color_exp_bias[0],\n"
     "           system_constants_.color_exp_bias[1], system_constants_.color_exp_bias[2],\n"
     "           system_constants_.color_exp_bias[3], system_constants_.ndc_scale[0],\n"
     "           system_constants_.ndc_scale[1], system_constants_.ndc_scale[2],\n"
     "           system_constants_.ndc_offset[0], system_constants_.ndc_offset[1],\n"
     "           system_constants_.ndc_offset[2], system_constants_.alpha_test_reference);\n"
     "  }\n"
     "\n"
     "  // Update the graphics pipeline, and if the new graphics pipeline has a\n"
     "  // different layout, invalidate incompatible descriptor sets before updating\n"),
    (VRTC_C, "frame trace: the render target dump before a resolve, and the readback instrument",
     "  // Sort the invocations to reduce context and binding switches.\n"
     "  std::sort(dump_invocations_.begin(), dump_invocations_.end());\n",
     "  // Sort the invocations to reduce context and binding switches.\n"
     "  std::sort(dump_invocations_.begin(), dump_invocations_.end());\n"
     "  // DAO/Warden: in the traced frame, what the resolve will read - the render\n"
     "  // targets owning the dumped EDRAM range, or none (BUG-039: the resolve read\n"
     "  // the EDRAM buffer's stale clear colour, not the draws).\n"
     "  if (dao_trace::Active()) {\n"
     "    XELOGI(\"FT dump: EDRAM base {} rows {} row_length {} pitch {}: {} rectangles\",\n"
     "           dump_base, dump_rows, dump_row_length_used, dump_pitch,\n"
     "           dump_invocations_.size());\n"
     "    for (const DumpInvocation& invocation : dump_invocations_) {\n"
     "      const RenderTargetKey k = invocation.rectangle.render_target->key();\n"
     "      XELOGI(\"FT dump: RT base {} pitch_tiles {} msaa {} {} {} -> rows {}+{}, first row \"\n"
     "             \"from {}, last row to {}\",\n"
     "             k.base_tiles, k.pitch_tiles_at_32bpp, uint32_t(k.msaa_samples),\n"
     "             k.is_depth ? \"depth\" : \"color\",\n"
     "             k.is_depth ? xenos::GetDepthRenderTargetFormatName(k.GetDepthFormat())\n"
     "                        : xenos::GetColorRenderTargetFormatName(k.GetColorFormat()),\n"
     "             invocation.rectangle.row_first, invocation.rectangle.rows,\n"
     "             invocation.rectangle.row_first_start, invocation.rectangle.row_last_end);\n"
     "      XELOGI(\"FT dump: image {}\",\n"
     "             fmt::ptr(static_cast<VulkanRenderTarget*>(invocation.rectangle.render_target)\n"
     "                          ->image()));\n"
     "    }\n"
     "  }\n"
     "  // DAO/Warden instrument (BUG-039): DAO_HACK_RT_READBACK=<dir>. In the traced frame, the\n"
     "  // first dumped colour render target's image (top-left 1280x720) is copied to a\n"
     "  // host-visible buffer; at the next dump the buffer is read back, summarised and\n"
     "  // written to <dir>/rt-readback.raw. Ground truth for whether draws reach the image.\n"
     "  {\n"
     "    static const char* rt_readback_dir = getenv(\"DAO_HACK_RT_READBACK\");\n"
     "    static VkBuffer rt_readback_buffer = VK_NULL_HANDLE;\n"
     "    static VkDeviceMemory rt_readback_memory = VK_NULL_HANDLE;\n"
     "    static bool rt_readback_pending = false;\n"
     "    static uint64_t rt_readback_submission = 0;\n"
     "    static uint32_t rt_readback_width = 0;\n"
     "    static uint32_t rt_readback_height = 0;\n"
     "    constexpr VkDeviceSize kRtReadbackSize = VkDeviceSize(1280) * 720 * 4;\n"
     "    if (rt_readback_dir) {\n"
     "      const ui::vulkan::VulkanDevice* const rb_device = command_processor_.GetVulkanDevice();\n"
     "      const ui::vulkan::VulkanDevice::Functions& rb_dfn = rb_device->functions();\n"
     "      const VkDevice rb_vk_device = rb_device->device();\n"
     "      if (rt_readback_pending) {\n"
     "        rt_readback_pending = false;\n"
     "        command_processor_.CheckSubmissionCompletionAndDeviceLoss(rt_readback_submission);\n"
     "        void* mapping = nullptr;\n"
     "        if (rb_dfn.vkMapMemory(rb_vk_device, rt_readback_memory, 0, VK_WHOLE_SIZE, 0,\n"
     "                               &mapping) == VK_SUCCESS) {\n"
     "          VkMappedMemoryRange range;\n"
     "          range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;\n"
     "          range.pNext = nullptr;\n"
     "          range.memory = rt_readback_memory;\n"
     "          range.offset = 0;\n"
     "          range.size = VK_WHOLE_SIZE;\n"
     "          rb_dfn.vkInvalidateMappedMemoryRanges(rb_vk_device, 1, &range);\n"
     "          const uint8_t* bytes = static_cast<const uint8_t*>(mapping);\n"
     "          const uint32_t n = rt_readback_width * rt_readback_height * 4;\n"
     "          uint32_t with_colour = 0;\n"
     "          uint32_t with_alpha = 0;\n"
     "          for (uint32_t i = 0; i < n; i += 4) {\n"
     "            with_colour += (bytes[i] | bytes[i + 1] | bytes[i + 2]) != 0;\n"
     "            with_alpha += bytes[i + 3] != 0;\n"
     "          }\n"
     "          XELOGI(\"FT RT readback {}x{}: {} pixels with colour, {} with alpha, first \"\n"
     "                 \"{:02X} {:02X} {:02X} {:02X}, middle {:02X} {:02X} {:02X} {:02X}\",\n"
     "                 rt_readback_width, rt_readback_height, with_colour, with_alpha, bytes[0],\n"
     "                 bytes[1], bytes[2], bytes[3], bytes[n / 2], bytes[n / 2 + 1],\n"
     "                 bytes[n / 2 + 2], bytes[n / 2 + 3]);\n"
     "          const std::string path = std::string(rt_readback_dir) + \"/rt-readback.raw\";\n"
     "          if (FILE* file = fopen(path.c_str(), \"wb\")) {\n"
     "            fwrite(bytes, 1, n, file);\n"
     "            fclose(file);\n"
     "          }\n"
     "          rb_dfn.vkUnmapMemory(rb_vk_device, rt_readback_memory);\n"
     "        }\n"
     "      }\n"
     "      if (dao_trace::Active() && !dump_invocations_.empty()) {\n"
     "        if (rt_readback_buffer == VK_NULL_HANDLE) {\n"
     "          ui::vulkan::util::CreateDedicatedAllocationBuffer(\n"
     "              rb_device, kRtReadbackSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,\n"
     "              ui::vulkan::util::MemoryPurpose::kReadback, rt_readback_buffer,\n"
     "              rt_readback_memory);\n"
     "        }\n"
     "        auto& rb_rt = *static_cast<VulkanRenderTarget*>(\n"
     "            dump_invocations_.front().rectangle.render_target);\n"
     "        const RenderTargetKey rb_key = rb_rt.key();\n"
     "        if (rt_readback_buffer != VK_NULL_HANDLE && !rb_key.is_depth &&\n"
     "            rb_key.msaa_samples == xenos::MsaaSamples::k1X) {\n"
     "          rt_readback_width = std::min<uint32_t>(rb_key.GetWidth(), 1280);\n"
     "          rt_readback_height = std::min<uint32_t>(\n"
     "              GetRenderTargetHeight(rb_key.pitch_tiles_at_32bpp, rb_key.msaa_samples), 720);\n"
     "          TransitionRenderTarget(rb_rt, VK_PIPELINE_STAGE_TRANSFER_BIT,\n"
     "                                 VK_ACCESS_TRANSFER_READ_BIT,\n"
     "                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);\n"
     "          command_processor_.SubmitBarriers(true);\n"
     "          VkBufferImageCopy region = {};\n"
     "          region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;\n"
     "          region.imageSubresource.layerCount = 1;\n"
     "          region.imageExtent.width = rt_readback_width;\n"
     "          region.imageExtent.height = rt_readback_height;\n"
     "          region.imageExtent.depth = 1;\n"
     "          command_processor_.deferred_command_buffer().CmdVkCopyImageToBuffer(\n"
     "              rb_rt.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rt_readback_buffer, 1,\n"
     "              &region);\n"
     "          TransitionRenderTarget(rb_rt, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,\n"
     "                                 VK_ACCESS_SHADER_READ_BIT,\n"
     "                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);\n"
     "          rt_readback_pending = true;\n"
     "          rt_readback_submission = command_processor_.GetCurrentSubmission();\n"
     "          XELOGI(\"FT RT readback recorded for RT base {} ({}x{})\", rb_key.base_tiles,\n"
     "                 rt_readback_width, rt_readback_height);\n"
     "        }\n"
     "      }\n"
     "    }\n"
     "  }\n"
     "\n"),
    (SST_C, "fragment shaders: gl_SampleMask is written (full coverage) whenever it is declared",
     "void SpirvShaderTranslator::StartFragmentShaderInMain() {\n"
     "  // TODO(Triang3l): With sample shading (for depth format conversion) only\n",
     "void SpirvShaderTranslator::StartFragmentShaderInMain() {\n"
     "  // DAO/Warden (BUG-039): gl_SampleMask is declared for alpha to coverage, but\n"
     "  // only written on the alpha-test path, which a shader with early fragment tests\n"
     "  // skips. A declared, unwritten sample mask output is undefined coverage; desktop\n"
     "  // drivers happen to keep the primitive's coverage, the Adreno 740 driver\n"
     "  // discards every fragment. Full coverage first, so alpha to coverage can only\n"
     "  // narrow it.\n"
     "  if (output_fragment_sample_mask_ != spv::NoResult) {\n"
     "    id_vector_temp_.clear();\n"
     "    id_vector_temp_.push_back(const_int_0_);\n"
     "    builder_->createStore(\n"
     "        builder_->makeIntConstant(-1),\n"
     "        builder_->createAccessChain(spv::StorageClassOutput,\n"
     "                                    output_fragment_sample_mask_, id_vector_temp_));\n"
     "  }\n"
     "  // TODO(Triang3l): With sample shading (for depth format conversion) only\n"),
    (DFT_H, "frame trace: the string header",
     "#include <cstdlib>\n",
     "#include <cstdlib>\n"
     "#include <cstring>\n"),
    (DFT_H, "frame trace: 'busy' traces the first frame after a busy one; RequestTrace",
     "inline bool Active() {\n"
     "  static const long target = [] {\n"
     "    const char* env = std::getenv(\"DAO_GPU_FRAME_TRACE\");\n"
     "    return env ? std::atol(env) : -1L;\n"
     "  }();\n"
     "  return target >= 0 && SwapCount() == uint32_t(target) + 1;\n"
     "}\n",
     "// DAO/Warden: a trace requested at run time - RequestTrace(n) traces the frame n\n"
     "// swaps from now (warden_game.cc requests one a few seconds after the game enters\n"
     "// the mode named in files/trace-mode).\n"
     "inline std::atomic<uint32_t>& RequestedSwap() {\n"
     "  static std::atomic<uint32_t> swap{0};\n"
     "  return swap;\n"
     "}\n"
     "inline void RequestTrace(uint32_t swaps_from_now) {\n"
     "  RequestedSwap().store(SwapCount() + swaps_from_now, std::memory_order_relaxed);\n"
     "}\n"
     "inline bool Active() {\n"
     "  static const long target = [] {\n"
     "    const char* env = std::getenv(\"DAO_GPU_FRAME_TRACE\");\n"
     "    return env ? std::atol(env) : -1L;\n"
     "  }();\n"
     "  // DAO/Warden: DAO_GPU_FRAME_TRACE=busy traces the first frame after one with 60\n"
     "  // or more submitted draws - the first frame of a fuller scene, wherever it falls\n"
     "  // in a run whose timing varies.\n"
     "  static const bool trace_busy = [] {\n"
     "    const char* env = std::getenv(\"DAO_GPU_FRAME_TRACE\");\n"
     "    return env && std::strcmp(env, \"busy\") == 0;\n"
     "  }();\n"
     "  const uint32_t requested = RequestedSwap().load(std::memory_order_relaxed);\n"
     "  if (requested && SwapCount() == requested) {\n"
     "    return true;\n"
     "  }\n"
     "  if (trace_busy) {\n"
     "    static uint32_t chosen = 0;\n"
     "    const uint32_t swap = SwapCount();\n"
     "    if (!chosen && swap &&\n"
     "        StatsHistory()[(swap - 1) % kStatsHistory].submitted >= 60) {\n"
     "      chosen = swap;\n"
     "    }\n"
     "    return chosen && swap == chosen;\n"
     "  }\n"
     "  return target >= 0 && SwapCount() == uint32_t(target) + 1;\n"
     "}\n"),
    (DFT_H, "frame trace: uploads into DAO_LOG_UPLOADS ranges are always logged",
     "inline bool Active() {\n",
     "// DAO/Warden (3.8): GPU time per render pass in the traced frame - a timestamp before\n"
     "// each pass and after it (vulkan_command_processor.cc), read back at a later swap once\n"
     "// the frame's submission has completed, and logged as \"FT gpu\" lines.\n"
     "struct PassTiming {\n"
     "  uint32_t width;\n"
     "  uint32_t height;\n"
     "  uint32_t draws;\n"
     "  uint32_t first_draw;\n"
     "  uint64_t first_vs;  // the pass's first draw's shaders, to tell the passes apart\n"
     "  uint64_t first_ps;\n"
     "};\n"
     "inline PassTiming pass_timings[1024];\n"
     "inline uint32_t pass_count = 0;\n"
     "inline uint32_t draws_in_frame = 0;\n"
     "inline bool timing_pending = false;\n"
     "inline uint64_t timing_submission = 0;\n"
     "inline uint64_t timestamp_pool = 0;  // a VkQueryPool\n"
     "inline float timestamp_period_ns = 1.0f;\n"
     "// DAO/Warden (BUG-052): the last 64 draws, recorded always, so a resolve can say what\n"
     "// was drawn before it - the HUD portraits are drawn and resolved inside the frame an\n"
     "// area load begins in, which no trace armed by the load reaches.\n"
     "struct RecentDraw {\n"
     "  uint64_t vs_hash;\n"
     "  uint64_t ps_hash;\n"
     "  uint32_t prim_type;\n"
     "  uint32_t index_count;\n"
     "  uint32_t surface_info;   // RB_SURFACE_INFO\n"
     "  uint32_t color_info;     // RB_COLOR_INFO\n"
     "  uint32_t depth_info;     // RB_DEPTH_INFO\n"
     "  uint32_t depth_control;  // RB_DEPTHCONTROL\n"
     "  uint32_t color_mask;     // RB_COLOR_MASK\n"
     "  uint32_t texture[6][3];  // the first six used fetch constants, dwords 0-2\n"
     "  uint32_t ps_float_count;    // the pixel shader's float constants read\n"
     "  uint32_t ps_float_zero;     // of them, zero vectors\n"
     "  uint32_t ps_index[8];       // the first eight read (0xFFFF: none)\n"
     "  uint32_t ps_value[8][4];    // their values, as bits\n"
     "  uint32_t vs_float_count;    // the vertex shader's float constants read\n"
     "  uint32_t vs_float_zero;     // of them, zero vectors\n"
     "  uint32_t vs_float_bad;      // of them, holding a NaN or an infinity\n"
     "  uint32_t vs_float_sum;      // a checksum of their bits\n"
     "  uint64_t vs_zero[4];        // which VS constants read were zero vectors\n"
     "  uint32_t number;            // recent_draw_count when it was recorded\n"
     "  // BUG-049: where the draw lands - the host viewport and scissor as set\n"
     "  // (VulkanCommandProcessor::UpdateDynamicState), and the guest's registers for them.\n"
     "  float host_viewport[4];      // x y width height\n"
     "  int32_t host_scissor[4];     // x y width height\n"
     "  uint32_t window_offset;      // PA_SC_WINDOW_OFFSET\n"
     "  uint32_t window_scissor[2];  // PA_SC_WINDOW_SCISSOR_TL, _BR\n"
     "  uint32_t screen_scissor[2];  // PA_SC_SCREEN_SCISSOR_TL, _BR\n"
     "  uint32_t vte_cntl;           // PA_CL_VTE_CNTL\n"
     "  uint32_t su_sc_mode;         // PA_SU_SC_MODE_CNTL\n"
     "  uint32_t viewport[4];        // PA_CL_VPORT_XSCALE, _XOFFSET, _YSCALE, _YOFFSET (bits)\n"
     "  uint32_t vfetch[2];          // vertex fetch constant 95 (\"vf0\": D3D's stream 0)\n"
     "  uint32_t texture0_high[3];   // the first texture's fetch constant, dwords 3-5\n"
     "};\n"
     "inline RecentDraw recent_draws[64];\n"
     "inline uint32_t recent_draw_count = 0;\n"
     "// DAO/Warden (BUG-049): the current draw's texture bindings are logged (its pixel\n"
     "// shader is DAO_HACK_LOG_BINDINGS_PS's); how many such draws so far.\n"
     "inline bool log_bindings = false;\n"
     "inline uint32_t log_binding_draws = 0;\n"
     "// DAO/Warden (BUG-049): a gate for an event that can repeat every frame - the first\n"
     "// 16, then one a second; count says how many came, logged or not.\n"
     "struct EventGate {\n"
     "  uint32_t count = 0;\n"
     "  int64_t last_ms = 0;\n"
     "  bool Pass() {\n"
     "    ++count;\n"
     "    const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(\n"
     "                               std::chrono::steady_clock::now().time_since_epoch())\n"
     "                               .count();\n"
     "    if (count <= 16 || now_ms - last_ms >= 1000) {\n"
     "      last_ms = now_ms;\n"
     "      return true;\n"
     "    }\n"
     "    return false;\n"
     "  }\n"
     "};\n"
     "// DAO/Warden (BUG-049): the last 128 events of the textures at\n"
     "// DAO_HACK_RESOLVE_DUMP_ADDRESS - loads and watch firings, each with its texture\n"
     "// object and its key's distinguishing bits - printed when DAO_HACK_LOG_BINDINGS_PS's\n"
     "// pixel shader binds one of them: which texture it got, and its history.\n"
     "struct TextureEvent {\n"
     "  const void* texture;\n"
     "  uint32_t kind;  // 0 loaded, 1 loaded in a batch, 2 outdated by the GPU, 3 by the\n"
     "                  // CPU, 4 created\n"
     "  uint32_t key_bits;  // see TextureKeyBits\n"
     "  int64_t ms;\n"
     "};\n"
     "constexpr uint32_t kTextureEventCount = 4096;  // some 40 s of them\n"
     "inline TextureEvent texture_events[kTextureEventCount];\n"
     "inline uint32_t texture_event_count = 0;\n"
     "inline std::mutex texture_events_mutex;\n"
     "inline uint32_t TextureKeyBits(uint32_t tiled, uint32_t endianness,\n"
     "                               uint32_t signed_separate, uint32_t scaled_resolve,\n"
     "                               uint32_t pitch, uint32_t format) {\n"
     "  return tiled | (endianness << 1) | (signed_separate << 3) | (scaled_resolve << 4) |\n"
     "         (pitch << 8) | (format << 20);\n"
     "}\n"
     "inline void RecordTextureEvent(const void* texture, uint32_t kind, uint32_t key_bits) {\n"
     "  std::lock_guard<std::mutex> lock(texture_events_mutex);\n"
     "  TextureEvent& e = texture_events[texture_event_count++ % kTextureEventCount];\n"
     "  e.texture = texture;\n"
     "  e.kind = kind;\n"
     "  e.key_bits = key_bits;\n"
     "  e.ms = std::chrono::duration_cast<std::chrono::milliseconds>(\n"
     "             std::chrono::steady_clock::now().time_since_epoch())\n"
     "             .count();\n"
     "}\n"
     "// The bound texture's last 32 events, oldest first, then each other texture object\n"
     "// seen at the address: its key, its events, and its last.\n"
     "inline void LogTextureEvents(const void* bound) {\n"
     "  static const char* const kKinds[8] = {\"loaded\", \"loaded in a batch\",\n"
     "                                        \"outdated by the GPU\", \"outdated by the CPU\",\n"
     "                                        \"created\", \"?\", \"?\", \"?\"};\n"
     "  std::lock_guard<std::mutex> lock(texture_events_mutex);\n"
     "  const uint32_t n = texture_event_count < kTextureEventCount ? texture_event_count\n"
     "                                                              : kTextureEventCount;\n"
     "  const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(\n"
     "                             std::chrono::steady_clock::now().time_since_epoch())\n"
     "                             .count();\n"
     "  uint32_t mine[32];\n"
     "  uint32_t mine_count = 0;\n"
     "  struct Other {\n"
     "    const void* texture;\n"
     "    uint32_t key_bits, events, last;\n"
     "  } others[8];\n"
     "  uint32_t other_count = 0;\n"
     "  for (uint32_t back = 0; back < n; ++back) {\n"
     "    const uint32_t index = (texture_event_count - 1 - back) % kTextureEventCount;\n"
     "    const TextureEvent& e = texture_events[index];\n"
     "    if (e.texture == bound) {\n"
     "      if (mine_count < 32) {\n"
     "        mine[mine_count++] = index;\n"
     "      }\n"
     "      continue;\n"
     "    }\n"
     "    uint32_t o = 0;\n"
     "    while (o < other_count && others[o].texture != e.texture) {\n"
     "      ++o;\n"
     "    }\n"
     "    if (o == other_count) {\n"
     "      if (other_count == 8) {\n"
     "        continue;\n"
     "      }\n"
     "      others[other_count++] = Other{e.texture, e.key_bits, 0, index};\n"
     "    }\n"
     "    ++others[o].events;\n"
     "  }\n"
     "  XELOGI(\"FT tex events: {} in the ring; the bound texture {}'s last {}:\", n,\n"
     "         fmt::ptr(bound), mine_count);\n"
     "  for (uint32_t i = mine_count; i-- > 0;) {\n"
     "    const TextureEvent& e = texture_events[mine[i]];\n"
     "    XELOGI(\"FT tex event: {} key {:08X}, {} ms ago\", kKinds[e.kind & 7], e.key_bits,\n"
     "           now_ms - e.ms);\n"
     "  }\n"
     "  for (uint32_t o = 0; o < other_count; ++o) {\n"
     "    const TextureEvent& last = texture_events[others[o].last];\n"
     "    XELOGI(\"FT tex events: other texture {} key {:08X}: {} in the ring, the last {} \"\n"
     "           \"{} ms ago\",\n"
     "           fmt::ptr(others[o].texture), others[o].key_bits, others[o].events,\n"
     "           kKinds[last.kind & 7], now_ms - last.ms);\n"
     "  }\n"
     "}\n"
     "// DAO/Warden (BUG-052): the last 64 LOAD_ALU_CONSTANT packets, with the draw count\n"
     "// when each came - where the constants of the draws before a resolve came from.\n"
     "struct RecentConstantLoad {\n"
     "  uint32_t address;     // guest address read\n"
     "  uint32_t first_word;  // first constant dword written (4 a constant; 1024+: PS)\n"
     "  uint32_t word_count;\n"
     "  uint32_t draw_count;  // recent_draw_count when it came\n"
     "};\n"
     "inline RecentConstantLoad recent_constant_loads[64];\n"
     "inline uint32_t recent_constant_load_count = 0;\n"
     "// The set bits of a bit map as ranges, \"12-54 60\".\n"
     "inline std::string BitRanges(const uint64_t* bits, uint32_t count) {\n"
     "  std::string out;\n"
     "  for (uint32_t i = 0; i < count;) {\n"
     "    if (!((bits[i >> 6] >> (i & 63)) & 1)) {\n"
     "      ++i;\n"
     "      continue;\n"
     "    }\n"
     "    uint32_t j = i;\n"
     "    while (j + 1 < count && ((bits[(j + 1) >> 6] >> ((j + 1) & 63)) & 1)) {\n"
     "      ++j;\n"
     "    }\n"
     "    if (!out.empty()) {\n"
     "      out += ' ';\n"
     "    }\n"
     "    out += std::to_string(i);\n"
     "    if (j > i) {\n"
     "      out += '-';\n"
     "      out += std::to_string(j);\n"
     "    }\n"
     "    i = j + 1;\n"
     "  }\n"
     "  return out;\n"
     "}\n"
     "inline void RecordDraw(const RegisterFile& regs, uint64_t vs_hash, uint64_t ps_hash,\n"
     "                       uint32_t ps_texture_mask, uint32_t prim_type,\n"
     "                       uint32_t index_count, const uint64_t* ps_float_bitmap,\n"
     "                       const uint64_t* vs_float_bitmap) {\n"
     "  RecentDraw& d = recent_draws[recent_draw_count++ % 64];\n"
     "  d.vs_hash = vs_hash;\n"
     "  d.ps_hash = ps_hash;\n"
     "  d.prim_type = prim_type;\n"
     "  d.index_count = index_count;\n"
     "  d.surface_info = regs.values[XE_GPU_REG_RB_SURFACE_INFO];\n"
     "  d.color_info = regs.values[XE_GPU_REG_RB_COLOR_INFO];\n"
     "  d.depth_info = regs.values[XE_GPU_REG_RB_DEPTH_INFO];\n"
     "  d.depth_control = regs.values[XE_GPU_REG_RB_DEPTHCONTROL];\n"
     "  d.color_mask = regs.values[XE_GPU_REG_RB_COLOR_MASK];\n"
     "  d.vs_float_count = 0;\n"
     "  d.vs_float_zero = 0;\n"
     "  d.vs_float_bad = 0;\n"
     "  d.vs_float_sum = 0;\n"
     "  d.vs_zero[0] = d.vs_zero[1] = d.vs_zero[2] = d.vs_zero[3] = 0;\n"
     "  d.number = recent_draw_count - 1;\n"
     "  for (uint32_t k = 0; k < 4; ++k) {\n"
     "    d.host_viewport[k] = 0.0f;\n"
     "    d.host_scissor[k] = 0;\n"
     "  }\n"
     "  d.window_offset = regs.values[XE_GPU_REG_PA_SC_WINDOW_OFFSET];\n"
     "  d.window_scissor[0] = regs.values[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL];\n"
     "  d.window_scissor[1] = regs.values[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR];\n"
     "  d.screen_scissor[0] = regs.values[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL];\n"
     "  d.screen_scissor[1] = regs.values[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR];\n"
     "  d.vte_cntl = regs.values[XE_GPU_REG_PA_CL_VTE_CNTL];\n"
     "  d.su_sc_mode = regs.values[XE_GPU_REG_PA_SU_SC_MODE_CNTL];\n"
     "  d.viewport[0] = regs.values[XE_GPU_REG_PA_CL_VPORT_XSCALE];\n"
     "  d.viewport[1] = regs.values[XE_GPU_REG_PA_CL_VPORT_XOFFSET];\n"
     "  d.viewport[2] = regs.values[XE_GPU_REG_PA_CL_VPORT_YSCALE];\n"
     "  d.viewport[3] = regs.values[XE_GPU_REG_PA_CL_VPORT_YOFFSET];\n"
     "  d.vfetch[0] = regs.values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + 95 * 2];\n"
     "  d.vfetch[1] = regs.values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + 95 * 2 + 1];\n"
     "  for (uint32_t word = 0; vs_float_bitmap && word < 4; ++word) {\n"
     "    uint64_t bits = vs_float_bitmap[word];\n"
     "    while (bits) {\n"
     "      const uint32_t index = word * 64 + uint32_t(__builtin_ctzll(bits));\n"
     "      bits &= bits - 1;\n"
     "      const uint32_t* v = &regs.values[XE_GPU_REG_SHADER_CONSTANT_000_X + index * 4];\n"
     "      ++d.vs_float_count;\n"
     "      bool zero = true;\n"
     "      bool bad = false;\n"
     "      for (uint32_t k = 0; k < 4; ++k) {\n"
     "        zero = zero && !(v[k] & 0x7FFFFFFF);\n"
     "        bad = bad || ((v[k] >> 23) & 0xFF) == 0xFF;\n"
     "        d.vs_float_sum = d.vs_float_sum * 31 + v[k];\n"
     "      }\n"
     "      d.vs_float_zero += zero;\n"
     "      if (zero) {\n"
     "        d.vs_zero[index >> 6] |= uint64_t(1) << (index & 63);\n"
     "      }\n"
     "      d.vs_float_bad += bad;\n"
     "    }\n"
     "  }\n"
     "  d.ps_float_count = 0;\n"
     "  d.ps_float_zero = 0;\n"
     "  uint32_t ps_listed = 0;\n"
     "  for (uint32_t i = 0; i < 8; ++i) {\n"
     "    d.ps_index[i] = 0xFFFF;\n"
     "  }\n"
     "  for (uint32_t word = 0; ps_float_bitmap && word < 4; ++word) {\n"
     "    uint64_t bits = ps_float_bitmap[word];\n"
     "    while (bits) {\n"
     "      const uint32_t index = word * 64 + uint32_t(__builtin_ctzll(bits));\n"
     "      bits &= bits - 1;\n"
     "      const uint32_t* v = &regs.values[XE_GPU_REG_SHADER_CONSTANT_256_X + index * 4];\n"
     "      ++d.ps_float_count;\n"
     "      if (!(v[0] & 0x7FFFFFFF) && !(v[1] & 0x7FFFFFFF) && !(v[2] & 0x7FFFFFFF) &&\n"
     "          !(v[3] & 0x7FFFFFFF)) {\n"
     "        ++d.ps_float_zero;\n"
     "      }\n"
     "      if (ps_listed < 8) {\n"
     "        d.ps_index[ps_listed] = index;\n"
     "        for (uint32_t k = 0; k < 4; ++k) {\n"
     "          d.ps_value[ps_listed][k] = v[k];\n"
     "        }\n"
     "        ++ps_listed;\n"
     "      }\n"
     "    }\n"
     "  }\n"
     "  uint32_t mask = ps_texture_mask;\n"
     "  d.texture0_high[0] = d.texture0_high[1] = d.texture0_high[2] = 0;\n"
     "  for (uint32_t t = 0; t < 6; ++t) {\n"
     "    d.texture[t][0] = d.texture[t][1] = d.texture[t][2] = 0;\n"
     "    if (mask) {\n"
     "      const uint32_t index = uint32_t(__builtin_ctz(mask));\n"
     "      mask &= mask - 1;\n"
     "      for (uint32_t w = 0; w < 3; ++w) {\n"
     "        d.texture[t][w] = regs.values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + index * 6 + w];\n"
     "      }\n"
     "      for (uint32_t w = 0; t == 0 && w < 3; ++w) {\n"
     "        d.texture0_high[w] =\n"
     "            regs.values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + index * 6 + 3 + w];\n"
     "      }\n"
     "    }\n"
     "  }\n"
     "}\n"
     "// DAO/Warden: DAO_LOG_UPLOADS names guest physical ranges (\"start+length\" in hex,\n"
     "// comma separated) whose shared-memory uploads are logged in every frame.\n"
     "inline bool UploadLogged(uint32_t start, uint32_t length) {\n"
     "  struct Range {\n"
     "    uint32_t start;\n"
     "    uint32_t length;\n"
     "  };\n"
     "  static const std::vector<Range> ranges = [] {\n"
     "    std::vector<Range> out;\n"
     "    const char* env = std::getenv(\"DAO_LOG_UPLOADS\");\n"
     "    while (env && *env) {\n"
     "      char* end = nullptr;\n"
     "      const unsigned long range_start = std::strtoul(env, &end, 16);\n"
     "      if (end == env || *end != '+') {\n"
     "        break;\n"
     "      }\n"
     "      const unsigned long range_length = std::strtoul(end + 1, &end, 16);\n"
     "      out.push_back({uint32_t(range_start), uint32_t(range_length)});\n"
     "      env = *end == ',' ? end + 1 : end;\n"
     "    }\n"
     "    return out;\n"
     "  }();\n"
     "  for (const Range& range : ranges) {\n"
     "    if (start < range.start + range.length && range.start < start + length) {\n"
     "      return true;\n"
     "    }\n"
     "  }\n"
     "  return false;\n"
     "}\n"
     "inline bool Active() {\n"),
    (VRTC_C, "resolve: say when the region is empty",
     "  // Nothing to copy/clear.\n"
     "  if (!resolve_info.coordinate_info.width_div_8 || !resolve_info.height_div_8) {\n"
     "    return true;\n"
     "  }",
     "  // Nothing to copy/clear.\n"
     "  if (!resolve_info.coordinate_info.width_div_8 || !resolve_info.height_div_8) {\n"
     "    // DAO/Warden: said once a second - a title whose every resolve is empty shows\n"
     "    // a black frame, and nothing else in the log would say why.\n"
     "    static uint32_t empty_resolves = 0;\n"
     "    if (++empty_resolves % 60 == 1) {\n"
     "      XELOGW(\"Resolve {}: empty region ({}x{} /8, EDRAM offset {},{} /8), dest base \"\n"
     "             \"{:08X} - nothing written\",\n"
     "             empty_resolves, resolve_info.coordinate_info.width_div_8,\n"
     "             resolve_info.height_div_8,\n"
     "             resolve_info.coordinate_info.edram_offset_x_div_8,\n"
     "             resolve_info.coordinate_info.edram_offset_y_div_8,\n"
     "             resolve_info.copy_dest_base);\n"
     "    }\n"
     "    return true;\n"
     "  }"),
    (PM4_H, "WAIT_REG_MEM: say what a stalled wait is waiting on",
     "        xe::dao_host::ShortSleep(kWaitPollMicroseconds);\n",
     "        xe::dao_host::ShortSleep(kWaitPollMicroseconds);\n"
     "        // DAO/Warden: a wait of a second is a wait on something that is not\n"
     "        // coming; say what, once a second (app/src/main/cpp/warden_gpu.cc).\n"
     "        if (++stalled_polls % 10000 == 0) {\n"
     "          XELOGW(\"GPU: WAIT_REG_MEM stalled {} s on {} {:08X}: value {:08X} & \"\n"
     "                 \"{:08X} vs ref {:08X}, wait_info {:08X}\",\n"
     "                 stalled_polls / 10000, is_memory ? \"memory\" : \"register\",\n"
     "                 poll_reg_addr, value, mask, ref, wait_info);\n"
     "        }\n"),
    (VK_API_H, "include vulkan.hpp before vulkan_to_string.hpp",
     '#include "third_party/Vulkan-Headers/include/vulkan/vulkan_to_string.hpp"',
     "// DAO: vulkan.hpp must be included first. vulkan_to_string.hpp includes\n"
     "// vulkan.hpp, and vulkan.hpp then re-includes vulkan_to_string.hpp for its own\n"
     "// to_string() calls. Taking to_string first sets its include guard, so that\n"
     "// re-include is skipped and vulkan.hpp fails on an undeclared to_string.\n"
     '#include "third_party/Vulkan-Headers/include/vulkan/vulkan.hpp"\n'
     '#include "third_party/Vulkan-Headers/include/vulkan/vulkan_to_string.hpp"'),

    # DAORecomp added two parameters to Presenter's interface for its own features:
    # dao_sdr (tonemap the game's HDR output to SDR when capturing a screenshot) and
    # dao_generated_ref (whether a mailbox slot also receives a generated frame).
    # The Vulkan presenter has to match the base class. Neither feature exists on
    # this backend yet, so both are handled honestly rather than faked.
    (VKP_H, "CaptureGuestOutput takes dao_sdr",
     "  bool CaptureGuestOutput(RawImage& image_out) override;",
     "  // DAO: dao_sdr requests the game's HDR output tonemapped to SDR. Not yet\n"
     "  // implemented on Vulkan; captures are returned as encoded.\n"
     "  bool CaptureGuestOutput(RawImage& image_out, bool dao_sdr) override;"),

    (VKP_H, "RefreshGuestOutputImpl takes dao_generated_ref",
     "      std::function<bool(GuestOutputRefreshContext& context)> refresher,\n"
     "      bool& is_8bpc_out_ref) override;",
     "      std::function<bool(GuestOutputRefreshContext& context)> refresher,\n"
     "      bool& is_8bpc_out_ref, bool& dao_generated_ref) override;"),

    (VKP_C, "CaptureGuestOutput definition takes dao_sdr",
     "bool VulkanPresenter::CaptureGuestOutput(RawImage& image_out) {",
     "bool VulkanPresenter::CaptureGuestOutput(RawImage& image_out, bool dao_sdr) {\n"
     "  // TODO(dao): honour dao_sdr by tonemapping game HDR output to SDR, as the\n"
     "  // D3D12 presenter does via DaoTonemapCapture. Until then a capture of an\n"
     "  // HDR frame comes back encoded rather than tonemapped.\n"
     "  (void)dao_sdr;"),

    (VKP_C, "RefreshGuestOutputImpl definition takes dao_generated_ref",
     "    std::function<bool(GuestOutputRefreshContext& context)> refresher,\n"
     "    bool& is_8bpc_out_ref) {\n"
     "  assert_not_zero(frontbuffer_width);",
     "    std::function<bool(GuestOutputRefreshContext& context)> refresher,\n"
     "    bool& is_8bpc_out_ref, bool& dao_generated_ref) {\n"
     "  // DAO/Warden (PLAN 9.14): dao_generated_ref - the slot's generated frame, below.\n"
     "  assert_not_zero(frontbuffer_width);"),
    (VKP_C, "9.14: frame generation's hooks declared",
     "bool VulkanPresenter::RefreshGuestOutputImpl(\n",
     "// DAO/Warden (PLAN 9.14): app/src/main/cpp/warden_frame_generation.cc - the generated\n"
     "// frames a guest output gets (0 without frame generation) - and\n"
     "// dao_frame_pacer_android.cc - which of them the calling thread paints.\n"
     "extern \"C\" uint32_t warden_frame_generation_count();\n"
     "extern \"C\" uint32_t warden_pacer_generated_index();\n"
     "\n"
     "bool VulkanPresenter::RefreshGuestOutputImpl(\n"),
    (VKP_C, "9.14: the slot's generated frame, written by the refresher too",
     "  VulkanGuestOutputRefreshContext context(\n"
     "      is_8bpc_out_ref, image_instance.image->image(),\n"
     "      image_instance.image->view(), image_instance.version,\n"
     "      image_instance.ever_successfully_refreshed);\n"
     "  bool refresher_succeeded = refresher(context);\n",
     "  // DAO/Warden (PLAN 9.14): the slot's generated frames (frame generation,\n"
     "  // app/src/main/cpp/warden_frame_generation.cc - warden_frame_generation_count() of\n"
     "  // them, up to kDaoMaxGenerated), written by the refresher too: images like the guest\n"
     "  // output's, made when asked for - none shown if one can't be made (not tried again\n"
     "  // at this size). Painting has its own references. (Their size changes with the\n"
     "  // guest output's, whose recreation above has awaited the slot's last refresh.)\n"
     "  const uint32_t dao_generated_count =\n"
     "      std::min(warden_frame_generation_count(),\n"
     "               VulkanGuestOutputRefreshContext::kDaoMaxGenerated);\n"
     "  for (uint32_t i = 0; i < VulkanGuestOutputRefreshContext::kDaoMaxGenerated; ++i) {\n"
     "    std::shared_ptr<GuestOutputImage>& dao_generated_image =\n"
     "        dao_generated_images_[mailbox_index][i];\n"
     "    if (dao_generated_image &&\n"
     "        (dao_generated_image->extent().width != frontbuffer_width ||\n"
     "         dao_generated_image->extent().height != frontbuffer_height)) {\n"
     "      guest_output_image_refresher_completion_timeline_\n"
     "          .AwaitSubmissionAndUpdateCompleted(\n"
     "              image_instance.last_refresher_submission);\n"
     "      dao_generated_image.reset();\n"
     "    }\n"
     "    if (dao_generated_ref && i < dao_generated_count && !dao_generated_image &&\n"
     "        (dao_generated_failed_size_[0] != frontbuffer_width ||\n"
     "         dao_generated_failed_size_[1] != frontbuffer_height)) {\n"
     "      dao_generated_image = GuestOutputImage::Create(\n"
     "          vulkan_device_, frontbuffer_width, frontbuffer_height);\n"
     "      if (!dao_generated_image) {\n"
     "        XELOGE(\"VulkanPresenter: Failed to create the generated frame {}x{} image\",\n"
     "               frontbuffer_width, frontbuffer_height);\n"
     "        dao_generated_failed_size_[0] = frontbuffer_width;\n"
     "        dao_generated_failed_size_[1] = frontbuffer_height;\n"
     "      }\n"
     "    }\n"
     "    dao_generated_ref =\n"
     "        dao_generated_ref && (i >= dao_generated_count || dao_generated_image);\n"
     "  }\n"
     "\n"
     "  VulkanGuestOutputRefreshContext context(\n"
     "      is_8bpc_out_ref, image_instance.image->image(),\n"
     "      image_instance.image->view(), image_instance.version,\n"
     "      image_instance.ever_successfully_refreshed);\n"
     "  for (uint32_t i = 0; dao_generated_ref && i < dao_generated_count; ++i) {\n"
     "    context.dao_set_generated_image(\n"
     "        i, dao_generated_images_[mailbox_index][i]->image());\n"
     "  }\n"
     "  bool refresher_succeeded = refresher(context);\n"),
    (VKP_H, "9.14: the refresh context's generated frame",
     "    bool image_ever_written_previously() const {\n"
     "      return image_ever_written_previously_;\n"
     "    }\n"
     "\n"
     "   private:\n",
     "    bool image_ever_written_previously() const {\n"
     "      return image_ever_written_previously_;\n"
     "    }\n"
     "    // DAO/Warden (PLAN 9.14): the slot's generated frames to write too - images like\n"
     "    // image(), released in the same state - VK_NULL_HANDLE past their count.\n"
     "    static constexpr uint32_t kDaoMaxGenerated = 3;\n"
     "    VkImage dao_generated_image(uint32_t i) const {\n"
     "      return i < kDaoMaxGenerated ? dao_generated_images_[i] : VK_NULL_HANDLE;\n"
     "    }\n"
     "    void dao_set_generated_image(uint32_t i, VkImage image) {\n"
     "      dao_generated_images_[i] = image;\n"
     "    }\n"
     "\n"
     "   private:\n"
     "    VkImage dao_generated_images_[kDaoMaxGenerated] = {};\n"),
    (VKP_H, "9.14: each mailbox slot's generated frame",
     "  std::array<GuestOutputImageInstance, kGuestOutputMailboxSize>\n"
     "      guest_output_images_;\n",
     "  std::array<GuestOutputImageInstance, kGuestOutputMailboxSize>\n"
     "      guest_output_images_;\n"
     "  // DAO/Warden (PLAN 9.14): each slot's generated frames (RefreshGuestOutputImpl),\n"
     "  // and the size one couldn't be made at.\n"
     "  std::array<std::array<std::shared_ptr<GuestOutputImage>,\n"
     "                        VulkanGuestOutputRefreshContext::kDaoMaxGenerated>,\n"
     "             kGuestOutputMailboxSize>\n"
     "      dao_generated_images_;\n"
     "  uint32_t dao_generated_failed_size_[2] = {};\n"),
    (VKP_C, "9.14: a pair's generated frame painted from the slot's generated image",
     "      guest_output_image =\n"
     "          guest_output_images_[guest_output_mailbox_index].image;\n"
     "    }\n"
     "    // Incremented the reference count of the guest output image - safe to leave\n"
     "    // the consumer critical section now as everything here either will be using\n",
     "      guest_output_image =\n"
     "          guest_output_images_[guest_output_mailbox_index].image;\n"
     "      // DAO/Warden (PLAN 9.14): frame generation's group (dao_frame_pacer_android.cc)\n"
     "      // paints the slot's generated frames first, the one it names.\n"
     "      if (DaoPacerPainting() == DaoPacerStage::kGenerated &&\n"
     "          guest_output_properties.dao_generated) {\n"
     "        const uint32_t dao_index = warden_pacer_generated_index();\n"
     "        if (dao_index < VulkanGuestOutputRefreshContext::kDaoMaxGenerated &&\n"
     "            dao_generated_images_[guest_output_mailbox_index][dao_index]) {\n"
     "          guest_output_image =\n"
     "              dao_generated_images_[guest_output_mailbox_index][dao_index];\n"
     "        }\n"
     "      }\n"
     "    }\n"
     "    // Incremented the reference count of the guest output image - safe to leave\n"
     "    // the consumer critical section now as everything here either will be using\n"),
    (VKP_C, "9.14: the swapchain's frame generation hook declared",
     "VkSwapchainKHR VulkanPresenter::PaintContext::CreateSwapchainForVulkanSurface(\n",
     "// DAO/Warden (PLAN 9.14): whether frame generation presents\n"
     "// (app/src/main/cpp/warden_frame_generation.cc): each guest output is presented as two,\n"
     "// the second held by its desired time - four more images, and FIFO, not MAILBOX,\n"
     "// whose newer present replaces the one still queued (run 429: one of each pair never\n"
     "// reached the panel). An image is acquired at once with a semaphore that signals when\n"
     "// the compositor lets it go, and the paint's wait for it holds up the game's next\n"
     "// frame on the one queue: with two more, the GPU idled a fifth of the time (runs\n"
     "// 431-435).\n"
     "extern \"C\" bool warden_frame_generation_presents();\n"
     "\n"
     "VkSwapchainKHR VulkanPresenter::PaintContext::CreateSwapchainForVulkanSurface(\n"),
    (VKP_C, "9.14: the swapchain's image count",
     "  swapchain_create_info.minImageCount =\n"
     "      std::max(kSubmissionCount, surface_capabilities.minImageCount);\n",
     "  // (3: upstream's kSubmissionCount, which the edit below raises for the paints.)\n"
     "  // Two more for each present of a guest output's group with frame generation.\n"
     "  swapchain_create_info.minImageCount =\n"
     "      std::max(uint32_t(3), surface_capabilities.minImageCount) +\n"
     "      (warden_frame_generation_presents()\n"
     "           ? 2 * (warden_frame_generation_count() + 1)\n"
     "           : 0);\n"),
    (VKP_C, "9.14: FIFO, not MAILBOX, while frame generation presents pairs",
     "  } else if (cvars::vulkan_allow_present_mode_mailbox &&\n"
     "             std::ranges::find(std::as_const(present_modes),\n",
     "  } else if (cvars::vulkan_allow_present_mode_mailbox &&\n"
     "             !warden_frame_generation_presents() &&  // DAO/Warden (PLAN 9.14)\n"
     "             std::ranges::find(std::as_const(present_modes),\n"),
    ("xenia/ui/presenter.cc", "9.14: the guest output thread paints while frame generation presents",
     "  if (surface_paint_connection_has_implicit_vsync_) {\n"
     "    // Don't be causing host vertical sync CPU waits in the thread generating\n"
     "    // the guest output.\n",
     "  // DAO/Warden (PLAN 9.14): except while frame generation presents its pairs\n"
     "  // (dao_frame_pacer_android.cc), which the guest output thread paints - FIFO then,\n"
     "  // with two more images so that it rarely waits for one (warden_frame_pacing.cc\n"
     "  // reports the waits). The UI thread would paint only the real frame (run 430).\n"
     "  if (surface_paint_connection_has_implicit_vsync_ &&\n"
     "      !warden_frame_generation_presents()) {\n"
     "    // Don't be causing host vertical sync CPU waits in the thread generating\n"
     "    // the guest output.\n"),
    ("xenia/ui/presenter.cc", "9.14: the frame generation hook declared",
     "Presenter::PaintMode Presenter::GetDesiredPaintModeFromUIThread(\n",
     "// DAO/Warden (PLAN 9.14): app/src/main/cpp/warden_frame_generation.cc.\n"
     "extern \"C\" bool warden_frame_generation_presents();\n"
     "\n"
     "Presenter::PaintMode Presenter::GetDesiredPaintModeFromUIThread(\n"),
    (VKP_H, "9.14: descriptors for the generated frames' images too",
     "      kGuestOutputDescriptorSetIntermediate0Sampled =\n"
     "          kGuestOutputDescriptorSetGuestOutput0Sampled +\n"
     "          kGuestOutputMailboxSize,\n",
     "      // DAO/Warden (PLAN 9.14): the mailbox's images and each slot's generated frames.\n"
     "      kGuestOutputDescriptorSetIntermediate0Sampled =\n"
     "          kGuestOutputDescriptorSetGuestOutput0Sampled +\n"
     "          (1 + VulkanGuestOutputRefreshContext::kDaoMaxGenerated) *\n"
     "              kGuestOutputMailboxSize,\n"),
    (VKP_H, "9.14: paint references for the generated frames' images too",
     "    std::array<std::pair<uint64_t, std::shared_ptr<GuestOutputImage>>,\n"
     "               kGuestOutputMailboxSize>\n"
     "        guest_output_image_paint_refs;\n",
     "    // DAO/Warden (PLAN 9.14): the mailbox's images and each slot's generated frames.\n"
     "    // With three, six images in turn replaced one at every paint, each replacement\n"
     "    // waiting for the GPU to finish that image's last paint (run 432: the title at\n"
     "    // 22 fps, the GPU 81% busy).\n"
     "    std::array<std::pair<uint64_t, std::shared_ptr<GuestOutputImage>>,\n"
     "               (1 + VulkanGuestOutputRefreshContext::kDaoMaxGenerated) *\n"
     "                   kGuestOutputMailboxSize>\n"
     "        guest_output_image_paint_refs;\n"),
    (VKP_H, "9.14: paint submissions in flight for two presents a guest output",
     "    static constexpr uint32_t kSubmissionCount = 3;\n",
     "    // DAO/Warden (PLAN 9.14): 16, not 3 - with frame generation each guest output is\n"
     "    // two to four paints, and the command processor runs up to its kMaxFramesInFlight\n"
     "    // (3) frames ahead of the GPU: with 3, then 6, the guest output thread waited in\n"
     "    // the real frame's paint for the GPU to finish older paints, 40 ms on average, and\n"
     "    // the GPU ran dry (runs 428-434: 22 fps, the GPU 80% busy and clocked down).\n"
     "    // Without frame generation the swapchain's three images still bound the paints in\n"
     "    // flight.\n"
     "    static constexpr uint32_t kSubmissionCount = 16;\n"),
    (VKP_C, "9.14: guest output images take transfers in (the generated frame's copy)",
     "  image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |\n"
     "                            VK_IMAGE_USAGE_SAMPLED_BIT |\n"
     "                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;\n",
     "  // DAO/Warden (PLAN 9.14): and a transfer destination - frame generation's frame\n"
     "  // is copied into a slot's generated image.\n"
     "  image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |\n"
     "                            VK_IMAGE_USAGE_TRANSFER_DST_BIT |\n"
     "                            VK_IMAGE_USAGE_SAMPLED_BIT |\n"
     "                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;\n"),

    # BUG-016. graphics_system.cc includes platform_win.h unconditionally, while the
    # only Windows code in the file (the Optimus / PowerXpress dllexports) is properly
    # guarded. Harmless in a Windows-only project; fatal anywhere else.
    (DAO_GFX_SYSTEM, "BUG-016: guard the Windows-only include",
     '#include "xenia/base/platform_win.h"',
     "#if XE_PLATFORM_WIN32\n"
     '#include "xenia/base/platform_win.h"\n'
     "#endif"),

    # BUG-017. Xenia's Android window file was not updated when OnActualSizeUpdate
    # gained a cause_action parameter, so it passes three arguments to a four-argument
    # function. Every other platform passes kManual. Nobody builds the Android path.
    (ANDROID_WINDOW, "BUG-017: pass the resize cause OnActualSizeUpdate now requires",
     "  OnActualSizeUpdate(physical_width, physical_height, destruction_receiver);",
     "  OnActualSizeUpdate(physical_width, physical_height,\n"
     "                     WindowResizeAction::kManual, destruction_receiver);"),

    # BUG-018. Xenia's POSIX threading uses glibc robust mutexes. Android's bionic
    # implements none of that API - no PTHREAD_MUTEX_ROBUST, no
    # pthread_mutexattr_setrobust, no pthread_mutex_consistent. Without robustness a
    # lock can never report EOWNERDEAD, so the recovery paths are unreachable rather
    # than merely unsupported; these inert definitions keep them compiling.
    (THREADING_POSIX, "BUG-018: bionic has no robust-mutex API",
     "#include <unistd.h>",
     "#include <unistd.h>\n"
     "\n"
     "// DAO: see BUG-018. bionic implements no robust-mutex API, and without\n"
     "// robustness pthread_mutex_lock cannot return EOWNERDEAD, so the recovery\n"
     "// branches guarded by it below are dead code rather than broken code.\n"
     "#if !defined(PTHREAD_MUTEX_ROBUST)\n"
     "#define PTHREAD_MUTEX_ROBUST 0\n"
     "static inline int pthread_mutexattr_setrobust(pthread_mutexattr_t*, int) {\n"
     "  return 0;\n"
     "}\n"
     "static inline int pthread_mutex_consistent(pthread_mutex_t*) { return 0; }\n"
     "#endif"),

    # BUG-016 (second site). DAORecomp raises the frame limiter's priority with raw
    # Win32, unguarded. Guarding rather than switching to xe::threading's portable
    # set_priority is deliberate: this is a bare std::thread, so
    # Thread::GetCurrentThread() returns null for it and the portable call would
    # silently do nothing - including on Windows, where the current code works.
    (DAO_GFX_SYSTEM, "BUG-016: guard the Win32 thread-priority call",
     "            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);",
     "#if XE_PLATFORM_WIN32\n"
     "            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);\n"
     "#else\n"
     "            // TODO(dao): raise this thread's priority on Android. Needs\n"
     "            // setpriority() or Android's own thread API - xe::threading's\n"
     "            // portable setter needs a Thread object, and this is a bare\n"
     "            // std::thread.\n"
     "#endif"),

    # BUG-019. Xenia's Android thread naming helper is called from a const method but
    # is not const itself. The mutex it takes is already declared mutable, which says
    # the author intended exactly this and missed the buffer beside it - the Android
    # path is never compiled, so it was never caught.
    (THREADING_POSIX, "BUG-019: Android name helper must be const",
     "  void SetAndroidPreApi26Name(const std::string_view name) {",
     "  void SetAndroidPreApi26Name(const std::string_view name) const {"),

    (THREADING_POSIX, "BUG-019: and its buffer mutable, like the mutex beside it",
     "  char android_pre_api_26_name_[16];",
     "  mutable char android_pre_api_26_name_[16];"),

    # BUG-020. filesystem_android.cc adds Android-specific helpers *and* redefines
    # SetAttributes as `return false`, colliding with the working stat/chmod
    # implementation in filesystem_posix.cc. Both files are needed on Android - the
    # POSIX one provides to_path, GetExecutablePath, Seek and the rest - so the
    # duplicate makes the target unlinkable. Android supports chmod on regular files,
    # so the POSIX implementation is the correct one to keep; if the stub won instead,
    # setting a file read-only would silently do nothing.
    (ANDROID_FILESYSTEM, "BUG-020: drop the stub that collides with the POSIX one",
     "bool SetAttributes(const std::filesystem::path& path, uint64_t attributes) {\n"
     "  return false;\n"
     "}",
     "// DAO: see BUG-020. Upstream defined a `return false` SetAttributes here, which\n"
     "// collides with filesystem_posix.cc's working stat/chmod implementation. Both\n"
     "// files are required on Android, so the duplicate prevented linking at all.\n"
     "// The POSIX implementation is used."),

    # BUG-003. reinterpret_cast may convert pointer<->integer but not between two
    # different integer types. glibc's pthread_t is unsigned long, which *is*
    # uintptr_t on LP64, so the cast is to the same type and compiles. Android's
    # bionic defines pthread_t as signed long, making it a different type and the
    # cast ill-formed. static_cast is correct here: the value is only compared for
    # equality to detect a re-entrant owner.
    (DAO_MUTEX, "BUG-003: static_cast the thread id, bionic's pthread_t is signed",
     "  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pthread_self()));",
     "  return static_cast<uint64_t>(pthread_self());"),

    # DAORecomp's own resolve shader uses HLSL's saturate() directly. Every other
    # Xenia shader uses saturate_xe, the xesl abstraction that becomes saturate() in
    # HLSL and clamp(x, 0, 1) in GLSL. The raw form compiles only in their
    # Direct3D-only build; GLSL has no saturate.
    (DAO_RESOLVE, "use the portable saturate_xe so the shader compiles as GLSL",
     "        saturate(texel_fetch_2d_xe(xe_resolve_source,",
     "        saturate_xe(texel_fetch_2d_xe(xe_resolve_source,"),

    (VCP_H, "drop the kernel include",
     '#include "xenia/kernel/kernel_state.h"\n', ""),

    (VCP_H, "constructor takes no KernelState",
     "  VulkanCommandProcessor(VulkanGraphicsSystem* graphics_system,\n"
     "                         kernel::KernelState* kernel_state);",
     "  // DAO: no kernel. The parameter is kept so the shape matches upstream's\n"
     "  // CreateCommandProcessor call sites; it is unused.\n"
     "  VulkanCommandProcessor(VulkanGraphicsSystem* graphics_system,\n"
     "                         void* unused = nullptr);"),

    # Exactly the substitution DAORecomp made in d3d12_command_processor.cc: the
    # emulator headers out, dao_host.h in. dao_frame_trace.h is needed too because
    # DAORecomp's pm4_command_processor_implement.h calls into dao_trace; despite
    # living under gpu/d3d12/ its contents are backend-agnostic (it includes only
    # base/logging, register_file, registers, texture_info and xenos).
    (VCP_C, "replace the emulator includes with DAORecomp's host interface",
     '#include "xenia/apu/audio_system.h"\n',
     '#include "xenia/dao_host.h"\n'
     '#include "xenia/gpu/d3d12/dao_frame_trace.h"\n'),

    (VCP_C, "drop the kernel include",
     '#include "xenia/kernel/kernel_state.h"\n', ""),

    (VCP_C, "drop the emulator include",
     '#include "xenia/emulator.h"\n', ""),

    (VCP_C, "drop the user module include",
     '#include "xenia/kernel/user_module.h"\n', ""),

    (VCP_C, "constructor no longer forwards a KernelState to the base",
     "VulkanCommandProcessor::VulkanCommandProcessor(\n"
     "    VulkanGraphicsSystem* graphics_system, kernel::KernelState* kernel_state)\n"
     "    : CommandProcessor(graphics_system, kernel_state),",
     "VulkanCommandProcessor::VulkanCommandProcessor(\n"
     "    VulkanGraphicsSystem* graphics_system, void* unused)\n"
     "    : CommandProcessor(graphics_system),"),

    (VCP_C, "window title no longer names the audio system",
     "  auto* audio_system = kernel_state_->emulator()->audio_system();\n"
     "  if (audio_system) {\n"
     "    title << \" - \" << audio_system->name();\n"
     "  }\n",
     "  // DAO: upstream appended the audio system's name here. There is no\n"
     "  // emulator object to ask, and the title is cosmetic.\n"),

    (VGS_H, "Setup takes Memory, matching DAORecomp's GraphicsSystem",
     "  X_STATUS Setup(cpu::Processor* processor, kernel::KernelState* kernel_state,\n"
     "                 ui::WindowedAppContext* app_context,\n"
     "                 bool with_presentation) override;",
     "  X_STATUS Setup(Memory* memory, ui::WindowedAppContext* app_context,\n"
     "                 bool with_presentation) override;"),

    (VGS_C, "Setup implementation matches the new signature",
     "X_STATUS VulkanGraphicsSystem::Setup(cpu::Processor* processor,\n"
     "                                     kernel::KernelState* kernel_state,\n"
     "                                     ui::WindowedAppContext* app_context,\n"
     "                                     bool with_presentation) {\n"
     "  provider_ = xe::ui::vulkan::VulkanProvider::Create(true, with_presentation);\n"
     "  return GraphicsSystem::Setup(processor, kernel_state, app_context,\n"
     "                               with_presentation);\n"
     "}",
     "X_STATUS VulkanGraphicsSystem::Setup(Memory* memory,\n"
     "                                     ui::WindowedAppContext* app_context,\n"
     "                                     bool with_presentation) {\n"
     "  provider_ = xe::ui::vulkan::VulkanProvider::Create(true, with_presentation);\n"
     "  return GraphicsSystem::Setup(memory, app_context, with_presentation);\n"
     "}"),

    (VGS_C, "command processor is constructed without a KernelState",
     "  return std::make_unique<VulkanCommandProcessor>(this, kernel_state_);",
     "  return std::make_unique<VulkanCommandProcessor>(this);"),

    # BUG-059 (session 35). Without a provider - no Vulkan instance, as when a GPU debug
    # layer is named that the loader cannot find (ErrorLayerNotPresent) - Setup went on
    # to create the command processor, whose constructor reads through the null
    # provider: a SIGSEGV at the boot. Upstream's emulator checks the provider before it
    # picks this backend; this app sets the graphics system up directly.
    (VGS_C, "no provider: the setup fails rather than going on",
     "  provider_ = xe::ui::vulkan::VulkanProvider::Create(true, with_presentation);\n",
     "  provider_ = xe::ui::vulkan::VulkanProvider::Create(true, with_presentation);\n"
     "  if (!provider_) {\n"
     "    return X_STATUS_UNSUCCESSFUL;\n"
     "  }\n"),

    # Phase 4.2. Xenia's instance requests VK_KHR_surface and the platform surface
    # extension, and nothing else presentation-related. Without
    # VK_EXT_swapchain_colorspace, vkGetPhysicalDeviceSurfaceFormatsKHR is required to
    # report only VK_COLOR_SPACE_SRGB_NONLINEAR_KHR - so the Thor's Display-P3 panel
    # came back looking like an sRGB one. That is the spec working as intended, not a
    # driver limitation: the Phase 0 probe recorded VK_EXT_swapchain_colorspace as
    # available (device/vulkan-capabilities.txt).
    #
    # Decision D8 defaults to wide-gamut Display-P3 SDR, so this is needed to reach
    # the default, not an optional extra. Enabling it only makes more colour spaces
    # *enumerable*; nothing changes until a swapchain asks for one.
    (VKI_H, "declare VK_EXT_swapchain_colorspace",
     "    bool ext_EXT_debug_utils = false;                          // #129",
     "    // DAO: without this, surface format queries may only report\n"
     "    // VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, which hides the Display-P3 and HDR10\n"
     "    // colour spaces this panel supports. Needed for decision D8.\n"
     "    bool ext_EXT_swapchain_colorspace = false;                 // #105\n"
     "    bool ext_EXT_debug_utils = false;                          // #129"),

    # Phase 4.2a. window.cc includes imgui.h and imgui_drawer.h and then never names
    # ImGui, ImDraw, ImVec or ImFont anywhere in its 758 lines - the includes are dead.
    # They still broke the build, because third_party/imgui is an unfetched submodule
    # in this checkout, so the include simply does not resolve.
    #
    # That mattered more than it looks. Xenia's Presenter cannot connect to a surface
    # without a Window (SetWindowSurfaceFromUIThread asserts a surface cannot exist
    # without one, and calls window_->RequestPaint()), so "window.cc needs ImGui" read
    # as "presenting at all needs ImGui", which the plan had placed in Phase 10. It
    # does not: it needs two dead includes removed.
    (WINDOW_C, "drop window.cc's unused ImGui includes",
     '#include "third_party/imgui/imgui.h"\n'
     '#include "xenia/base/assert.h"\n'
     '#include "xenia/base/clock.h"\n'
     '#include "xenia/base/logging.h"\n'
     '#include "xenia/ui/imgui_drawer.h"\n'
     '#include "xenia/ui/presenter.h"',
     "// DAO: imgui.h and imgui_drawer.h removed - window.cc includes both and uses\n"
     "// neither, and third_party/imgui is not fetched in this checkout. Restore them\n"
     "// if upstream ever makes the window layer genuinely depend on ImGui.\n"
     '#include "xenia/base/assert.h"\n'
     '#include "xenia/base/clock.h"\n'
     '#include "xenia/base/logging.h"\n'
     '#include "xenia/ui/presenter.h"'),

    # BUG-112: every wait of the host for the GPU goes through the completion timeline;
    # each is counted and timed for the GPU status line (warden_gpu.cc) - how long the
    # command processor stands still for the GPU each tick, which a driver's fence
    # latency or a slow query decides.
    (GCT_H, "the waits for the GPU timed",
     "    if (UpdateAndGetCompletedSubmission() < awaited_submission) {\n"
     "      AwaitSubmissionImpl(awaited_submission);\n"
     "    }\n",
     "    if (UpdateAndGetCompletedSubmission() < awaited_submission) {\n"
     "      // DAO/Warden (BUG-112): the wait timed for the GPU status line.\n"
     "      const auto wait_began = std::chrono::steady_clock::now();\n"
     "      AwaitSubmissionImpl(awaited_submission);\n"
     "      warden_gpu_wait_note(std::chrono::duration<double, std::milli>(\n"
     "                               std::chrono::steady_clock::now() - wait_began)\n"
     "                               .count());\n"
     "    }\n"),
    (GCT_H, "the wait's clock",
     "#include <algorithm>\n",
     "#include <algorithm>\n"
     "#include <chrono>  // DAO/Warden (BUG-112)\n"),
    (GCT_H, "the wait note's declaration",
     "namespace xe {\n"
     "namespace ui {\n",
     "// DAO/Warden (BUG-112): app/src/main/cpp/warden_gpu.cc.\n"
     "extern \"C\" void warden_gpu_wait_note(double milliseconds);\n"
     "\n"
     "namespace xe {\n"
     "namespace ui {\n"),
    # BUG-112: the pipeline barriers and render passes executed, counted for the GPU
    # status line - what a driver that drains the GPU at each barrier (Mesa's Turnip) pays per
    # frame, which Xenia's EDRAM emulation makes many of.
    (DCB_C, "the barriers and passes counted",
     "      case Command::kVkPipelineBarrier: {\n"
     "        auto& args = *reinterpret_cast<const ArgsVkPipelineBarrier*>(stream);\n",
     "      case Command::kVkPipelineBarrier: {\n"
     "        auto& args = *reinterpret_cast<const ArgsVkPipelineBarrier*>(stream);\n"
     "        warden_gpu_barrier_note(args.image_memory_barrier_count,\n"
     "                                args.buffer_memory_barrier_count);  // DAO/Warden (BUG-112)\n"),
    (DCB_C, "the passes counted",
     "      case Command::kVkBeginRenderPass: {\n",
     "      case Command::kVkBeginRenderPass: {\n"
     "        warden_gpu_pass_note();  // DAO/Warden (BUG-112)\n"),
    (DCB_C, "the counts' declarations",
     "namespace xe {\n"
     "namespace gpu {\n"
     "namespace vulkan {\n",
     "// DAO/Warden (BUG-112): app/src/main/cpp/warden_gpu.cc.\n"
     "extern \"C\" void warden_gpu_barrier_note(uint32_t image_barriers, uint32_t buffer_barriers);\n"
     "extern \"C\" void warden_gpu_pass_note();\n"
     "\n"
     "namespace xe {\n"
     "namespace gpu {\n"
     "namespace vulkan {\n"),
    # BUG-112: every vkQueueSubmit and vkQueuePresentKHR timed on the host (the present
    # inside 4.7's edit of its lines) - a driver that blocks in them would show as the
    # GPU idle and the command processor idle at once, which T30 does.
    (VKD_H, "the submits timed",
     "    const VkResult submit_result =\n"
     "        functions().vkQueueSubmit(queue, submit_count, submits, fence);\n",
     "    // DAO/Warden (BUG-112): the call's time, for the GPU status line.\n"
     "    const auto submit_began = std::chrono::steady_clock::now();\n"
     "    const VkResult submit_result =\n"
     "        functions().vkQueueSubmit(queue, submit_count, submits, fence);\n"
     "    warden_gpu_submit_note(std::chrono::duration<double, std::milli>(\n"
     "                               std::chrono::steady_clock::now() - submit_began)\n"
     "                               .count());\n"),
    (VKD_H, "the submit note's declaration and clock",
     "#include <atomic>\n"
     "#include <memory>\n",
     "#include <atomic>\n"
     "#include <chrono>  // DAO/Warden (BUG-112)\n"
     "#include <memory>\n"
     "\n"
     "// DAO/Warden (BUG-112): app/src/main/cpp/warden_gpu.cc.\n"
     "extern \"C\" void warden_gpu_submit_note(double milliseconds);\n"),
    (VKP_C, "the present note's declaration",
     "namespace xe {\n"
     "namespace ui {\n"
     "namespace vulkan {\n",
     "// DAO/Warden (BUG-112): app/src/main/cpp/warden_gpu.cc.\n"
     "extern \"C\" void warden_gpu_present_note(double milliseconds);\n"
     "\n"
     "namespace xe {\n"
     "namespace ui {\n"
     "namespace vulkan {\n"),

    (VKI_C, "request VK_EXT_swapchain_colorspace",
     '  if (with_surface) {\n'
     '    // #1.\n'
     '    requested_extensions.emplace("VK_KHR_surface",\n'
     '                                 &vulkan_instance->extensions_.ext_KHR_surface);',
     '  if (with_surface) {\n'
     '    // #1.\n'
     '    requested_extensions.emplace("VK_KHR_surface",\n'
     '                                 &vulkan_instance->extensions_.ext_KHR_surface);\n'
     '    // #105. DAO: makes the non-sRGB colour spaces enumerable - see D8. Depends\n'
     '    // on VK_KHR_surface, hence its position inside this block.\n'
     '    requested_extensions.emplace(\n'
     '        "VK_EXT_swapchain_colorspace",\n'
     '        &vulkan_instance->extensions_.ext_EXT_swapchain_colorspace);'),
    (VDF_INC, "3.8: vkCmdWriteTimestamp for GPU pass timing",
     "XE_UI_VULKAN_FUNCTION(vkCmdResetQueryPool)\n",
     "XE_UI_VULKAN_FUNCTION(vkCmdResetQueryPool)\n"
     "XE_UI_VULKAN_FUNCTION(vkCmdWriteTimestamp)  // DAO/Warden: GPU pass timing (3.8)\n"),
    (VDF_INC, "3.8: vkGetQueryPoolResults for GPU pass timing",
     "XE_UI_VULKAN_FUNCTION(vkDestroyQueryPool)\n",
     "XE_UI_VULKAN_FUNCTION(vkDestroyQueryPool)\n"
     "XE_UI_VULKAN_FUNCTION(vkGetQueryPoolResults)  // DAO/Warden: GPU pass timing (3.8)\n"),
    (DCB_H, "3.8: the deferred timestamp command",
     "    kVkResetQueryPool,\n",
     "    kVkResetQueryPool,\n"
     "    kVkWriteTimestamp,  // DAO/Warden: GPU pass timing (3.8)\n"),
    (DCB_H, "3.8: CmdVkWriteTimestamp",
     "  void CmdClearAttachmentsEmplace(uint32_t attachment_count,\n",
     "  // DAO/Warden: GPU pass timing (3.8).\n"
     "  void CmdVkWriteTimestamp(VkPipelineStageFlagBits pipeline_stage,\n"
     "                           VkQueryPool query_pool, uint32_t query) {\n"
     "    auto& args = *reinterpret_cast<ArgsVkWriteTimestamp*>(\n"
     "        WriteCommand(Command::kVkWriteTimestamp, sizeof(ArgsVkWriteTimestamp)));\n"
     "    args.pipeline_stage = pipeline_stage;\n"
     "    args.query_pool = query_pool;\n"
     "    args.query = query;\n"
     "  }\n"
     "\n"
     "  void CmdClearAttachmentsEmplace(uint32_t attachment_count,\n"),
    (DCB_H, "3.8: the timestamp command's arguments",
     "  struct ArgsVkResetQueryPool {\n"
     "    VkQueryPool query_pool;\n"
     "    uint32_t first_query;\n"
     "    uint32_t query_count;\n"
     "  };\n",
     "  struct ArgsVkResetQueryPool {\n"
     "    VkQueryPool query_pool;\n"
     "    uint32_t first_query;\n"
     "    uint32_t query_count;\n"
     "  };\n"
     "\n"
     "  struct ArgsVkWriteTimestamp {  // DAO/Warden: GPU pass timing (3.8)\n"
     "    VkPipelineStageFlagBits pipeline_stage;\n"
     "    VkQueryPool query_pool;\n"
     "    uint32_t query;\n"
     "  };\n"),
    (DCB_C, "3.8: executing the timestamp command",
     "      case Command::kVkBeginConditionalRenderingEXT: {\n",
     "      case Command::kVkWriteTimestamp: {  // DAO/Warden: GPU pass timing (3.8)\n"
     "        auto& args = *reinterpret_cast<const ArgsVkWriteTimestamp*>(stream);\n"
     "        dfn.vkCmdWriteTimestamp(command_buffer, args.pipeline_stage,\n"
     "                                args.query_pool, args.query);\n"
     "      } break;\n"
     "      case Command::kVkBeginConditionalRenderingEXT: {\n"),
    (VCP_C, "3.8: in the traced frame, a timestamp before each render pass",
     "  // The sample locations for automatic depth layout transitions and for the\n"
     "  // subpass rasterization state when variableSampleLocations is false.\n"
     "  deferred_command_buffer_.CmdVkBeginRenderPass(\n",
     "  // DAO/Warden (3.8): in the traced frame, a timestamp before each render pass,\n"
     "  // the pool created and its period read on first use.\n"
     "  if (dao_trace::Active() && dao_trace::pass_count < 1023) {\n"
     "    const ui::vulkan::VulkanDevice* timing_device = GetVulkanDevice();\n"
     "    if (!dao_trace::timestamp_pool) {\n"
     "      VkQueryPoolCreateInfo pool_info = {};\n"
     "      pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;\n"
     "      pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;\n"
     "      pool_info.queryCount = 2048;\n"
     "      VkQueryPool pool = VK_NULL_HANDLE;\n"
     "      if (timing_device->functions().vkCreateQueryPool(\n"
     "              timing_device->device(), &pool_info, nullptr, &pool) == VK_SUCCESS) {\n"
     "        dao_trace::timestamp_pool = reinterpret_cast<uint64_t>(pool);\n"
     "      }\n"
     "      VkPhysicalDeviceProperties physical_properties;\n"
     "      timing_device->vulkan_instance()->functions().vkGetPhysicalDeviceProperties(\n"
     "          timing_device->physical_device(), &physical_properties);\n"
     "      dao_trace::timestamp_period_ns = physical_properties.limits.timestampPeriod;\n"
     "    }\n"
     "    if (dao_trace::timestamp_pool) {\n"
     "      const VkQueryPool pool = reinterpret_cast<VkQueryPool>(dao_trace::timestamp_pool);\n"
     "      if (dao_trace::pass_count == 0) {\n"
     "        deferred_command_buffer_.CmdVkResetQueryPool(pool, 0, 2048);\n"
     "      }\n"
     "      dao_trace::PassTiming& timing = dao_trace::pass_timings[dao_trace::pass_count];\n"
     "      timing.width = framebuffer->host_extent.width;\n"
     "      timing.height = framebuffer->host_extent.height;\n"
     "      timing.draws = 0;\n"
     "      timing.first_draw = dao_trace::draws_in_frame;\n"
     "      timing.first_vs = 0;\n"
     "      timing.first_ps = 0;\n"
     "      deferred_command_buffer_.CmdVkWriteTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,\n"
     "                                                   pool, dao_trace::pass_count * 2);\n"
     "      ++dao_trace::pass_count;\n"
     "      dao_trace::timing_pending = true;\n"
     "    }\n"
     "  }\n"
     "  // The sample locations for automatic depth layout transitions and for the\n"
     "  // subpass rasterization state when variableSampleLocations is false.\n"
     "  deferred_command_buffer_.CmdVkBeginRenderPass(\n"),
    (VCP_C, "3.8: in the traced frame, a timestamp after each render pass",
     "  deferred_command_buffer_.CmdVkEndRenderPass();\n"
     "  current_render_pass_ = VK_NULL_HANDLE;\n"
     "  current_framebuffer_ = nullptr;\n",
     "  deferred_command_buffer_.CmdVkEndRenderPass();\n"
     "  current_render_pass_ = VK_NULL_HANDLE;\n"
     "  current_framebuffer_ = nullptr;\n"
     "  // DAO/Warden (3.8): the pass's closing timestamp, while the frame is traced.\n"
     "  if (dao_trace::Active() && dao_trace::timestamp_pool && dao_trace::pass_count) {\n"
     "    deferred_command_buffer_.CmdVkWriteTimestamp(\n"
     "        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,\n"
     "        reinterpret_cast<VkQueryPool>(dao_trace::timestamp_pool),\n"
     "        dao_trace::pass_count * 2 - 1);\n"
     "    dao_trace::timing_submission = GetCurrentSubmission();\n"
     "  }\n"),
    (VCP_C, "3.8: DAO_HACK_SPLIT_PASS ends a traced render pass every n draws",
     "  // After all commands that may dispatch, copy or insert barriers, submit the\n"
     "  // barriers (may end the render pass), and (re)enter the render pass before\n"
     "  // drawing.\n",
     "  // DAO/Warden (3.8): DAO_HACK_SPLIT_PASS=<n>[:<first>[:<last>]] - in the traced frame,\n"
     "  // a render pass is ended once it holds n draws (only for the draws first..last,\n"
     "  // counted from 0, when given), so each piece of it gets its own timestamps.\n"
     "  {\n"
     "    static const char* split_text = std::getenv(\"DAO_HACK_SPLIT_PASS\");\n"
     "    static uint32_t split = 0, split_first = 0, split_last = UINT32_MAX;\n"
     "    static bool split_parsed = false;\n"
     "    if (!split_parsed) {\n"
     "      split_parsed = true;\n"
     "      if (split_text) {\n"
     "        char* end = nullptr;\n"
     "        split = uint32_t(std::strtoul(split_text, &end, 10));\n"
     "        if (end && *end == ':') {\n"
     "          split_first = uint32_t(std::strtoul(end + 1, &end, 10));\n"
     "          if (end && *end == ':') {\n"
     "            split_last = uint32_t(std::strtoul(end + 1, &end, 10));\n"
     "          }\n"
     "        }\n"
     "      }\n"
     "    }\n"
     "    const uint32_t draw_index = dao_trace::draws_in_frame;\n"
     "    const bool in_range = draw_index >= split_first && draw_index <= split_last;\n"
     "    const bool range_end = split_last != UINT32_MAX && draw_index == split_last + 1;\n"
     "    if (split && dao_trace::Active() && dao_trace::pass_count &&\n"
     "        current_render_pass_ != VK_NULL_HANDLE &&\n"
     "        ((in_range && (dao_trace::pass_timings[dao_trace::pass_count - 1].draws >= split ||\n"
     "                       (split_first && draw_index == split_first))) ||\n"
     "         range_end)) {\n"
     "      EndRenderPass();\n"
     "    }\n"
     "  }\n"
     "  // After all commands that may dispatch, copy or insert barriers, submit the\n"
     "  // barriers (may end the render pass), and (re)enter the render pass before\n"
     "  // drawing.\n"),
    (VCP_C, "3.8: each traced draw counts toward the render pass it is drawn in",
     "  SubmitBarriersAndEnterRenderTargetCacheRenderPass(\n"
     "      render_target_cache_->last_update_render_pass(),\n"
     "      render_target_cache_->last_update_framebuffer());\n",
     "  SubmitBarriersAndEnterRenderTargetCacheRenderPass(\n"
     "      render_target_cache_->last_update_render_pass(),\n"
     "      render_target_cache_->last_update_framebuffer());\n"
     "  // DAO/Warden (3.8): the draw counts toward the pass it is drawn in.\n"
     "  if (dao_trace::Active()) {\n"
     "    ++dao_trace::draws_in_frame;\n"
     "    if (dao_trace::pass_count) {\n"
     "      dao_trace::PassTiming& pass_timing =\n"
     "          dao_trace::pass_timings[dao_trace::pass_count - 1];\n"
     "      if (!pass_timing.draws++) {\n"
     "        pass_timing.first_vs = vertex_shader->ucode_data_hash();\n"
     "        pass_timing.first_ps = pixel_shader ? pixel_shader->ucode_data_hash() : 0;\n"
     "      }\n"
     "    }\n"
     "  }\n"),
    (SST_FETCH, "3.8 experiment: <cstdlib> for DAO_HACK_IMPLICIT_LOD",
     "#include <cmath>\n",
     "#include <cmath>\n"
     "#include <cstdlib>  // DAO/Warden: std::getenv, for DAO_HACK_IMPLICIT_LOD (3.8)\n"
     "// DAO/Warden experiment (3.8): DAO_HACK_SIMPLE_FETCH - every texture fetch as a plain\n"
     "// unsigned sample: the per-component sign is constant (no signed sample, no sign,\n"
     "// bias or gamma switch), 2D fetches use implicit LOD plus bias, and coordinates are\n"
     "// not snapped to texel centers - to measure what the fetch emulation costs per pixel.\n"
     "static inline bool DaoSimpleFetch() {\n"
     "  static const bool simple = std::getenv(\"DAO_HACK_SIMPLE_FETCH\") != nullptr;\n"
     "  return simple;\n"
     "}\n"),
    (SST_FETCH, "3.8 experiment: DAO_HACK_IMPLICIT_LOD samples 2D textures with implicit LOD plus bias",
     "        bool use_lod_bias = use_computed_lod &&\n"
     "                            !instr.attributes.use_register_gradients &&\n"
     "                            instr.dimension == xenos::FetchOpDimension::kCube;\n",
     "        // DAO/Warden experiment (3.8): DAO_HACK_IMPLICIT_LOD - 2D textures\n"
     "        // too (not 3D or stacked: their path reads the gradients regardless), since\n"
     "        // explicit-gradient sampling is the slow kind on mobile GPUs.\n"
     "        static const bool dao_implicit_lod = std::getenv(\"DAO_HACK_IMPLICIT_LOD\") != nullptr;\n"
     "        bool use_lod_bias = use_computed_lod &&\n"
     "                            !instr.attributes.use_register_gradients &&\n"
     "                            (instr.dimension == xenos::FetchOpDimension::kCube ||\n"
     "                             ((dao_implicit_lod || DaoSimpleFetch() ||\n"
     "                               (is_pixel_shader()\n"
     "                                    ? GetSpirvShaderModification().pixel.textures_isotropic_lod\n"
     "                                    : GetSpirvShaderModification().vertex.textures_isotropic_lod)) &&\n"
     "                              instr.dimension == xenos::FetchOpDimension::k2D));\n"),
    (SST_FETCH, "3.8 experiment: DAO_HACK_SIMPLE_FETCH snaps no coordinates",
     "    bool point_snap = instr.CanSnapToTexelCenter(use_computed_lod);\n",
     "    bool point_snap =\n"
     "        !DaoSimpleFetch() && !std::getenv(\"DAO_HACK_NO_SNAP\") &&  // DAO/Warden (3.8)\n"
     "        instr.CanSnapToTexelCenter(use_computed_lod);\n"),
    (SST_FETCH, "3.8 experiment: DAO_HACK_SIMPLE_FETCH takes every component as unsigned",
     "            spv::Id result_component_sign = builder_->createTriOp(\n"
     "                spv::OpBitFieldUExtract, type_uint_, swizzled_signs_word,\n"
     "                builder_->makeUintConstant(swizzled_signs_word_offset +\n"
     "                                           2 * result_component_index),\n"
     "                const_uint_2);\n",
     "            // DAO/Warden (3.8): unsigned (0) when the modification says every\n"
     "            // texture is (textures_unsigned), or under DAO_HACK_SIMPLE_FETCH.\n"
     "            spv::Id result_component_sign =\n"
     "                (DaoSimpleFetch() ||\n"
     "                 (is_pixel_shader() ? GetSpirvShaderModification().pixel.textures_unsigned\n"
     "                                    : GetSpirvShaderModification().vertex.textures_unsigned))\n"
     "                    ? builder_->makeUintConstant(0)\n"
     "                    : builder_->createTriOp(\n"
     "                          spv::OpBitFieldUExtract, type_uint_, swizzled_signs_word,\n"
     "                          builder_->makeUintConstant(swizzled_signs_word_offset +\n"
     "                                                     2 * result_component_index),\n"
     "                          const_uint_2);\n"),
    (SST_H, "3.8: the shader modification version, for the texture bits",
     "    static constexpr uint32_t kVersion = 19;\n",
     "    static constexpr uint32_t kVersion = 21;  // DAO/Warden: + the texture bits (3.8),"
     " the vertex fetch bits (10.9)\n"),
    (SST_H, "3.8: the vertex shader's texture modification bits",
     "      xenos::TessellationMode tessellation_mode : 2;\n"
     "    } vertex;\n",
     "      xenos::TessellationMode tessellation_mode : 2;\n"
     "      // DAO/Warden (3.8): from the fetch constants of the textures the shader\n"
     "      // fetches. textures_unsigned: every sign field is unsigned, so fetches are\n"
     "      // emitted without the per-component sign handling (its switches and signed\n"
     "      // samples cost the Adreno a third of the main pass even when unused).\n"
     "      // textures_isotropic_lod: equal per-axis gradient exponent adjustments, so\n"
     "      // 2D fetches with a computed LOD take an implicit LOD plus bias, which is then\n"
     "      // exact (explicit-gradient sampling is the slow kind on mobile GPUs).\n"
     "      uint32_t textures_unsigned : 1;\n"
     "      uint32_t textures_isotropic_lod : 1;\n"
     "      // textures_integer_scale_round_only: no texture has integer-scale lane bits\n"
     "      // (0:23) - the post-fetch conversion is only the 16-bit rounding of normalized\n"
     "      // results, emitted without its branches.\n"
     "      uint32_t textures_integer_scale_round_only : 1;\n"
     "    } vertex;\n"),
    (SST_H, "3.8: the pixel shader's texture modification bits",
     "      uint32_t zpd_total : 1;\n"
     "    } pixel;\n",
     "      uint32_t zpd_total : 1;\n"
     "      // DAO/Warden (3.8): from the fetch constants of the textures the shader\n"
     "      // fetches. textures_unsigned: every sign field is unsigned, so fetches are\n"
     "      // emitted without the per-component sign handling (its switches and signed\n"
     "      // samples cost the Adreno a third of the main pass even when unused).\n"
     "      // textures_isotropic_lod: equal per-axis gradient exponent adjustments, so\n"
     "      // 2D fetches with a computed LOD take an implicit LOD plus bias, which is then\n"
     "      // exact (explicit-gradient sampling is the slow kind on mobile GPUs).\n"
     "      uint32_t textures_unsigned : 1;\n"
     "      uint32_t textures_isotropic_lod : 1;\n"
     "      // textures_integer_scale_round_only: no texture has integer-scale lane bits\n"
     "      // (0:23) - the post-fetch conversion is only the 16-bit rounding of normalized\n"
     "      // results, emitted without its branches.\n"
     "      uint32_t textures_integer_scale_round_only : 1;\n"
     "    } pixel;\n"),
    (VPC_C, "3.8: the texture modification bits from the fetch constants",
     "SpirvShaderTranslator::Modification\n"
     "VulkanPipelineCache::GetCurrentVertexShaderModification(\n",
     "// DAO/Warden (3.8): the shader modification texture bits, from the fetch constants\n"
     "// of the textures the shader fetches - sign fields in dword 0 bits 2:9, gradient\n"
     "// exponent adjustments in dword 4 bits 22:26 (h) and 27:31 (v).\n"
     "static void DaoTextureModificationBits(const Shader& shader, const RegisterFile& regs,\n"
     "                                       uint32_t& textures_unsigned,\n"
     "                                       uint32_t& textures_isotropic_lod,\n"
     "                                       uint32_t& textures_integer_scale_round_only) {\n"
     "  textures_unsigned = 1;\n"
     "  textures_isotropic_lod = 1;\n"
     "  textures_integer_scale_round_only = 1;\n"
     "  for (const Shader::TextureBinding& binding : shader.texture_bindings()) {\n"
     "    const uint32_t base =\n"
     "        XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + binding.fetch_constant * 6;\n"
     "    if ((regs.values[base] >> 2) & 0xFF) {\n"
     "      textures_unsigned = 0;\n"
     "    }\n"
     "    const uint32_t dword_4 = regs.values[base + 4];\n"
     "    if (((dword_4 >> 22) & 0x1F) != ((dword_4 >> 27) & 0x1F)) {\n"
     "      textures_isotropic_lod = 0;\n"
     "    }\n"
     "    const xenos::xe_gpu_texture_fetch_t fetch = regs.GetTextureFetch(binding.fetch_constant);\n"
     "    if (TextureCache::DaoIntegerScaleBits(fetch, texture_util::SwizzleSigns(fetch)) &\n"
     "        ((UINT32_C(1) << 24) - 1)) {\n"
     "      textures_integer_scale_round_only = 0;\n"
     "    }\n"
     "  }\n"
     "}\n"
     "\n"
     "SpirvShaderTranslator::Modification\n"
     "VulkanPipelineCache::GetCurrentVertexShaderModification(\n"),
    (VPC_C, "3.8: the vertex shader's texture modification bits",
     "  return modification;\n"
     "}\n"
     "\n"
     "SpirvShaderTranslator::Modification\n"
     "VulkanPipelineCache::GetCurrentPixelShaderModification(\n",
     "  // DAO/Warden (3.8): the texture bits.\n"
     "  {\n"
     "    uint32_t textures_unsigned, textures_isotropic_lod, textures_integer_scale_round_only;\n"
     "    DaoTextureModificationBits(shader, regs, textures_unsigned, textures_isotropic_lod,\n"
     "                               textures_integer_scale_round_only);\n"
     "    modification.vertex.textures_unsigned = textures_unsigned;\n"
     "    modification.vertex.textures_isotropic_lod = textures_isotropic_lod;\n"
     "    modification.vertex.textures_integer_scale_round_only =\n"
     "        textures_integer_scale_round_only;\n"
     "  }\n"
     "\n"
     "  return modification;\n"
     "}\n"
     "\n"
     "SpirvShaderTranslator::Modification\n"
     "VulkanPipelineCache::GetCurrentPixelShaderModification(\n"),
    (VPC_C, "3.8: the pixel shader's texture modification bits",
     "        modification.pixel.rt0_blend_a_factor_for_premult =\n"
     "            xenos::BlendFactor::kSrcAlpha;\n"
     "      }\n"
     "    }\n"
     "  }\n"
     "\n"
     "  return modification;\n"
     "}\n",
     "        modification.pixel.rt0_blend_a_factor_for_premult =\n"
     "            xenos::BlendFactor::kSrcAlpha;\n"
     "      }\n"
     "    }\n"
     "  }\n"
     "\n"
     "  // DAO/Warden (3.8): the texture bits.\n"
     "  {\n"
     "    uint32_t textures_unsigned, textures_isotropic_lod, textures_integer_scale_round_only;\n"
     "    DaoTextureModificationBits(shader, regs, textures_unsigned, textures_isotropic_lod,\n"
     "                               textures_integer_scale_round_only);\n"
     "    modification.pixel.textures_unsigned = textures_unsigned;\n"
     "    modification.pixel.textures_isotropic_lod = textures_isotropic_lod;\n"
     "    modification.pixel.textures_integer_scale_round_only =\n"
     "        textures_integer_scale_round_only;\n"
     "  }\n"
     "\n"
     "  return modification;\n"
     "}\n"),
    (SST_FETCH, "3.8: each size's reciprocal once per fetch (10.9: decoded on the CPU)",
     "      // Z size is not scaled (depth/layers don't change with resolution).\n"
     "    }\n",
     "      // Z size is not scaled (depth/layers don't change with resolution).\n"
     "    }\n"
     "    // DAO/Warden (3.8): each size's reciprocal, once per fetch, so coordinate\n"
     "    // normalization, offsets and texel snapping multiply instead of dividing - four\n"
     "    // divisions or more a fetch otherwise, on a GPU where a division is a\n"
     "    // reciprocal and more. Exact for power-of-two sizes, within an ulp otherwise.\n"
     "    // size_rcp_source says which size each was taken from: a use whose size has\n"
     "    // been reassigned since divides as before.\n"
     "    // DAO/Warden (PLAN 10.9): the 2D and cube width and height and their reciprocals\n"
     "    // decoded on the CPU - where nothing above makes them the draw's own (no locked\n"
     "    // mip's grid for unnormalized coordinates, no resolution scale) and the fetch is\n"
     "    // not 1D (whose width the data's dimensions select).\n"
     "    const bool size_decoded =\n"
     "        dao_fetch_decoded &&\n"
     "        (coordinate_dimension == xenos::FetchOpDimension::k2D ||\n"
     "         coordinate_dimension == xenos::FetchOpDimension::kCube) &&\n"
     "        instr.dimension != xenos::FetchOpDimension::k1D &&\n"
     "        !(instr.opcode == ucode::FetchOpcode::kTextureFetch &&\n"
     "          instr.dimension == xenos::FetchOpDimension::k2D &&\n"
     "          instr.attributes.unnormalized_coordinates) &&\n"
     "        is_texture_resolved == spv::NoResult;\n"
     "    if (size_decoded) {\n"
     "      for (uint32_t i = 0; i < 2; ++i) {\n"
     "        if (size[i] != spv::NoResult) {\n"
     "          size[i] = load_fetch_decoded(0, i);\n"
     "        }\n"
     "      }\n"
     "    }\n"
     "    spv::Id size_rcp[3] = {};\n"
     "    spv::Id size_rcp_source[3] = {};\n"
     "    for (uint32_t i = 0; i < 3; ++i) {\n"
     "      if (size[i] != spv::NoResult) {\n"
     "        size_rcp[i] = size_decoded && i < 2\n"
     "                          ? load_fetch_decoded(0, 2 + i)\n"
     "                          : builder_->createNoContractionBinOp(\n"
     "                                spv::OpFDiv, type_float_,\n"
     "                                builder_->makeFloatConstant(1.0f), size[i]);\n"
     "        size_rcp_source[i] = size[i];\n"
     "      }\n"
     "    }\n"
     "    auto divide_by_size = [&](spv::Id value, spv::Id size_id) -> spv::Id {\n"
     "      for (uint32_t i = 0; i < 3; ++i) {\n"
     "        if (size_id != spv::NoResult && size_rcp_source[i] == size_id) {\n"
     "          return builder_->createNoContractionBinOp(spv::OpFMul, type_float_, value,\n"
     "                                                    size_rcp[i]);\n"
     "        }\n"
     "      }\n"
     "      return builder_->createNoContractionBinOp(spv::OpFDiv, type_float_, value,\n"
     "                                                size_id);\n"
     "    };\n"),
    (SST_FETCH, "3.8: coordinate normalization multiplies by the reciprocal",
     "          assert_true(size_component != spv::NoResult);\n"
     "          coordinate_ref = builder_->createNoContractionBinOp(\n"
     "              spv::OpFDiv, type_float_, coordinate_ref, size_component);\n",
     "          assert_true(size_component != spv::NoResult);\n"
     "          coordinate_ref = divide_by_size(coordinate_ref, size_component);  // DAO/Warden\n"),
    (SST_FETCH, "3.8: the offset multiplies by the reciprocal",
     "            spv::Id component_offset_normalized =\n"
     "                builder_->createNoContractionBinOp(\n"
     "                    spv::OpFDiv, type_float_, component_offset, size_component);\n",
     "            spv::Id component_offset_normalized =\n"
     "                divide_by_size(component_offset, size_component);  // DAO/Warden (3.8)\n"),
    (SST_FETCH, "3.8: texel snapping multiplies by the reciprocal",
     "            snapped = builder_->createNoContractionBinOp(\n"
     "                spv::OpFDiv, type_float_,\n"
     "                builder_->createNoContractionBinOp(\n"
     "                    spv::OpFAdd, type_float_, snapped,\n"
     "                    builder_->makeFloatConstant(0.5f)),\n"
     "                size[i]);\n"
     "            coordinates[i] = builder_->createTriOp(\n"
     "                spv::OpSelect, type_float_, snap, snapped,\n"
     "                builder_->createNoContractionBinOp(\n"
     "                    spv::OpFAdd, type_float_, coordinates[i],\n"
     "                    builder_->createNoContractionBinOp(\n"
     "                        spv::OpFDiv, type_float_,\n"
     "                        builder_->makeFloatConstant(kTextureCoordEpsilon),\n"
     "                        size[i])));\n",
     "            // DAO/Warden (3.8): multiplications by the size's reciprocal.\n"
     "            snapped = divide_by_size(\n"
     "                builder_->createNoContractionBinOp(\n"
     "                    spv::OpFAdd, type_float_, snapped,\n"
     "                    builder_->makeFloatConstant(0.5f)),\n"
     "                size[i]);\n"
     "            coordinates[i] = builder_->createTriOp(\n"
     "                spv::OpSelect, type_float_, snap, snapped,\n"
     "                builder_->createNoContractionBinOp(\n"
     "                    spv::OpFAdd, type_float_, coordinates[i],\n"
     "                    divide_by_size(builder_->makeFloatConstant(kTextureCoordEpsilon),\n"
     "                                   size[i])));\n"),
    (SST_FETCH, "3.8 experiment: DAO_HACK_NO_INTEGER_SCALE skips the post-fetch integer-scale block",
     "          spv::Id integer_scale_active = builder_->createBinOp(\n"
     "              spv::OpINotEqual, type_bool_,\n"
     "              builder_->createBinOp(\n"
     "                  spv::OpBitwiseAnd, type_uint_, integer_scale_bits_packed,\n"
     "                  builder_->makeUintConstant((UINT32_C(1) << 26) - 1)),\n"
     "              const_uint_0_);\n",
     "          // DAO/Warden experiment (3.8): DAO_HACK_NO_INTEGER_SCALE - never taken.\n"
     "          static const bool dao_no_integer_scale =\n"
     "              std::getenv(\"DAO_HACK_NO_INTEGER_SCALE\") != nullptr;\n"
     "          spv::Id integer_scale_active =\n"
     "              dao_no_integer_scale\n"
     "                  ? builder_->makeBoolConstant(false)\n"
     "                  : builder_->createBinOp(\n"
     "                        spv::OpINotEqual, type_bool_,\n"
     "                        builder_->createBinOp(\n"
     "                            spv::OpBitwiseAnd, type_uint_, integer_scale_bits_packed,\n"
     "                            builder_->makeUintConstant((UINT32_C(1) << 26) - 1)),\n"
     "                        const_uint_0_);\n"),
    (TC_H, "3.8: the integer scale bits, public for the shader modification",
     "  uint32_t GetActiveIntegerScaleBits(uint32_t fetch_constant_index) const {\n",
     "  // DAO/Warden (3.8): a fetch constant's integer scale bits, for the shader\n"
     "  // modification the pipeline cache picks per draw.\n"
     "  static uint32_t DaoIntegerScaleBits(const xenos::xe_gpu_texture_fetch_t& fetch,\n"
     "                                      uint8_t swizzled_signs) {\n"
     "    return GetIntegerScaleBits(fetch, swizzled_signs);\n"
     "  }\n"
     "\n"
     "  uint32_t GetActiveIntegerScaleBits(uint32_t fetch_constant_index) const {\n"),
    (VPC_C, "3.8: the texture cache and texture_util, for the integer-scale modification bit",
     "#include \"xenia/gpu/registers.h\"\n",
     "#include \"xenia/gpu/registers.h\"\n"
     "#include \"xenia/gpu/texture_cache.h\"  // DAO/Warden (3.8)\n"
     "#include \"xenia/gpu/texture_util.h\"   // DAO/Warden (3.8)\n"),
    (SST_FETCH, "3.8: with textures_integer_scale_round_only, the post-fetch block as a select",
     "        spv::Id integer_scale_result =\n"
     "            builder_->createCompositeConstruct(type_float4_, id_vector_temp_);\n"
     "        {\n"
     "          // Uniform early out. Zero means leave the sample alone.\n",
     "        spv::Id integer_scale_result =\n"
     "            builder_->createCompositeConstruct(type_float4_, id_vector_temp_);\n"
     "        // DAO/Warden (3.8): no texture of the shader has integer-scale lane bits,\n"
     "        // so all the block below can do is round a normalized result (bit 24) to 16\n"
     "        // fractional bits - the same arithmetic, as a select, without its branches.\n"
     "        if (is_pixel_shader()\n"
     "                ? GetSpirvShaderModification().pixel.textures_integer_scale_round_only\n"
     "                : GetSpirvShaderModification().vertex.textures_integer_scale_round_only) {\n"
     "          spv::Id normalized = builder_->createBinOp(\n"
     "              spv::OpINotEqual, type_bool_,\n"
     "              builder_->createBinOp(spv::OpBitwiseAnd, type_uint_, integer_scale_bits_packed,\n"
     "                                    builder_->makeUintConstant(UINT32_C(1) << 24)),\n"
     "              const_uint_0_);\n"
     "          spv::Id rounded = builder_->createNoContractionBinOp(\n"
     "              spv::OpVectorTimesScalar, type_float4_, integer_scale_result,\n"
     "              builder_->makeFloatConstant(65536.0f));\n"
     "          rounded = builder_->createUnaryBuiltinCall(\n"
     "              type_float4_, ext_inst_glsl_std_450_, GLSLstd450RoundEven, rounded);\n"
     "          rounded = builder_->createNoContractionBinOp(\n"
     "              spv::OpVectorTimesScalar, type_float4_, rounded,\n"
     "              builder_->makeFloatConstant(1.0f / 65536.0f));\n"
     "          integer_scale_result = builder_->createTriOp(\n"
     "              spv::OpSelect, type_float4_,\n"
     "              builder_->smearScalar(spv::NoPrecision, normalized, type_bool4_),\n"
     "              rounded, integer_scale_result);\n"
     "        } else\n"
     "        {\n"
     "          // Uniform early out. Zero means leave the sample alone.\n"),
    (VRTC_C, "3.8 experiment: DAO_HACK_NO_TRANSFERS skips the transfer draws",
     "    if (!current_transfers.empty()) {\n"
     "      uint32_t dest_pitch_tiles = dest_rt_key.GetPitchTiles();\n",
     "    // DAO/Warden experiment (3.8): DAO_HACK_NO_TRANSFERS skips the transfer draws\n"
     "    // (the resolve clear below stays), to measure the transfer passes - the\n"
     "    // picture is wrong wherever the game reads what a transfer would have copied.\n"
     "    static const bool dao_no_transfers =\n"
     "        std::getenv(\"DAO_HACK_NO_TRANSFERS\") != nullptr;\n"
     "    if (!current_transfers.empty() && !dao_no_transfers) {\n"
     "      uint32_t dest_pitch_tiles = dest_rt_key.GetPitchTiles();\n"),
    (SST_ALU, "3.8: the zero rule tested without NMin (scalar and dot products)",
     "spv::Id SpirvShaderTranslator::ZeroIfAnyOperandIsZero(spv::Id value,\n"
     "                                                      spv::Id operand_0_abs,\n"
     "                                                      spv::Id operand_1_abs) {\n"
     "  EnsureBuildPointAvailable();\n"
     "  assert_true(builder_->getNumComponents(value) == 1);\n"
     "  assert_true(builder_->getNumComponents(operand_0_abs) == 1);\n"
     "  assert_true(builder_->getNumComponents(operand_1_abs) == 1);\n"
     "  return builder_->createTriOp(\n"
     "      spv::OpSelect, type_float_,\n"
     "      builder_->createBinOp(spv::OpFOrdEqual, type_bool_,\n"
     "                            builder_->createBinBuiltinCall(\n"
     "                                type_float_, ext_inst_glsl_std_450_,\n"
     "                                GLSLstd450NMin, operand_0_abs, operand_1_abs),\n"
     "                            const_float_0_),\n"
     "      const_float_0_, value);\n"
     "}\n",
     "// DAO/Warden (3.8): how the Shader Model 3 zero rule (min(|a|, |b|) == 0 gives +0)\n"
     "// is emitted. By default (|a| == 0) || (|b| == 0) - exact, and without NMin, whose\n"
     "// NaN handling the Adreno compiler expands: upstream's NMin form cost about a third\n"
     "// of the King's Camp's main pass (runs 251-254). DAO_HACK_NO_MUL_ZERO selects others,\n"
     "// for measurement: \"nmin\" - upstream's; \"fmin\" - FMin(|a|, |b|) == 0, exact unless an\n"
     "// operand is NaN and the hardware min returns it; \"parity\" - \"or\" for shaders with\n"
     "// an even ucode hash, \"fmin\" for odd, both measured in one run; anything else - IEEE\n"
     "// multiplication, no rule (\"vs\" or \"ps\": in that stage only; not exact - 0 * inf is\n"
     "// NaN, 0 * -x is -0).\n"
     "enum class DaoMulZero { kNMin, kIeee, kOr, kFMin };\n"
     "static bool DaoEquals(const char* a, const char* b) {\n"
     "  while (*a && *a == *b) {\n"
     "    ++a;\n"
     "    ++b;\n"
     "  }\n"
     "  return *a == *b;\n"
     "}\n"
     "static DaoMulZero DaoMulZeroMode(bool pixel_shader, uint64_t ucode_hash) {\n"
     "  static const char* const value = std::getenv(\"DAO_HACK_NO_MUL_ZERO\");\n"
     "  if (!value) {\n"
     "    // PLAN 10.9: the FMin form - exact on the Adreno 740, whose min returns the\n"
     "    // other operand for a NaN (tools/mintest, run 525: 84 of 84 cases, and the\n"
     "    // two forms equal on 100 operand pairs) - a min and a compare in place of\n"
     "    // two compares and an or. \"or\" keeps the Or form.\n"
     "    return DaoMulZero::kFMin;\n"
     "  }\n"
     "  if (DaoEquals(value, \"nmin\")) {\n"
     "    return DaoMulZero::kNMin;\n"
     "  }\n"
     "  if (DaoEquals(value, \"or\")) {\n"
     "    return DaoMulZero::kOr;\n"
     "  }\n"
     "  if (DaoEquals(value, \"fmin\")) {\n"
     "    return DaoMulZero::kFMin;\n"
     "  }\n"
     "  if (DaoEquals(value, \"parity\")) {\n"
     "    return (ucode_hash & 1) ? DaoMulZero::kFMin : DaoMulZero::kOr;\n"
     "  }\n"
     "  if (DaoEquals(value, \"vs\")) {\n"
     "    return pixel_shader ? DaoMulZero::kOr : DaoMulZero::kIeee;\n"
     "  }\n"
     "  if (DaoEquals(value, \"ps\")) {\n"
     "    return pixel_shader ? DaoMulZero::kIeee : DaoMulZero::kOr;\n"
     "  }\n"
     "  return DaoMulZero::kIeee;\n"
     "}\n"
     "// The rule's test - true where the product becomes +0 - for operands already made\n"
     "// absolute, scalars or vectors of type_float, against zero of that type.\n"
     "static spv::Id DaoZeroTest(SpirvBuilder& builder, spv::Id ext_inst_glsl_std_450,\n"
     "                           DaoMulZero mode, spv::Id type_bool, spv::Id type_float,\n"
     "                           spv::Id zero, spv::Id operand_0_abs,\n"
     "                           spv::Id operand_1_abs) {\n"
     "  if (mode == DaoMulZero::kOr) {\n"
     "    return builder.createBinOp(\n"
     "        spv::OpLogicalOr, type_bool,\n"
     "        builder.createBinOp(spv::OpFOrdEqual, type_bool, operand_0_abs, zero),\n"
     "        builder.createBinOp(spv::OpFOrdEqual, type_bool, operand_1_abs, zero));\n"
     "  }\n"
     "  return builder.createBinOp(\n"
     "      spv::OpFOrdEqual, type_bool,\n"
     "      builder.createBinBuiltinCall(\n"
     "          type_float, ext_inst_glsl_std_450,\n"
     "          mode == DaoMulZero::kFMin ? GLSLstd450FMin : GLSLstd450NMin,\n"
     "          operand_0_abs, operand_1_abs),\n"
     "      zero);\n"
     "}\n"
     "\n"
     "spv::Id SpirvShaderTranslator::ZeroIfAnyOperandIsZero(spv::Id value,\n"
     "                                                      spv::Id operand_0_abs,\n"
     "                                                      spv::Id operand_1_abs) {\n"
     "  EnsureBuildPointAvailable();\n"
     "  assert_true(builder_->getNumComponents(value) == 1);\n"
     "  assert_true(builder_->getNumComponents(operand_0_abs) == 1);\n"
     "  assert_true(builder_->getNumComponents(operand_1_abs) == 1);\n"
     "  // DAO/Warden (3.8): the rule's form per DaoMulZeroMode.\n"
     "  const DaoMulZero mode =\n"
     "      DaoMulZeroMode(is_pixel_shader(), current_shader().ucode_data_hash());\n"
     "  if (mode == DaoMulZero::kIeee) {\n"
     "    return value;\n"
     "  }\n"
     "  return builder_->createTriOp(\n"
     "      spv::OpSelect, type_float_,\n"
     "      DaoZeroTest(*builder_, ext_inst_glsl_std_450_, mode, type_bool_, type_float_,\n"
     "                  const_float_0_, operand_0_abs, operand_1_abs),\n"
     "      const_float_0_, value);\n"
     "}\n"),
    (SST_ALU, "3.8: the zero rule skipped for vectors in the IEEE experiment",
     "      if (multiplicands_different) {\n"
     "        // Shader Model 3: +0 or denormal * anything = +-0.\n",
     "      // DAO/Warden (3.8): no rule at all in DaoMulZeroMode's IEEE experiment.\n"
     "      if (multiplicands_different &&\n"
     "          DaoMulZeroMode(is_pixel_shader(), current_shader().ucode_data_hash()) !=\n"
     "              DaoMulZero::kIeee) {\n"
     "        // Shader Model 3: +0 or denormal * anything = +-0.\n"),
    (SST_ALU, "3.8: the zero rule tested without NMin (vectors)",
     "        spv::Id different_zero = builder_->createBinOp(\n"
     "            spv::OpFOrdEqual, type_bool_vectors_[different_count - 1],\n"
     "            builder_->createBinBuiltinCall(\n"
     "                different_type, ext_inst_glsl_std_450_, GLSLstd450NMin,\n"
     "                different_operands[0], different_operands[1]),\n"
     "            const_float_vectors_0_[different_count - 1]);\n",
     "        // DAO/Warden (3.8): the rule's form per DaoMulZeroMode.\n"
     "        spv::Id different_zero = DaoZeroTest(\n"
     "            *builder_, ext_inst_glsl_std_450_,\n"
     "            DaoMulZeroMode(is_pixel_shader(), current_shader().ucode_data_hash()),\n"
     "            type_bool_vectors_[different_count - 1], different_type,\n"
     "            const_float_vectors_0_[different_count - 1], different_operands[0],\n"
     "            different_operands[1]);\n"),
    (VCP_C, "BUG-052: the face-bones write watch's entry points",
     "#include \"xenia/gpu/vulkan/vulkan_command_processor.h\"\n",
     "#include \"xenia/gpu/vulkan/vulkan_command_processor.h\"\n"
     "\n"
     "// DAO/Warden (BUG-052): warden_memory.cc - find a value in guest memory and watch\n"
     "// its pages for writes; log what the watch saw.\n"
     "namespace warden {\n"
     "void DaoFindAndWatch(const uint32_t* values, uint32_t count);\n"
     "void DaoWatchPoll();\n"
     "void SampleGuestThreadsNow(const char* why);  // warden_trace.cc\n"
     "}  // namespace warden\n"),
    (VKD_H, "4.7: VK_GOOGLE_display_timing's flag",
     "    bool ext_EXT_conditional_rendering = false;  // #82\n",
     "    bool ext_EXT_conditional_rendering = false;  // #82\n"
     "    // DAO/Warden (4.7): when frames reach the panel, and frame pacing.\n"
     "    bool ext_GOOGLE_display_timing = false;  // #93\n"),
    # PLAN 9.4 (session 35): FSR 3.1's passes (the FidelityFX SDK's, warden_fsr3.cc)
    # write storage images in formats beyond the base set, some declared without a
    # format - two core features Xenia does not ask for. Session 36: and their 16-bit
    # permutations (FFX_HALF) declare Int16 (the reactive mask, RCAS) besides Float16
    # (below, Vulkan 1.2's) - the Adreno runs 16-bit arithmetic at twice the rate.
    (VKD_C, "9.4: the storage image and 16-bit features FSR 3.1's passes use",
     "    XE_UI_VULKAN_FEATURE(sparseResidencyBuffer)\n"
     "  }\n",
     "    XE_UI_VULKAN_FEATURE(sparseResidencyBuffer)\n"
     "    // DAO/Warden (PLAN 9.4): for FSR 3.1's passes.\n"
     "    enabled_features.shaderStorageImageExtendedFormats =\n"
     "        supported_features.shaderStorageImageExtendedFormats;\n"
     "    enabled_features.shaderStorageImageWriteWithoutFormat =\n"
     "        supported_features.shaderStorageImageWriteWithoutFormat;\n"
     "    // DAO/Warden (PLAN 9.14): and FSR 3.1's frame interpolation, which reads its\n"
     "    // output image back, bound without a format.\n"
     "    enabled_features.shaderStorageImageReadWithoutFormat =\n"
     "        supported_features.shaderStorageImageReadWithoutFormat;\n"
     "    enabled_features.shaderInt16 = supported_features.shaderInt16;\n"
     "  }\n"),
    (VKD_C, "9.4, 9.14: 16-bit floats for FSR 3.1, timeline semaphores for frame generation",
     "      XE_UI_VULKAN_FEATURE_2(features_1_2, hostQueryReset);\n",
     "      XE_UI_VULKAN_FEATURE_2(features_1_2, hostQueryReset);\n"
     "      // DAO/Warden (PLAN 9.4): FSR 3.1's 16-bit permutations (warden_fsr3.cc).\n"
     "      features_1_2.enabled.shaderFloat16 = features_1_2.supported.shaderFloat16;\n"
     "      // DAO/Warden (PLAN 9.14): frame generation's timeline semaphore (warden_lsfg.cc).\n"
     "      features_1_2.enabled.timelineSemaphore =\n"
     "          features_1_2.supported.timelineSemaphore;\n"),
    # PLAN 9.14 (session 36): frame generation submits on a queue of its own (lsfg-vk's
    # backend on the game's device - warden_lsfg.cc); the emulator only ever takes queue 0.
    (VKD_C, "9.14: a second queue in the graphics and compute family",
     "  device->queue_families_[device->queue_family_graphics_compute_].queues.resize(\n"
     "      std::max(size_t(1),\n"
     "               device->queue_families_[device->queue_family_graphics_compute_]\n"
     "                   .queues.size()));\n"
,
     "  device->queue_families_[device->queue_family_graphics_compute_].queues.resize(\n"
     "      std::max(size_t(1),\n"
     "               device->queue_families_[device->queue_family_graphics_compute_]\n"
     "                   .queues.size()));\n"
     "  // DAO/Warden (PLAN 9.14): a second queue in the graphics and compute family, for\n"
     "  // frame generation's own submissions (warden_lsfg.cc) - the emulator takes queue 0.\n"
     "  if (with_gpu_emulation &&\n"
     "      queue_families[device->queue_family_graphics_compute_].queueCount >= 2) {\n"
     "    device->queue_families_[device->queue_family_graphics_compute_].queues.resize(\n"
     "        std::max(size_t(2),\n"
     "                 device->queue_families_[device->queue_family_graphics_compute_]\n"
     "                     .queues.size()));\n"
     "  }\n"
),
    (VKD_C, "4.7: VK_GOOGLE_display_timing enabled with the swapchain",
     "    XE_UI_VULKAN_STRUCT_EXTENSION(KHR_swapchain)\n",
     "    XE_UI_VULKAN_STRUCT_EXTENSION(KHR_swapchain)\n"
     "    // #93. DAO/Warden (4.7): when frames reach the panel, and frame pacing\n"
     "    // (app/src/main/cpp/warden_frame_pacing.cc).\n"
     "    XE_UI_VULKAN_STRUCT_EXTENSION(GOOGLE_display_timing)\n"),
    # 3.7 (session 35): GPU memory accounting. The driver says only how much the process
    # holds (1.9 GB at the King's Camp); the app wraps each device's vkAllocateMemory and
    # vkFreeMemory to keep every live allocation with the code that made it
    # (app/src/main/cpp/warden_gpu_memory.cc). VMA, which the texture cache allocates
    # through, takes its functions from this table later, so it is counted too.
    (VKD_C, "3.7: the allocation functions handed to the app's accounting (declaration)",
     "#include <vector>\n",
     "#include <vector>\n"
     "\n"
     "// DAO/Warden (3.7): app/src/main/cpp/warden_gpu_memory.cc.\n"
     "extern \"C\" void warden_vk_wrap_device_functions(VkDevice device,\n"
     "                                                PFN_vkAllocateMemory* allocate,\n"
     "                                                PFN_vkFreeMemory* free);\n"),
    (VKD_C, "3.7: the allocation functions handed to the app's accounting",
     "  if (!functions_loaded) {\n"
     "    XELOGE(\"Failed to get all Vulkan device function pointers for '{}'\",\n"
     "           properties.deviceName);\n"
     "    return nullptr;\n"
     "  }\n",
     "  if (!functions_loaded) {\n"
     "    XELOGE(\"Failed to get all Vulkan device function pointers for '{}'\",\n"
     "           properties.deviceName);\n"
     "    return nullptr;\n"
     "  }\n"
     "  warden_vk_wrap_device_functions(device->device_, &dfn.vkAllocateMemory,\n"
     "                                  &dfn.vkFreeMemory);\n"),
    # 3.7 / BUG-060 (session 35): the driver's statistics per pipeline - its registers,
    # its private memory, its spills - through VK_KHR_pipeline_executable_properties,
    # which the Adreno 740 has. Enabled with the device; captured and logged only with
    # files/hack-pipeline-stats (app/src/main/cpp/warden_gpu_memory.cc).
    (VKD_H, "3.7: VK_KHR_pipeline_executable_properties's flag",
     "    bool ext_EXT_conditional_rendering = false;  // #82\n",
     "    bool ext_EXT_conditional_rendering = false;  // #82\n"
     "    // DAO/Warden (3.7): the driver's statistics per pipeline.\n"
     "    bool ext_KHR_pipeline_executable_properties = false;  // #270\n"),
    (VKD_C, "3.7: VK_KHR_pipeline_executable_properties requested",
     "      XE_UI_VULKAN_LOCAL_EXTENSION(EXT_custom_border_color)\n",
     "      XE_UI_VULKAN_LOCAL_EXTENSION(EXT_custom_border_color)\n"
     "      // #270. DAO/Warden (3.7): the driver's statistics per pipeline.\n"
     "      XE_UI_VULKAN_STRUCT_EXTENSION(KHR_pipeline_executable_properties)\n"),
    (VKD_C, "3.7: VK_KHR_pipeline_executable_properties's features",
     "      features_EXT_conditional_rendering;\n",
     "      features_EXT_conditional_rendering;\n"
     "  VulkanFeatures<\n"
     "      VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR,\n"
     "      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR>\n"
     "      features_KHR_pipeline_executable_properties;\n"),
    (VKD_C, "3.7: VK_KHR_pipeline_executable_properties's features linked",
     "      features_EXT_conditional_rendering.Link(supported_features_2,\n"
     "                                              device_create_info);\n"
     "    }\n",
     "      features_EXT_conditional_rendering.Link(supported_features_2,\n"
     "                                              device_create_info);\n"
     "    }\n"
     "    if (device->extensions_.ext_KHR_pipeline_executable_properties) {\n"
     "      features_KHR_pipeline_executable_properties.Link(supported_features_2,\n"
     "                                                       device_create_info);\n"
     "    }\n"),
    (VKD_C, "3.7: VK_KHR_pipeline_executable_properties's feature enabled",
     "      XE_UI_VULKAN_FEATURE_2(features_EXT_conditional_rendering,\n"
     "                             conditionalRendering)\n"
     "    }\n"
     "  }\n",
     "      XE_UI_VULKAN_FEATURE_2(features_EXT_conditional_rendering,\n"
     "                             conditionalRendering)\n"
     "    }\n"
     "  }\n"
     "  if (device->extensions_.ext_KHR_pipeline_executable_properties) {\n"
     "    features_KHR_pipeline_executable_properties.enabled.pipelineExecutableInfo =\n"
     "        features_KHR_pipeline_executable_properties.supported\n"
     "            .pipelineExecutableInfo;\n"
     "  }\n"),
    (VPC_C, "3.7: the driver's pipeline statistics (declarations)",
     "#include \"xenia/gpu/vulkan/vulkan_pipeline_cache.h\"\n",
     "#include \"xenia/gpu/vulkan/vulkan_pipeline_cache.h\"\n"
     "\n"
     "#include <mutex>\n"
     "#include <unordered_set>\n"
     "\n"
     "// DAO/Warden (BUG-060): the storage's pipelines, by description hash, when they are\n"
     "// created at first use - so that use does not write them to the storage again.\n"
     "static std::mutex dao_stored_pipeline_hashes_mutex;\n"
     "static std::unordered_set<uint64_t> dao_stored_pipeline_hashes;\n"
     "\n"
     "// DAO/Warden (3.7): app/src/main/cpp/warden_gpu_memory.cc.\n"
     "extern \"C\" bool warden_precreate_stored_pipelines();\n"
     "extern \"C\" bool warden_pipeline_statistics_wanted();\n"
     "extern \"C\" void warden_pipeline_statistics(VkDevice device, VkPipeline pipeline,\n"
     "                                            uint64_t vertex_shader,\n"
     "                                            uint64_t pixel_shader);\n"),
    (VPC_C, "3.7: the driver's pipeline statistics captured",
     "  pipeline_create_info.flags = 0;\n"
     "  pipeline_create_info.stageCount = shader_stage_count;\n",
     "  pipeline_create_info.flags = 0;\n"
     "  // DAO/Warden (3.7): the driver's statistics for each new pipeline.\n"
     "  const bool dao_pipeline_statistics =\n"
     "      vulkan_device->extensions().ext_KHR_pipeline_executable_properties &&\n"
     "      warden_pipeline_statistics_wanted();\n"
     "  if (dao_pipeline_statistics) {\n"
     "    pipeline_create_info.flags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;\n"
     "  }\n"
     "  pipeline_create_info.stageCount = shader_stage_count;\n"),
    # BUG-060 (session 35): the stored pipelines are not created at startup. Each whose
    # shaders spill registers holds about 24 MB of the driver's memory (scratch reserved
    # for every wave the GPU can run), and the storage grows with every area visited -
    # 182 pipelines after a day's tests, 1.17 GB at each start. With the driver's
    # VkPipelineCache loaded just above, a pipeline created when first drawn with is a
    # cache hit, not a compile. files/hack-precreate-pipelines creates them all, as
    # upstream does.
    (VPC_C, "BUG-060: the stored pipelines created when first drawn with, not at startup",
     "  // Create pipelines from stored descriptions.\n"
     "  if (!pipeline_stored_descriptions.empty()) {\n",
     "  // Create pipelines from stored descriptions - DAO/Warden (BUG-060): not by default;\n"
     "  // each is created when first drawn with, a hit in the driver's cache loaded above.\n"
     "  const bool dao_precreate = warden_precreate_stored_pipelines();\n"
     "  if (!pipeline_stored_descriptions.empty() && !dao_precreate) {\n"
     "    std::lock_guard<std::mutex> lock(dao_stored_pipeline_hashes_mutex);\n"
     "    for (const PipelineStoredDescription& dao_stored :\n"
     "         pipeline_stored_descriptions) {\n"
     "      dao_stored_pipeline_hashes.insert(dao_stored.description_hash);\n"
     "    }\n"
     "    XELOGI(\"Pipeline cache: {} stored pipelines ({} descriptions) left to be \"\n"
     "           \"created when first drawn with (BUG-060)\",\n"
     "           dao_stored_pipeline_hashes.size(), pipeline_stored_descriptions.size());\n"
     "  }\n"
     "  if (!pipeline_stored_descriptions.empty() && dao_precreate) {\n"),
    (VPC_C, "BUG-060: a stored pipeline not written to the storage again at its first use",
     "    pipeline_storage_file_flush_needed_ = true;\n"
     "    PipelineStoredDescription stored_description;\n"
     "    stored_description.description_hash = description.GetHash();\n"
     "    std::memcpy(&stored_description.description, &description,\n"
     "                sizeof(description));\n"
     "    storage_writer_.QueuePipelineWrite(stored_description);\n",
     "    // DAO/Warden (BUG-060): a pipeline the storage holds, created at its first use,\n"
     "    // is not written again - the file grew by every pipeline a session used.\n"
     "    bool dao_already_stored;\n"
     "    {\n"
     "      std::lock_guard<std::mutex> lock(dao_stored_pipeline_hashes_mutex);\n"
     "      dao_already_stored =\n"
     "          dao_stored_pipeline_hashes.count(description.GetHash()) != 0;\n"
     "    }\n"
     "    if (!dao_already_stored) {\n"
     "      pipeline_storage_file_flush_needed_ = true;\n"
     "      PipelineStoredDescription stored_description;\n"
     "      stored_description.description_hash = description.GetHash();\n"
     "      std::memcpy(&stored_description.description, &description,\n"
     "                  sizeof(description));\n"
     "      storage_writer_.QueuePipelineWrite(stored_description);\n"
     "    }\n"),
    (VPC_C, "3.7: the driver's pipeline statistics logged",
     "  // Store the new pipeline, handling placeholder hot-swap.\n",
     "  if (dao_pipeline_statistics) {\n"
     "    warden_pipeline_statistics(\n"
     "        device, pipeline,\n"
     "        creation_arguments.vertex_shader->shader().ucode_data_hash(),\n"
     "        creation_arguments.pixel_shader\n"
     "            ? creation_arguments.pixel_shader->shader().ucode_data_hash()\n"
     "            : 0);\n"
     "  }\n"
     "\n"
     "  // Store the new pipeline, handling placeholder hot-swap.\n"),
    (VKP_C, "4.7, 6.7: the frame pacer's present hooks",
     "#include \"xenia/ui/vulkan/vulkan_presenter.h\"\n",
     "#include \"xenia/ui/vulkan/vulkan_presenter.h\"\n"
     "\n"
     "// DAO/Warden (4.7, 6.7): app/src/main/cpp/warden_frame_pacing.cc - each present's\n"
     "// timing (its id and, when paced, a time before which it must not reach the panel),\n"
     "// and each present afterwards: the frame-time report, and the panel's own times read\n"
     "// back.\n"
     "extern \"C\" const void* warden_present_timing(VkDevice device, VkSwapchainKHR swapchain,\n"
     "                                              bool display_timing);\n"
     "extern \"C\" void warden_presented(VkDevice device, VkSwapchainKHR swapchain);\n"
     "extern \"C\" int64_t warden_acquire_begin();\n"
     "extern \"C\" void warden_acquire_end(int64_t start);\n"
     "// DAO/Warden (PLAN 9.3): the Colour setting's swapchain format, if the surface has\n"
     "// it (app/src/main/cpp/warden_gpu.cc); false leaves the choice to the code below.\n"
     "extern \"C\" bool warden_surface_format(const VkSurfaceFormatKHR* formats,\n"
     "                                      uint32_t count, VkSurfaceFormatKHR* chosen);\n"
     "// DAO/Warden (PLAN 9.7, 10.1): the Upscaling setting's FXAA - the app's shader for\n"
     "// the dithered bilinear pass, with the same inputs (app/src/main/cpp/warden_gpu.cc).\n"
     "extern \"C\" void warden_paint_shader(size_t* code_size, const uint32_t** code);\n"),
    # PLAN 9.3 (session 35). Xenia's presenter picks an 8-bit sRGB swapchain, and only
    # that: the Thor's surface also offers 10-bit sRGB and 10-bit Display-P3 (the
    # bring-up report's "swapchain formats"). The settings' Colour picks among them
    # (warden_gpu.cc, warden_surface_format: 10-bit sRGB by default, "vivid" 10-bit
    # Display-P3); when it declines, Xenia's own choice stands. The render pass and the
    # paint pipelines already follow the swapchain's format.
    # PLAN 9.7 (session 35). Xenia's Vulkan backend has no anti-aliasing of its own
    # (Direct3D 12's FXAA pass was never ported). The app's FXAA shader
    # (app/src/main/cpp/shaders/warden_fxaa.ps.glsl, compiled by
    # tools/build_xenia_shaders.py) has the bilinear pass's interface, so it is the
    # module of that pass's dithered variant, which nothing else here uses (Xenia dithers
    # only when asked, for 8-bit output): Settings -> Upscaling -> FXAA is the bilinear
    # effect with dithering on, switched while the game runs (PLAN 10.1).
    (VKP_C, "9.7: the bilinear pass's shader may be the app's FXAA",
     "      default:\n"
     "        // Not supported by this implementation.\n"
     "        continue;\n"
     "    }\n"
     "    if (dfn.vkCreateShaderModule(device, &shader_module_create_info, nullptr,\n"
     "                                 &guest_output_paint_fs_[i]) != VK_SUCCESS) {\n",
     "      default:\n"
     "        // Not supported by this implementation.\n"
     "        continue;\n"
     "    }\n"
     "    // DAO/Warden (PLAN 9.7, 10.1): the dithered bilinear pass is the app's FXAA.\n"
     "    if (guest_output_paint_effect == GuestOutputPaintEffect::kBilinearDither) {\n"
     "      warden_paint_shader(&shader_module_create_info.codeSize,\n"
     "                          &shader_module_create_info.pCode);\n"
     "    }\n"
     "    if (dfn.vkCreateShaderModule(device, &shader_module_create_info, nullptr,\n"
     "                                 &guest_output_paint_fs_[i]) != VK_SUCCESS) {\n"),
    # PLAN 9.2 (session 35): HDR10. The Thor's Vulkan swapchain offers no PQ colour
    # space, so the HDR10 swapchain is the 10-bit sRGB one with its window's buffers
    # tagged BT.2020 PQ once it is made (warden_swapchain_created), and the presenter's
    # final pass - the one drawing into the swapchain - draws with its HDR variant
    # (app/src/main/cpp/shaders/warden_hdr_*.ps.glsl: Xenia's passes, untouched, their
    # result encoded), made beside each effect's own module, chosen while
    # warden_hdr_key says the output is HDR10, with that state's constants. The
    # swapchain's pipeline is made again when the key changes, as for a new format.
    (VKP_C, "9.2: the HDR10 hooks declared",
     "#include \"xenia/ui/vulkan/vulkan_util.h\"\n",
     "#include \"xenia/ui/vulkan/vulkan_util.h\"\n"
     "\n"
     "// DAO/Warden (PLAN 9.2): HDR10 output (app/src/main/cpp/warden_gpu.cc) - each final\n"
     "// effect's HDR variant (false: none); the output's HDR state for the swapchain's\n"
     "// pipeline (0: standard range), with its specialization constants; and each\n"
     "// swapchain made, its window's buffers tagged BT.2020 PQ when it is HDR10's.\n"
     "extern \"C\" bool warden_hdr_paint_shader(int effect, size_t* code_size,\n"
     "                                        const uint32_t** code);\n"
     "extern \"C\" uint32_t warden_hdr_key(const VkSpecializationInfo** specialization);\n"
     "extern \"C\" void warden_swapchain_created(VkFormat format,\n"
     "                                         VkColorSpaceKHR color_space);\n"),
    (VKP_H, "9.2: the swapchain pipeline's HDR10 state",
     "      VkPipeline swapchain_pipeline = VK_NULL_HANDLE;\n"
     "      VkFormat swapchain_format = VK_FORMAT_UNDEFINED;\n"
     "    };\n",
     "      VkPipeline swapchain_pipeline = VK_NULL_HANDLE;\n"
     "      VkFormat swapchain_format = VK_FORMAT_UNDEFINED;\n"
     "      // DAO/Warden (PLAN 9.2): the HDR10 state it was made for (warden_hdr_key).\n"
     "      uint32_t warden_hdr_key = 0;\n"
     "    };\n"),
    (VKP_H, "9.2: the final effects' HDR10 modules",
     "  std::array<VkShaderModule, size_t(GuestOutputPaintEffect::kCount)>\n"
     "      guest_output_paint_fs_ = {};\n",
     "  std::array<VkShaderModule, size_t(GuestOutputPaintEffect::kCount)>\n"
     "      guest_output_paint_fs_ = {};\n"
     "  // DAO/Warden (PLAN 9.2): each final effect's HDR10 variant, for the swapchain's\n"
     "  // pipeline while the output is HDR10.\n"
     "  std::array<VkShaderModule, size_t(GuestOutputPaintEffect::kCount)>\n"
     "      warden_hdr_paint_fs_ = {};\n"),
    (VKP_C, "9.2: the final effects' HDR10 variants made",
     "    if (dfn.vkCreateShaderModule(device, &shader_module_create_info, nullptr,\n"
     "                                 &guest_output_paint_fs_[i]) != VK_SUCCESS) {\n"
     "      XELOGE(\n"
     "          \"VulkanPresenter: Failed to create the guest output painting shader \"\n"
     "          \"module for effect {}\",\n"
     "          i);\n"
     "      return false;\n"
     "    }\n"
     "  }\n",
     "    if (dfn.vkCreateShaderModule(device, &shader_module_create_info, nullptr,\n"
     "                                 &guest_output_paint_fs_[i]) != VK_SUCCESS) {\n"
     "      XELOGE(\n"
     "          \"VulkanPresenter: Failed to create the guest output painting shader \"\n"
     "          \"module for effect {}\",\n"
     "          i);\n"
     "      return false;\n"
     "    }\n"
     "    // DAO/Warden (PLAN 9.2): the effect's HDR10 variant, if it can be the last pass.\n"
     "    if (warden_hdr_paint_shader(int(guest_output_paint_effect),\n"
     "                                &shader_module_create_info.codeSize,\n"
     "                                &shader_module_create_info.pCode) &&\n"
     "        dfn.vkCreateShaderModule(device, &shader_module_create_info, nullptr,\n"
     "                                 &warden_hdr_paint_fs_[i]) != VK_SUCCESS) {\n"
     "      XELOGE(\"VulkanPresenter: no HDR10 variant of effect {}\", i);\n"
     "      warden_hdr_paint_fs_[i] = VK_NULL_HANDLE;\n"
     "    }\n"
     "  }\n"),
    (VKP_C, "9.2: the HDR10 variants destroyed",
     "  for (VkShaderModule& shader_module : guest_output_paint_fs_) {\n"
     "    util::DestroyAndNullHandle(dfn.vkDestroyShaderModule, device,\n"
     "                               shader_module);\n"
     "  }\n",
     "  for (VkShaderModule& shader_module : guest_output_paint_fs_) {\n"
     "    util::DestroyAndNullHandle(dfn.vkDestroyShaderModule, device,\n"
     "                               shader_module);\n"
     "  }\n"
     "  for (VkShaderModule& shader_module : warden_hdr_paint_fs_) {\n"
     "    util::DestroyAndNullHandle(dfn.vkDestroyShaderModule, device,\n"
     "                               shader_module);\n"
     "  }\n"),
    # BUG-062 (session 35): upstream compares the swapchain pipeline's swapchain_format
    # with the render pass's but never sets it, so the final pass's pipeline was
    # destroyed and made again at every paint, each time after waiting for the previous
    # paint's submission to complete.
    (VKP_C, "9.2, BUG-062: the swapchain's pipeline made again only when outdated",
     "        if (swapchain_effect_pipeline.swapchain_pipeline != VK_NULL_HANDLE &&\n"
     "            swapchain_effect_pipeline.swapchain_format !=\n"
     "                paint_context_.swapchain_render_pass_format) {\n",
     "        // DAO/Warden (PLAN 9.2): and when the output's HDR10 state changed.\n"
     "        const uint32_t warden_hdr = warden_hdr_key(nullptr);\n"
     "        if (swapchain_effect_pipeline.swapchain_pipeline != VK_NULL_HANDLE &&\n"
     "            (swapchain_effect_pipeline.swapchain_format !=\n"
     "                 paint_context_.swapchain_render_pass_format ||\n"
     "             swapchain_effect_pipeline.warden_hdr_key != warden_hdr)) {\n"),
    (VKP_C, "BUG-062: the swapchain pipeline's format recorded",
     "          swapchain_effect_pipeline.swapchain_pipeline =\n"
     "              CreateGuestOutputPaintPipeline(\n"
     "                  swapchain_effect, paint_context_.swapchain_render_pass);\n",
     "          swapchain_effect_pipeline.swapchain_pipeline =\n"
     "              CreateGuestOutputPaintPipeline(\n"
     "                  swapchain_effect, paint_context_.swapchain_render_pass);\n"
     "          // DAO/Warden (BUG-062): what it was made for - upstream never records the\n"
     "          // format it compares, so it was made again at every paint.\n"
     "          swapchain_effect_pipeline.swapchain_format =\n"
     "              paint_context_.swapchain_render_pass_format;\n"
     "          swapchain_effect_pipeline.warden_hdr_key = warden_hdr;\n"),
    (VKP_C, "9.2: the swapchain's pipeline draws with the HDR10 variant",
     "  stages[0].module = guest_output_paint_vs_;\n"
     "  stages[1].module = guest_output_paint_fs_[size_t(effect)];\n",
     "  stages[0].module = guest_output_paint_vs_;\n"
     "  stages[1].module = guest_output_paint_fs_[size_t(effect)];\n"
     "  // DAO/Warden (PLAN 9.2): into the swapchain while the output is HDR10, the\n"
     "  // effect's HDR variant, with that state's constants.\n"
     "  if (render_pass == paint_context_.swapchain_render_pass &&\n"
     "      render_pass != VK_NULL_HANDLE &&\n"
     "      warden_hdr_paint_fs_[size_t(effect)] != VK_NULL_HANDLE &&\n"
     "      warden_hdr_key(&stages[1].pSpecializationInfo)) {\n"
     "    stages[1].module = warden_hdr_paint_fs_[size_t(effect)];\n"
     "  }\n"),
    (VKP_C, "9.2: each swapchain made, its window's buffers tagged for HDR10",
     "      uint32_t(swapchain_create_info.imageColorSpace),\n"
     "      uint32_t(swapchain_create_info.presentMode));\n",
     "      uint32_t(swapchain_create_info.imageColorSpace),\n"
     "      uint32_t(swapchain_create_info.presentMode));\n"
     "  // DAO/Warden (PLAN 9.2): HDR10's swapchain tagged BT.2020 PQ.\n"
     "  warden_swapchain_created(swapchain_create_info.imageFormat,\n"
     "                           swapchain_create_info.imageColorSpace);\n"),
    (VKP_C, "9.3: the Colour setting's swapchain format first",
     "  VkSurfaceFormatKHR image_format;\n"
     "  if (surface_formats.empty() ||\n"
     "      (surface_formats.size() == 1 ||\n"
     "       surface_formats[0].format == VK_FORMAT_UNDEFINED)) {\n",
     "  VkSurfaceFormatKHR image_format;\n"
     "  // DAO/Warden (PLAN 9.3): the Colour setting's format, when the surface has it.\n"
     "  if (warden_surface_format(surface_formats.data(),\n"
     "                            uint32_t(surface_formats.size()), &image_format)) {\n"
     "  } else if (surface_formats.empty() ||\n"
     "             (surface_formats.size() == 1 ||\n"
     "              surface_formats[0].format == VK_FORMAT_UNDEFINED)) {\n"),
    (VKP_C, "4.7: the wait for a swapchain image timed",
     "  uint32_t swapchain_image_index;\n"
     "  VkResult acquire_result = dfn.vkAcquireNextImageKHR(\n"
     "      device, paint_context_.swapchain, UINT64_MAX, acquire_semaphore,\n"
     "      VK_NULL_HANDLE, &swapchain_image_index);\n",
     "  uint32_t swapchain_image_index;\n"
     "  // DAO/Warden (4.7): how long this thread - the GPU command processor's - waits for\n"
     "  // an image, per pacing mode (app/src/main/cpp/warden_frame_pacing.cc).\n"
     "  const int64_t dao_acquire_start = warden_acquire_begin();\n"
     "  VkResult acquire_result = dfn.vkAcquireNextImageKHR(\n"
     "      device, paint_context_.swapchain, UINT64_MAX, acquire_semaphore,\n"
     "      VK_NULL_HANDLE, &swapchain_image_index);\n"
     "  warden_acquire_end(dao_acquire_start);\n"),
    (VKP_C, "4.7, 6.7: each present timed, paced and reported",
     "  present_info.pResults = nullptr;\n"
     "  VkResult present_result;\n"
     "  {\n"
     "    const VulkanDevice::Queue::Acquisition queue_acquisition =\n"
     "        vulkan_device_->AcquireQueue(paint_context_.present_queue_family, 0);\n"
     "    present_result =\n"
     "        dfn.vkQueuePresentKHR(queue_acquisition.queue(), &present_info);\n"
     "  }\n",
     "  present_info.pResults = nullptr;\n"
     "  // DAO/Warden (4.7): the frame's id and, when paced, the refresh it is held for\n"
     "  // (VK_GOOGLE_display_timing; none without the extension).\n"
     "  present_info.pNext = warden_present_timing(\n"
     "      device, paint_context_.swapchain,\n"
     "      vulkan_device_->extensions().ext_GOOGLE_display_timing);\n"
     "  VkResult present_result;\n"
     "  {\n"
     "    const VulkanDevice::Queue::Acquisition queue_acquisition =\n"
     "        vulkan_device_->AcquireQueue(paint_context_.present_queue_family, 0);\n"
     "    const auto present_began = std::chrono::steady_clock::now();  // DAO/Warden (BUG-112)\n"
     "    present_result =\n"
     "        dfn.vkQueuePresentKHR(queue_acquisition.queue(), &present_info);\n"
     "    warden_gpu_present_note(std::chrono::duration<double, std::milli>(\n"
     "                                std::chrono::steady_clock::now() - present_began)\n"
     "                                .count());  // BUG-112\n"
     "  }\n"
     "  warden_presented(device, paint_context_.swapchain);  // DAO/Warden (4.7, 6.7)\n"),
    (DAO_GFX_SYSTEM, "4.9: the host GPU's loss hook declared",
     "#include \"xenia/ui/windowed_app_context.h\"\n",
     "#include \"xenia/ui/windowed_app_context.h\"\n"
     "\n"
     "// DAO/Warden (4.9): app/src/main/cpp/warden_lifecycle.cc - the player is told and the\n"
     "// app ends, where xe::FatalError aborted the process with no word on Android.\n"
     "extern \"C\" void warden_host_gpu_lost(bool is_responsible);\n"),
    (DAO_GFX_SYSTEM, "4.9: the host GPU's loss told to the player, not an abort",
     "  xe::FatalError(\"Graphics device lost (probably due to an internal error)\");\n",
     "  XELOGE(\"Graphics device lost (probably due to an internal error)\");\n"
     "  warden_host_gpu_lost(is_responsible);  // DAO/Warden (4.9)\n"),
    (VRTC_H, "3.8: the direct resolve's pipeline",
     "  VkPipelineLayout resolve_copy_pipeline_layout_ = VK_NULL_HANDLE;\n",
     "  VkPipelineLayout resolve_copy_pipeline_layout_ = VK_NULL_HANDLE;\n"
     "  // DAO/Warden (3.8): a resolve copy straight from a single-sampled k_8_8_8_8\n"
     "  // render target (DAORecomp's dao_resolve_direct), without the render target's\n"
     "  // dump to edram_buffer_ first - one full-image copy less per resolve.\n"
     "  VkPipelineLayout dao_resolve_direct_pipeline_layout_ = VK_NULL_HANDLE;\n"
     "  VkPipeline dao_resolve_direct_pipeline_ = VK_NULL_HANDLE;\n"),
    (VRTC_H, "3.8: the direct resolve's source check",
     "  void DumpRenderTargets(uint32_t dump_base, uint32_t dump_row_length_used,\n"
     "                         uint32_t dump_rows, uint32_t dump_pitch,\n"
     "                         bool native_layout);\n",
     "  void DumpRenderTargets(uint32_t dump_base, uint32_t dump_row_length_used,\n"
     "                         uint32_t dump_rows, uint32_t dump_pitch,\n"
     "                         bool native_layout);\n"
     "  // DAO/Warden (3.8): the one render target a plain 32bpp copy can be done straight\n"
     "  // from - single-sampled k_8_8_8_8, owning the whole resolved area at the resolve's\n"
     "  // pitch, to an unscaled k_8_8_8_8 destination - or null (DAORecomp's\n"
     "  // D3D12RenderTargetCache::GetResolvedArea, without its texture writes).\n"
     "  VulkanRenderTarget* DaoDirectResolveSource(\n"
     "      const draw_util::ResolveInfo& resolve_info,\n"
     "      draw_util::ResolveCopyShaderIndex copy_shader, bool copy_dest_scaled,\n"
     "      uint32_t dump_base, uint32_t dump_row_length_used, uint32_t dump_rows,\n"
     "      uint32_t dump_pitch);\n"),
    (VRTC_C, "3.8: the direct resolve's shader",
     "#include \"xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_full_8bpp_scaled_cs.h\"\n"
     "}  // namespace shaders\n",
     "#include \"xenia/gpu/shaders/bytecode/vulkan_spirv/resolve_full_8bpp_scaled_cs.h\"\n"
     "// DAO/Warden (3.8): DAORecomp's resolve straight from a render target.\n"
     "#include \"xenia/gpu/shaders/bytecode/vulkan_spirv/dao_resolve_direct_cs.h\"\n"
     "}  // namespace shaders\n"),
    (VRTC_C, "3.8: the direct resolve's pipeline created",
     "    resolve_copy_pipelines_[i] = resolve_copy_pipeline;\n"
     "  }\n"
     "\n"
     "  // TODO(Triang3l): All paths (FSI).\n",
     "    resolve_copy_pipelines_[i] = resolve_copy_pipeline;\n"
     "  }\n"
     "\n"
     "  // DAO/Warden (3.8): the direct resolve - the render target as a sampled image in\n"
     "  // set 0 (as the dumps bind it), the destination in set 1 and the copy's push\n"
     "  // constants, as the copy pipelines have them. Unscaled only. Without it every\n"
     "  // resolve takes the dump as before.\n"
     "  if (!draw_resolution_scaled && path_ == Path::kHostRenderTargets &&\n"
     "      !std::getenv(\"DAO_HACK_NO_RESOLVE_DIRECT\")) {\n"
     "    VkDescriptorSetLayout dao_direct_set_layouts[2] = {\n"
     "        descriptor_set_layout_sampled_image_,\n"
     "        resolve_copy_descriptor_set_layouts[kResolveCopyDescriptorSetDest]};\n"
     "    VkPipelineLayoutCreateInfo dao_direct_layout_create_info =\n"
     "        resolve_copy_pipeline_layout_create_info;\n"
     "    dao_direct_layout_create_info.setLayoutCount = 2;\n"
     "    dao_direct_layout_create_info.pSetLayouts = dao_direct_set_layouts;\n"
     "    resolve_copy_push_constant_range.size =\n"
     "        sizeof(draw_util::ResolveCopyShaderConstants);\n"
     "    dao_direct_layout_create_info.pPushConstantRanges =\n"
     "        &resolve_copy_push_constant_range;\n"
     "    if (dfn.vkCreatePipelineLayout(device, &dao_direct_layout_create_info, nullptr,\n"
     "                                   &dao_resolve_direct_pipeline_layout_) ==\n"
     "        VK_SUCCESS) {\n"
     "      dao_resolve_direct_pipeline_ = ui::vulkan::util::CreateComputePipeline(\n"
     "          vulkan_device, dao_resolve_direct_pipeline_layout_,\n"
     "          shaders::dao_resolve_direct_cs, sizeof(shaders::dao_resolve_direct_cs));\n"
     "    }\n"
     "    if (dao_resolve_direct_pipeline_ == VK_NULL_HANDLE) {\n"
     "      XELOGE(\n"
     "          \"VulkanRenderTargetCache: Failed to create the direct resolve copy \"\n"
     "          \"pipeline; resolves dump the render targets as before\");\n"
     "    } else {\n"
     "      XELOGI(\"VulkanRenderTargetCache: plain 8888 resolves straight from the \"\n"
     "             \"render target (DAO/Warden 3.8)\");\n"
     "    }\n"
     "  }\n"
     "\n"
     "  // TODO(Triang3l): All paths (FSI).\n"),
    (VRTC_C, "3.8: the direct resolve's pipeline destroyed",
     "  for (VkPipeline& resolve_copy_pipeline : resolve_copy_pipelines_) {\n"
     "    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,\n"
     "                                           resolve_copy_pipeline);\n"
     "  }\n",
     "  for (VkPipeline& resolve_copy_pipeline : resolve_copy_pipelines_) {\n"
     "    ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,\n"
     "                                           resolve_copy_pipeline);\n"
     "  }\n"
     "  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,\n"
     "                                         dao_resolve_direct_pipeline_);\n"
     "  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,\n"
     "                                         dao_resolve_direct_pipeline_layout_);\n"),
    (VRTC_C, "3.8: a plain 8888 resolve straight from its render target",
     "  if (resolve_info.copy_dest_extent_length) {\n"
     "    if (GetPath() == Path::kHostRenderTargets) {\n"
     "      // Dump the current contents of the render targets owning the affected\n"
     "      // range to edram_buffer_.\n"
     "      // TODO(Triang3l): Direct host render target -> shared memory resolve\n"
     "      // shaders for non-converting cases.\n"
     "      DumpRenderTargets(dump_base, dump_row_length_used, dump_rows, dump_pitch,\n"
     "                        copy_native);\n"
     "    }\n"
     "    bool copy_dest_scaled = draw_resolution_scaled && !copy_native;\n"
     "\n"
     "    draw_util::ResolveCopyShaderConstants copy_shader_constants;\n"
     "    uint32_t copy_group_count_x, copy_group_count_y;\n"
     "    draw_util::ResolveCopyShaderIndex copy_shader = resolve_info.GetCopyShader(\n"
     "        copy_native ? 1 : draw_resolution_scale_x(),\n"
     "        copy_native ? 1 : draw_resolution_scale_y(), copy_shader_constants,\n"
     "        copy_group_count_x, copy_group_count_y);\n"
     "    assert_true(copy_group_count_x && copy_group_count_y);\n",
     "  if (resolve_info.copy_dest_extent_length) {\n"
     "    bool copy_dest_scaled = draw_resolution_scaled && !copy_native;\n"
     "\n"
     "    draw_util::ResolveCopyShaderConstants copy_shader_constants;\n"
     "    uint32_t copy_group_count_x, copy_group_count_y;\n"
     "    draw_util::ResolveCopyShaderIndex copy_shader = resolve_info.GetCopyShader(\n"
     "        copy_native ? 1 : draw_resolution_scale_x(),\n"
     "        copy_native ? 1 : draw_resolution_scale_y(), copy_shader_constants,\n"
     "        copy_group_count_x, copy_group_count_y);\n"
     "    assert_true(copy_group_count_x && copy_group_count_y);\n"
     "    // DAO/Warden (3.8): a plain 32bpp copy of one single-sampled k_8_8_8_8 render\n"
     "    // target is done straight from it, not from its dump to edram_buffer_ - the\n"
     "    // same bytes (dao_resolve_direct), one full-image copy less. Otherwise the\n"
     "    // render targets owning the affected range are dumped as before.\n"
     "    VulkanRenderTarget* dao_direct_source = nullptr;\n"
     "    if (GetPath() == Path::kHostRenderTargets) {\n"
     "      dao_direct_source = DaoDirectResolveSource(\n"
     "          resolve_info, copy_shader, copy_dest_scaled, dump_base,\n"
     "          dump_row_length_used, dump_rows, dump_pitch);\n"
     "      if (!dao_direct_source) {\n"
     "        DumpRenderTargets(dump_base, dump_row_length_used, dump_rows, dump_pitch,\n"
     "                          copy_native);\n"
     "      }\n"
     "      static uint32_t dao_resolves = 0, dao_direct_resolves = 0;\n"
     "      dao_direct_resolves += dao_direct_source != nullptr;\n"
     "      if (++dao_resolves % 600 == 0) {\n"
     "        XELOGI(\"Resolve: {} of the last 600 straight from the render target\",\n"
     "               dao_direct_resolves);\n"
     "        dao_direct_resolves = 0;\n"
     "      }\n"
     "    }\n"),
    (VRTC_C, "3.8: the direct resolve bound and dispatched",
     "          UseEdramBuffer(EdramBufferUsage::kComputeRead);\n"
     "          // Fully native resolves use the unscaled shader variant with the\n"
     "          // full push constant layout.\n"
     "          VkPipelineLayout copy_pipeline_layout =\n"
     "              copy_native ? resolve_copy_native_pipeline_layout_\n"
     "                          : resolve_copy_pipeline_layout_;\n"
     "          command_processor_.BindExternalComputePipeline(\n"
     "              copy_native ? resolve_copy_native_pipelines_[size_t(copy_shader)]\n"
     "                          : resolve_copy_pipelines_[size_t(copy_shader)]);\n"
     "          VkDescriptorSet descriptor_sets[kResolveCopyDescriptorSetCount] = {};\n"
     "          descriptor_sets[kResolveCopyDescriptorSetEdram] =\n"
     "              edram_storage_buffer_descriptor_set_;\n",
     "          // Fully native resolves use the unscaled shader variant with the\n"
     "          // full push constant layout.\n"
     "          VkPipelineLayout copy_pipeline_layout =\n"
     "              copy_native ? resolve_copy_native_pipeline_layout_\n"
     "                          : resolve_copy_pipeline_layout_;\n"
     "          VkDescriptorSet descriptor_sets[kResolveCopyDescriptorSetCount] = {};\n"
     "          if (dao_direct_source) {\n"
     "            // DAO/Warden (3.8): the render target itself in set 0, read as the\n"
     "            // dumps read it, with the EDRAM base relative to its own.\n"
     "            TransitionRenderTarget(*dao_direct_source,\n"
     "                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,\n"
     "                                   VK_ACCESS_SHADER_READ_BIT,\n"
     "                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);\n"
     "            copy_pipeline_layout = dao_resolve_direct_pipeline_layout_;\n"
     "            command_processor_.BindExternalComputePipeline(\n"
     "                dao_resolve_direct_pipeline_);\n"
     "            descriptor_sets[0] = dao_direct_source->GetDescriptorSetTransferSource();\n"
     "            copy_shader_constants.dest_relative.edram_info.base_tiles =\n"
     "                resolve_info.color_edram_info.base_tiles -\n"
     "                dao_direct_source->key().base_tiles;\n"
     "          } else {\n"
     "            UseEdramBuffer(EdramBufferUsage::kComputeRead);\n"
     "            command_processor_.BindExternalComputePipeline(\n"
     "                copy_native ? resolve_copy_native_pipelines_[size_t(copy_shader)]\n"
     "                            : resolve_copy_pipelines_[size_t(copy_shader)]);\n"
     "            descriptor_sets[kResolveCopyDescriptorSetEdram] =\n"
     "                edram_storage_buffer_descriptor_set_;\n"
     "          }\n"),
    (VRTC_C, "3.8: the direct resolve's source check",
     "void VulkanRenderTargetCache::DumpRenderTargets(uint32_t dump_base,\n",
     "// DAO/Warden (3.8): DAORecomp's D3D12RenderTargetCache::GetResolvedArea without its\n"
     "// texture writes - the render target a plain 32bpp copy can be done straight from.\n"
     "VulkanRenderTargetCache::VulkanRenderTarget*\n"
     "VulkanRenderTargetCache::DaoDirectResolveSource(\n"
     "    const draw_util::ResolveInfo& resolve_info,\n"
     "    draw_util::ResolveCopyShaderIndex copy_shader, bool copy_dest_scaled,\n"
     "    uint32_t dump_base, uint32_t dump_row_length_used, uint32_t dump_rows,\n"
     "    uint32_t dump_pitch) {\n"
     "  if (dao_resolve_direct_pipeline_ == VK_NULL_HANDLE || copy_dest_scaled ||\n"
     "      GetPath() != Path::kHostRenderTargets ||\n"
     "      copy_shader != draw_util::ResolveCopyShaderIndex::kFast32bpp1x2xMSAA ||\n"
     "      resolve_info.IsCopyingDepth()) {\n"
     "    return nullptr;\n"
     "  }\n"
     "  const draw_util::ResolveEdramInfo edram_info = resolve_info.color_edram_info;\n"
     "  const reg::RB_COPY_DEST_INFO dest_info = resolve_info.copy_dest_info;\n"
     "  if (edram_info.msaa_samples != xenos::MsaaSamples::k1X ||\n"
     "      xenos::ColorRenderTargetFormat(edram_info.format) !=\n"
     "          xenos::ColorRenderTargetFormat::k_8_8_8_8 ||\n"
     "      dest_info.copy_dest_array ||\n"
     "      xenos::ColorFormat(dest_info.copy_dest_format) !=\n"
     "          xenos::ColorFormat::k_8_8_8_8 ||\n"
     "      uint32_t(dest_info.copy_dest_endian) > uint32_t(xenos::Endian::k16in32)) {\n"
     "    return nullptr;\n"
     "  }\n"
     "  // One render target owns the whole area, without EDRAM wrapping.\n"
     "  GetResolveCopyRectanglesToDump(dump_base, dump_row_length_used, dump_rows,\n"
     "                                 dump_pitch, dump_rectangles_);\n"
     "  if (dump_rectangles_.size() != 1) {\n"
     "    return nullptr;\n"
     "  }\n"
     "  const ResolveCopyDumpRectangle& rectangle = dump_rectangles_.front();\n"
     "  if (rectangle.row_first || rectangle.rows != dump_rows ||\n"
     "      rectangle.row_first_start ||\n"
     "      rectangle.row_last_end != dump_row_length_used ||\n"
     "      dump_base + (dump_rows - 1) * dump_pitch + dump_row_length_used >\n"
     "          xenos::kEdramTileCount) {\n"
     "    return nullptr;\n"
     "  }\n"
     "  auto& render_target = *static_cast<VulkanRenderTarget*>(rectangle.render_target);\n"
     "  const RenderTargetKey key = render_target.key();\n"
     "  if (key.is_depth || key.msaa_samples != xenos::MsaaSamples::k1X ||\n"
     "      key.GetColorFormat() != xenos::ColorRenderTargetFormat::k_8_8_8_8 ||\n"
     "      key.next_period || key.pitch_tiles_at_32bpp != edram_info.pitch_tiles ||\n"
     "      key.base_tiles > edram_info.base_tiles) {\n"
     "    return nullptr;\n"
     "  }\n"
     "  // The area in the render target, laid out like the EDRAM from its base.\n"
     "  const uint32_t pitch = edram_info.pitch_tiles;\n"
     "  const uint32_t base_offset = edram_info.base_tiles - key.base_tiles;\n"
     "  const uint32_t source_x =\n"
     "      base_offset % pitch * xenos::kEdramTileWidthSamples +\n"
     "      (resolve_info.coordinate_info.edram_offset_x_div_8 << 3);\n"
     "  const uint32_t source_y =\n"
     "      base_offset / pitch * xenos::kEdramTileHeightSamples +\n"
     "      (resolve_info.coordinate_info.edram_offset_y_div_8 << 3);\n"
     "  const uint32_t width = resolve_info.coordinate_info.width_div_8 << 3;\n"
     "  const uint32_t height = resolve_info.height_div_8 << 3;\n"
     "  if (source_x + width > pitch * xenos::kEdramTileWidthSamples ||\n"
     "      source_x + width > key.GetWidth() ||\n"
     "      source_y + height >\n"
     "          GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples)) {\n"
     "    return nullptr;\n"
     "  }\n"
     "  return &render_target;\n"
     "}\n"
     "\n"
     "void VulkanRenderTargetCache::DumpRenderTargets(uint32_t dump_base,\n"),
    (PM4_H, "BUG-047: the failed packet's hook declared",
     "#if !defined(NDEBUG)\n"
     "#define XE_ENABLE_PM4_DISASM 1\n"
     "#endif\n",
     "#if !defined(NDEBUG)\n"
     "#define XE_ENABLE_PM4_DISASM 1\n"
     "#endif\n"
     "\n"
     "// DAO/Warden (BUG-047): app/src/main/cpp/warden_gpu.cc logs the words around a packet\n"
     "// the command processor cannot execute, the stretch it was executing and the last\n"
     "// kicks, before the trap.\n"
     "extern \"C\" void warden_gpu_packet_failed(bool primary, uint32_t buffer,\n"
     "                                         uint32_t buffer_dwords, uint32_t read_index,\n"
     "                                         uint32_t write_index, uint32_t position);\n"),
    (PM4_H, "BUG-047: a failed packet in the primary ring logged",
     "      XELOGE(\"**** PRIMARY RINGBUFFER: Failed to execute packet.\");\n"
     "      assert_always();\n",
     "      XELOGE(\"**** PRIMARY RINGBUFFER: Failed to execute packet.\");\n"
     "      warden_gpu_packet_failed(true, primary_buffer_ptr_, primary_buffer_size_ / 4,\n"
     "                               read_index, write_index,\n"
     "                               uint32_t(reader_.read_offset() / 4));  // DAO/Warden\n"
     "      assert_always();\n"),
    (PM4_H, "BUG-047: a failed packet in an indirect buffer logged",
     "        XELOGE(\"**** INDIRECT RINGBUFFER: Failed to execute packet.\");\n"
     "        assert_always();\n",
     "        XELOGE(\"**** INDIRECT RINGBUFFER: Failed to execute packet.\");\n"
     "        warden_gpu_packet_failed(false, ptr, count, 0, count,\n"
     "                                 uint32_t(reader_.read_offset() / 4));  // DAO/Warden\n"
     "        assert_always();\n"),
    (VCP_C, "BUG-049: the host viewport and scissor into the draw's record",
     "  SetScissor(scissor_rect);\n",
     "  SetScissor(scissor_rect);\n"
     "  // DAO/Warden (BUG-049): the host viewport and scissor into the draw's record, for\n"
     "  // the draws logged before a dumped resolve - where each landed.\n"
     "  if (dao_trace::recent_draw_count) {\n"
     "    dao_trace::RecentDraw& dao_draw =\n"
     "        dao_trace::recent_draws[(dao_trace::recent_draw_count - 1) % 64];\n"
     "    dao_draw.host_viewport[0] = viewport.x;\n"
     "    dao_draw.host_viewport[1] = viewport.y;\n"
     "    dao_draw.host_viewport[2] = viewport.width;\n"
     "    dao_draw.host_viewport[3] = viewport.height;\n"
     "    dao_draw.host_scissor[0] = scissor_rect.offset.x;\n"
     "    dao_draw.host_scissor[1] = scissor_rect.offset.y;\n"
     "    dao_draw.host_scissor[2] = int32_t(scissor_rect.extent.width);\n"
     "    dao_draw.host_scissor[3] = int32_t(scissor_rect.extent.height);\n"
     "  }\n"),
    (TC_C, "BUG-049: a watched texture's loads in a batch logged",
     "      texture->LogAction(\"Loaded\");\n",
     "      texture->LogAction(\"Loaded\");\n"
     "      // DAO/Warden (BUG-049): the texture at DAO_HACK_RESOLVE_DUMP_ADDRESS (re)loaded\n"
     "      // in a batch, the first 64.\n"
     "      {\n"
     "        static const char* dao_watch_text =\n"
     "            std::getenv(\"DAO_HACK_RESOLVE_DUMP_ADDRESS\");\n"
     "        static const uint32_t dao_watch_page =\n"
     "            dao_watch_text\n"
     "                ? (uint32_t(std::strtoul(dao_watch_text, nullptr, 16)) >> 12)\n"
     "                : 0;\n"
     "        static dao_trace::EventGate dao_watch_gate;\n"
     "        const TextureKey& dao_key = texture->key();\n"
     "        if (dao_watch_page && dao_key.base_page == dao_watch_page &&\n"
     "            dao_watch_gate.Pass()) {\n"
     "          XELOGI(\"FT tex @{:08X}+{:X} fmt {} {}x{}: loaded in a batch ({} so far)\",\n"
     "                 dao_key.base_page << 12, texture->GetGuestBaseSize(),\n"
     "                 uint32_t(dao_key.format), dao_key.GetWidth(), dao_key.GetHeight(),\n"
     "                 dao_watch_gate.count);\n"
     "        }\n"
     "        if (dao_watch_page && dao_key.base_page == dao_watch_page) {\n"
     "          dao_trace::RecordTextureEvent(\n"
     "              texture, 1,\n"
     "              dao_trace::TextureKeyBits(dao_key.tiled, uint32_t(dao_key.endianness),\n"
     "                                        dao_key.signed_separate, dao_key.scaled_resolve,\n"
     "                                        dao_key.pitch, uint32_t(dao_key.format)));\n"
     "        }\n"
     "      }\n"),
    (TC_C, "BUG-049: a watched texture's creation recorded",
     "  texture->LogAction(\"Created\");\n",
     "  texture->LogAction(\"Created\");\n"
     "  // DAO/Warden (BUG-049): a texture at DAO_HACK_RESOLVE_DUMP_ADDRESS created.\n"
     "  {\n"
     "    static const char* dao_watch_text = std::getenv(\"DAO_HACK_RESOLVE_DUMP_ADDRESS\");\n"
     "    static const uint32_t dao_watch_page =\n"
     "        dao_watch_text ? (uint32_t(std::strtoul(dao_watch_text, nullptr, 16)) >> 12)\n"
     "                       : 0;\n"
     "    if (dao_watch_page && key.base_page == dao_watch_page) {\n"
     "      dao_trace::RecordTextureEvent(\n"
     "          texture, 4,\n"
     "          dao_trace::TextureKeyBits(key.tiled, uint32_t(key.endianness),\n"
     "                                    key.signed_separate, key.scaled_resolve, key.pitch,\n"
     "                                    uint32_t(key.format)));\n"
     "    }\n"
     "  }\n"),
    (TC_C, "BUG-049: a watched texture's watch firing logged",
     "  texture.WatchCallback(global_lock, argument != 0);\n",
     "  texture.WatchCallback(global_lock, argument != 0);\n"
     "  // DAO/Warden (BUG-049): the texture at DAO_HACK_RESOLVE_DUMP_ADDRESS outdated by a\n"
     "  // write, the first 64 - a resolve's or the CPU's.\n"
     "  {\n"
     "    static const char* dao_watch_text = std::getenv(\"DAO_HACK_RESOLVE_DUMP_ADDRESS\");\n"
     "    static const uint32_t dao_watch_page =\n"
     "        dao_watch_text ? (uint32_t(std::strtoul(dao_watch_text, nullptr, 16)) >> 12)\n"
     "                       : 0;\n"
     "    static dao_trace::EventGate dao_watch_gate;\n"
     "    const TextureKey& dao_key = texture.key();\n"
     "    if (dao_watch_page && dao_key.base_page == dao_watch_page &&\n"
     "        dao_watch_gate.Pass()) {\n"
     "      XELOGI(\"FT tex @{:08X} fmt {} {}x{} {}: outdated by a {} write ({} so far)\",\n"
     "             dao_key.base_page << 12, uint32_t(dao_key.format), dao_key.GetWidth(),\n"
     "             dao_key.GetHeight(), argument ? \"mips\" : \"base\",\n"
     "             invalidated_by_gpu ? \"GPU\" : \"CPU\", dao_watch_gate.count);\n"
     "    }\n"
     "    if (dao_watch_page && dao_key.base_page == dao_watch_page) {\n"
     "      dao_trace::RecordTextureEvent(\n"
     "          &texture, invalidated_by_gpu ? 2 : 3,\n"
     "          dao_trace::TextureKeyBits(dao_key.tiled, uint32_t(dao_key.endianness),\n"
     "                                    dao_key.signed_separate, dao_key.scaled_resolve,\n"
     "                                    dao_key.pitch, uint32_t(dao_key.format)));\n"
     "    }\n"
     "  }\n"),
    (VCP_C, "BUG-049 experiment: DAO_HACK_RELOAD_ADDRESS - the scene copy reloaded",
     "  texture_cache_->RequestTextures(used_texture_mask);\n",
     "  // DAO/Warden (BUG-049) experiment: DAO_HACK_RELOAD_ADDRESS=<hex> - before every\n"
     "  // second draw of DAO_HACK_LOG_BINDINGS_PS's pixel shader, the 1280x736 scene copy\n"
     "  // there is marked written by the GPU, so its texture loads again from the shared\n"
     "  // memory: two saves in one run, the first as it is, the second reloaded.\n"
     "  {\n"
     "    static const char* dao_ps_text = std::getenv(\"DAO_HACK_LOG_BINDINGS_PS\");\n"
     "    static const uint64_t dao_ps =\n"
     "        dao_ps_text ? uint64_t(std::strtoull(dao_ps_text, nullptr, 16)) : 0;\n"
     "    static const char* dao_reload_text = std::getenv(\"DAO_HACK_RELOAD_ADDRESS\");\n"
     "    static const uint32_t dao_reload =\n"
     "        dao_reload_text ? uint32_t(std::strtoul(dao_reload_text, nullptr, 16)) : 0;\n"
     "    static uint32_t dao_ps_draws = 0;\n"
     "    if (dao_ps && pixel_shader && pixel_shader->ucode_data_hash() == dao_ps) {\n"
     "      ++dao_ps_draws;\n"
     "      dao_trace::log_binding_draws = dao_ps_draws;\n"
     "      if (dao_reload && dao_ps_draws % 2 == 0) {\n"
     "        texture_cache_->MarkRangeAsResolved(dao_reload, 0x398000, false);\n"
     "        XELOGW(\"GPU: experiment - {:08X}+398000 marked written before draw {} of the \"\n"
     "               \"pixel shader\",\n"
     "               dao_reload, dao_ps_draws);\n"
     "      } else {\n"
     "        XELOGW(\"GPU: draw {} of the pixel shader, as it is\", dao_ps_draws);\n"
     "      }\n"
     "    }\n"
     "  }\n"
     "  texture_cache_->RequestTextures(used_texture_mask);\n"),
    (DFT_H, "BUG-049: chrono for the event gate",
     "#include <atomic>\n",
     "#include <atomic>\n"
     "#include <chrono>  // DAO/Warden (BUG-049)\n"),
    (SS_H, "10.3: the pipeline storage read from its start",
     "    // Read pipeline descriptions.\n",
     "    // DAO/Warden (10.3): an \"a+\" stream starts at the end on Android's bionic (BSD\n"
     "    // stdio), not at the start as glibc's and MSVC's do - the header read found\n"
     "    // nothing, and the storage was emptied at every start.\n"
     "    xe::filesystem::Seek(pipeline_storage_file_, 0, SEEK_SET);\n"
     "    // Read pipeline descriptions.\n"),
    (SS_H, "10.3: the shader storage read from its start",
     "    // Load shaders from storage.\n",
     "    // DAO/Warden (10.3): from the start, as above.\n"
     "    xe::filesystem::Seek(shader_storage_file_, 0, SEEK_SET);\n"
     "    // Load shaders from storage.\n"),
    (VPC_H, "10.3: the storage flushed at every submission",
     "  void EndSubmission();\n",
     "  void EndSubmission();\n"
     "  // DAO/Warden (10.3): the shader and pipeline storage flushed, and the driver's\n"
     "  // pipeline cache saved while new pipelines appear - EndSubmission flushes, but this\n"
     "  // command processor calls it only for a pipeline not yet created, and the process\n"
     "  // is killed, never shut down, so the cache was never saved.\n"
     "  void DaoFlushStorage();\n"),
    (VPC_C, "BUG-091: includes for the cache save's thread and the creation timing",
     "#include \"xenia/base/xxhash.h\"\n",
     "#include \"xenia/base/xxhash.h\"\n"
     "\n"
     "#include <atomic>   // DAO/Warden (BUG-091)\n"
     "#include <chrono>   // DAO/Warden (BUG-091)\n"
     "#include <thread>   // DAO/Warden (BUG-091)\n"),
    (VPC_C, "BUG-091: a pipeline's creation timed, a line for each slow one",
     "  VkPipeline pipeline;\n"
     "  VkResult result = dfn.vkCreateGraphicsPipelines(\n"
     "      device, vk_pipeline_cache_, 1, &pipeline_create_info, nullptr, &pipeline);\n",
     "  VkPipeline pipeline;\n"
     "  // DAO/Warden (BUG-091): pipelines are created when first drawn with (BUG-049,\n"
     "  // BUG-060), on this thread: each that takes a while is a hitch, so it is logged.\n"
     "  const auto dao_create_started = std::chrono::steady_clock::now();\n"
     "  VkResult result = dfn.vkCreateGraphicsPipelines(\n"
     "      device, vk_pipeline_cache_, 1, &pipeline_create_info, nullptr, &pipeline);\n"
     "  {\n"
     "    const double dao_create_ms = std::chrono::duration<double, std::milli>(\n"
     "                                     std::chrono::steady_clock::now() -\n"
     "                                     dao_create_started)\n"
     "                                     .count();\n"
     "    static double dao_create_total_ms = 0;\n"
     "    static uint32_t dao_create_count = 0;\n"
     "    dao_create_total_ms += dao_create_ms;\n"
     "    ++dao_create_count;\n"
     "    if (dao_create_ms >= 3.0) {\n"
     "      XELOGI(\"Pipeline created in {:.1f} ms: VS {:016X} PS {:016X} ({} so far, {:.0f} \"\n"
     "             \"ms in all)\",\n"
     "             dao_create_ms,\n"
     "             creation_arguments.vertex_shader->shader().ucode_data_hash(),\n"
     "             creation_arguments.pixel_shader\n"
     "                 ? creation_arguments.pixel_shader->shader().ucode_data_hash()\n"
     "                 : 0,\n"
     "             dao_create_count, dao_create_total_ms);\n"
     "    }\n"
     "  }\n"),
    (VPC_C, "10.3: the storage flushed, the driver's pipeline cache saved",
     "void VulkanPipelineCache::ShutdownShaderStorage() {\n",
     "void VulkanPipelineCache::DaoFlushStorage() {\n"
     "  if (shader_storage_file_flush_needed_ || pipeline_storage_file_flush_needed_) {\n"
     "    storage_writer_.RequestFlush(shader_storage_file_flush_needed_,\n"
     "                                 pipeline_storage_file_flush_needed_);\n"
     "    shader_storage_file_flush_needed_ = false;\n"
     "    pipeline_storage_file_flush_needed_ = false;\n"
     "  }\n"
     "  // The driver's cache every 30 s while the pipeline count grows - read and written\n"
     "  // on a thread of its own (BUG-091: on this thread, 33 MB copied out of the driver\n"
     "  // and written to storage between two submissions stalled the game when a\n"
     "  // conversation's first pipelines appeared), and written only when the driver's\n"
     "  // data changed (pipelines from the storage are hits in it: no change), through a\n"
     "  // temporary file, so a process killed mid-write leaves the last whole one.\n"
     "  static int64_t dao_saved_ms = 0;\n"
     "  static size_t dao_saved_pipelines = 0;\n"
     "  static std::atomic<bool> dao_saving{false};\n"
     "  if (vk_pipeline_cache_ == VK_NULL_HANDLE || vk_pipeline_cache_path_.empty() ||\n"
     "      pipelines_.size() == dao_saved_pipelines) {\n"
     "    return;\n"
     "  }\n"
     "  const int64_t now_ms = int64_t(xe::Clock::QueryHostUptimeMillis());\n"
     "  if (dao_saved_ms && now_ms - dao_saved_ms < 30000) {\n"
     "    return;\n"
     "  }\n"
     "  if (dao_saving.exchange(true)) {\n"
     "    return;  // the last save still running\n"
     "  }\n"
     "  dao_saved_ms = now_ms ? now_ms : 1;\n"
     "  dao_saved_pipelines = pipelines_.size();\n"
     "  const ui::vulkan::VulkanDevice* const vulkan_device =\n"
     "      command_processor_.GetVulkanDevice();\n"
     "  const ui::vulkan::VulkanDevice::Functions* const dfn = &vulkan_device->functions();\n"
     "  const VkDevice device = vulkan_device->device();\n"
     "  const VkPipelineCache cache = vk_pipeline_cache_;\n"
     "  const std::filesystem::path path = vk_pipeline_cache_path_;\n"
     "  const size_t pipelines = dao_saved_pipelines;\n"
     "  std::thread([dfn, device, cache, path, pipelines] {\n"
     "    // What the file holds: its size and hash, from the file itself the first time.\n"
     "    static size_t dao_file_size = SIZE_MAX;\n"
     "    static uint64_t dao_file_hash = 0;\n"
     "    const auto started = std::chrono::steady_clock::now();\n"
     "    if (dao_file_size == SIZE_MAX) {\n"
     "      dao_file_size = 0;\n"
     "      if (FILE* file = xe::filesystem::OpenFile(path, \"rb\")) {\n"
     "        std::vector<uint8_t> old;\n"
     "        xe::filesystem::Seek(file, 0, SEEK_END);\n"
     "        const int64_t size = xe::filesystem::Tell(file);\n"
     "        if (size > 0) {\n"
     "          old.resize(size_t(size));\n"
     "          xe::filesystem::Seek(file, 0, SEEK_SET);\n"
     "          old.resize(fread(old.data(), 1, old.size(), file));\n"
     "        }\n"
     "        fclose(file);\n"
     "        dao_file_size = old.size();\n"
     "        dao_file_hash = XXH3_64bits(old.data(), old.size());\n"
     "      }\n"
     "    }\n"
     "    size_t cache_size = 0;\n"
     "    std::vector<uint8_t> cache_data;\n"
     "    if (dfn->vkGetPipelineCacheData(device, cache, &cache_size, nullptr) == VK_SUCCESS &&\n"
     "        cache_size) {\n"
     "      cache_data.resize(cache_size);\n"
     "      if (dfn->vkGetPipelineCacheData(device, cache, &cache_size, cache_data.data()) !=\n"
     "          VK_SUCCESS) {\n"
     "        cache_data.clear();\n"
     "      }\n"
     "      cache_data.resize(std::min(cache_size, cache_data.size()));\n"
     "    }\n"
     "    if (!cache_data.empty()) {\n"
     "      const uint64_t hash = XXH3_64bits(cache_data.data(), cache_data.size());\n"
     "      const double ms = std::chrono::duration<double, std::milli>(\n"
     "                            std::chrono::steady_clock::now() - started)\n"
     "                            .count();\n"
     "      if (cache_data.size() == dao_file_size && hash == dao_file_hash) {\n"
     "        XELOGI(\"VkPipelineCache data unchanged ({} bytes, {} pipelines): not written \"\n"
     "               \"({:.0f} ms, on its own thread)\",\n"
     "               cache_data.size(), pipelines, ms);\n"
     "      } else {\n"
     "        std::filesystem::path temporary = path;\n"
     "        temporary += \".new\";\n"
     "        if (FILE* cache_file = xe::filesystem::OpenFile(temporary, \"wb\")) {\n"
     "          const bool written = fwrite(cache_data.data(), 1, cache_data.size(),\n"
     "                                      cache_file) == cache_data.size();\n"
     "          fclose(cache_file);\n"
     "          std::error_code error;\n"
     "          if (written) {\n"
     "            std::filesystem::rename(temporary, path, error);\n"
     "            dao_file_size = cache_data.size();\n"
     "            dao_file_hash = hash;\n"
     "          }\n"
     "          XELOGI(\"Saved {} bytes of VkPipelineCache data, {} pipelines ({:.0f} ms, on \"\n"
     "                 \"its own thread)\",\n"
     "                 cache_data.size(), pipelines,\n"
     "                 std::chrono::duration<double, std::milli>(\n"
     "                     std::chrono::steady_clock::now() - started)\n"
     "                     .count());\n"
     "        }\n"
     "      }\n"
     "    }\n"
     "    dao_saving.store(false);\n"
     "  }).detach();\n"
     "}\n"
     "\n"
     "void VulkanPipelineCache::ShutdownShaderStorage() {\n"),
    (VCP_C, "10.3: the pipeline storage flushed at every submission",
     "  bool is_closing_frame = is_swap && frame_open_;\n",
     "  // DAO/Warden (10.3): the shader and pipeline storage flushed, and the driver's\n"
     "  // pipeline cache saved now and then (vulkan_pipeline_cache.cc).\n"
     "  pipeline_cache_->DaoFlushStorage();\n"
     "  bool is_closing_frame = is_swap && frame_open_;\n"),
    (TU_C, "BUG-049 experiment: the cstdlib include for DAO_HACK_BASE_ONLY_ADDRESS",
     "#include <cstring>\n",
     "#include <cstdlib>  // DAO/Warden (BUG-049)\n"
     "#include <cstring>\n"),
    (TU_C, "BUG-049 experiment: DAO_HACK_BASE_ONLY_ADDRESS - one texture's base level only",
     "  if (mip_max_level != 0) {\n"
     "    if (mip_min_level != 0 && base_page == mip_page) {\n",
     "  // DAO/Warden (BUG-049) experiment: DAO_HACK_BASE_ONLY_ADDRESS=<hex> - a texture at\n"
     "  // that base address is taken as its base level alone, whatever mips its fetch\n"
     "  // constant declares (the scene copy the save thumbnail samples, minified).\n"
     "  {\n"
     "    static const char* dao_text = std::getenv(\"DAO_HACK_BASE_ONLY_ADDRESS\");\n"
     "    static const uint32_t dao_page =\n"
     "        dao_text ? (uint32_t(std::strtoul(dao_text, nullptr, 16)) >> 12) & 0x1FFFF : 0;\n"
     "    if (dao_page && base_page == dao_page) {\n"
     "      mip_min_level = 0;\n"
     "      mip_max_level = 0;\n"
     "    }\n"
     "  }\n"
     "  if (mip_max_level != 0) {\n"
     "    if (mip_min_level != 0 && base_page == mip_page) {\n"),
    (VRTC_C, "BUG-049: a dumped resolve's source and destination logged",
     "  // Copy at 1x1 if all render targets owning the source are native.\n",
     "  // DAO/Warden (BUG-049): the resolves files/hack-resolve-dump dumps - by destination\n"
     "  // pitch - say where they copy from and to; those written from its address too,\n"
     "  // through an event gate.\n"
     "  {\n"
     "    static const char* dao_pitch_text = std::getenv(\"DAO_HACK_RESOLVE_DUMP_PITCH\");\n"
     "    static const char* dao_address_text =\n"
     "        std::getenv(\"DAO_HACK_RESOLVE_DUMP_ADDRESS\");\n"
     "    static const uint32_t dao_pitch =\n"
     "        dao_pitch_text ? uint32_t(std::strtoul(dao_pitch_text, nullptr, 16)) : 0;\n"
     "    static const uint32_t dao_address =\n"
     "        dao_address_text ? uint32_t(std::strtoul(dao_address_text, nullptr, 16)) : 0;\n"
     "    const uint32_t dao_dest_pitch =\n"
     "        register_file().values[XE_GPU_REG_RB_COPY_DEST_PITCH];\n"
     "    static dao_trace::EventGate dao_address_gate;\n"
     "    if ((dao_pitch && dao_dest_pitch == dao_pitch) ||\n"
     "        (dao_address && resolve_info.copy_dest_extent_start == dao_address &&\n"
     "         dao_address_gate.Pass())) {\n"
     "      const draw_util::ResolveEdramInfo dao_edram = resolve_info.IsCopyingDepth()\n"
     "                                                     ? resolve_info.depth_edram_info\n"
     "                                                     : resolve_info.color_edram_info;\n"
     "      XELOGI(\n"
     "          \"GPU: resolve to {:08X}+{:X} (pitch {:08X}, base {:08X}): from EDRAM base {} \"\n"
     "          \"tiles, pitch {} tiles, format {}, {}x MSAA{}, offset ({} {}) size {}x{}, the \"\n"
     "          \"rectangle ({} {})-({} {}) after window offset ({} {}); dest info {:08X}, \"\n"
     "          \"dest coordinates {:08X}, copy control {:08X}\",\n"
     "          resolve_info.copy_dest_extent_start, resolve_info.copy_dest_extent_length,\n"
     "          dao_dest_pitch, resolve_info.copy_dest_base, uint32_t(dao_edram.base_tiles),\n"
     "          uint32_t(dao_edram.pitch_tiles), uint32_t(dao_edram.format),\n"
     "          1u << uint32_t(dao_edram.msaa_samples), dao_edram.is_depth ? \", depth\" : \"\",\n"
     "          resolve_info.coordinate_info.edram_offset_x_div_8 * 8,\n"
     "          resolve_info.coordinate_info.edram_offset_y_div_8 * 8,\n"
     "          resolve_info.coordinate_info.width_div_8 * 8, resolve_info.height_div_8 * 8,\n"
     "          resolve_info.dao_rect[0], resolve_info.dao_rect[1], resolve_info.dao_rect[2],\n"
     "          resolve_info.dao_rect[3], resolve_info.dao_window_offset[0],\n"
     "          resolve_info.dao_window_offset[1], resolve_info.copy_dest_info.value,\n"
     "          resolve_info.copy_dest_coordinate_info.packed,\n"
     "          resolve_info.rb_copy_control.value);\n"
     "    }\n"
     "  }\n"
     "  // Copy at 1x1 if all render targets owning the source are native.\n"),
    # PLAN 9.4 (session 35): FSR 3.1 in the game's frames - DAORecomp's temporal
    # upscaling (gpu/d3d12/dao_upscale*.cc) ported to this backend in
    # app/src/main/cpp/warden_upscale.cc. The class with its hooks is declared here (the
    # runtime does not include the app's headers); its implementation, nested in it, is a
    # friend of the render target caches (their render targets, the transitions it reads
    # them with). The deferred command buffer gets a callback: the FidelityFX SDK records
    # FSR 3.1's dispatch itself, into the real command buffer as the deferred one is
    # executed. The hooks: a draw's viewport (the scene's jitter), after a draw (the image
    # before the UI), a resolve (the scene's depth), the swap (the upscaled frame shown
    # instead of the front buffer).
    ("xenia/gpu/render_target_cache.h", "9.4: FSR 3.1's upscaler reads the render targets",
     "class RenderTargetCache {\n"
     " public:\n",
     "// DAO/Warden (PLAN 9.4): FSR 3.1's upscaling of the game's frames\n"
     "// (app/src/main/cpp/warden_upscale.cc) reads the render targets.\n"
     "class WardenUpscaler;\n"
     "\n"
     "class RenderTargetCache {\n"
     " public:\n"
     "  friend class WardenUpscaler;\n"),
    (VRTC_H, "9.4: FSR 3.1's upscaler reads the render targets and transitions them",
     "class VulkanRenderTargetCache final : public RenderTargetCache {\n"
     " public:\n",
     "class VulkanRenderTargetCache final : public RenderTargetCache {\n"
     " public:\n"
     "  friend class ::xe::gpu::WardenUpscaler;  // DAO/Warden (PLAN 9.4)\n"),
    (VCP_H, "9.4: FSR 3.1's upscaler, its hooks",
     "}  // namespace vulkan\n"
     "}  // namespace gpu\n"
     "}  // namespace xe\n"
     "\n"
     "#endif  // XENIA_GPU_VULKAN_VULKAN_COMMAND_PROCESSOR_H_",
     "}  // namespace vulkan\n"
     "\n"
     "// DAO/Warden (PLAN 9.4): FSR 3.1's upscaling of the game's frames - DAORecomp's\n"
     "// temporal upscaling on this backend (app/src/main/cpp/warden_upscale.cc). Off\n"
     "// unless files/fsr3 asks for it.\n"
     "class WardenUpscaler {\n"
     " public:\n"
     "  static void Initialize(vulkan::VulkanCommandProcessor& command_processor,\n"
     "                         vulkan::VulkanRenderTargetCache& render_target_cache);\n"
     "  static void Shutdown();\n"
     "  // Before a draw's system constants: the scene's draws jittered through\n"
     "  // viewport_info.ndc_offset, and what the upscaler takes from the draw noted.\n"
     "  static void OnDraw(const RegisterFile& regs, draw_util::ViewportInfo& viewport_info,\n"
     "                     reg::RB_DEPTHCONTROL depth_control, uint32_t color_mask,\n"
     "                     uint32_t pixel_shader_texture_mask, uint64_t pixel_shader_hash,\n"
     "                     uint64_t vertex_shader_hash, bool window_offset_in_edram);\n"
     "  // After the draw is recorded.\n"
     "  static void AfterDraw();\n"
     "  // PLAN 9.21: whether the draw is dropped - the game's corner vignette while the\n"
     "  // Corner vignette option says off (with or without FSR 3.1).\n"
     "  static bool SkipDraw(const RegisterFile& regs, uint64_t pixel_shader_hash,\n"
     "                       uint32_t pixel_shader_texture_mask);\n"
     "  // In a resolve, before the render targets are read.\n"
     "  static void OnResolve(const draw_util::ResolveInfo& resolve_info);\n"
     "  // At the swap: the front buffer's view, size and format, the upscaled frame's\n"
     "  // instead when there is one.\n"
     "  static void OnSwap(VkImageView& view, uint32_t& width, uint32_t& height,\n"
     "                     xenos::TextureFormat& format);\n"
     "  // Whether the last swap's frame is FSR 3.1's: upscaled and sharpened, so the\n"
     "  // presenter runs none of its own effects over it (the Upscaling setting).\n"
     "  static bool FrameUpscaled();\n"
     "  // PLAN 9.14: frame generation (app/src/main/cpp/warden_frame_generation.cc).\n"
     "  // Before the guest output is refreshed: whether it gets a generated frame - the\n"
     "  // one between it and the guest output before - shown first; generated_shown, the\n"
     "  // presenter's pacer showing them (Presenter::DaoGeneratedFramesShown).\n"
     "  static bool FrameGenerationReady(uint32_t width, uint32_t height,\n"
     "                                   bool generated_shown);\n"
     "  // In the refresh, the guest output image (and its view) written and released (the\n"
     "  // presenter's internal state): taken by the generator, and the generated frames\n"
     "  // written into generated_images[3] - when the refresh has them - left in the\n"
     "  // same state.\n"
     "  static void OnGuestOutput(VkImage image, VkImageView image_view,\n"
     "                            const VkImage* generated_images, uint32_t width,\n"
     "                            uint32_t height);\n"
     "  class Impl;\n"
     "};\n"
     "\n"
     "}  // namespace gpu\n"
     "}  // namespace xe\n"
     "\n"
     "#endif  // XENIA_GPU_VULKAN_VULKAN_COMMAND_PROCESSOR_H_"),
    (DCB_H, "9.4: a callback run as the buffer is executed",
     "  void CmdVkDispatch(uint32_t group_count_x, uint32_t group_count_y,\n"
     "                     uint32_t group_count_z) {\n",
     "  // DAO/Warden (PLAN 9.4): code run as the buffer is executed, with the real\n"
     "  // command buffer - FSR 3.1's dispatch, which the FidelityFX SDK records itself.\n"
     "  void CmdWardenCallback(void (*callback)(void* data, VkCommandBuffer command_buffer),\n"
     "                         void* data) {\n"
     "    auto& args = *reinterpret_cast<ArgsWardenCallback*>(\n"
     "        WriteCommand(Command::kWardenCallback, sizeof(ArgsWardenCallback)));\n"
     "    args.callback = callback;\n"
     "    args.data = data;\n"
     "  }\n"
     "\n"
     "  void CmdVkDispatch(uint32_t group_count_x, uint32_t group_count_y,\n"
     "                     uint32_t group_count_z) {\n"),
    (DCB_H, "9.4: the callback command",
     "    kVkSetViewport,\n"
     "  };\n",
     "    kVkSetViewport,\n"
     "    kWardenCallback,  // DAO/Warden (PLAN 9.4)\n"
     "  };\n"),
    (DCB_H, "9.4: the callback's arguments",
     "  struct ArgsVkDispatch {\n",
     "  struct ArgsWardenCallback {\n"
     "    void (*callback)(void* data, VkCommandBuffer command_buffer);\n"
     "    void* data;\n"
     "  };\n"
     "\n"
     "  struct ArgsVkDispatch {\n"),
    (DCB_C, "9.4: the callback run",
     "      case Command::kVkDispatch: {\n",
     "      case Command::kWardenCallback: {  // DAO/Warden (PLAN 9.4)\n"
     "        auto& args = *reinterpret_cast<const ArgsWardenCallback*>(stream);\n"
     "        args.callback(args.data, command_buffer);\n"
     "      } break;\n"
     "\n"
     "      case Command::kVkDispatch: {\n"),
    (VCP_C, "9.4: FSR 3.1's upscaler set up with the context",
     "  zpd_counter_index_force_update_ = true;\n"
     "\n"
     "  return true;\n"
     "}\n",
     "  zpd_counter_index_force_update_ = true;\n"
     "\n"
     "  // DAO/Warden (PLAN 9.4): FSR 3.1's upscaling of the game's frames.\n"
     "  WardenUpscaler::Initialize(*this, *render_target_cache_);\n"
     "\n"
     "  return true;\n"
     "}\n"),
    (VCP_C, "9.4: FSR 3.1's upscaler shut down with the context",
     "void VulkanCommandProcessor::ShutdownContext() {\n"
     "  AwaitAllQueueOperationsCompletion();\n",
     "void VulkanCommandProcessor::ShutdownContext() {\n"
     "  AwaitAllQueueOperationsCompletion();\n"
     "  WardenUpscaler::Shutdown();  // DAO/Warden (PLAN 9.4)\n"),
    (VCP_C, "9.4: the scene's draws jittered for FSR 3.1",
     "  draw_util::GetHostViewportInfo(&gviargs, viewport_info);\n",
     "  draw_util::GetHostViewportInfo(&gviargs, viewport_info);\n"
     "  // DAO/Warden (PLAN 9.4): FSR 3.1's jitter for the scene's draws, and what its\n"
     "  // upscaler takes from each draw.\n"
     "  WardenUpscaler::OnDraw(\n"
     "      regs, viewport_info, normalized_depth_control, normalized_color_mask,\n"
     "      pixel_shader ? pixel_shader->GetUsedTextureMaskAfterTranslation() : 0,\n"
     "      pixel_shader ? pixel_shader->ucode_data_hash() : 0,\n"
     "      vertex_shader->ucode_data_hash(), window_offset_tiles != 0);\n"),
    (CP_C, "BUG-117: the GPU's progress noted where the read pointer is written back",
     "    if (read_ptr_writeback_ptr_) {\n"
     "      xe::store_and_swap<uint32_t>(\n"
     "          memory_->TranslatePhysical(read_ptr_writeback_ptr_), read_ptr_index_);\n"
     "    }\n",
     "    if (read_ptr_writeback_ptr_) {\n"
     "      xe::store_and_swap<uint32_t>(\n"
     "          memory_->TranslatePhysical(read_ptr_writeback_ptr_), read_ptr_index_);\n"
     "    }\n"
     "    // DAO/Warden (BUG-117): the title's fence wait sleeps on this with Heat guard's\n"
     "    // GPU wait on (warden_gpu.cc's warden_gpu_progress_note).\n"
     "    warden_gpu_progress_note();\n"),
    (CP_C, "BUG-117: the progress note's declaration",
     "#include \"xenia/gpu/command_processor.h\"\n",
     "#include \"xenia/gpu/command_processor.h\"\n"
     "\n"
     "// DAO/Warden (BUG-117): the GPU's progress, for the title's fence wait (warden_gpu.cc).\n"
     "extern \"C\" void warden_gpu_progress_note();\n"),
    (VCP_C, "9.21: the corner vignette's draw dropped while the option says off",
     "  if (pixel_shader && pixel_shader->memexport_eM_written() != 0 &&\n"
     "      device_properties.fragmentStoresAndAtomics) {\n",
     "  // DAO/Warden (PLAN 9.21): the game's corner vignette, a draw of the HUD's, is\n"
     "  // dropped while the Corner vignette option says off (warden_upscale.cc).\n"
     "  if (pixel_shader &&\n"
     "      WardenUpscaler::SkipDraw(regs, pixel_shader->ucode_data_hash(),\n"
     "                               pixel_shader->GetUsedTextureMaskAfterTranslation())) {\n"
     "    return true;\n"
     "  }\n"
     "  if (pixel_shader && pixel_shader->memexport_eM_written() != 0 &&\n"
     "      device_properties.fragmentStoresAndAtomics) {\n"),
    (VCP_C, "9.4: after a draw, FSR 3.1's image before the UI",
     "  // Invalidate textures in memexported memory and watch for changes.\n",
     "  // DAO/Warden (PLAN 9.4): the image before the UI, after the last full-screen\n"
     "  // pass reading the scene.\n"
     "  WardenUpscaler::AfterDraw();\n"
     "\n"
     "  // Invalidate textures in memexported memory and watch for changes.\n"),
    (VCP_C, "9.4: FSR 3.1's frame shown instead of the front buffer",
     "  VkImageView swap_texture_view = texture_cache_->RequestSwapTexture(\n"
     "      frontbuffer_width_scaled, frontbuffer_height_scaled, frontbuffer_format);\n"
     "  if (swap_texture_view == VK_NULL_HANDLE) {\n"
     "    return;\n"
     "  }\n",
     "  VkImageView swap_texture_view = texture_cache_->RequestSwapTexture(\n"
     "      frontbuffer_width_scaled, frontbuffer_height_scaled, frontbuffer_format);\n"
     "  if (swap_texture_view == VK_NULL_HANDLE) {\n"
     "    return;\n"
     "  }\n"
     "  // DAO/Warden (PLAN 9.4): FSR 3.1's upscaled scene with the UI put back over it,\n"
     "  // at the size it is shown, instead of the front buffer.\n"
     "  WardenUpscaler::OnSwap(swap_texture_view, frontbuffer_width_scaled,\n"
     "                         frontbuffer_height_scaled, frontbuffer_format);\n"),
    (VCP_H, "9.14: the gamma ramp for frame generation's image before the UI",
     "  bool submission_open() const { return submission_open_; }\n",
     "  bool submission_open() const { return submission_open_; }\n"
     "  // DAO/Warden (PLAN 9.14): the 256-entry gamma ramp, which frame generation\n"
     "  // applies to the image before the UI as the swap applies it to the front buffer.\n"
     "  const reg::DC_LUT_30_COLOR* warden_gamma_ramp_256_entry_table() const {\n"
     "    return gamma_ramp_256_entry_table();\n"
     "  }\n"),
    (VCP_C, "9.14: whether the guest output gets a generated frame",
     "  auto aspect = graphics_system_->GetScaledAspectRatio();\n",
     "  auto aspect = graphics_system_->GetScaledAspectRatio();\n"
     "\n"
     "  // DAO/Warden (PLAN 9.14): whether this guest output gets a generated frame - the\n"
     "  // one between it and the guest output before - shown first, by the presenter's\n"
     "  // frame generation pacer.\n"
     "  const bool warden_generated = WardenUpscaler::FrameGenerationReady(\n"
     "      frontbuffer_width_scaled, frontbuffer_height_scaled,\n"
     "      presenter->DaoGeneratedFramesShown());\n"),
    (VCP_C, "9.14: the guest output to frame generation, the generated frame written",
     "        // Need to submit all the commands before giving the image back to the\n"
     "        // presenter so it can submit its own commands for displaying it to the\n"
     "        // queue, and also need to submit the release barrier.\n"
     "        EndSubmission(true);\n"
     "        return true;\n"
     "      });\n",
     "        // DAO/Warden (PLAN 9.14): frame generation takes the guest output as\n"
     "        // written, and writes the generated frames when this refresh has them.\n"
     "        const VkImage warden_generated_images[3] = {\n"
     "            vulkan_context.dao_generated_image(0),\n"
     "            vulkan_context.dao_generated_image(1),\n"
     "            vulkan_context.dao_generated_image(2)};\n"
     "        WardenUpscaler::OnGuestOutput(vulkan_context.image(),\n"
     "                                      vulkan_context.image_view(),\n"
     "                                      warden_generated_images,\n"
     "                                      frontbuffer_width_scaled,\n"
     "                                      frontbuffer_height_scaled);\n"
     "\n"
     "        // Need to submit all the commands before giving the image back to the\n"
     "        // presenter so it can submit its own commands for displaying it to the\n"
     "        // queue, and also need to submit the release barrier.\n"
     "        EndSubmission(true);\n"
     "        return true;\n"
     "      },\n"
     "      // DAO/Warden (PLAN 9.4): FSR 3.1's frame as sharpened - DAORecomp's flag,\n"
     "      // which keeps the presenter's CAS and FSR 1 off it (run over it before, the\n"
     "      // owner found both on at once, 2026-10-30).\n"
     "      WardenUpscaler::FrameUpscaled(), false, warden_generated);\n"),
    (VRTC_C, "9.4: FSR 3.1's scene depth at a resolve",
     "  // Copy at 1x1 if all render targets owning the source are native.\n",
     "  // DAO/Warden (PLAN 9.4): FSR 3.1's upscaler takes the scene's depth here.\n"
     "  WardenUpscaler::OnResolve(resolve_info);\n"
     "\n"
     "  // Copy at 1x1 if all render targets owning the source are native.\n"),
    (VCP_C, "9.14: frames in flight - the switch's declaration",
     "DECLARE_bool(readback_resolve_half_pixel_offset);\n"
     "\n"
     "namespace xe {\n",
     "DECLARE_bool(readback_resolve_half_pixel_offset);\n"
     "\n"
     "// DAO/Warden (PLAN 9.14): how many frames the command processor lets the GPU be\n"
     "// behind - 3 as Xenia has it, or 2 or 1 by files/frames-in-flight\n"
     "// (warden_frame_pacing.cc). Fewer, less from the title's swap to the panel when the\n"
     "// GPU is the limit.\n"
     "extern \"C\" uint32_t warden_frames_in_flight();\n"
     "\n"
     "namespace xe {\n"),
    (VCP_C, "9.14: frames in flight - the frame's wait",
     "          ? closed_frame_submissions_[frame_current_ % kMaxFramesInFlight]\n",
     "          // DAO/Warden (PLAN 9.14): frame N waits for frame N - K's submission,\n"
     "          // K frames in flight (3, Xenia's own, unless files/frames-in-flight\n"
     "          // says 2 or 1); the slot of frame N - K is (N + 3 - K) % 3.\n"
     "          ? closed_frame_submissions_[(frame_current_ + kMaxFramesInFlight -\n"
     "                                       warden_frames_in_flight()) %\n"
     "                                      kMaxFramesInFlight]\n"),
    (LOGGING_C, "11.1: Debugging Mode's log file - getenv",
     "#include <android/log.h>\n",
     "#include <android/log.h>\n"
     "#include <cstdio>   // DAO/Warden (PLAN 11.1)\n"
     "#include <cstdlib>  // DAO/Warden (PLAN 11.1)\n"),
    (LOGGING_C, "11.1: Debugging Mode's log file - a file sink beside logcat",
     "  if (cvars::log_to_logcat) {\n"
     "    logger_->AddLogSink(std::make_unique<AndroidLogSink>(app_name));\n"
     "  }\n",
     "  if (cvars::log_to_logcat) {\n"
     "    logger_->AddLogSink(std::make_unique<AndroidLogSink>(app_name));\n"
     "  }\n"
     "  // DAO/Warden (PLAN 11.1): Debugging Mode - the log to a file in the app's storage\n"
     "  // too (warden_debug_mode.cc names it): an end user cannot reach logcat. Flushed\n"
     "  // after each batch the writer thread takes, so a crash loses little.\n"
     "  if (const char* dao_log_file = std::getenv(\"WARDEN_LOG_FILE\")) {\n"
     "    if (FILE* file = std::fopen(dao_log_file, \"wt\")) {\n"
     "      logger_->AddLogSink(std::make_unique<FileLogSink>(file, true));\n"
     "    }\n"
     "  }\n"),
    (SST_H, "10.9: the fetch constants decoded on the CPU - the layout and the decoder",
     "  static constexpr uint32_t kSpirvMagicToolId = 26;\n",
     "  static constexpr uint32_t kSpirvMagicToolId = 26;\n"
     "\n"
     "  // DAO/Warden (PLAN 10.9): each fetch constant decoded on the CPU once a draw, in the\n"
     "  // fetch constants' buffer after the 32 constants - kFetchDecodedVectors float vectors\n"
     "  // each: the 2D and cube width and height and their reciprocals; the LOD bias and the\n"
     "  // horizontal, vertical and larger gradient exponent biases; the LOD bias with the\n"
     "  // larger gradient bias, the result exponent bias.\n"
     "  static constexpr uint32_t kFetchDecodedVectors = 3;\n"
     "  static void DecodeFetchConstants(const uint32_t* constants, float* decoded);\n"),
    (SST_C, "10.9: the decoded fetch constants in the fetch constants' block",
     "    spv::Id type_fetch_constants =\n"
     "        builder_->makeStructType(id_vector_temp_, \"XeFetchConstants\");\n"
     "    builder_->addMemberName(type_fetch_constants, 0, \"fetch_constants\");\n"
     "    builder_->addMemberDecoration(type_fetch_constants, 0,\n"
     "                                  spv::DecorationOffset, 0);\n",
     "    // DAO/Warden (PLAN 10.9): the constants decoded on the CPU, after them.\n"
     "    id_vector_temp_.push_back(builder_->makeArrayType(\n"
     "        type_float4_, builder_->makeUintConstant(32 * kFetchDecodedVectors),\n"
     "        sizeof(float) * 4));\n"
     "    builder_->addDecoration(id_vector_temp_.back(), spv::DecorationArrayStride,\n"
     "                            sizeof(float) * 4);\n"
     "    spv::Id type_fetch_constants =\n"
     "        builder_->makeStructType(id_vector_temp_, \"XeFetchConstants\");\n"
     "    builder_->addMemberName(type_fetch_constants, 0, \"fetch_constants\");\n"
     "    builder_->addMemberDecoration(type_fetch_constants, 0,\n"
     "                                  spv::DecorationOffset, 0);\n"
     "    builder_->addMemberName(type_fetch_constants, 1, \"fetch_decoded\");\n"
     "    builder_->addMemberDecoration(type_fetch_constants, 1, spv::DecorationOffset,\n"
     "                                  int(sizeof(uint32_t) * 6 * 32));\n"),
    (SST_C, "10.9: the fetch constants' decoder",
     "std::vector<uint8_t> SpirvShaderTranslator::CompleteTranslation() {\n",
     "// DAO/Warden (PLAN 10.9): what the texture fetches decoded from each fetch constant per\n"
     "// invocation, once a draw on the CPU (vulkan_command_processor.cc) - with the same\n"
     "// arithmetic, so the same values: the integers and their sums are exact, and the\n"
     "// reciprocals are rounded correctly where the GPU's division may be off by an ulp\n"
     "// (power-of-two sizes exact either way).\n"
     "void SpirvShaderTranslator::DecodeFetchConstants(const uint32_t* constants,\n"
     "                                                 float* decoded) {\n"
     "  for (uint32_t i = 0; i < 32; ++i) {\n"
     "    const uint32_t* words = constants + 6 * i;\n"
     "    float* out = decoded + 4 * kFetchDecodedVectors * i;\n"
     "    // Word 2: the width and height minus 1, 13 bits each.\n"
     "    const float width = float((words[2] & 0x1FFF) + 1);\n"
     "    const float height = float(((words[2] >> 13) & 0x1FFF) + 1);\n"
     "    out[0] = width;\n"
     "    out[1] = height;\n"
     "    out[2] = 1.0f / width;\n"
     "    out[3] = 1.0f / height;\n"
     "    // Word 4: the LOD bias in 1/32 (bits 12:21), the horizontal and vertical gradient\n"
     "    // exponent biases (bits 22:26, 27:31), all signed.\n"
     "    const int32_t lod_bias = int32_t(words[4] << 10) >> 22;\n"
     "    const int32_t gradient_h = int32_t(words[4] << 5) >> 27;\n"
     "    const int32_t gradient_v = int32_t(words[4]) >> 27;\n"
     "    const float lod = float(lod_bias) * (1.0f / 32.0f);\n"
     "    const float gradient_max = float(gradient_h > gradient_v ? gradient_h : gradient_v);\n"
     "    out[4] = lod;\n"
     "    out[5] = float(gradient_h);\n"
     "    out[6] = float(gradient_v);\n"
     "    out[7] = gradient_max;\n"
     "    out[8] = lod + gradient_max;\n"
     "    // Word 3: the result exponent bias (bits 13:18, signed), as 2 to its power.\n"
     "    int32_t exponent = int32_t(words[3] << 13) >> 26;\n"
     "    float scale = 1.0f;\n"
     "    for (; exponent > 0; --exponent) {\n"
     "      scale *= 2.0f;\n"
     "    }\n"
     "    for (; exponent < 0; ++exponent) {\n"
     "      scale *= 0.5f;\n"
     "    }\n"
     "    out[9] = scale;\n"
     "    out[10] = 0.0f;\n"
     "    out[11] = 0.0f;\n"
     "  }\n"
     "}\n"
     "\n"
     "std::vector<uint8_t> SpirvShaderTranslator::CompleteTranslation() {\n"),
    (SST_FETCH, "10.9: the decoded fetch constant's loads",
     "    uint32_t fetch_constant_index = instr.operands[1].storage_index;\n"
     "    uint32_t fetch_constant_word_0_index = 6 * fetch_constant_index;\n",
     "    uint32_t fetch_constant_index = instr.operands[1].storage_index;\n"
     "    uint32_t fetch_constant_word_0_index = 6 * fetch_constant_index;\n"
     "    // DAO/Warden (PLAN 10.9): values of the fetch constant decoded on the CPU once a\n"
     "    // draw (SpirvShaderTranslator::DecodeFetchConstants) rather than here per\n"
     "    // invocation; files/hack-fetch-raw decodes them here as before.\n"
     "    static const bool dao_fetch_decoded = std::getenv(\"DAO_HACK_FETCH_RAW\") == nullptr;\n"
     "    auto load_fetch_decoded = [&](uint32_t vector, uint32_t component) -> spv::Id {\n"
     "      std::vector<spv::Id> chain = {\n"
     "          builder_->makeIntConstant(1),\n"
     "          builder_->makeIntConstant(\n"
     "              int(fetch_constant_index * kFetchDecodedVectors + vector)),\n"
     "          builder_->makeIntConstant(int(component))};\n"
     "      return builder_->createLoad(\n"
     "          builder_->createAccessChain(spv::StorageClassUniform,\n"
     "                                      uniform_fetch_constants_, chain),\n"
     "          spv::NoPrecision);\n"
     "    };\n"),
    (SST_FETCH, "10.9: the fetch constant's LOD bias decoded",
     "        spv::Id lod = builder_->createNoContractionBinOp(\n"
     "            spv::OpFMul, type_float_,\n"
     "            builder_->createUnaryOp(\n"
     "                spv::OpConvertSToF, type_float_,\n"
     "                builder_->createTriOp(spv::OpBitFieldSExtract, type_int_,\n"
     "                                      fetch_constant_word_4_signed,\n"
     "                                      builder_->makeUintConstant(12),\n"
     "                                      builder_->makeUintConstant(10))),\n"
     "            builder_->makeFloatConstant(1.0f / 32.0f));\n",
     "        spv::Id lod =\n"
     "            dao_fetch_decoded  // DAO/Warden (PLAN 10.9)\n"
     "                ? load_fetch_decoded(1, 0)\n"
     "                : builder_->createNoContractionBinOp(\n"
     "                      spv::OpFMul, type_float_,\n"
     "                      builder_->createUnaryOp(\n"
     "                          spv::OpConvertSToF, type_float_,\n"
     "                          builder_->createTriOp(spv::OpBitFieldSExtract, type_int_,\n"
     "                                                fetch_constant_word_4_signed,\n"
     "                                                builder_->makeUintConstant(12),\n"
     "                                                builder_->makeUintConstant(10))),\n"
     "                      builder_->makeFloatConstant(1.0f / 32.0f));\n"),
    (SST_FETCH, "10.9: the larger gradient exponent bias decoded",
     "          spv::Id grad_exp_adjust_max = builder_->createBinBuiltinCall(\n"
     "              type_int_, ext_inst_glsl_std_450_, GLSLstd450SMax,\n"
     "              builder_->createTriOp(spv::OpBitFieldSExtract, type_int_,\n"
     "                                    fetch_constant_word_4_signed,\n"
     "                                    builder_->makeUintConstant(22),\n"
     "                                    builder_->makeUintConstant(5)),\n"
     "              builder_->createTriOp(spv::OpBitFieldSExtract, type_int_,\n"
     "                                    fetch_constant_word_4_signed,\n"
     "                                    builder_->makeUintConstant(27),\n"
     "                                    builder_->makeUintConstant(5)));\n"
     "          lod = builder_->createNoContractionBinOp(\n"
     "              spv::OpFAdd, type_float_, lod,\n"
     "              builder_->createUnaryOp(spv::OpConvertSToF, type_float_,\n"
     "                                      grad_exp_adjust_max));\n",
     "          if (dao_fetch_decoded) {\n"
     "            // DAO/Warden (PLAN 10.9): with the fetch constant's LOD bias alone, the\n"
     "            // sum decoded too - the same single rounding.\n"
     "            lod = !instr.attributes.use_register_lod && !instr.attributes.lod_bias\n"
     "                      ? load_fetch_decoded(2, 0)\n"
     "                      : builder_->createNoContractionBinOp(\n"
     "                            spv::OpFAdd, type_float_, lod, load_fetch_decoded(1, 3));\n"
     "          } else {\n"
     "          spv::Id grad_exp_adjust_max = builder_->createBinBuiltinCall(\n"
     "              type_int_, ext_inst_glsl_std_450_, GLSLstd450SMax,\n"
     "              builder_->createTriOp(spv::OpBitFieldSExtract, type_int_,\n"
     "                                    fetch_constant_word_4_signed,\n"
     "                                    builder_->makeUintConstant(22),\n"
     "                                    builder_->makeUintConstant(5)),\n"
     "              builder_->createTriOp(spv::OpBitFieldSExtract, type_int_,\n"
     "                                    fetch_constant_word_4_signed,\n"
     "                                    builder_->makeUintConstant(27),\n"
     "                                    builder_->makeUintConstant(5)));\n"
     "          lod = builder_->createNoContractionBinOp(\n"
     "              spv::OpFAdd, type_float_, lod,\n"
     "              builder_->createUnaryOp(spv::OpConvertSToF, type_float_,\n"
     "                                      grad_exp_adjust_max));\n"
     "          }\n"),
    (SST_FETCH, "10.9: the gradient exponent biases decoded",
     "          spv::Id grad_exp_adjust_h = builder_->createUnaryOp(\n"
     "              spv::OpConvertSToF, type_float_,\n"
     "              builder_->createTriOp(spv::OpBitFieldSExtract, type_int_,\n"
     "                                    fetch_constant_word_4_signed,\n"
     "                                    builder_->makeUintConstant(22),\n"
     "                                    builder_->makeUintConstant(5)));\n"
     "          spv::Id grad_exp_adjust_v = builder_->createUnaryOp(\n"
     "              spv::OpConvertSToF, type_float_,\n"
     "              builder_->createTriOp(spv::OpBitFieldSExtract, type_int_,\n"
     "                                    fetch_constant_word_4_signed,\n"
     "                                    builder_->makeUintConstant(27),\n"
     "                                    builder_->makeUintConstant(5)));\n",
     "          spv::Id grad_exp_adjust_h =\n"
     "              dao_fetch_decoded  // DAO/Warden (PLAN 10.9)\n"
     "                  ? load_fetch_decoded(1, 1)\n"
     "                  : builder_->createUnaryOp(\n"
     "                        spv::OpConvertSToF, type_float_,\n"
     "                        builder_->createTriOp(spv::OpBitFieldSExtract, type_int_,\n"
     "                                              fetch_constant_word_4_signed,\n"
     "                                              builder_->makeUintConstant(22),\n"
     "                                              builder_->makeUintConstant(5)));\n"
     "          spv::Id grad_exp_adjust_v =\n"
     "              dao_fetch_decoded\n"
     "                  ? load_fetch_decoded(1, 2)\n"
     "                  : builder_->createUnaryOp(\n"
     "                        spv::OpConvertSToF, type_float_,\n"
     "                        builder_->createTriOp(spv::OpBitFieldSExtract, type_int_,\n"
     "                                              fetch_constant_word_4_signed,\n"
     "                                              builder_->makeUintConstant(27),\n"
     "                                              builder_->makeUintConstant(5)));\n"),
    (SST_FETCH, "10.9: the result exponent bias decoded",
     "        spv::Id result_exponent_bias = builder_->createBinBuiltinCall(\n"
     "            type_float_, ext_inst_glsl_std_450_, GLSLstd450Ldexp,\n"
     "            const_float_1_,\n"
     "            builder_->createTriOp(spv::OpBitFieldSExtract, type_int_,\n"
     "                                  fetch_constant_word_3_signed,\n"
     "                                  builder_->makeUintConstant(13),\n"
     "                                  builder_->makeUintConstant(6)));\n",
     "        spv::Id result_exponent_bias =\n"
     "            dao_fetch_decoded  // DAO/Warden (PLAN 10.9)\n"
     "                ? load_fetch_decoded(2, 1)\n"
     "                : builder_->createBinBuiltinCall(\n"
     "                      type_float_, ext_inst_glsl_std_450_, GLSLstd450Ldexp,\n"
     "                      const_float_1_,\n"
     "                      builder_->createTriOp(spv::OpBitFieldSExtract, type_int_,\n"
     "                                            fetch_constant_word_3_signed,\n"
     "                                            builder_->makeUintConstant(13),\n"
     "                                            builder_->makeUintConstant(6)));\n"),
    (VCP_C, "10.9: the fetch constants decoded on the CPU, after them",
     "      constexpr size_t kFetchConstantsSize = sizeof(uint32_t) * 6 * 32;\n"
     "      uint8_t* mapping = uniform_buffer_pool_->Request(\n"
     "          frame_current_, kFetchConstantsSize, uniform_buffer_alignment,\n"
     "          buffer_info.buffer, buffer_info.offset);\n"
     "      if (!mapping) {\n"
     "        return false;\n"
     "      }\n"
     "      buffer_info.range = VkDeviceSize(kFetchConstantsSize);\n"
     "      std::memcpy(mapping, &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0],\n"
     "                  kFetchConstantsSize);\n",
     "      constexpr size_t kFetchConstantsSize = sizeof(uint32_t) * 6 * 32;\n"
     "      // DAO/Warden (PLAN 10.9): each constant decoded once here, after them, for the\n"
     "      // translated shaders to read rather than decode per invocation.\n"
     "      constexpr size_t kFetchDecodedSize =\n"
     "          sizeof(float) * 4 * SpirvShaderTranslator::kFetchDecodedVectors * 32;\n"
     "      uint8_t* mapping = uniform_buffer_pool_->Request(\n"
     "          frame_current_, kFetchConstantsSize + kFetchDecodedSize,\n"
     "          uniform_buffer_alignment, buffer_info.buffer, buffer_info.offset);\n"
     "      if (!mapping) {\n"
     "        return false;\n"
     "      }\n"
     "      buffer_info.range = VkDeviceSize(kFetchConstantsSize + kFetchDecodedSize);\n"
     "      std::memcpy(mapping, &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0],\n"
     "                  kFetchConstantsSize);\n"
     "      SpirvShaderTranslator::DecodeFetchConstants(\n"
     "          &regs[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0],\n"
     "          reinterpret_cast<float*>(mapping + kFetchConstantsSize));\n"),
    (SST_H, "10.9: the vertex fetch bits in the vertex shader's modification",
     "    } vertex;\n"
     "    struct PixelShaderModification {\n",
     "      // DAO/Warden (PLAN 10.9): for each of the shader's first four vertex bindings\n"
     "      // (vertex_bindings() order), 3 bits each: the shared memory storage buffer\n"
     "      // its vertex buffer lies within, 1 + the binding, or 0 when not within one\n"
     "      // (the words then load through the switch over the bindings, as before);\n"
     "      // and 1 + the fetch constant's endianness, or 0 for the dynamic swap. From\n"
     "      // the fetch constants at the draw (vulkan_pipeline_cache.cc).\n"
     "      uint32_t vfetch_binding : 12;\n"
     "      uint32_t vfetch_endian : 12;\n"
     "    } vertex;\n"
     "    struct PixelShaderModification {\n"),
    (SST_H, "10.9: the endian swap for a known endianness",
     "  spv::Id EndianSwap32Uint(spv::Id value, spv::Id endian);\n",
     "  spv::Id EndianSwap32Uint(spv::Id value, spv::Id endian);\n"
     "  // DAO/Warden (PLAN 10.9): the same for an endianness known when the shader is\n"
     "  // made - without the branches.\n"
     "  spv::Id DaoEndianSwap32UintStatic(spv::Id value, xenos::Endian endian);\n"),
    (SST_H, "10.9: the shared memory load from a known binding",
     "  spv::Id LoadUint32FromSharedMemory(spv::Id address_dwords_int);\n",
     "  // DAO/Warden (PLAN 10.9): dao_known_binding 0 and up loads straight from that\n"
     "  // shared memory storage buffer, without the switch over them.\n"
     "  spv::Id LoadUint32FromSharedMemory(spv::Id address_dwords_int,\n"
     "                                     int dao_known_binding = -1);\n"),
    (SST_C, "10.9: the endian swap for a known endianness",
     "spv::Id SpirvShaderTranslator::EndianSwap32Uint(spv::Id value, spv::Id endian) {\n",
     "// DAO/Warden (PLAN 10.9): EndianSwap32Uint's operations for an endianness known when\n"
     "// the shader is made (the vertex fetch bits of the modification), without its\n"
     "// branches - the same swaps, so the same words.\n"
     "spv::Id SpirvShaderTranslator::DaoEndianSwap32UintStatic(spv::Id value,\n"
     "                                                         xenos::Endian endian) {\n"
     "  if (endian == xenos::Endian::kNone) {\n"
     "    return value;\n"
     "  }\n"
     "  spv::Id type = builder_->getTypeId(value);\n"
     "  int num_components = builder_->getNumTypeComponents(type);\n"
     "  auto typed = [&](unsigned int scalar) -> spv::Id {\n"
     "    spv::Id constant = builder_->makeUintConstant(scalar);\n"
     "    if (num_components <= 1) {\n"
     "      return constant;\n"
     "    }\n"
     "    id_vector_temp_.clear();\n"
     "    id_vector_temp_.insert(id_vector_temp_.cend(), num_components, constant);\n"
     "    return builder_->makeCompositeConstant(type, id_vector_temp_);\n"
     "  };\n"
     "  if (endian == xenos::Endian::k8in16 || endian == xenos::Endian::k8in32) {\n"
     "    spv::Id const_8 = typed(8);\n"
     "    spv::Id const_00ff00ff = typed(0x00FF00FF);\n"
     "    value = builder_->createBinOp(\n"
     "        spv::OpBitwiseOr, type,\n"
     "        builder_->createBinOp(\n"
     "            spv::OpBitwiseAnd, type,\n"
     "            builder_->createBinOp(spv::OpShiftRightLogical, type, value, const_8),\n"
     "            const_00ff00ff),\n"
     "        builder_->createBinOp(\n"
     "            spv::OpShiftLeftLogical, type,\n"
     "            builder_->createBinOp(spv::OpBitwiseAnd, type, value, const_00ff00ff),\n"
     "            const_8));\n"
     "  }\n"
     "  if (endian == xenos::Endian::k8in32 || endian == xenos::Endian::k16in32) {\n"
     "    value = builder_->createQuadOp(\n"
     "        spv::OpBitFieldInsert, type,\n"
     "        builder_->createBinOp(spv::OpShiftRightLogical, type, value, typed(16)),\n"
     "        value, builder_->makeIntConstant(16), builder_->makeIntConstant(16));\n"
     "  }\n"
     "  return value;\n"
     "}\n"
     "\n"
     "spv::Id SpirvShaderTranslator::EndianSwap32Uint(spv::Id value, spv::Id endian) {\n"),
    (SST_C, "10.9: the shared memory load from a known binding",
     "spv::Id SpirvShaderTranslator::LoadUint32FromSharedMemory(\n"
     "    spv::Id address_dwords_int) {\n"
     "  spv::StorageClass storage_class = features_.spirv_version >= spv::Spv_1_3\n"
     "                                        ? spv::StorageClassStorageBuffer\n"
     "                                        : spv::StorageClassUniform;\n"
     "\n"
     "  uint32_t binding_count_log2 = GetSharedMemoryStorageBufferCountLog2();\n",
     "spv::Id SpirvShaderTranslator::LoadUint32FromSharedMemory(\n"
     "    spv::Id address_dwords_int, int dao_known_binding) {\n"
     "  spv::StorageClass storage_class = features_.spirv_version >= spv::Spv_1_3\n"
     "                                        ? spv::StorageClassStorageBuffer\n"
     "                                        : spv::StorageClassUniform;\n"
     "\n"
     "  uint32_t binding_count_log2 = GetSharedMemoryStorageBufferCountLog2();\n"
     "\n"
     "  // DAO/Warden (PLAN 10.9): the binding known at the draw - the load straight from\n"
     "  // it, the address within it as the switch below would take it. A word past the\n"
     "  // buffer's end is replaced by 0 by the caller's bounds check as before.\n"
     "  if (binding_count_log2 && dao_known_binding >= 0) {\n"
     "    uint32_t known_address_bits = (29 - 2) - binding_count_log2;\n"
     "    id_vector_temp_.clear();\n"
     "    id_vector_temp_.push_back(builder_->makeIntConstant(dao_known_binding));\n"
     "    // The only SSBO struct member.\n"
     "    id_vector_temp_.push_back(const_int_0_);\n"
     "    id_vector_temp_.push_back(builder_->createBinOp(\n"
     "        spv::OpBitwiseAnd, type_int_, address_dwords_int,\n"
     "        builder_->makeIntConstant(\n"
     "            int((uint32_t(1) << known_address_bits) - 1))));\n"
     "    return builder_->createLoad(\n"
     "        builder_->createAccessChain(storage_class, buffers_shared_memory_,\n"
     "                                    id_vector_temp_),\n"
     "        spv::NoPrecision);\n"
     "  }\n"),
    (SST_FETCH, "10.9: the fetch's binding and endianness from the modification",
     "  uint32_t fetch_constant_word_0_index = instr.operands[1].storage_index << 1;\n"
     "  uint32_t fetch_constant_word_1_index = fetch_constant_word_0_index + 1;\n",
     "  uint32_t fetch_constant_word_0_index = instr.operands[1].storage_index << 1;\n"
     "  uint32_t fetch_constant_word_1_index = fetch_constant_word_0_index + 1;\n"
     "  // DAO/Warden (PLAN 10.9): this fetch constant's shared memory binding and\n"
     "  // endianness when known at the draw (the modification's vertex fetch bits, for\n"
     "  // the shader's first four vertex bindings): the words load straight from the\n"
     "  // binding and swap without branches.\n"
     "  int dao_known_binding = -1;\n"
     "  int dao_known_endian = -1;\n"
     "  if (is_vertex_shader()) {\n"
     "    const std::vector<Shader::VertexBinding>& dao_vertex_bindings =\n"
     "        current_shader().vertex_bindings();\n"
     "    const auto& dao_modification = GetSpirvShaderModification().vertex;\n"
     "    for (size_t i = 0; i < dao_vertex_bindings.size() && i < 4; ++i) {\n"
     "      if (dao_vertex_bindings[i].fetch_constant != instr.operands[1].storage_index) {\n"
     "        continue;\n"
     "      }\n"
     "      uint32_t known_binding = (dao_modification.vfetch_binding >> (3 * i)) & 7;\n"
     "      if (known_binding) {\n"
     "        dao_known_binding = int(known_binding) - 1;\n"
     "      }\n"
     "      uint32_t known_endian = (dao_modification.vfetch_endian >> (3 * i)) & 7;\n"
     "      if (known_endian) {\n"
     "        dao_known_endian = int(known_endian) - 1;\n"
     "      }\n"
     "      break;\n"
     "    }\n"
     "  }\n"),
    (SST_FETCH, "10.9: the words loaded from the known binding",
     "    spv::Id loaded_word = LoadUint32FromSharedMemory(word_address);\n",
     "    spv::Id loaded_word =\n"
     "        LoadUint32FromSharedMemory(word_address, dao_known_binding);  // DAO/Warden\n"),
    (SST_FETCH, "10.9: the words swapped for the known endianness",
     "  words = EndianSwap32Uint(\n"
     "      words, builder_->createBinOp(spv::OpBitwiseAnd, type_uint_,\n"
     "                                   fetch_constant_word_1,\n"
     "                                   builder_->makeUintConstant(0b11)));\n",
     "  words = dao_known_endian >= 0  // DAO/Warden (PLAN 10.9)\n"
     "              ? DaoEndianSwap32UintStatic(words, xenos::Endian(dao_known_endian))\n"
     "              : EndianSwap32Uint(\n"
     "                    words, builder_->createBinOp(spv::OpBitwiseAnd, type_uint_,\n"
     "                                                 fetch_constant_word_1,\n"
     "                                                 builder_->makeUintConstant(0b11)));\n"),
    (VPC_C, "10.9: the vertex shader's vertex fetch bits",
     "  return modification;\n"
     "}\n"
     "\n"
     "SpirvShaderTranslator::Modification\n"
     "VulkanPipelineCache::GetCurrentPixelShaderModification(\n",
     "  // DAO/Warden (PLAN 10.9): the vertex fetch bits - for the shader's first four\n"
     "  // vertex bindings, from their fetch constants: the shared memory storage buffer\n"
     "  // the vertex buffer lies within (128 MB each on the Adreno; 0 when it crosses\n"
     "  // one's end, leaving the switch over them) and its endianness, so the fetches\n"
     "  // are translated for them rather than deciding per word.\n"
     "  {\n"
     "    const std::vector<Shader::VertexBinding>& vertex_bindings =\n"
     "        shader.vertex_bindings();\n"
     "    const uint32_t binding_shift =\n"
     "        (29 - 2) - shader_translator_->GetSharedMemoryStorageBufferCountLog2();\n"
     "    uint32_t vfetch_binding = 0;\n"
     "    uint32_t vfetch_endian = 0;\n"
     "    for (size_t i = 0; i < vertex_bindings.size() && i < 4; ++i) {\n"
     "      const uint32_t* fetch =\n"
     "          &regs.values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 +\n"
     "                       vertex_bindings[i].fetch_constant * 2];\n"
     "      const uint32_t base = (fetch[0] >> 2) & 0x07FFFFFF;\n"
     "      const uint32_t size = (fetch[1] >> 2) & ((UINT32_C(1) << 24) - 1);\n"
     "      vfetch_endian |= ((fetch[1] & 3) + 1) << (3 * i);\n"
     "      if (size && (base >> binding_shift) == ((base + size - 1) >> binding_shift)) {\n"
     "        vfetch_binding |= ((base >> binding_shift) + 1) << (3 * i);\n"
     "      }\n"
     "    }\n"
     "    modification.vertex.vfetch_binding = vfetch_binding;\n"
     "    modification.vertex.vfetch_endian = vfetch_endian;\n"
     "  }\n"
     "\n"
     "  return modification;\n"
     "}\n"
     "\n"
     "SpirvShaderTranslator::Modification\n"
     "VulkanPipelineCache::GetCurrentPixelShaderModification(\n"),
    # BUG-084: a log line is formatted into the thread's 64 KiB buffer with
    # fmt::format_to_n, whose result.size is the length the whole line would have had,
    # not what fits - and AppendLine copied that many bytes out of the buffer. A report
    # of the port's longer than the buffer (the sync report during an area load) read
    # past the buffer's end and killed the process in the ring's memcpy, three times in
    # one night. The length appended is now at most the buffer's; the line is cut
    # short rather than the process.
    (LOG_H, "BUG-084: a line longer than the thread buffer is cut, not read past",
     "  auto result = fmt::format_to_n(target.first, target.second,\n"
     "                                 fmt::runtime(format), args...);\n"
     "  internal::AppendLogLine(log_level, prefix_char, result.size);\n",
     "  auto result = fmt::format_to_n(target.first, target.second,\n"
     "                                 fmt::runtime(format), args...);\n"
     "  // DAO/Warden (BUG-084): result.size is the untruncated length; never more\n"
     "  // than the buffer holds.\n"
     "  internal::AppendLogLine(log_level, prefix_char,\n"
     "                          result.size < target.second ? result.size : target.second);\n"),
    (LOG_H, "BUG-084: the batch logger's length is clamped the same way",
     "  void submit(char prefix_char) {\n"
     "    logging::internal::AppendLogLine(ll, prefix_char, total_size);\n"
     "  }\n",
     "  void submit(char prefix_char) {\n"
     "    // DAO/Warden (BUG-084): never more than the thread buffer holds.\n"
     "    const size_t capacity = logging::internal::GetThreadBuffer().second;\n"
     "    logging::internal::AppendLogLine(ll, prefix_char,\n"
     "                                     total_size < capacity ? total_size : capacity);\n"
     "  }\n"),
    # The deck's faces: the HUD's party portraits are GPU renders (BUG-052) - each a
    # 146x146 k_8_8_8_8 resolve, destination pitch 009200A0 - that the CPU never reads.
    # While the second screen's deck is pulling, each such resolve is copied out of the
    # shared memory into a small readback buffer, waited for (four times at an area
    # load, a few milliseconds) and handed to warden_deck.cc, which untiles it into the
    # Party tab's avatar for that slot.
    (VCP_C, "deck: the party portraits' entry points",
     "#include \"xenia/gpu/vulkan/vulkan_command_processor.h\"\n",
     "#include \"xenia/gpu/vulkan/vulkan_command_processor.h\"\n"
     "\n"
     "// DAO/Warden (the deck's faces): warden_deck.cc.\n"
     "namespace warden::deck {\n"
     "bool WantsPortraits();\n"
     "void NotePortrait(uint32_t address, const uint8_t* data, uint32_t length);\n"
     "}  // namespace warden::deck\n"),
    (VCP_C, "deck: the party portraits' renders read back for the second screen",
     "  // CPU readback resolve path (if not disabled).\n"
     "  ReadbackResolveMode readback_mode = GetReadbackResolveMode();\n",
     "  // DAO/Warden (the deck's faces): a party portrait's render, read back while the\n"
     "  // deck pulls - copied out, waited for, untiled on the CPU (warden_deck.cc).\n"
     "  if (register_file_->values[XE_GPU_REG_RB_COPY_DEST_PITCH] == 0x009200A0 &&\n"
     "      written_length == 0x19000 && !is_scaled && warden::deck::WantsPortraits()) {\n"
     "    static VkBuffer portrait_buffer = VK_NULL_HANDLE;\n"
     "    static VkDeviceMemory portrait_memory = VK_NULL_HANDLE;\n"
     "    const ui::vulkan::VulkanDevice* const portrait_device = GetVulkanDevice();\n"
     "    if (portrait_buffer == VK_NULL_HANDLE) {\n"
     "      ui::vulkan::util::CreateDedicatedAllocationBuffer(\n"
     "          portrait_device, 0x19000, VK_BUFFER_USAGE_TRANSFER_DST_BIT,\n"
     "          ui::vulkan::util::MemoryPurpose::kReadback, portrait_buffer,\n"
     "          portrait_memory);\n"
     "    }\n"
     "    if (portrait_buffer != VK_NULL_HANDLE) {\n"
     "      shared_memory_->Use(VulkanSharedMemory::Usage::kRead);\n"
     "      SubmitBarriers(true);\n"
     "      VkBufferCopy portrait_region = {};\n"
     "      portrait_region.srcOffset = written_address;\n"
     "      portrait_region.dstOffset = 0;\n"
     "      portrait_region.size = written_length;\n"
     "      deferred_command_buffer_.CmdVkCopyBuffer(shared_memory_->buffer(),\n"
     "                                               portrait_buffer, 1, &portrait_region);\n"
     "      // Ends the submission and waits for it.\n"
     "      CheckSubmissionCompletionAndDeviceLoss(GetCurrentSubmission());\n"
     "      void* portrait_mapping = nullptr;\n"
     "      if (portrait_device->functions().vkMapMemory(\n"
     "              portrait_device->device(), portrait_memory, 0, VK_WHOLE_SIZE, 0,\n"
     "              &portrait_mapping) == VK_SUCCESS) {\n"
     "        VkMappedMemoryRange portrait_range = {};\n"
     "        portrait_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;\n"
     "        portrait_range.memory = portrait_memory;\n"
     "        portrait_range.size = VK_WHOLE_SIZE;\n"
     "        portrait_device->functions().vkInvalidateMappedMemoryRanges(\n"
     "            portrait_device->device(), 1, &portrait_range);\n"
     "        warden::deck::NotePortrait(written_address,\n"
     "                                   static_cast<const uint8_t*>(portrait_mapping),\n"
     "                                   written_length);\n"
     "        portrait_device->functions().vkUnmapMemory(portrait_device->device(),\n"
     "                                                   portrait_memory);\n"
     "      }\n"
     "      // The wait ended the submission: anything recorded below needs a new one.\n"
     "      if (!BeginSubmission(true)) {\n"
     "        return false;\n"
     "      }\n"
     "    }\n"
     "  }\n"
     "\n"
     "  // CPU readback resolve path (if not disabled).\n"
     "  ReadbackResolveMode readback_mode = GetReadbackResolveMode();\n"),
    # BUG-049 again (the owner, 2026-11-02: the saves' thumbnails missing): the save's
    # thumbnail is a 448x256 k_8_8_8_8 resolve (destination pitch 010001C0, 0x70000
    # bytes) the title then copies into scrnshot.bin from guest memory - which a resolve
    # never reaches here (readback_resolve stays "none": every resolve read back costs
    # most of the frame rate, BUG-039). Session 34's whole thumbnail (run 313) came with
    # the readback marker on. This one resolve is read back in full - once a save, a
    # wait of a few milliseconds - and nothing else.
    (VCP_C, "BUG-049: the save thumbnail's resolve read back",
     "  ReadbackResolveMode readback_mode = GetReadbackResolveMode();\n"
     "  if (readback_mode == ReadbackResolveMode::kDisabled || !written_length) {\n",
     "  ReadbackResolveMode readback_mode = GetReadbackResolveMode();\n"
     "  // DAO/Warden (BUG-049): the save thumbnail's resolve, read back in full.\n"
     "  if (register_file_->values[XE_GPU_REG_RB_COPY_DEST_PITCH] == 0x010001C0 &&\n"
     "      written_length == 0x70000) {\n"
     "    readback_mode = ReadbackResolveMode::kFull;\n"
     "  }\n"
     "  if (readback_mode == ReadbackResolveMode::kDisabled || !written_length) {\n"),
    # PLAN 9.16 (1080p): files/hack-edram-map holding a number N - the render targets of
    # the next N updates whose targets change, one line each (base, pitch, MSAA, format,
    # the range the cache gives each), and every resolve meanwhile: which pass of a
    # frame runs past the EDRAM's end at a render size above 1280x720. Checked about
    # once a second; armed again when the file is written anew.
    (RTC_C, "9.16: the EDRAM map's counter",
     "namespace xe {\nnamespace gpu {\n",
     "namespace xe {\nnamespace gpu {\n"
     "\n"
     "// DAO/Warden (PLAN 9.16): files/hack-edram-map's lines still to log.\n"
     "std::atomic<int> dao_edram_map_left{0};\n"
     "\n"
     "void DaoEdramMapCheck() {\n"
     "  static auto checked = std::chrono::steady_clock::time_point();\n"
     "  static long armed_size = -1;\n"
     "  const auto now = std::chrono::steady_clock::now();\n"
     "  if (now - checked < std::chrono::seconds(1)) {\n"
     "    return;\n"
     "  }\n"
     "  checked = now;\n"
     "  const char* dir = std::getenv(\"WARDEN_FILES_DIR\");\n"
     "  FILE* file = dir ? std::fopen((std::string(dir) + \"/hack-edram-map\").c_str(), \"r\")\n"
     "                   : nullptr;\n"
     "  if (!file) {\n"
     "    armed_size = -1;\n"
     "    return;\n"
     "  }\n"
     "  int count = 0;\n"
     "  if (std::fscanf(file, \"%d\", &count) != 1) {\n"
     "    count = 0;\n"
     "  }\n"
     "  std::fseek(file, 0, SEEK_END);\n"
     "  const long size = std::ftell(file) + count;\n"
     "  std::fclose(file);\n"
     "  if (size != armed_size) {\n"
     "    armed_size = size;\n"
     "    dao_edram_map_left.store(count);\n"
     "    XELOGI(\"EDRAM map: the next {} render target changes\", count);\n"
     "  }\n"
     "}\n"),
    (RTC_C, "9.16: the EDRAM map's includes",
     "#include <cmath>\n",
     "#include <atomic>\n#include <chrono>\n#include <cmath>\n#include <cstdio>\n"
     "#include <cstdlib>\n#include <string>\n"),
    (RTC_C, "9.16: the EDRAM map - each change of the render targets",
     "  if (keep_aliased_depth) {\n"
     "    // The host depth must be current for the color owner's whole range.\n",
     "  // DAO/Warden (PLAN 9.16): files/hack-edram-map.\n"
     "  DaoEdramMapCheck();\n"
     "  if (dao_edram_map_left.load(std::memory_order_relaxed) > 0) {\n"
     "    static uint32_t dao_last_keys[1 + xenos::kMaxColorRenderTargets] = {};\n"
     "    bool dao_changed = false;\n"
     "    for (uint32_t i = 0; i < 1 + xenos::kMaxColorRenderTargets; ++i) {\n"
     "      const uint32_t key = ((ownership_rts_used_bits >> i) & 1) ? rt_keys[i].key : 0;\n"
     "      dao_changed |= key != dao_last_keys[i];\n"
     "      dao_last_keys[i] = key;\n"
     "    }\n"
     "    if (dao_changed && dao_edram_map_left.fetch_sub(1) > 0) {\n"
     "      std::string dao_line;\n"
     "      for (uint32_t i = 0; i < edram_bases_sorted_count; ++i) {\n"
     "        const RenderTargetKey& k = rt_keys[edram_bases_sorted[i].second];\n"
     "        dao_line += fmt::format(\" | {}{} base {} pitch {} msaa {} fmt {} {}bpp range {}+{}\",\n"
     "                                k.is_depth ? \"D\" : \"C\", edram_bases_sorted[i].second,\n"
     "                                k.base_tiles, k.pitch_tiles_at_32bpp,\n"
     "                                uint32_t(k.msaa_samples), uint32_t(k.resource_format),\n"
     "                                ((rts_are_64bpp >> edram_bases_sorted[i].second) & 1) ? 64 : 32,\n"
     "                                rt_starts_tiles[i], rt_lengths_tiles[i]);\n"
     "      }\n"
     "      XELOGI(\"EDRAM map: height {} pitch {} window offset {}{}\", height_used,\n"
     "             pitch_tiles_at_32bpp, window_offset_tiles, dao_line);\n"
     "    }\n"
     "  }\n"
     "\n"
     "  if (keep_aliased_depth) {\n"
     "    // The host depth must be current for the color owner's whole range.\n"),
    (VRTC_C, "9.16: the EDRAM map's counter, declared",
     "#include \"xenia/gpu/vulkan/vulkan_render_target_cache.h\"\n",
     "#include \"xenia/gpu/vulkan/vulkan_render_target_cache.h\"\n"
     "\n"
     "#include <atomic>\n"
     "namespace xe {\nnamespace gpu {\n"
     "extern std::atomic<int> dao_edram_map_left;  // render_target_cache.cc (PLAN 9.16)\n"
     "}  // namespace gpu\n}  // namespace xe\n"),
    (VRTC_C, "9.16: the EDRAM map - each resolve",
     "    return false;\n  }\n\n  if (copy_dest_info_out) {\n",
     "    return false;\n  }\n\n"
     "  // DAO/Warden (PLAN 9.16): files/hack-edram-map - each resolve while it logs.\n"
     "  {\n"
     "    if (dao_edram_map_left.load(std::memory_order_relaxed) > 0) {\n"
     "      const draw_util::ResolveEdramInfo& e = resolve_info.IsCopyingDepth()\n"
     "                                                 ? resolve_info.depth_edram_info\n"
     "                                                 : resolve_info.color_edram_info;\n"
     "      XELOGI(\"EDRAM map: resolve {} {}x{} at ({} {}) from base {} pitch {} fmt {} \"\n"
     "             \"{}bpp msaa {} to {:08X}\",\n"
     "             resolve_info.IsCopyingDepth() ? \"depth\" : \"colour\",\n"
     "             uint32_t(resolve_info.coordinate_info.width_div_8) << 3,\n"
     "             uint32_t(resolve_info.height_div_8) << 3,\n"
     "             uint32_t(resolve_info.coordinate_info.edram_offset_x_div_8) << 3,\n"
     "             uint32_t(resolve_info.coordinate_info.edram_offset_y_div_8) << 3,\n"
     "             uint32_t(e.base_tiles), uint32_t(e.pitch_tiles), uint32_t(e.format),\n"
     "             e.format_is_64bpp ? 64 : 32, uint32_t(e.msaa_samples),\n"
     "             resolve_info.copy_dest_base);\n"
     "    }\n"
     "  }\n"
     "\n"
     "  if (copy_dest_info_out) {\n"),
]


def graft_files():
    out = {}
    for d in GRAFT_DIRS:
        base = CANARY / d
        if not base.is_dir():
            continue
        for p in sorted(base.rglob("*")):
            if p.is_file() and p.suffix in SRC_EXT and not EXCLUDE.search(p.name):
                out[p.relative_to(CANARY).as_posix()] = p
    for d, pattern in GRAFT_GLOBS:
        for p in sorted((CANARY / d).glob(pattern)):
            if p.is_file() and p.suffix in SRC_EXT and not EXCLUDE.search(p.name):
                out[p.relative_to(CANARY).as_posix()] = p
    return out


def main():
    ap = argparse.ArgumentParser(description="Assemble the grafted Xenia tree")
    ap.add_argument("--check", action="store_true", help="verify inputs and edits, write nothing")
    ap.add_argument("--clean", action="store_true", help="remove the assembled tree")
    a = ap.parse_args()

    if a.clean:
        tree = OUT.parent
        if tree.is_dir():
            shutil.rmtree(tree)
            print(f"removed {tree.relative_to(ROOT)}")
        else:
            print("nothing to remove")
        return 0

    for p in (DAO_XENIA, CANARY):
        if not p.is_dir():
            print(f"error: missing input {p}", file=sys.stderr)
            print("       see THIRD_PARTY.md for how to fetch it", file=sys.stderr)
            return 1

    graft = graft_files()
    print(f"inputs")
    print(f"  DAORecomp vendored Xenia   {DAO_XENIA.relative_to(ROOT)}")
    print(f"  Xenia Canary               {CANARY.relative_to(ROOT)}")
    print(f"  files to graft             {len(graft)}")

    # Verify every edit applies against pristine upstream before touching anything.
    print()
    print("edits")
    pristine = {}
    failures = 0
    for rel, desc, find, _repl in EDITS:
        if rel not in pristine:
            # Resolve against whichever source actually provides the file in the
            # assembled tree. Several files exist in both checkouts with different
            # contents: the graft set comes from Canary and everything else from
            # DAORecomp, so checking the wrong one silently fails to match.
            src = (CANARY / rel) if rel in graft else (DAO_XENIA / rel)
            if not src.is_file():
                src = (DAO_XENIA / rel) if rel in graft else (CANARY / rel)
            if not src.is_file():
                print(f"  [FAIL] {rel}: not found in either source")
                failures += 1
                continue
            pristine[rel] = src.read_text(encoding="utf-8", errors="replace")
        n = pristine[rel].count(find)
        mark = " OK " if n == 1 else "FAIL"
        if n != 1:
            failures += 1
        print(f"  [{mark}] {Path(rel).name:<34} {desc}" + ("" if n == 1 else f"  (matched {n}x)"))
    if failures:
        print()
        print(f"{failures} edit(s) did not match exactly once. Upstream has probably moved;")
        print("check the pinned commits in THIRD_PARTY.md before changing these edits.")
        return 1

    if a.check:
        print()
        print("all edits apply cleanly; nothing written (--check)")
        return 0

    # Base layer: DAORecomp's subset, verbatim.
    if OUT.parent.is_dir():
        shutil.rmtree(OUT.parent)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(DAO_XENIA, OUT)
    base_count = sum(1 for p in OUT.rglob("*") if p.is_file())

    # Graft layer, then the edits.
    added = replaced = 0
    for rel, src in graft.items():
        dst = OUT / rel
        if dst.exists():
            replaced += 1
        else:
            added += 1
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    applied = 0
    for rel, desc, find, repl in EDITS:
        dst = OUT / rel
        text = dst.read_text(encoding="utf-8", errors="replace")
        if text.count(find) != 1:
            print(f"error: edit no longer matches after copy: {rel}: {desc}", file=sys.stderr)
            return 1
        dst.write_text(text.replace(find, repl, 1), encoding="utf-8", newline="\n")
        applied += 1

    # Some of Xenia's shaders include FidelityFX by a path relative to the repository
    # root ("../../../../third_party/FidelityFX-FSR/..."), which the compiler resolves
    # from the including file rather than from an -I path. Link Canary's third_party
    # in at the equivalent place so those resolve.
    link = OUT.parent / "third_party"
    target = CANARY.parent / "third_party"
    if not link.exists() and target.is_dir():
        linked = False
        try:
            link.symlink_to(target, target_is_directory=True)
            linked = True
        except (OSError, NotImplementedError):
            # Windows needs either privileges or a junction; a junction needs neither.
            import subprocess as _sp
            linked = _sp.run(["cmd", "/c", "mklink", "/J", str(link), str(target)],
                             capture_output=True).returncode == 0
        print()
        print(f"  third_party link           {'created' if linked else 'FAILED'}"
              f" -> {target.relative_to(ROOT)}")
        if not linked:
            print("    FidelityFX shaders will not compile without it")

    # The D3D12 backend is left in place. The frontend references nothing from it, and
    # keeping both backends present makes diffing against the reference build easier.
    print()
    print("assembled")
    print(f"  base files from DAORecomp  {base_count}")
    print(f"  grafted (new)              {added}")
    print(f"  grafted (overwrote)        {replaced}")
    print(f"  edits applied              {applied}")
    print(f"  output                     {OUT.relative_to(ROOT)}")
    total = sum(1 for p in OUT.rglob("*") if p.is_file())
    print(f"  total files                {total}")
    print()
    print("  NEXT: the tree was rebuilt, so the generated SPIR-V shader headers are")
    print("        gone. Six backend files include them, so run:")
    print("          python tools/build_xenia_shaders.py")
    print("          python tools/compile_runtime.py --target android")
    return 0


if __name__ == "__main__":
    sys.exit(main())
