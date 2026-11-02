#!/usr/bin/env python3
"""
prepare_lsfg.py - lsfg-vk's backend made ready to run on the game's own device (PLAN 9.14).

lsfg-vk (third_party/lsfg-vk, v2.0.0-dev at 0213be9, GPL-3.0 - THIRD_PARTY.md) makes a
Vulkan instance and device of its own and trades frames with the application as external
memory and a timeline semaphore passed as opaque file descriptors. The Adreno 740 exports
semaphores only as sync fds, which carry binary semaphores only, so here the backend runs
on Xenia's device instead (app/src/main/cpp/warden_lsfg.cc, which takes the place of the
backend's src/lsfgvk.cpp). Its shared layer, lsfg-vk-common, already takes an externally
managed device; these edits let that device have a queue of its own for its one-off
submissions (Xenia submits on queue 0 of the family, the graft makes a second), use the
16-bit shaders where the device enables shaderFloat16 (the external constructor never
looked), and record the generation into a command buffer of the application's - the
game's own, so that it runs in the game's submissions on queue 0 (session 37: the Adreno
time-slices its queues, so a queue of the generation's own bought nothing).

The sources the app compiles are copied to build/lsfg-vk with the edits applied; each edit
is anchored on the pinned upstream text and checked alone.

Usage:
  python tools/prepare_lsfg.py           # build/lsfg-vk from third_party/lsfg-vk
  python tools/prepare_lsfg.py --check   # the edits apply to the pin; nothing written
"""
import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "third_party" / "lsfg-vk"
OUT = ROOT / "build" / "lsfg-vk"
PIN = "0213be9ff93121283ad46a27119f40ece0e6f301"

# What is copied: the shared layer and the backend; the app compiles a part of them
# (app/src/main/cpp/CMakeLists.txt, warden_lsfg).
FOLDERS = ["lsfg-vk-common", "lsfg-vk-backend"]

VK_HPP = "lsfg-vk-common/include/lsfg-vk-common/vulkan/vulkan.hpp"
VK_CPP = "lsfg-vk-common/src/vulkan/vulkan.cpp"
CB_HPP = "lsfg-vk-common/include/lsfg-vk-common/vulkan/command_buffer.hpp"
CB_CPP = "lsfg-vk-common/src/vulkan/command_buffer.cpp"

