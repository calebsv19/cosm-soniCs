#include "vk_renderer.h"

#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_vulkan.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VK_RENDERER_CAPTURE_FILENAME "vk_frame.ppm"

#define VK_RENDERER_VERTEX_BUFFER_SIZE (256 * 1024)

#if defined(VK_RENDERER_DEBUG) && VK_RENDERER_DEBUG
#define VK_RENDERER_DEBUG_ENABLED 1
#else
#define VK_RENDERER_DEBUG_ENABLED 0
#endif

#if defined(VK_RENDERER_FRAME_DEBUG) && VK_RENDERER_FRAME_DEBUG && VK_RENDERER_DEBUG_ENABLED
#define VK_RENDERER_FRAME_DEBUG_ENABLED 1
#else
#define VK_RENDERER_FRAME_DEBUG_ENABLED 0
#endif

#if VK_RENDERER_DEBUG_ENABLED
#define VK_RENDERER_DEBUG_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define VK_RENDERER_DEBUG_LOG(...)
#endif

#if VK_RENDERER_FRAME_DEBUG_ENABLED
#define VK_RENDERER_FRAME_DEBUG_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define VK_RENDERER_FRAME_DEBUG_LOG(...)
#endif

static int s_logged_vertex_buffer_failure = 0;
static int s_logged_logical_size = 0;
static int s_logged_capture_dump = 0;
#if VK_RENDERER_FRAME_DEBUG_ENABLED
static uint64_t s_total_vertices_logged = 0;
static uint64_t s_total_lines_logged = 0;
#endif

static VkResult create_descriptor_resources(VkRenderer* renderer) {
    VkDescriptorSetLayoutBinding sampler_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = NULL,
    };

    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &sampler_binding,
    };

    VkResult result = vkCreateDescriptorSetLayout(renderer->context.device, &layout_info, NULL,
                                                  &renderer->sampler_set_layout);
    if (result != VK_SUCCESS) return result;

    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 4096,
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 4096,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };

    return vkCreateDescriptorPool(renderer->context.device, &pool_info, NULL,
                                  &renderer->descriptor_pool);
}

static void flush_transient_textures(VkRenderer* renderer, VkRendererFrameState* frame) {
    if (!frame || !frame->transient_textures) {
        frame->transient_texture_count = 0;
        return;
    }
    for (uint32_t i = 0; i < frame->transient_texture_count; ++i) {
        vk_renderer_texture_destroy(renderer, &frame->transient_textures[i]);
    }
    frame->transient_texture_count = 0;
}

static VkResult create_render_pass(VkRenderer* renderer) {
    VkAttachmentDescription color_attachment = {
        .format = renderer->context.swapchain.image_format,
        .samples = renderer->config.msaa_samples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentReference color_reference = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_reference,
    };

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo render_pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    return vkCreateRenderPass(renderer->context.device, &render_pass_info, NULL,
                              &renderer->render_pass);
}

static void destroy_framebuffers(VkRenderer* renderer) {
    if (!renderer->swapchain_framebuffers) return;
    for (uint32_t i = 0; i < renderer->swapchain_framebuffer_count; ++i) {
        if (renderer->swapchain_framebuffers[i]) {
            vkDestroyFramebuffer(renderer->context.device,
                                 renderer->swapchain_framebuffers[i], NULL);
        }
    }
    free(renderer->swapchain_framebuffers);
    renderer->swapchain_framebuffers = NULL;
    renderer->swapchain_framebuffer_count = 0;
}

static VkResult create_framebuffers(VkRenderer* renderer) {
    destroy_framebuffers(renderer);

    uint32_t count = renderer->context.swapchain.image_count;
    renderer->swapchain_framebuffers =
        (VkFramebuffer*)calloc(count, sizeof(VkFramebuffer));
    if (!renderer->swapchain_framebuffers) return VK_ERROR_OUT_OF_HOST_MEMORY;

    for (uint32_t i = 0; i < count; ++i) {
        VkImageView attachments[] = {renderer->context.swapchain.image_views[i]};

        VkFramebufferCreateInfo framebuffer_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = renderer->render_pass,
            .attachmentCount = 1,
            .pAttachments = attachments,
            .width = renderer->context.swapchain.extent.width,
            .height = renderer->context.swapchain.extent.height,
            .layers = 1,
        };

        VkResult result = vkCreateFramebuffer(renderer->context.device, &framebuffer_info, NULL,
                                              &renderer->swapchain_framebuffers[i]);
        if (result != VK_SUCCESS) return result;
    }

    renderer->swapchain_framebuffer_count = count;
    return VK_SUCCESS;
}

static VkResult create_pipeline_cache(VkRenderer* renderer) {
    VkPipelineCacheCreateInfo cache_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
    };
    return vkCreatePipelineCache(renderer->context.device, &cache_info, NULL,
                                 &renderer->pipeline_cache);
}

