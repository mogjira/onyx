#ifndef TANTO_V_IMAGE_H
#define TANTO_V_IMAGE_H

/*
 * Utility functions for Tanto Images
 */

#include "v_memory.h"
#include "v_command.h"

typedef enum {
    TANTO_V_IMAGE_FILE_TYPE_PNG,
    TANTO_V_IMAGE_FILE_TYPE_JPG
} Tanto_V_ImageFileType;

Tanto_V_Image tanto_v_CreateImageAndSampler(
    const uint32_t width, 
    const uint32_t height,
    const VkFormat format,
    const VkImageUsageFlags usageFlags,
    const VkImageAspectFlags aspectMask,
    const VkSampleCountFlags sampleCount,
    const VkFilter filter,
    const uint32_t queueFamilyIndex);

void tanto_v_TransitionImageLayout(const VkImageLayout oldLayout, const VkImageLayout newLayout, Tanto_V_Image* image);

void tanto_v_CopyBufferToImage(const Tanto_V_BufferRegion* region,
        Tanto_V_Image* image);

void tanto_v_CmdCopyBufferToImage(const VkCommandBuffer cmdbuf, const Tanto_V_BufferRegion* region,
        Tanto_V_Image* image);

void tanto_v_CmdCopyImageToBuffer(const VkCommandBuffer cmdbuf, const Tanto_V_Image* image, Tanto_V_BufferRegion* region);
void tanto_v_CmdTransitionImageLayout(const VkCommandBuffer cmdbuf, const Tanto_V_Barrier barrier, 
        const VkImageLayout oldLayout, const VkImageLayout newLayout, VkImage image);

void tanto_v_SaveImage(Tanto_V_Image* image, Tanto_V_ImageFileType fileType, const char* name);

void tanto_v_ClearColorImage(Tanto_V_Image* image);

#endif /* end of include guard: TANTO_V_IMAGE_H */
