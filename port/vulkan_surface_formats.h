/* Complete WSI format enumeration, including lists that grow between queries.
 * Selection retains the backend's UNORM preference and legacy undefined case. */
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>

static inline VkResult buzz_vk_choose_surface_format(
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR query,
    VkPhysicalDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR* output)
{
    /* A changing surface must not leave initialization retrying forever.
     * Never select from a truncated list: the preferred format may be last. */
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        uint32_t count = 0;
        VkResult result = query(device, surface, &count, NULL);
        if (result != VK_SUCCESS) return result;
        if (!count) return VK_ERROR_FORMAT_NOT_SUPPORTED;
        if ((uint64_t)count * sizeof(VkSurfaceFormatKHR) > SIZE_MAX)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        VkSurfaceFormatKHR* formats = malloc((size_t)count * sizeof(*formats));
        if (!formats) return VK_ERROR_OUT_OF_HOST_MEMORY;
        const uint32_t capacity = count;
        result = query(device, surface, &count, formats);
        if (result == VK_SUCCESS && count && count <= capacity) {
            VkSurfaceFormatKHR selected = formats[0];
            if (count == 1 && selected.format == VK_FORMAT_UNDEFINED) {
                selected.format = VK_FORMAT_B8G8R8A8_UNORM;
            } else {
                static const VkFormat preferred[] = {
                    VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM
                };
                int found = 0;
                for (unsigned k = 0; k < 2 && !found; ++k)
                    for (uint32_t i = 0; i < count; ++i)
                        if (formats[i].format == preferred[k]) {
                            selected = formats[i]; found = 1; break;
                        }
            }
            free(formats);
            *output = selected;
            return VK_SUCCESS;
        }
        free(formats);
        if (result != VK_INCOMPLETE)
            return result == VK_SUCCESS ? VK_ERROR_FORMAT_NOT_SUPPORTED : result;
    }
    return VK_INCOMPLETE;
}