static VkResult ensure_frame_vertex_buffer(VkRenderer* renderer,
                                           VkRendererFrameState* frame,
                                           VkDeviceSize required) {
    if (frame->vertex_buffer.buffer != VK_NULL_HANDLE &&
        frame->vertex_offset + required <= frame->vertex_buffer.size) {
        return VK_SUCCESS;
    }

    VkDeviceSize new_size = VK_RENDERER_VERTEX_BUFFER_SIZE;
    while (new_size < required) new_size *= 2;

    VkDeviceSize previous_offset = frame->vertex_offset;

    VkAllocatedBuffer new_buffer = {0};
    VkResult result = vk_renderer_memory_create_buffer(
        &renderer->context, new_size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &new_buffer);
    if (result != VK_SUCCESS) {
        if (!s_logged_vertex_buffer_failure) {
            fprintf(stderr,
                    "[vulkan] vk_renderer_memory_create_buffer failed (%d) when requesting %llu bytes "
                    "(frame_index=%u).\n",
                    result, (unsigned long long)new_size, renderer->current_frame_index);
            s_logged_vertex_buffer_failure = 1;
        }
        return result;
    }
    s_logged_vertex_buffer_failure = 0;

    if (frame->vertex_buffer.buffer != VK_NULL_HANDLE && frame->vertex_offset > 0) {
        memcpy(new_buffer.mapped, frame->vertex_buffer.mapped, (size_t)frame->vertex_offset);
        vk_renderer_memory_destroy_buffer(&renderer->context, &frame->vertex_buffer);
    }

    frame->vertex_buffer = new_buffer;
    frame->vertex_offset = previous_offset;
    return VK_SUCCESS;
}

static void push_basic_constants(VkRenderer* renderer,
                                 VkCommandBuffer cmd,
                                 VkRendererPipelineKind kind) {
    float logical_w = renderer->draw_state.logical_size[0];
    float logical_h = renderer->draw_state.logical_size[1];

    if (logical_w <= 0.0f || logical_h <= 0.0f) {
        logical_w = (float)renderer->context.swapchain.extent.width;
        logical_h = (float)renderer->context.swapchain.extent.height;
    }

    float viewport_data[4] = {
        logical_w,
        logical_h,
        0.0f,
        0.0f,
    };

    float color_data[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    vkCmdPushConstants(cmd, renderer->pipelines[kind].layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(viewport_data), viewport_data);
    vkCmdPushConstants(cmd, renderer->pipelines[kind].layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       sizeof(viewport_data),
                       sizeof(color_data), color_data);
}

static void flush_vertex_range(VkRenderer* renderer,
                               const VkRendererFrameState* frame,
                               VkDeviceSize offset,
                               VkDeviceSize bytes) {
#if VK_RENDERER_FRAME_DEBUG_ENABLED
    static uint32_t s_last_flush_frame = UINT32_MAX;
    static unsigned s_flush_logs = 0;
    uint32_t frame_index = renderer->current_frame_index;
    if (frame_index != s_last_flush_frame) {
        s_last_flush_frame = frame_index;
        s_flush_logs = 0;
    }
    if (s_flush_logs < 8 && bytes >= sizeof(float) * 6) {
        const float* floats =
            (const float*)((const uint8_t*)frame->vertex_buffer.mapped + offset);
        VK_RENDERER_FRAME_DEBUG_LOG(
            "[vulkan] flush_vertex_range frame=%u offset=%llu bytes=%llu firstVertex=(%.2f, %.2f, %.2f, %.2f, %.2f, %.2f)\n",
            frame_index,
            (unsigned long long)offset,
            (unsigned long long)bytes,
            floats[0], floats[1], floats[2], floats[3],
            floats[4], floats[5]);
        s_flush_logs++;
    }
#endif
    VkMappedMemoryRange range = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = frame->vertex_buffer.memory,
        .offset = offset,
        .size = bytes,
    };
    vkFlushMappedMemoryRanges(renderer->context.device, 1, &range);
}

static VkResult vk_renderer_debug_capture_create(VkRenderer* renderer) {
    VkRendererDebugCapture* capture = &renderer->debug_capture;
    memset(capture, 0, sizeof(*capture));

    capture->width = renderer->context.swapchain.extent.width;
    capture->height = renderer->context.swapchain.extent.height;

    if (capture->width == 0 || capture->height == 0) {
        return VK_SUCCESS;
    }

    VkDeviceSize size = (VkDeviceSize)capture->width * capture->height * 4u;
    VkResult result = vk_renderer_memory_create_buffer(
        &renderer->context, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &capture->buffer);
    if (result != VK_SUCCESS) {
        VK_RENDERER_DEBUG_LOG("[vulkan] debug capture buffer allocation failed: %d\n", result);
        memset(capture, 0, sizeof(*capture));
        return result;
    }

    capture->requested = VK_FALSE;
    capture->dumped = VK_FALSE;
    capture->frame_counter = 0;
    capture->frame_trigger = 20;
    return VK_SUCCESS;
}

