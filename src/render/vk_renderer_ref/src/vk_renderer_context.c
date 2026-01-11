#include "vk_renderer_context.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data) {
    (void)message_type;
    (void)user_data;

    if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        fprintf(stderr, "[vulkan] %s\n", callback_data->pMessage);
    }
    return VK_FALSE;
}

static VkBool32 validation_layers_available(const char* const* layers,
                                            uint32_t layer_count) {
    uint32_t available_count = 0;
    if (vkEnumerateInstanceLayerProperties(&available_count, NULL) != VK_SUCCESS) {
        return VK_FALSE;
    }

    if (available_count == 0) {
        return VK_FALSE;
    }

    VkLayerProperties* available =
        (VkLayerProperties*)malloc(sizeof(VkLayerProperties) * available_count);
    if (!available) {
        return VK_FALSE;
    }

    VkResult result = vkEnumerateInstanceLayerProperties(&available_count, available);
    if (result != VK_SUCCESS) {
        free(available);
        return VK_FALSE;
    }

    for (uint32_t i = 0; i < layer_count; ++i) {
        VkBool32 found = VK_FALSE;
        for (uint32_t j = 0; j < available_count; ++j) {
            if (strcmp(layers[i], available[j].layerName) == 0) {
                found = VK_TRUE;
                break;
            }
        }
        if (!found) {
            free(available);
            return VK_FALSE;
        }
    }

    free(available);
    return VK_TRUE;
}

static VkResult create_debug_messenger(VkInstance instance,
                                       VkBool32 enable,
                                       VkDebugUtilsMessengerEXT* messenger) {
    if (!enable) {
        *messenger = VK_NULL_HANDLE;
        return VK_SUCCESS;
    }

    VkDebugUtilsMessengerCreateInfoEXT create_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = NULL,
        .flags = 0,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
        .pUserData = NULL,
    };

    PFN_vkCreateDebugUtilsMessengerEXT func =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkCreateDebugUtilsMessengerEXT");

    if (!func) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    return func(instance, &create_info, NULL, messenger);
}

static void destroy_debug_messenger(VkInstance instance,
                                    VkDebugUtilsMessengerEXT messenger) {
    if (messenger == VK_NULL_HANDLE) return;
    PFN_vkDestroyDebugUtilsMessengerEXT func =
        (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func) {
        func(instance, messenger, NULL);
    }
}

static VkResult get_instance_extensions(SDL_Window* window,
                                        const VkRendererConfig* config,
                                        VkBool32 enable_validation,
                                        char*** out_extensions,
                                        uint32_t* out_count) {
    unsigned int sdl_count = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &sdl_count, NULL)) {
        fprintf(stderr, "SDL_Vulkan_GetInstanceExtensions count failed: %s\n", SDL_GetError());
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    VkBool32 add_portability_extension = VK_FALSE;
#if defined(__APPLE__)
    add_portability_extension = VK_TRUE;
#endif

    uint32_t total = sdl_count + (enable_validation ? 1u : 0u) +
                     config->extra_instance_extension_count +
                     (add_portability_extension ? 1u : 0u);
    char** list = (char**)calloc(total, sizeof(char*));
    if (!list) return VK_ERROR_OUT_OF_HOST_MEMORY;

    if (!SDL_Vulkan_GetInstanceExtensions(window, &sdl_count, (const char**)list)) {
        fprintf(stderr, "SDL_Vulkan_GetInstanceExtensions list failed: %s\n", SDL_GetError());
        free(list);
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    uint32_t index = sdl_count;
    if (enable_validation) {
        list[index++] = (char*)VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }

    for (uint32_t i = 0; i < config->extra_instance_extension_count; ++i) {
        list[index++] = (char*)config->extra_instance_extensions[i];
    }

    if (add_portability_extension) {
        VkBool32 already_present = VK_FALSE;
        for (uint32_t i = 0; i < index; ++i) {
            if (strcmp(list[i], VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {
                already_present = VK_TRUE;
                break;
            }
        }
        if (!already_present) {
            list[index++] = (char*)VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
        }
    }

    *out_extensions = list;
    *out_count = index;
    return VK_SUCCESS;
}

static VkResult create_instance(VkRendererContext* ctx,
                                SDL_Window* window,
                                const VkRendererConfig* config,
                                VkBool32* out_validation_enabled) {
    const char* validation_layers[] = {"VK_LAYER_KHRONOS_validation"};
    VkBool32 enable_validation = config->enable_validation;

    if (enable_validation && !validation_layers_available(validation_layers, 1)) {
        fprintf(stderr,
                "[vulkan] validation layer requested but not available; disabling validation.\n");
        enable_validation = VK_FALSE;
    }

    for (;;) {
        char** extensions = NULL;
        uint32_t extension_count = 0;
        VkResult result = get_instance_extensions(window, config, enable_validation, &extensions,
                                                  &extension_count);
        if (result != VK_SUCCESS) {
            return result;
        }

        VkApplicationInfo app_info = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = NULL,
            .pApplicationName = "SDL Vulkan Renderer",
            .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
            .pEngineName = "custom",
            .engineVersion = VK_MAKE_VERSION(0, 1, 0),
            .apiVersion = VK_API_VERSION_1_2,
        };

        VkInstanceCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .pApplicationInfo = &app_info,
            .enabledExtensionCount = extension_count,
            .ppEnabledExtensionNames = (const char* const*)extensions,
            .enabledLayerCount = enable_validation ? 1u : 0u,
            .ppEnabledLayerNames = enable_validation ? validation_layers : NULL,
        };

#if defined(__APPLE__)
        create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

        VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = NULL,
            .flags = 0,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debug_callback,
            .pUserData = NULL,
        };

        if (enable_validation) {
            create_info.pNext = &debug_create_info;
        }

        result = vkCreateInstance(&create_info, NULL, &ctx->instance);
        free(extensions);

        if (result == VK_SUCCESS) {
            if (out_validation_enabled) {
                *out_validation_enabled = enable_validation;
            }
            return VK_SUCCESS;
        }

        if (enable_validation &&
            (result == VK_ERROR_LAYER_NOT_PRESENT || result == VK_ERROR_EXTENSION_NOT_PRESENT)) {
            fprintf(stderr,
                    "[vulkan] validation layer failed to load (error %d); retrying without validation.\n",
                    result);
            enable_validation = VK_FALSE;
            continue;
        }

        return result;
    }
}

