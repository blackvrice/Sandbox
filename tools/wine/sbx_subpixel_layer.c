// 시험 전용 Vulkan 레이어 (ADR-0018): lavapipe 의 viewportSubPixelBits 를 8 로 올려 Wine 의 vkd3d(D3D12)가 FL 11_0 을 받게 한다.
// 제품 코드가 아니다 — tools/wine/run.sh 가 VK_LAYER_PATH 로 끼운다.
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <string.h>
#include <stdlib.h>

static PFN_vkGetInstanceProcAddr g_next_gipa;
static PFN_vkGetPhysicalDeviceProperties g_next_props;
static PFN_vkGetPhysicalDeviceProperties2 g_next_props2;
static PFN_vkGetPhysicalDeviceProperties2 g_next_props2khr;

static void patch(VkPhysicalDeviceProperties* p) {
    if (p->limits.viewportSubPixelBits < 8) p->limits.viewportSubPixelBits = 8;
}
static VKAPI_ATTR void VKAPI_CALL my_props(VkPhysicalDevice pd, VkPhysicalDeviceProperties* p) { g_next_props(pd, p); patch(p); }
static VKAPI_ATTR void VKAPI_CALL my_props2(VkPhysicalDevice pd, VkPhysicalDeviceProperties2* p) { g_next_props2(pd, p); patch(&p->properties); }
static VKAPI_ATTR void VKAPI_CALL my_props2khr(VkPhysicalDevice pd, VkPhysicalDeviceProperties2* p) { g_next_props2khr(pd, p); patch(&p->properties); }

static VKAPI_ATTR VkResult VKAPI_CALL my_create_instance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks* a, VkInstance* inst) {
    VkLayerInstanceCreateInfo* chain = (VkLayerInstanceCreateInfo*)ci->pNext;
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && chain->function == VK_LAYER_LINK_INFO))
        chain = (VkLayerInstanceCreateInfo*)chain->pNext;
    if (!chain) return VK_ERROR_INITIALIZATION_FAILED;
    g_next_gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)g_next_gipa(NULL, "vkCreateInstance");
    VkResult r = create(ci, a, inst);
    if (r != VK_SUCCESS) return r;
    g_next_props = (PFN_vkGetPhysicalDeviceProperties)g_next_gipa(*inst, "vkGetPhysicalDeviceProperties");
    g_next_props2 = (PFN_vkGetPhysicalDeviceProperties2)g_next_gipa(*inst, "vkGetPhysicalDeviceProperties2");
    g_next_props2khr = (PFN_vkGetPhysicalDeviceProperties2)g_next_gipa(*inst, "vkGetPhysicalDeviceProperties2KHR");
    return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL sbx_GetInstanceProcAddr(VkInstance inst, const char* name) {
    if (!strcmp(name, "vkCreateInstance")) return (PFN_vkVoidFunction)my_create_instance;
    if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)sbx_GetInstanceProcAddr;
    if (!strcmp(name, "vkGetPhysicalDeviceProperties")) return (PFN_vkVoidFunction)my_props;
    if (!strcmp(name, "vkGetPhysicalDeviceProperties2") && g_next_props2) return (PFN_vkVoidFunction)my_props2;
    if (!strcmp(name, "vkGetPhysicalDeviceProperties2KHR") && g_next_props2khr) return (PFN_vkVoidFunction)my_props2khr;
    return g_next_gipa ? g_next_gipa(inst, name) : NULL;
}

VKAPI_ATTR VkResult VKAPI_CALL sbx_NegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* v) {
    if (v->loaderLayerInterfaceVersion > 2) v->loaderLayerInterfaceVersion = 2;
    v->pfnGetInstanceProcAddr = sbx_GetInstanceProcAddr;
    v->pfnGetDeviceProcAddr = NULL;
    v->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