static void vk_renderer_debug_capture_destroy(VkRenderer* renderer) {
    VkRendererDebugCapture* capture = &renderer->debug_capture;
    if (capture->buffer.buffer != VK_NULL_HANDLE) {
        vk_renderer_memory_destroy_buffer(&renderer->context, &capture->buffer);
    }
    memset(capture, 0, sizeof(*capture));
}

static void vk_renderer_debug_capture_dump(VkRenderer* renderer) {
    VkRendererDebugCapture* capture = &renderer->debug_capture;
    if (!capture->requested || capture->dumped) return;

    VkDevice device = renderer->context.device;
    vkDeviceWaitIdle(device);

    if (!capture->buffer.mapped || capture->width == 0 || capture->height == 0) {
        capture->dumped = VK_TRUE;
        return;
    }

    const uint8_t* pixels = (const uint8_t*)capture->buffer.mapped;
    uint32_t width = capture->width;
    uint32_t height = capture->height;

    FILE* file = fopen(VK_RENDERER_CAPTURE_FILENAME, "wb");
    if (file) {
        fprintf(file, "P6\n%u %u\n255\n", width, height);
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* row = pixels + ((size_t)y * width * 4u);
            for (uint32_t x = 0; x < width; ++x) {
                fwrite(row + x * 4u, 1, 3, file);
            }
        }
        fclose(file);
        if (!s_logged_capture_dump) {
            VK_RENDERER_DEBUG_LOG("[vulkan] wrote debug capture to %s\n", VK_RENDERER_CAPTURE_FILENAME);
            s_logged_capture_dump = 1;
        }
    } else {
        VK_RENDERER_DEBUG_LOG("[vulkan] failed to write debug capture file %s\n", VK_RENDERER_CAPTURE_FILENAME);
    }

#if VK_RENDERER_DEBUG_ENABLED
    const uint8_t* first = pixels;
    const uint8_t* mid = pixels + ((size_t)(height / 2) * width + (width / 2)) * 4u;
    const uint8_t* last = pixels + ((size_t)(height - 1) * width + (width - 1)) * 4u;
    VK_RENDERER_DEBUG_LOG(
        "[vulkan] capture sample pixels tl=(%u,%u,%u,%u) center=(%u,%u,%u,%u) br=(%u,%u,%u,%u)\n",
        first ? first[0] : 0, first ? first[1] : 0, first ? first[2] : 0, first ? first[3] : 0,
        mid ? mid[0] : 0, mid ? mid[1] : 0, mid ? mid[2] : 0, mid ? mid[3] : 0,
        last ? last[0] : 0, last ? last[1] : 0, last ? last[2] : 0, last ? last[3] : 0);
#endif

    capture->dumped = VK_TRUE;
}

VkResult vk_renderer_init(VkRenderer* renderer,
                          SDL_Window* window,
                          const VkRendererConfig* config) {
    if (!renderer || !window) return VK_ERROR_INITIALIZATION_FAILED;
    memset(renderer, 0, sizeof(*renderer));
    renderer->current_frame_index = UINT32_MAX;

    VkRendererConfig local_config;
    if (config) {
        local_config = *config;
    } else {
        vk_renderer_config_set_defaults(&local_config);
    }
    renderer->config = local_config;

    VkResult result =
        vk_renderer_context_create(&renderer->context, window, &renderer->config);
    if (result != VK_SUCCESS) {
        vk_renderer_shutdown(renderer);
        return result;
    }

    result = create_descriptor_resources(renderer);
    if (result != VK_SUCCESS) {
        vk_renderer_shutdown(renderer);
        return result;
    }

    result = create_render_pass(renderer);
    if (result != VK_SUCCESS) {
        vk_renderer_shutdown(renderer);
        return result;
    }

    result = create_pipeline_cache(renderer);
    if (result != VK_SUCCESS) {
        vk_renderer_shutdown(renderer);
        return result;
    }

    result = vk_renderer_pipeline_create_all(&renderer->context, renderer->render_pass,
                                             renderer->sampler_set_layout, renderer->pipeline_cache,
                                             renderer->pipelines);
    if (result != VK_SUCCESS) {
        vk_renderer_shutdown(renderer);
        return result;
    }

    result = vk_renderer_commands_init(renderer, &renderer->command_pool,
                                       renderer->config.frames_in_flight);
    if (result != VK_SUCCESS) {
        vk_renderer_shutdown(renderer);
        return result;
    }

    for (uint32_t i = 0; i < renderer->frame_count; ++i) {
        result = ensure_frame_vertex_buffer(renderer, &renderer->frames[i],
                                            VK_RENDERER_VERTEX_BUFFER_SIZE);
        if (result != VK_SUCCESS) {
            vk_renderer_shutdown(renderer);
            return result;
        }
    }

    result = create_framebuffers(renderer);
    if (result != VK_SUCCESS) {
        vk_renderer_shutdown(renderer);
        return result;
    }

    renderer->draw_state.current_color[0] = 1.0f;
    renderer->draw_state.current_color[1] = 1.0f;
    renderer->draw_state.current_color[2] = 1.0f;
    renderer->draw_state.current_color[3] = 1.0f;
    renderer->draw_state.logical_size[0] = (float)renderer->context.swapchain.extent.width;
    renderer->draw_state.logical_size[1] = (float)renderer->context.swapchain.extent.height;
    renderer->draw_state.draw_call_count = 0;

    vk_renderer_debug_capture_create(renderer);

    return VK_SUCCESS;
}