static VkBool32 queue_family_supports_surface(VkPhysicalDevice device,
                                              uint32_t index,
                                              VkSurfaceKHR surface) {
    VkBool32 present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &present);
    return present;
}

typedef struct QueueFamilySelection {
    uint32_t graphics_index;
    uint32_t present_index;
    VkBool32 has_graphics;
    VkBool32 has_present;
} QueueFamilySelection;

static QueueFamilySelection select_queue_families(VkPhysicalDevice device,
                                                  VkSurfaceKHR surface) {
    QueueFamilySelection selection = {0, 0, VK_FALSE, VK_FALSE};

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    if (count == 0) return selection;

    VkQueueFamilyProperties* properties =
        (VkQueueFamilyProperties*)malloc(sizeof(VkQueueFamilyProperties) * count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties);

    for (uint32_t i = 0; i < count; ++i) {
        if (!selection.has_graphics &&
            (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            selection.graphics_index = i;
            selection.has_graphics = VK_TRUE;
        }

        if (!selection.has_present && queue_family_supports_surface(device, i, surface)) {
            selection.present_index = i;
            selection.has_present = VK_TRUE;
        }

        if (selection.has_graphics && selection.has_present) break;
    }

    free(properties);
    return selection;
}

static VkBool32 device_supports_extensions(VkPhysicalDevice device,
                                           const char* const* extensions,
                                           uint32_t extension_count) {
    uint32_t available_count = 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &available_count, NULL);
    VkExtensionProperties* available =
        (VkExtensionProperties*)malloc(sizeof(VkExtensionProperties) * available_count);
    vkEnumerateDeviceExtensionProperties(device, NULL, &available_count, available);

    VkBool32 supported = VK_TRUE;
    for (uint32_t i = 0; i < extension_count; ++i) {
        VkBool32 found = VK_FALSE;
        for (uint32_t j = 0; j < available_count; ++j) {
            if (strcmp(extensions[i], available[j].extensionName) == 0) {
                found = VK_TRUE;
                break;
            }
        }
        if (!found) {
            supported = VK_FALSE;
            break;
        }
    }

    free(available);
    return supported;
}

static VkBool32 device_supports_swapchain(VkPhysicalDevice device,
                                          VkSurfaceKHR surface) {
    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, NULL);
    if (format_count == 0) return VK_FALSE;
    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, NULL);
    if (present_mode_count == 0) return VK_FALSE;
    return VK_TRUE;
}

static VkBool32 device_is_suitable(VkPhysicalDevice device,
                                   VkSurfaceKHR surface,
                                   const VkRendererConfig* config) {
    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    uint32_t extension_count = 1;

    VkBool32 supports_required = device_supports_extensions(device, extensions, extension_count);
    if (!supports_required) return VK_FALSE;

    if (config->extra_device_extension_count > 0) {
        supports_required = device_supports_extensions(
            device, config->extra_device_extensions, config->extra_device_extension_count);
        if (!supports_required) return VK_FALSE;
    }

    if (!device_supports_swapchain(device, surface)) return VK_FALSE;

    QueueFamilySelection families = select_queue_families(device, surface);
    return families.has_graphics && families.has_present;
}

