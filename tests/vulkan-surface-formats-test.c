/* Mock WSI queries: no Vulkan device, display, game assets or loader needed. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

static unsigned allocations, releases, live, fail_allocation;
static void* test_allocate(size_t bytes)
{
    ++allocations;
    if (fail_allocation && allocations == fail_allocation) return NULL;
    void* p = malloc(bytes);
    assert(p); ++live; return p;
}
static void test_release(void* p)
{
    assert(p && live); --live; ++releases; free(p);
}
#define malloc test_allocate
#define free test_release
#include "../port/vulkan_surface_formats.h"
#undef malloc
#undef free

static struct {
    uint32_t first_count, available, bgra, rgba;
    unsigned counts, fills, always_incomplete, empty_fill, undefined;
    VkResult count_error, fill_error;
} fixture;

static VkSurfaceFormatKHR entry(uint32_t i)
{
    VkSurfaceFormatKHR value = {
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        i & 1 ? VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
    };
    if (i == fixture.rgba) value.format = VK_FORMAT_R8G8B8A8_UNORM;
    if (i == fixture.bgra) value.format = VK_FORMAT_B8G8R8A8_UNORM;
    if (fixture.undefined) value.format = VK_FORMAT_UNDEFINED;
    return value;
}

static VKAPI_ATTR VkResult VKAPI_CALL query(VkPhysicalDevice device,
    VkSurfaceKHR surface, uint32_t* count, VkSurfaceFormatKHR* formats)
{
    (void)device; (void)surface;
    if (!formats) {
        *count = fixture.counts++ ? fixture.available : fixture.first_count;
        return fixture.count_error;
    }
    ++fixture.fills;
    if (fixture.fill_error != VK_SUCCESS) return fixture.fill_error;
    const uint32_t capacity = *count;
    const uint32_t written = fixture.empty_fill ? 0 :
        capacity < fixture.available ? capacity : fixture.available;
    for (uint32_t i = 0; i < written; ++i) formats[i] = entry(i);
    *count = written;
    return capacity < fixture.available || fixture.always_incomplete ? VK_INCOMPLETE : VK_SUCCESS;
}

static unsigned cases;
static void reset(uint32_t first, uint32_t available, uint32_t bgra, uint32_t rgba)
{
    assert(!live);
    memset(&fixture, 0, sizeof fixture);
    fixture.first_count = first; fixture.available = available;
    fixture.bgra = bgra; fixture.rgba = rgba;
    allocations = releases = fail_allocation = 0;
}
static void check(VkResult expected, uint32_t selected)
{
    const VkSurfaceFormatKHR sentinel = {VK_FORMAT_R32_SFLOAT, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    VkSurfaceFormatKHR actual = sentinel;
    const VkResult result = buzz_vk_choose_surface_format(query, VK_NULL_HANDLE, VK_NULL_HANDLE, &actual);
    assert(result == expected && !live);
    assert(allocations == releases + (fail_allocation != 0));
    if (expected == VK_SUCCESS) {
        VkSurfaceFormatKHR wanted = entry(selected);
        if (fixture.undefined) wanted.format = VK_FORMAT_B8G8R8A8_UNORM;
        assert(actual.format == wanted.format && actual.colorSpace == wanted.colorSpace);
    } else {
        assert(actual.format == sentinel.format && actual.colorSpace == sentinel.colorSpace);
    }
    ++cases;
}

int main(void)
{
    /* Preferred formats at every location, including beyond the old limit;
     * BGRA wins over an earlier RGBA entry and preserves its color space. */
    for (uint32_t n = 1; n <= 257; ++n) {
        for (uint32_t i = 0; i < n; ++i) {
            reset(n, n, i, 0); check(VK_SUCCESS, i);
            reset(n, n, UINT32_MAX, i); check(VK_SUCCESS, i);
        }
        reset(n, n, UINT32_MAX, UINT32_MAX); check(VK_SUCCESS, 0);
    }
    reset(64, 130, 129, 0); check(VK_SUCCESS, 129);
    assert(fixture.counts == 2 && fixture.fills == 2);
    reset(130, 3, UINT32_MAX, 2); check(VK_SUCCESS, 2);
    reset(1, 1, UINT32_MAX, UINT32_MAX); fixture.undefined = 1; check(VK_SUCCESS, 0);

    reset(0, 0, 0, 0); check(VK_ERROR_FORMAT_NOT_SUPPORTED, 0);
    assert(!allocations && !fixture.fills);
    reset(4, 4, 0, 0); fixture.empty_fill = 1; check(VK_ERROR_FORMAT_NOT_SUPPORTED, 0);
    reset(4, 4, 0, 0); fixture.count_error = VK_ERROR_SURFACE_LOST_KHR;
    check(VK_ERROR_SURFACE_LOST_KHR, 0); assert(!allocations && !fixture.fills);
    reset(4, 4, 0, 0); fixture.fill_error = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    check(VK_ERROR_OUT_OF_DEVICE_MEMORY, 0);
    reset(4, 4, 0, 0); fail_allocation = 1;
    check(VK_ERROR_OUT_OF_HOST_MEMORY, 0); assert(!fixture.fills);
    reset(64, 130, 129, 0); fail_allocation = 2;
    check(VK_ERROR_OUT_OF_HOST_MEMORY, 0); assert(fixture.fills == 1);
    reset(UINT32_MAX, UINT32_MAX, 0, 0);
    if ((uint64_t)UINT32_MAX * sizeof(VkSurfaceFormatKHR) <= SIZE_MAX) fail_allocation = 1;
    check(VK_ERROR_OUT_OF_HOST_MEMORY, 0); assert(!fixture.fills);
    reset(4, 4, 0, 0); fixture.always_incomplete = 1;
    check(VK_INCOMPLETE, 0); assert(fixture.counts == 8 && fixture.fills == 8);
    puts("PASS complete surface formats: growth, shrink, >64 entries, preference/color space, bounded retries, allocation and driver failures");
    printf("PASS %u cases; no leaked allocations; failure leaves selection unchanged\n", cases);
    return 0;
}