static VkRendererFrameState* active_frame(VkRenderer* renderer) {
    if (!renderer || renderer->current_frame_index >= renderer->frame_count) return NULL;
    return &renderer->frames[renderer->current_frame_index];
}

VkResult vk_renderer_begin_frame(VkRenderer* renderer,
                                 VkCommandBuffer* out_cmd,
                                 VkFramebuffer* out_framebuffer,
                                 VkExtent2D* out_extent) {
    if (!renderer || !out_cmd || !out_framebuffer) return VK_ERROR_INITIALIZATION_FAILED;

#if VK_RENDERER_FRAME_DEBUG_ENABLED
    static unsigned s_begin_log_count = 0;
#endif

    uint32_t frame_index = 0;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkResult result = vk_renderer_commands_begin_frame(renderer, &frame_index, &cmd);
    if (result != VK_SUCCESS) return result;

    renderer->current_frame_index = frame_index;

    VkRendererFrameState* frame = active_frame(renderer);
    if (!frame) return VK_ERROR_INITIALIZATION_FAILED;
    flush_transient_textures(renderer, frame);
    frame->vertex_offset = 0;
    renderer->draw_state.draw_call_count = 0;

    VkExtent2D extent = renderer->context.swapchain.extent;
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)extent.width,
        .height = (float)extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = extent,
    };

    VkClearValue clear = {
        .color = {
            .float32 = {renderer->config.clear_color[0], renderer->config.clear_color[1],
                        renderer->config.clear_color[2], renderer->config.clear_color[3]},
        },
    };

#if VK_RENDERER_FRAME_DEBUG_ENABLED
    VK_RENDERER_FRAME_DEBUG_LOG("[vulkan] clear_color = %.2f %.2f %.2f %.2f\n",
                                clear.color.float32[0], clear.color.float32[1],
                                clear.color.float32[2], clear.color.float32[3]);
#endif

    VkFramebuffer framebuffer =
        renderer->swapchain_framebuffers[renderer->swapchain_image_index];

#if VK_RENDERER_FRAME_DEBUG_ENABLED
    if (s_begin_log_count < 120) {
        VK_RENDERER_FRAME_DEBUG_LOG(
            "[vulkan] begin_frame frame=%u imageIndex=%u cmd=%p framebuffer=%p extent=%ux%u format=%u render_pass=%p\n",
            frame_index,
            renderer->swapchain_image_index,
            (void*)cmd,
            (void*)framebuffer,
            extent.width,
            extent.height,
            renderer->context.swapchain.image_format,
            (void*)renderer->render_pass);
        s_begin_log_count++;
    }
#endif

    VkRenderPassBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = renderer->render_pass,
        .framebuffer = framebuffer,
        .renderArea = {
            .offset = {0, 0},
            .extent = renderer->context.swapchain.extent,
        },
        .clearValueCount = 1,
        .pClearValues = &clear,
    };

    vkCmdBeginRenderPass(cmd, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    *out_cmd = cmd;
    *out_framebuffer = framebuffer;
    if (out_extent) *out_extent = renderer->context.swapchain.extent;
    return VK_SUCCESS;
}

VkResult vk_renderer_end_frame(VkRenderer* renderer,
                               VkCommandBuffer cmd) {
    if (!renderer) return VK_ERROR_INITIALIZATION_FAILED;
    VkRendererFrameState* frame = active_frame(renderer);
    if (!frame) return VK_ERROR_INITIALIZATION_FAILED;

    vkCmdEndRenderPass(cmd);

    VkRendererDebugCapture* capture = &renderer->debug_capture;
    if (capture->frame_counter < capture->frame_trigger) {
        capture->frame_counter++;
    }

    if (!capture->requested && capture->buffer.buffer != VK_NULL_HANDLE &&
        capture->frame_counter >= capture->frame_trigger) {
        VkImage image = renderer->context.swapchain.images[renderer->swapchain_image_index];

        VkImageMemoryBarrier to_transfer = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        };

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0, NULL,
                             0, NULL,
                             1, &to_transfer);

        VkBufferImageCopy region = {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = {0, 0, 0},
            .imageExtent = {
                .width = renderer->debug_capture.width,
                .height = renderer->debug_capture.height,
                .depth = 1,
            },
        };

        vkCmdCopyImageToBuffer(cmd,
                                image,
                                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                renderer->debug_capture.buffer.buffer,
                                1,
                                &region);

        VkImageMemoryBarrier to_present = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = 0,
        };

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0,
                             0, NULL,
                             0, NULL,
                             1, &to_present);

        capture->requested = VK_TRUE;
    }

    VkResult result =
        vk_renderer_commands_end_frame(renderer, renderer->current_frame_index, cmd);
    renderer->current_frame_index = UINT32_MAX;
    if (capture->requested && !capture->dumped) {
        vk_renderer_debug_capture_dump(renderer);
    }
    return result;
}