static VkResult pick_physical_device(VkRendererContext* ctx,
                                     const VkRendererConfig* config) {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &device_count, NULL);
    if (device_count == 0) {
        fprintf(stderr, "No Vulkan physical devices found.\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDevice* devices =
        (VkPhysicalDevice*)malloc(sizeof(VkPhysicalDevice) * device_count);
    vkEnumeratePhysicalDevices(ctx->instance, &device_count, devices);

    VkPhysicalDevice selected = VK_NULL_HANDLE;

    for (uint32_t i = 0; i < device_count; ++i) {
        VkPhysicalDevice device = devices[i];
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        if (!device_is_suitable(device, ctx->surface, config)) continue;

        if (config->prefer_discrete_gpu &&
            props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            selected = device;
            break;
        }

        if (selected == VK_NULL_HANDLE) {
            selected = device;
        }
    }

    if (selected == VK_NULL_HANDLE) {
        fprintf(stderr, "No suitable Vulkan device found.\n");
        free(devices);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    ctx->physical_device = selected;
    vkGetPhysicalDeviceProperties(ctx->physical_device, &ctx->physical_device_properties);
    vkGetPhysicalDeviceMemoryProperties(ctx->physical_device, &ctx->memory_properties);

    QueueFamilySelection families = select_queue_families(ctx->physical_device, ctx->surface);
    ctx->graphics_queue_family = families.graphics_index;
    ctx->present_queue_family = families.present_index;

    free(devices);
    return VK_SUCCESS;
}

static VkResult create_logical_device(VkRendererContext* ctx,
                                      const VkRendererConfig* config) {
    QueueFamilySelection families = select_queue_families(ctx->physical_device, ctx->surface);

    float queue_priority = 1.0f;

    VkDeviceQueueCreateInfo queue_create_infos[2];
    uint32_t queue_info_count = 0;

    queue_create_infos[queue_info_count++] = (VkDeviceQueueCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueFamilyIndex = families.graphics_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    if (families.graphics_index != families.present_index) {
        queue_create_infos[queue_info_count++] = (VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .queueFamilyIndex = families.present_index,
            .queueCount = 1,
            .pQueuePriorities = &queue_priority,
        };
    }

    VkPhysicalDeviceFeatures device_features = {
        .samplerAnisotropy = VK_TRUE,
    };

    const char* device_extensions[8];
    uint32_t device_extension_count = 0;
    device_extensions[device_extension_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    for (uint32_t i = 0; i < config->extra_device_extension_count; ++i) {
        device_extensions[device_extension_count++] = config->extra_device_extensions[i];
    }

    VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueCreateInfoCount = queue_info_count,
        .pQueueCreateInfos = queue_create_infos,
        .enabledExtensionCount = device_extension_count,
        .ppEnabledExtensionNames = device_extensions,
        .pEnabledFeatures = &device_features,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
    };

    VkResult result = vkCreateDevice(ctx->physical_device, &create_info, NULL, &ctx->device);
    if (result != VK_SUCCESS) return result;

    vkGetDeviceQueue(ctx->device, families.graphics_index, 0, &ctx->graphics_queue);
    vkGetDeviceQueue(ctx->device, families.present_index, 0, &ctx->present_queue);

    ctx->graphics_queue_family = families.graphics_index;
    ctx->present_queue_family = families.present_index;

    return VK_SUCCESS;
}

static VkSurfaceFormatKHR choose_surface_format(VkPhysicalDevice device,
                                                VkSurfaceKHR surface) {
    (void)device;
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, NULL);
    VkSurfaceFormatKHR* formats =
        (VkSurfaceFormatKHR*)malloc(sizeof(VkSurfaceFormatKHR) * count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, formats);

    VkSurfaceFormatKHR chosen = formats[0];
    for (uint32_t i = 0; i < count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = formats[i];
            break;
        }
    }

    free(formats);
    return chosen;
}

static VkPresentModeKHR choose_present_mode(VkPhysicalDevice device,
                                            VkSurfaceKHR surface) {
    (void)device;
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, NULL);
    VkPresentModeKHR* modes =
        (VkPresentModeKHR*)malloc(sizeof(VkPresentModeKHR) * count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, modes);

    VkPresentModeKHR chosen = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < count; ++i) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            chosen = modes[i];
            break;
        }
    }

    free(modes);
    return chosen;
}

static VkExtent2D choose_extent(SDL_Window* window,
                                VkSurfaceCapabilitiesKHR capabilities) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    SDL_Vulkan_GetDrawableSize(window, &width, &height);

    VkExtent2D extent = {
        .width = (uint32_t)width,
        .height = (uint32_t)height,
    };

    if (extent.width < capabilities.minImageExtent.width)
        extent.width = capabilities.minImageExtent.width;
    if (extent.width > capabilities.maxImageExtent.width)
        extent.width = capabilities.maxImageExtent.width;

    if (extent.height < capabilities.minImageExtent.height)
        extent.height = capabilities.minImageExtent.height;
    if (extent.height > capabilities.maxImageExtent.height)
        extent.height = capabilities.maxImageExtent.height;

    return extent;
}

