#include "vk_renderer_memory.h"
#include "vk_renderer_context.h"

#include <stdint.h>
#include <string.h>

uint32_t vk_renderer_memory_find_type(VkRendererContext* ctx,
                                      uint32_t type_filter,
                                      VkMemoryPropertyFlags properties) {
    for (uint32_t i = 0; i < ctx->memory_properties.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) &&
            (ctx->memory_properties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

VkResult vk_renderer_memory_create_buffer(VkRendererContext* ctx,
                                          VkDeviceSize size,
                                          VkBufferUsageFlags usage,
                                          VkMemoryPropertyFlags properties,
                                          VkAllocatedBuffer* out_buffer) {
    if (!out_buffer) return VK_ERROR_INITIALIZATION_FAILED;
    memset(out_buffer, 0, sizeof(*out_buffer));

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VkResult result = vkCreateBuffer(ctx->device, &buffer_info, NULL, &out_buffer->buffer);
    if (result != VK_SUCCESS) return result;

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(ctx->device, out_buffer->buffer, &mem_requirements);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_requirements.size,
        .memoryTypeIndex = vk_renderer_memory_find_type(ctx, mem_requirements.memoryTypeBits, properties),
    };

    if (alloc_info.memoryTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(ctx->device, out_buffer->buffer, NULL);
        out_buffer->buffer = VK_NULL_HANDLE;
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    result = vkAllocateMemory(ctx->device, &alloc_info, NULL, &out_buffer->memory);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(ctx->device, out_buffer->buffer, NULL);
        out_buffer->buffer = VK_NULL_HANDLE;
        return result;
    }

    out_buffer->size = size;

    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        vkMapMemory(ctx->device, out_buffer->memory, 0, size, 0, &out_buffer->mapped);
    }

    vkBindBufferMemory(ctx->device, out_buffer->buffer, out_buffer->memory, 0);
    return VK_SUCCESS;
}

void vk_renderer_memory_destroy_buffer(VkRendererContext* ctx,
                                       VkAllocatedBuffer* buffer) {
    if (!buffer) return;
    if (buffer->mapped) {
        vkUnmapMemory(ctx->device, buffer->memory);
        buffer->mapped = NULL;
    }
    if (buffer->memory) {
        vkFreeMemory(ctx->device, buffer->memory, NULL);
        buffer->memory = VK_NULL_HANDLE;
    }
    if (buffer->buffer) {
        vkDestroyBuffer(ctx->device, buffer->buffer, NULL);
        buffer->buffer = VK_NULL_HANDLE;
    }
    buffer->size = 0;
}

VkResult vk_renderer_memory_create_image(VkRendererContext* ctx,
                                         VkExtent3D extent,
                                         VkFormat format,
                                         VkImageTiling tiling,
                                         VkImageUsageFlags usage,
                                         VkMemoryPropertyFlags properties,
                                         VkAllocatedImage* out_image) {
    if (!out_image) return VK_ERROR_INITIALIZATION_FAILED;
    memset(out_image, 0, sizeof(*out_image));

    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = extent,
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = tiling,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkResult result = vkCreateImage(ctx->device, &image_info, NULL, &out_image->image);
    if (result != VK_SUCCESS) return result;

    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(ctx->device, out_image->image, &mem_requirements);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_requirements.size,
        .memoryTypeIndex = vk_renderer_memory_find_type(ctx, mem_requirements.memoryTypeBits, properties),
    };

    if (alloc_info.memoryTypeIndex == UINT32_MAX) {
        vkDestroyImage(ctx->device, out_image->image, NULL);
        out_image->image = VK_NULL_HANDLE;
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    result = vkAllocateMemory(ctx->device, &alloc_info, NULL, &out_image->memory);
    if (result != VK_SUCCESS) {
        vkDestroyImage(ctx->device, out_image->image, NULL);
        out_image->image = VK_NULL_HANDLE;
        return result;
    }

    vkBindImageMemory(ctx->device, out_image->image, out_image->memory, 0);

    out_image->extent = extent;
    out_image->format = format;

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = out_image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
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

    result = vkCreateImageView(ctx->device, &view_info, NULL, &out_image->view);
    if (result != VK_SUCCESS) {
        vkFreeMemory(ctx->device, out_image->memory, NULL);
        vkDestroyImage(ctx->device, out_image->image, NULL);
        out_image->memory = VK_NULL_HANDLE;
        out_image->image = VK_NULL_HANDLE;
    }

    return result;
}

void vk_renderer_memory_destroy_image(VkRendererContext* ctx,
                                      VkAllocatedImage* image) {
    if (!image) return;
    if (image->view) {
        vkDestroyImageView(ctx->device, image->view, NULL);
        image->view = VK_NULL_HANDLE;
    }
    if (image->memory) {
        vkFreeMemory(ctx->device, image->memory, NULL);
        image->memory = VK_NULL_HANDLE;
    }
    if (image->image) {
        vkDestroyImage(ctx->device, image->image, NULL);
        image->image = VK_NULL_HANDLE;
    }
    image->format = VK_FORMAT_UNDEFINED;
}
