// PLAN 9.21: whether a second Vulkan device of higher global priority gets its work
// through while the game renders (warden_priority_probe.cc; files/hack-priority-probe).
#ifndef WARDEN_PRIORITY_PROBE_H_
#define WARDEN_PRIORITY_PROBE_H_

namespace xe::ui::vulkan {
class VulkanDevice;
}

namespace warden::priority_probe {

// Waits delay_seconds on a thread of its own, then measures and logs - a second device
// at each global priority.
void Start(const xe::ui::vulkan::VulkanDevice* game, int delay_seconds);

}  // namespace warden::priority_probe

#endif  // WARDEN_PRIORITY_PROBE_H_
