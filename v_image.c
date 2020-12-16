#include "v_image.h"
#include "t_def.h"
#include "v_memory.h"
#include "v_video.h"
#include "v_command.h"
#include <string.h>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "thirdparty/stb_image.h"
#include "thirdparty/stb_image_write.h"

#define IMG_OUT_DIR "/out/images"

Tanto_V_Image tanto_v_CreateImageAndSampler(
    const uint32_t width, 
    const uint32_t height,
    const VkFormat format,
    const VkImageUsageFlags usageFlags,
    const VkImageAspectFlags aspectMask,
    const VkSampleCountFlags sampleCount,
    const VkFilter filter)
{
    Tanto_V_Image image = tanto_v_CreateImage(width, height, format, usageFlags, aspectMask, sampleCount);

    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = filter,
        .minFilter = filter,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .mipLodBias = 0.0,
        .anisotropyEnable = VK_FALSE,
        .compareEnable = VK_FALSE,
        .minLod = 0.0,
        .maxLod = 0.0, // must both be 0 when using unnormalizedCoordinates
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE // allow us to window coordinates in frag shader
    };

    V_ASSERT( vkCreateSampler(device, &samplerInfo, NULL, &image.sampler) );

    return image;
}

void tanto_v_TransitionImageLayout(const VkImageLayout oldLayout, const VkImageLayout newLayout, Tanto_V_Image* image)
{
    VkCommandPool cmdPool;

    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = graphicsQueueFamilyIndex,
    };

    vkCreateCommandPool(device, &poolInfo, NULL, &cmdPool);

    VkCommandBufferAllocateInfo cmdBufInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandBufferCount = 1,
        .commandPool = cmdPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    };

    VkCommandBuffer cmdBuf;

    vkAllocateCommandBuffers(device, &cmdBufInfo, &cmdBuf);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(cmdBuf, &beginInfo);

    VkImageSubresourceRange subResRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseArrayLayer = 0,
        .baseMipLevel = 0,
        .levelCount = 1,
        .layerCount = 1,
    };

    VkImageMemoryBarrier imgBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .image = image->handle,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .subresourceRange = subResRange,
        .srcAccessMask = 0,
        .dstAccessMask = 0,
    };

    vkCmdPipelineBarrier(cmdBuf, 
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &imgBarrier);

    vkEndCommandBuffer(cmdBuf);

    tanto_v_SubmitToQueueWait(&cmdBuf, TANTO_V_QUEUE_GRAPHICS_TYPE, 0);

    vkDestroyCommandPool(device, cmdPool, NULL);

    image->layout = newLayout;
}

void tanto_v_CopyBufferToImage(const Tanto_V_BufferRegion* region,
        Tanto_V_Image* image)
{
    Tanto_V_CommandPool cmd = tanto_v_RequestOneTimeUseCommand();

    VkImageLayout origLayout = image->layout;
    tanto_v_TransitionImageLayout(image->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, image);

    const VkImageSubresourceLayers subRes = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseArrayLayer = 0,
        .layerCount = 1, 
        .mipLevel = 0,
    };

    const VkOffset3D imgOffset = {
        .x = 0,
        .y = 0,
        .z = 0
    };

    const VkBufferImageCopy imgCopy = {
        .imageOffset = imgOffset,
        .imageExtent = image->extent,
        .imageSubresource = subRes,
        .bufferOffset = region->offset,
        .bufferImageHeight = 0,
        .bufferRowLength = 0
    };

    printf("Copying buffer to image...\n");
    vkCmdCopyBufferToImage(cmd.buffer, region->buffer, image->handle, 
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imgCopy);

    tanto_v_SubmitOneTimeCommandAndWait(&cmd, 0);

    tanto_v_TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, origLayout, image);

    printf("Copying complete.\n");
}

void tanto_v_SaveImage(Tanto_V_Image* image, Tanto_V_ImageFileType fileType, const char* filename)
{
    Tanto_V_BufferRegion region = tanto_v_RequestBufferRegion(
            image->size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, TANTO_V_MEMORY_HOST_TRANSFER_TYPE);

    VkImageLayout origLayout = image->layout;
    tanto_v_TransitionImageLayout(image->layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image);

    const VkImageSubresourceLayers subRes = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseArrayLayer = 0,
        .layerCount = 1, 
        .mipLevel = 0,
    };

    const VkOffset3D imgOffset = {
        .x = 0,
        .y = 0,
        .z = 0
    };

    TANTO_DEBUG_PRINT("Extent: %d, %d", image->extent.width, image->extent.height); 
    TANTO_DEBUG_PRINT("Image Layout: %d", image->layout);
    TANTO_DEBUG_PRINT("Orig Layout: %d", origLayout);
    TANTO_DEBUG_PRINT("Image size: %ld", image->size);

    const VkBufferImageCopy imgCopy = {
        .imageOffset = imgOffset,
        .imageExtent = image->extent,
        .imageSubresource = subRes,
        .bufferOffset = region.offset,
        .bufferImageHeight = 0,
        .bufferRowLength = 0
    };

    Tanto_V_CommandPool cmd = tanto_v_RequestOneTimeUseCommand();

    printf("Copying image to host...\n");
    vkCmdCopyImageToBuffer(cmd.buffer, image->handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, region.buffer, 1, &imgCopy);

    tanto_v_SubmitOneTimeCommandAndWait(&cmd, 0);

    tanto_v_TransitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, origLayout, image);

    printf("Copying complete.\n");
    printf("Writing out to jpg...\n");

    char strbuf[256];
    const char* pwd = getenv("PWD");
    assert(pwd);
    const char* dir = IMG_OUT_DIR;
    assert(strlen(pwd) + strlen(dir) + strlen(filename) < 256);
    strcpy(strbuf, pwd);
    strcat(strbuf, dir);
    strcat(strbuf, filename);

    TANTO_DEBUG_PRINT("Filepath: %s", strbuf);

    int r;
    switch (fileType)
    {
        case TANTO_V_IMAGE_FILE_TYPE_PNG: 
        {
            r = stbi_write_png(strbuf, image->extent.width, image->extent.height, 
            4, region.hostData, 0);
            break;
        }
        case TANTO_V_IMAGE_FILE_TYPE_JPG:
        {
            r = stbi_write_jpg(strbuf, image->extent.width, image->extent.height, 
            4, region.hostData, 0);
            break;
        }
    }

    assert( 0 != r );

    tanto_v_FreeBufferRegion(&region);

    printf("Image saved to %s!\n", strbuf);
}

void tanto_v_ClearColorImage(Tanto_V_Image* image)
{
    Tanto_V_CommandPool pool = tanto_v_RequestOneTimeUseCommand();

    VkClearColorValue clearColor = {
        .float32[0] = 0,
        .float32[1] = 0,
        .float32[2] = 0,
        .float32[3] = 0,
    };

    VkImageSubresourceRange range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseArrayLayer = 0,
        .baseMipLevel = 0,
        .layerCount = 1,
        .levelCount = 1
    };

    vkCmdClearColorImage(pool.buffer, image->handle, image->layout, &clearColor, 1, &range);

    tanto_v_SubmitOneTimeCommandAndWait(&pool, 0);
}