EDITS = [
    (VK_HPP, "the external device's constructor takes a queue index and the 16-bit choice",
     "            bool isGraphical = true,\n"
     "            std::optional<PFN_vkSetDeviceLoaderData> setLoaderData = std::nullopt,\n"
     "            const std::optional<std::filesystem::path>& cachefile = std::nullopt);\n",
     "            bool isGraphical = true,\n"
     "            std::optional<PFN_vkSetDeviceLoaderData> setLoaderData = std::nullopt,\n"
     "            const std::optional<std::filesystem::path>& cachefile = std::nullopt,\n"
     "            // DAO/Warden (PLAN 9.14): the queue of the family to submit on, and whether\n"
     "            // the device has shaderFloat16 enabled (the 16-bit shaders).\n"
     "            uint32_t queueIndex = 0, bool fp16 = false);\n"),
    (VK_CPP, "getQueue takes the queue's index",
     "    VkQueue getQueue(const VulkanDeviceFuncs& fd, VkDevice device,\n"
     "            std::optional<PFN_vkSetDeviceLoaderData> setLoaderData,\n"
     "            uint32_t cfi) {\n"
     "        VkQueue queue{};\n"
     "\n"
     "        fd.GetDeviceQueue(device, cfi, 0, &queue);\n",
     "    VkQueue getQueue(const VulkanDeviceFuncs& fd, VkDevice device,\n"
     "            std::optional<PFN_vkSetDeviceLoaderData> setLoaderData,\n"
     "            uint32_t cfi, uint32_t index = 0) {  // DAO/Warden (PLAN 9.14): index\n"
     "        VkQueue queue{};\n"
     "\n"
     "        fd.GetDeviceQueue(device, cfi, index, &queue);\n"),
    (VK_CPP, "the external device's constructor: its parameters",
     "        const std::optional<std::filesystem::path>& cachefile) :\n"
     "    instance(new VkInstance(instance)),\n",
     "        const std::optional<std::filesystem::path>& cachefile,\n"
     "        uint32_t queueIndex, bool fp16) :  // DAO/Warden (PLAN 9.14)\n"
     "    instance(new VkInstance(instance)),\n"),
    (VK_CPP, "the external device's constructor: the 16-bit choice taken",
     "    fp16(false),\n",
     "    fp16(fp16),  // DAO/Warden (PLAN 9.14)\n"),
    (VK_CPP, "the external device's constructor: its queue",
     "    device_funcs(deviceFuncs),\n"
     "    computeQueue(getQueue(this->device_funcs, *this->device,\n"
     "        this->setLoaderData,\n"
     "        this->queueFamilyIdx)),\n",
     "    device_funcs(deviceFuncs),\n"
     "    computeQueue(getQueue(this->device_funcs, *this->device,\n"
     "        this->setLoaderData,\n"
     "        this->queueFamilyIdx, queueIndex)),  // DAO/Warden (PLAN 9.14)\n"),
    (CB_HPP, "a command buffer of the application's, recorded into",
     "        CommandBuffer(const vk::Vulkan& vk);\n",
     "        CommandBuffer(const vk::Vulkan& vk);\n"
     "\n"
     "        /// DAO/Warden (PLAN 9.14): a command buffer of the application's, recorded\n"
     "        /// into and not owned (the game's own, the generation in its submissions)\n"
     "        explicit CommandBuffer(VkCommandBuffer external);\n"),
    (CB_CPP, "a command buffer of the application's: the constructor",
     "CommandBuffer::CommandBuffer(const vk::Vulkan& vk)\n"
     "        : commandBuffer(createCommandBuffer(vk)) {}\n",
     "CommandBuffer::CommandBuffer(const vk::Vulkan& vk)\n"
     "        : commandBuffer(createCommandBuffer(vk)) {}\n"
     "\n"
     "// DAO/Warden (PLAN 9.14): no deleter - the handle is the application's.\n"
     "CommandBuffer::CommandBuffer(VkCommandBuffer external)\n"
     "        : commandBuffer(new VkCommandBuffer(external)) {}\n"),
]


def pinned_head():
    head = SRC / ".git" / "HEAD"
    return head.read_text().strip() if head.exists() else ""


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="check the edits; write nothing")
    args = ap.parse_args()
    if not (SRC / "lsfg-vk-common").is_dir():
        sys.exit("third_party/lsfg-vk is missing - run tools/bootstrap.py (step 2)")
    if pinned_head() != PIN:
        sys.exit("third_party/lsfg-vk is at %s, not the pinned %s" % (pinned_head()[:12],
                                                                       PIN[:12]))
    failed = 0
    texts = {}
    for path, what, old, new in EDITS:
        text = texts.setdefault(path, (SRC / path).read_text(encoding="utf-8"))
        count = text.count(old)
        print("  [%s] %-28s %s" % ("OK" if count == 1 else "FAIL", Path(path).name, what))
        failed += count != 1
    if failed:
        sys.exit("%d edit(s) no longer apply to the pin - read their descriptions first" % failed)
    if args.check:
        print("\nall %d edits apply cleanly; nothing written (--check)" % len(EDITS))
        return
    if OUT.exists():
        shutil.rmtree(OUT)
    for folder in FOLDERS:
        shutil.copytree(SRC / folder, OUT / folder)
    shutil.copy2(SRC / "LICENSE.md", OUT / "LICENSE.md")
    for path, what, old, new in EDITS:
        target = OUT / path
        target.write_text(target.read_text(encoding="utf-8").replace(old, new),
                          encoding="utf-8", newline="")
    print("\nbuild/lsfg-vk: %s with %d edits" % (", ".join(FOLDERS), len(EDITS)))


if __name__ == "__main__":
    main()
