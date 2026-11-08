/* Real Win32/Vulkan WSI regression. No guest files, focus or desktop input.
 * Compile against the production adapter using test-vulkan-dpi.ps1. */
#include BUZZ_VULKAN_UPLOAD_SOURCE
#include <stdio.h>
#include <stdlib.h>

unsigned char* vm_base;
struct dpi_fentry { uint64_t address; void* function; const char* name; };
const struct dpi_fentry function_table[] = {{0,NULL,NULL}};
const uint64_t function_table_count = 0;
typedef HANDLE (WINAPI *get_context_fn)(void);
typedef BOOL (WINAPI *equal_context_fn)(HANDLE,HANDLE);
typedef HANDLE (WINAPI *set_context_fn)(HANDLE);
static get_context_fn get_context;
static equal_context_fn equal_context;
static HANDLE caller_context;
static int error_query_seen;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR capabilities_driver;
static void check(int ok,const char* why) {
    if(!ok){fprintf(stderr,"FAIL %s\n",why);exit(1);}
}
static VKAPI_ATTR VkResult VKAPI_CALL query_error(VkPhysicalDevice device,
    VkSurfaceKHR surface,VkSurfaceCapabilitiesKHR* caps) {
    (void)device;(void)surface;(void)caps;
    error_query_seen=1;
    check(equal_context(get_context(),(HANDLE)(INT_PTR)-4),"WSI error query must be per-monitor V2");
    return VK_ERROR_SURFACE_LOST_KHR;
}
static VKAPI_ATTR VkResult VKAPI_CALL query_flexible_extent(VkPhysicalDevice device,
    VkSurfaceKHR surface,VkSurfaceCapabilitiesKHR* caps) {
    check(equal_context(get_context(),(HANDLE)(INT_PTR)-4),"flexible extent query must be per-monitor V2");
    VkResult result=capabilities_driver(device,surface,caps);
    if(result==VK_SUCCESS) caps->currentExtent=(VkExtent2D){UINT32_MAX,UINT32_MAX};
    return result;
}
static void extent_contract(const char* phase) {
    unsigned w=0,h=0;
    check(rsx_host_window_client(&s_host_window,&w,&h),"physical client available");
    fprintf(stderr,"%s physical=%ux%u swapchain=%ux%u\n",phase,w,h,s_vk.sc_extent.width,s_vk.sc_extent.height);
    check(w==s_vk.sc_extent.width&&h==s_vk.sc_extent.height,"swapchain must equal physical client, not DPI-virtualized coordinates");
    check(equal_context(get_context(),caller_context),"caller DPI context restored");
}
int main(void) {
    HMODULE user32=GetModuleHandleA("user32.dll");
    get_context=(get_context_fn)GetProcAddress(user32,"GetThreadDpiAwarenessContext");
    equal_context=(equal_context_fn)GetProcAddress(user32,"AreDpiAwarenessContextsEqual");
    set_context_fn set_context=(set_context_fn)GetProcAddress(user32,"SetThreadDpiAwarenessContext");
    if(!get_context||!equal_context||!set_context)return 77;
    HANDLE original=set_context((HANDLE)(INT_PTR)-1);
    check(original!=NULL,"select unaware calling thread without changing process/system settings");
    caller_context=get_context();
    _putenv_s("RSX_PIXEL_WINDOW","1");
    _putenv_s("RSX_WINDOW_BACKGROUND","1");
    _putenv_s("PS3RECOMP_VK_WINDOW","1");
    _putenv_s("PS3RECOMP_VK_GUEST_PROGRAMS","0");
    _putenv_s("PS3RECOMP_RSX_ENGINE","vtable");
    ps3_host_set_window_size(1280,720);ps3_host_set_fullscreen(0);
    ps3_host_set_render_scale(100);
    check(!rsx_vulkan_backend_init(1280,720,"Buzz DPI synthetic regression"),"Vulkan initialization");
    check(rsx_vulkan_backend_windowed(),"real window/swapchain required");
    fprintf(stderr,"window DPI=%u\n",GetDpiForWindow(s_vk.window));
    extent_contract("initial");
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR driver=pvkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    capabilities_driver=driver;
    pvkGetPhysicalDeviceSurfaceCapabilitiesKHR=query_error;
    check(vk_create_swapchain()!=0&&error_query_seen,"injected early WSI error");
    check(equal_context(get_context(),caller_context),"error exit restores caller DPI context");
    pvkGetPhysicalDeviceSurfaceCapabilitiesKHR=driver;
    ps3_host_set_window_size(1600,900);vk_host_update_window_mode();vk_swapchain_lost();
    extent_contract("resized");
    pvkGetPhysicalDeviceSurfaceCapabilitiesKHR=query_flexible_extent;
    vk_swapchain_lost();extent_contract("flexible-extent");
    pvkGetPhysicalDeviceSurfaceCapabilitiesKHR=driver;
    /* Both presentation filters use the same physical-sized destination. */
    for(unsigned filter=0;filter<2;filter++) {
        ps3_host_set_upscaler(filter?PS3_HOST_UPSCALE_FSR1:PS3_HOST_UPSCALE_BILINEAR,80);
        rsx_vulkan_backend_present();
        extent_contract(filter?"FSR1":"bilinear");
        if(filter) {
            ps3_host_rect fit=ps3_host_fit_rect(s_vk.sc_extent.width,s_vk.sc_extent.height,s_vk.guest_w,s_vk.guest_h);
            if(fit.width>s_vk.width||fit.height>s_vk.height)
                check(s_fsr.ok&&s_fsr.mid_w==fit.width&&s_fsr.mid_h==fit.height,"FSR targets physical presentation size");
        }
    }
    ps3_host_set_fullscreen(1);vk_host_update_window_mode();vk_swapchain_lost();
    extent_contract("borderless");
    ps3_host_set_fullscreen(0);vk_host_update_window_mode();vk_swapchain_lost();
    extent_contract("windowed-again");
    rsx_vulkan_backend_shutdown();
    check(equal_context(get_context(),caller_context),"shutdown restores caller DPI context");
    set_context(original);
    puts("PASS physical-pixel WSI: initial/resize/fullscreen/filters/error exit; no system DPI change");
    return 0;
}