static void destroy_swapchain(VkRendererContext* ctx) {
    if (ctx->swapchain.image_views) {
        for (uint32_t i = 0; i < ctx->swapchain.image_count; ++i) {
            if (ctx->swapchain.image_views[i]) {
                vkDestroyImageView(ctx->device, ctx->swapchain.image_views[i], NULL);
            }
        }
        free(ctx->swapchain.image_views);
    }

    if (ctx->swapchain.images) {
        free(ctx->swapchain.images);
    }

    if (ctx->swapchain.handle != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(ctx->device, ctx->swapchain.handle, NULL);
    }

    ctx->swapchain = (VkRendererSwapchain){0};
}

static VkResult create_swapchain(VkRendererContext* ctx,
                                 SDL_Window* window,
                                 const VkRendererConfig* config) {
    (void)config;
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        ctx->physical_device, ctx->surface, &capabilities);

    VkSurfaceFormatKHR surface_format =
        choose_surface_format(ctx->physical_device, ctx->surface);
    VkPresentModeKHR present_mode =
        choose_present_mode(ctx->physical_device, ctx->surface);
    VkExtent2D extent = choose_extent(window, capabilities);

    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = ctx->surface,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    uint32_t queue_family_indices[] = {ctx->graphics_queue_family, ctx->present_queue_family};
    if (ctx->graphics_queue_family != ctx->present_queue_family) {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_family_indices;
    } else {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.queueFamilyIndexCount = 0;
        create_info.pQueueFamilyIndices = NULL;
    }

    VkResult result =
        vkCreateSwapchainKHR(ctx->device, &create_info, NULL, &ctx->swapchain.handle);
    if (result != VK_SUCCESS) return result;

    ctx->swapchain.image_format = surface_format.format;
    ctx->swapchain.extent = extent;

    vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain.handle, &image_count, NULL);
    ctx->swapchain.image_count = image_count;
    ctx->swapchain.images =
        (VkImage*)malloc(sizeof(VkImage) * ctx->swapchain.image_count);
    vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain.handle, &image_count,
                            ctx->swapchain.images);

    ctx->swapchain.image_views =
        (VkImageView*)calloc(ctx->swapchain.image_count, sizeof(VkImageView));
    for (uint32_t i = 0; i < ctx->swapchain.image_count; ++i) {
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = ctx->swapchain.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = ctx->swapchain.image_format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };

        result = vkCreateImageView(ctx->device, &view_info, NULL,
                                   &ctx->swapchain.image_views[i]);
        if (result != VK_SUCCESS) {
            return result;
        }
    }

    return VK_SUCCESS;
}

VkResult vk_renderer_context_create(VkRendererContext* ctx,
                                    SDL_Window* window,
                                    const VkRendererConfig* config) {
    if (!ctx || !window || !config) return VK_ERROR_INITIALIZATION_FAILED;
    memset(ctx, 0, sizeof(*ctx));

    VkBool32 validation_enabled = config->enable_validation;

    VkResult result = create_instance(ctx, window, config, &validation_enabled);
    if (result != VK_SUCCESS) return result;

    result = create_debug_messenger(ctx->instance, validation_enabled, &ctx->debug_messenger);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create debug messenger.\n");
    }

    if (!SDL_Vulkan_CreateSurface(window, ctx->instance, &ctx->surface)) {
        fprintf(stderr, "SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return VK_ERROR_SURFACE_LOST_KHR;
    }

    result = pick_physical_device(ctx, config);
    if (result != VK_SUCCESS) return result;

    result = create_logical_device(ctx, config);
    if (result != VK_SUCCESS) return result;

    result = create_swapchain(ctx, window, config);
    return result;
}

void vk_renderer_context_destroy(VkRendererContext* ctx) {
    if (!ctx) return;

    destroy_swapchain(ctx);

    if (ctx->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(ctx->device);
        vkDestroyDevice(ctx->device, NULL);
    }

    if (ctx->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);
    }

    destroy_debug_messenger(ctx->instance, ctx->debug_messenger);

    if (ctx->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(ctx->instance, NULL);
    }

    memset(ctx, 0, sizeof(*ctx));
}

VkResult vk_renderer_context_recreate_swapchain(VkRendererContext* ctx,
                                                SDL_Window* window,
                                                const VkRendererConfig* config) {
    vkDeviceWaitIdle(ctx->device);
    destroy_swapchain(ctx);
    return create_swapchain(ctx, window, config);
}