VkResult vk_renderer_recreate_swapchain(VkRenderer* renderer, SDL_Window* window) {
    if (!renderer || !window) return VK_ERROR_INITIALIZATION_FAILED;
    VkDevice device = renderer->context.device;
    vkDeviceWaitIdle(device);

    destroy_framebuffers(renderer);
    vk_renderer_debug_capture_destroy(renderer);
    vk_renderer_pipeline_destroy_all(&renderer->context, renderer->pipelines);

    VkResult result =
        vk_renderer_context_recreate_swapchain(&renderer->context, window, &renderer->config);
    if (result != VK_SUCCESS) return result;

    result = vk_renderer_pipeline_create_all(&renderer->context, renderer->render_pass,
                                             renderer->sampler_set_layout, renderer->pipeline_cache,
                                             renderer->pipelines);
    if (result != VK_SUCCESS) return result;

    result = create_framebuffers(renderer);
    if (result != VK_SUCCESS) return result;

    vk_renderer_debug_capture_create(renderer);

    if (renderer->draw_state.logical_size[0] <= 0.0f ||
        renderer->draw_state.logical_size[1] <= 0.0f) {
        renderer->draw_state.logical_size[0] =
            (float)renderer->context.swapchain.extent.width;
        renderer->draw_state.logical_size[1] =
            (float)renderer->context.swapchain.extent.height;
    }

    return VK_SUCCESS;
}

void vk_renderer_set_draw_color(VkRenderer* renderer, float r, float g, float b, float a) {
    if (!renderer) return;
#if VK_RENDERER_FRAME_DEBUG_ENABLED
    static uint32_t s_last_frame = UINT32_MAX;
    static unsigned s_color_logs = 0;
    uint32_t frame = renderer->current_frame_index;
    if (frame != s_last_frame) {
        s_last_frame = frame;
        s_color_logs = 0;
    }
    if (s_color_logs < 16) {
        VK_RENDERER_FRAME_DEBUG_LOG(
            "[vulkan] set_draw_color frame=%u rgba=%.2f %.2f %.2f %.2f\n",
            frame,
            r, g, b, a);
        s_color_logs++;
    }
#endif
    renderer->draw_state.current_color[0] = r;
    renderer->draw_state.current_color[1] = g;
    renderer->draw_state.current_color[2] = b;
    renderer->draw_state.current_color[3] = a;
}

void vk_renderer_set_logical_size(VkRenderer* renderer, float width, float height) {
    if (!renderer) return;
    renderer->draw_state.logical_size[0] = (width > 0.0f) ? width : 0.0f;
    renderer->draw_state.logical_size[1] = (height > 0.0f) ? height : 0.0f;

    if (width <= 0.0f || height <= 0.0f) {
        if (s_logged_logical_size != 1) {
            VK_RENDERER_DEBUG_LOG(
                "[vulkan] vk_renderer_set_logical_size received non-positive dimensions "
                "(%.2f, %.2f); will fall back to swapchain extent.\n",
                width, height);
            s_logged_logical_size = 1;
        }
        return;
    }

    if (s_logged_logical_size != 2) {
#if VK_RENDERER_DEBUG_ENABLED
        float extent_w = (float)renderer->context.swapchain.extent.width;
        float extent_h = (float)renderer->context.swapchain.extent.height;
        float scaleX = extent_w > 0.0f ? extent_w / width : 1.0f;
        float scaleY = extent_h > 0.0f ? extent_h / height : 1.0f;
        VK_RENDERER_DEBUG_LOG(
            "[vulkan] vk_renderer_set_logical_size %.2f x %.2f (scale %.2f x %.2f).\n",
            width, height, scaleX, scaleY);
#endif
        s_logged_logical_size = 2;
    }
}

void vk_renderer_draw_line(VkRenderer* renderer,
                           float x0,
                           float y0,
                           float x1,
                           float y1) {
    VkRendererFrameState* frame = active_frame(renderer);
    if (!frame) return;

    static int logged_line = 0;
    if (!logged_line) {
        VK_RENDERER_FRAME_DEBUG_LOG(
            "[vulkan] vk_renderer_draw_line first call (%.2f, %.2f) -> (%.2f, %.2f).\n",
            x0, y0, x1, y1);
        logged_line = 1;
    }

    const float vertices[] = {
        x0, y0, renderer->draw_state.current_color[0], renderer->draw_state.current_color[1],
        renderer->draw_state.current_color[2], renderer->draw_state.current_color[3],
        x1, y1, renderer->draw_state.current_color[0], renderer->draw_state.current_color[1],
        renderer->draw_state.current_color[2], renderer->draw_state.current_color[3],
    };

    VkDeviceSize bytes = sizeof(vertices);
    if (ensure_frame_vertex_buffer(renderer, frame, bytes) != VK_SUCCESS) return;

    uint8_t* dst = (uint8_t*)frame->vertex_buffer.mapped + frame->vertex_offset;
    memcpy(dst, vertices, bytes);

    VkDeviceSize offset = frame->vertex_offset;
    frame->vertex_offset += bytes;

    flush_vertex_range(renderer, frame, offset, bytes);

    VkBuffer buffers[] = {frame->vertex_buffer.buffer};
    VkDeviceSize offsets[] = {offset};
    VkCommandBuffer cmd = frame->command_buffer;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer->pipelines[VK_RENDERER_PIPELINE_LINES].pipeline);
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    push_basic_constants(renderer, cmd, VK_RENDERER_PIPELINE_LINES);
    vkCmdDraw(cmd, 2, 1, 0, 0);
    renderer->draw_state.draw_call_count++;

    if (renderer->debug_capture.frame_counter == renderer->debug_capture.frame_trigger) {
        VK_RENDERER_FRAME_DEBUG_LOG(
            "[vulkan] line vertices: (%.2f, %.2f) -> (%.2f, %.2f) color=%.2f %.2f %.2f %.2f (total=%llu)\n",
            x0, y0, x1, y1,
            renderer->draw_state.current_color[0],
            renderer->draw_state.current_color[1],
            renderer->draw_state.current_color[2],
            renderer->draw_state.current_color[3],
            (unsigned long long)++s_total_lines_logged);
    }
}

static void emit_filled_quad(VkRenderer* renderer,
                             VkRendererFrameState* frame,
                             const float quad[6][6],
                             VkRendererPipelineKind pipeline,
                             uint32_t vertex_count) {
    static int logged_quad = 0;
    VkDeviceSize bytes = sizeof(float) * 6 * vertex_count;
    if (ensure_frame_vertex_buffer(renderer, frame, bytes) != VK_SUCCESS) return;

    uint8_t* dst = (uint8_t*)frame->vertex_buffer.mapped + frame->vertex_offset;
    memcpy(dst, quad, (size_t)bytes);

    VkDeviceSize offset = frame->vertex_offset;
    frame->vertex_offset += bytes;

    flush_vertex_range(renderer, frame, offset, bytes);

    VkBuffer buffers[] = {frame->vertex_buffer.buffer};
    VkDeviceSize offsets[] = {offset};
    VkCommandBuffer cmd = frame->command_buffer;

    if (!logged_quad) {
        VK_RENDERER_FRAME_DEBUG_LOG(
            "[vulkan] emit_filled_quad pipeline=%p vertex_buffer=%p first vertex=(%.2f, %.2f) color=%.2f\n",
            (void*)renderer->pipelines[pipeline].pipeline,
            (void*)frame->vertex_buffer.buffer,
            quad[0][0], quad[0][1], quad[0][2]);
        logged_quad = 1;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer->pipelines[pipeline].pipeline);
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    push_basic_constants(renderer, cmd, pipeline);
    vkCmdDraw(cmd, vertex_count, 1, 0, 0);
    renderer->draw_state.draw_call_count++;
    if (renderer->debug_capture.frame_counter == renderer->debug_capture.frame_trigger) {
#if VK_RENDERER_FRAME_DEBUG_ENABLED
        const float* floats = (const float*)quad;
        VK_RENDERER_FRAME_DEBUG_LOG(
            "[vulkan] quad sample v0=(%.2f, %.2f, %.2f, %.2f) v2=(%.2f, %.2f, %.2f, %.2f) (total=%llu)\n",
            floats[0], floats[1], floats[2], floats[3],
            floats[12], floats[13], floats[14], floats[15],
            (unsigned long long)(s_total_vertices_logged += vertex_count));
#endif
    }
}

void vk_renderer_draw_rect(VkRenderer* renderer, const SDL_Rect* rect) {
    if (!rect) return;
    float x = (float)rect->x;
    float y = (float)rect->y;
    float w = (float)rect->w;
    float h = (float)rect->h;

    vk_renderer_draw_line(renderer, x, y, x + w, y);
    vk_renderer_draw_line(renderer, x + w, y, x + w, y + h);
    vk_renderer_draw_line(renderer, x + w, y + h, x, y + h);
    vk_renderer_draw_line(renderer, x, y + h, x, y);
}

void vk_renderer_fill_rect(VkRenderer* renderer, const SDL_Rect* rect) {
    VkRendererFrameState* frame = active_frame(renderer);
    if (!frame || !rect) return;

    static int logged_fill = 0;
    if (!logged_fill) {
        VK_RENDERER_FRAME_DEBUG_LOG(
            "[vulkan] vk_renderer_fill_rect first call x=%d y=%d w=%d h=%d color=%.2f,%.2f,%.2f,%.2f\n",
            rect->x, rect->y, rect->w, rect->h,
            renderer->draw_state.current_color[0],
            renderer->draw_state.current_color[1],
            renderer->draw_state.current_color[2],
            renderer->draw_state.current_color[3]);
        logged_fill = 1;
    }

    float x = (float)rect->x;
    float y = (float)rect->y;
    float w = (float)rect->w;
    float h = (float)rect->h;

    const float color[4] = {
        renderer->draw_state.current_color[0],
        renderer->draw_state.current_color[1],
        renderer->draw_state.current_color[2],
        renderer->draw_state.current_color[3],
    };

    float quad[6][6] = {
        {x, y, color[0], color[1], color[2], color[3]},
        {x + w, y, color[0], color[1], color[2], color[3]},
        {x + w, y + h, color[0], color[1], color[2], color[3]},
        {x, y, color[0], color[1], color[2], color[3]},
        {x + w, y + h, color[0], color[1], color[2], color[3]},
        {x, y + h, color[0], color[1], color[2], color[3]},
    };

    emit_filled_quad(renderer, frame, quad, VK_RENDERER_PIPELINE_SOLID, 6);
}

void vk_renderer_draw_texture(VkRenderer* renderer,
                              const VkRendererTexture* texture,
                              const SDL_Rect* src,
                              const SDL_Rect* dst) {
    VkRendererFrameState* frame = active_frame(renderer);
   if (!frame || !texture) return;

    static int logged_texture = 0;
    if (!logged_texture) {
        VK_RENDERER_FRAME_DEBUG_LOG(
            "[vulkan] vk_renderer_draw_texture first call dst=(%d,%d %dx%d) texExtent=%ux%u\n",
            dst ? dst->x : 0,
            dst ? dst->y : 0,
            dst ? dst->w : (int)texture->width,
            dst ? dst->h : (int)texture->height,
            texture->width,
            texture->height);
        logged_texture = 1;
    }

    SDL_Rect local_dst;
    if (!dst) {
        local_dst.x = 0;
        local_dst.y = 0;
        local_dst.w = (int)texture->width;
        local_dst.h = (int)texture->height;
        dst = &local_dst;
    }

    float sx = 0.0f;
    float sy = 0.0f;
    float sw = (float)texture->width;
    float sh = (float)texture->height;

    if (src) {
        sx = (float)src->x;
        sy = (float)src->y;
        sw = (float)src->w;
        sh = (float)src->h;
    }

    float u0 = sx / (float)texture->width;
    float v0 = sy / (float)texture->height;
    float u1 = (sx + sw) / (float)texture->width;
    float v1 = (sy + sh) / (float)texture->height;

    float x = (float)dst->x;
    float y = (float)dst->y;
    float w = (float)dst->w;
    float h = (float)dst->h;

    float textured_vertices[6][8] = {
        {x, y, u0, v0, 1.0f, 1.0f, 1.0f, 1.0f},
        {x + w, y, u1, v0, 1.0f, 1.0f, 1.0f, 1.0f},
        {x + w, y + h, u1, v1, 1.0f, 1.0f, 1.0f, 1.0f},
        {x, y, u0, v0, 1.0f, 1.0f, 1.0f, 1.0f},
        {x + w, y + h, u1, v1, 1.0f, 1.0f, 1.0f, 1.0f},
        {x, y + h, u0, v1, 1.0f, 1.0f, 1.0f, 1.0f},
    };

    VkDeviceSize bytes = sizeof(textured_vertices);
    if (ensure_frame_vertex_buffer(renderer, frame, bytes) != VK_SUCCESS) return;

    uint8_t* dst_ptr = (uint8_t*)frame->vertex_buffer.mapped + frame->vertex_offset;
    memcpy(dst_ptr, textured_vertices, bytes);

    VkDeviceSize offset = frame->vertex_offset;
    frame->vertex_offset += bytes;

    flush_vertex_range(renderer, frame, offset, bytes);

    VkBuffer buffers[] = {frame->vertex_buffer.buffer};
    VkDeviceSize offsets[] = {offset};
    VkCommandBuffer cmd = frame->command_buffer;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      renderer->pipelines[VK_RENDERER_PIPELINE_TEXTURED].pipeline);
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    push_basic_constants(renderer, cmd, VK_RENDERER_PIPELINE_TEXTURED);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            renderer->pipelines[VK_RENDERER_PIPELINE_TEXTURED].layout, 0, 1,
                            &texture->descriptor_set, 0, NULL);

    vkCmdDraw(cmd, 6, 1, 0, 0);
    renderer->draw_state.draw_call_count++;
    if (renderer->debug_capture.frame_counter == renderer->debug_capture.frame_trigger) {
        VK_RENDERER_FRAME_DEBUG_LOG(
            "[vulkan] texture draw dst=(%d,%d %dx%d) color=%.2f %.2f %.2f %.2f\n",
            dst ? dst->x : 0,
            dst ? dst->y : 0,
            dst ? dst->w : (int)texture->width,
            dst ? dst->h : (int)texture->height,
            renderer->draw_state.current_color[0],
            renderer->draw_state.current_color[1],
            renderer->draw_state.current_color[2],
            renderer->draw_state.current_color[3]);
    }
}

VkResult vk_renderer_upload_sdl_surface_with_filter(VkRenderer* renderer,
                                                    SDL_Surface* surface,
                                                    VkRendererTexture* out_texture,
                                                    VkFilter filter) {
    if (!renderer || !surface || !out_texture) return VK_ERROR_INITIALIZATION_FAILED;

    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
    SDL_Surface* used_surface = converted ? converted : surface;

    VkResult result = vk_renderer_texture_create_from_rgba(
        renderer, used_surface->pixels, (uint32_t)used_surface->w, (uint32_t)used_surface->h,
        filter, out_texture);

    if (converted) SDL_FreeSurface(converted);
    return result;
}

VkResult vk_renderer_upload_sdl_surface(VkRenderer* renderer,
                                        SDL_Surface* surface,
                                        VkRendererTexture* out_texture) {
    return vk_renderer_upload_sdl_surface_with_filter(
        renderer, surface, out_texture, VK_FILTER_LINEAR);
}

void vk_renderer_queue_texture_destroy(VkRenderer* renderer,
                                       VkRendererTexture* texture) {
    if (!renderer || !texture) return;
    if (!texture->descriptor_set && !texture->sampler && !texture->image.image) {
        return;
    }

    uint32_t frame_index = renderer->current_frame_index;
    if (frame_index == UINT32_MAX) {
        vk_renderer_texture_destroy(renderer, texture);
        return;
    }

    VkRendererFrameState* frame = &renderer->frames[frame_index];
    if (!frame) {
        vk_renderer_texture_destroy(renderer, texture);
        return;
    }

    if (frame->transient_texture_count >= frame->transient_texture_capacity) {
        uint32_t new_capacity = frame->transient_texture_capacity ? frame->transient_texture_capacity * 2 : 8;
        VkRendererTexture* resized = (VkRendererTexture*)realloc(frame->transient_textures,
                                                                 sizeof(VkRendererTexture) * new_capacity);
        if (!resized) {
            vk_renderer_texture_destroy(renderer, texture);
            return;
        }
        frame->transient_textures = resized;
        frame->transient_texture_capacity = new_capacity;
    }

    frame->transient_textures[frame->transient_texture_count++] = *texture;
    memset(texture, 0, sizeof(*texture));
}

void vk_renderer_shutdown(VkRenderer* renderer) {
    if (!renderer) return;
    VkDevice device = renderer->context.device;
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }

    if (renderer->frames) {
        for (uint32_t i = 0; i < renderer->frame_count; ++i) {
            VkRendererFrameState* frame = &renderer->frames[i];
            flush_transient_textures(renderer, frame);
            free(frame->transient_textures);
            frame->transient_textures = NULL;
            frame->transient_texture_capacity = 0;
            frame->transient_texture_count = 0;
            vk_renderer_memory_destroy_buffer(&renderer->context, &frame->vertex_buffer);
        }
    }

    destroy_framebuffers(renderer);
    vk_renderer_debug_capture_destroy(renderer);

    if (device != VK_NULL_HANDLE) {
        if (renderer->descriptor_pool) {
            vkDestroyDescriptorPool(device, renderer->descriptor_pool, NULL);
            renderer->descriptor_pool = VK_NULL_HANDLE;
        }

        if (renderer->sampler_set_layout) {
            vkDestroyDescriptorSetLayout(device, renderer->sampler_set_layout, NULL);
            renderer->sampler_set_layout = VK_NULL_HANDLE;
        }

        vk_renderer_pipeline_destroy_all(&renderer->context, renderer->pipelines);

        if (renderer->pipeline_cache) {
            vkDestroyPipelineCache(device, renderer->pipeline_cache, NULL);
            renderer->pipeline_cache = VK_NULL_HANDLE;
        }

        if (renderer->render_pass) {
            vkDestroyRenderPass(device, renderer->render_pass, NULL);
            renderer->render_pass = VK_NULL_HANDLE;
        }
    }

    vk_renderer_commands_destroy(renderer, &renderer->command_pool);

    vk_renderer_context_destroy(&renderer->context);
}
