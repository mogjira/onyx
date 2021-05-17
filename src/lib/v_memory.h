#ifndef OBDN_V_MEMORY_H
#define OBDN_V_MEMORY_H

#include "v_def.h"
#include <stdbool.h>

typedef uint32_t Obdn_V_BlockId;

//TODO this is brittle. we should probably change V_MemoryType to a mask with flags for the different
// requirements. One bit for host or device, one bit for transfer capabale, one bit for external, one bit for image or buffer
typedef enum {
    OBDN_V_MEMORY_HOST_GRAPHICS_TYPE,
    OBDN_V_MEMORY_HOST_TRANSFER_TYPE,
    OBDN_V_MEMORY_DEVICE_TYPE,
    OBDN_V_MEMORY_EXTERNAL_DEVICE_TYPE
} Obdn_V_MemoryType;

struct BlockChain;

typedef struct {
    VkDeviceSize           size;
    VkDeviceSize           offset;
    VkBuffer               buffer;
    uint8_t*               hostData; // if hostData not null, its mapped
    Obdn_V_BlockId         memBlockId;
    struct BlockChain*     pChain;
} Obdn_V_BufferRegion;

typedef struct {
    VkImage             handle;
    VkImageView         view;
    VkSampler           sampler;
    VkDeviceSize        size; // size in bytes. taken from GetMemReqs
    VkDeviceSize        offset;
    VkExtent3D          extent;
    VkImageLayout       layout;
    uint32_t            mipLevels;
    uint32_t            queueFamily;
    Obdn_V_BlockId      memBlockId;
    struct BlockChain*  pChain;
} Obdn_V_Image;

#define OBDN_1_MiB   (VkDeviceSize)0x100000
#define OBDN_100_MiB (VkDeviceSize)0x6400000
#define OBDN_256_MiB (VkDeviceSize)0x10000000
#define OBDN_512_MiB (VkDeviceSize)0x20000000
#define OBDN_1_GiB   (VkDeviceSize)0x40000000

void obdn_v_InitMemory(const Obdn_V_MemorySizes*);

Obdn_V_BufferRegion obdn_v_RequestBufferRegion(size_t size, 
        const VkBufferUsageFlags, const Obdn_V_MemoryType);

Obdn_V_BufferRegion obdn_v_RequestBufferRegionAligned(
        const size_t size, uint32_t alignment, const Obdn_V_MemoryType);

uint32_t obdn_v_GetMemoryType(uint32_t typeBits, const VkMemoryPropertyFlags properties);

Obdn_V_Image obdn_v_CreateImage(
        const uint32_t width, 
        const uint32_t height,
        const VkFormat format,
        const VkImageUsageFlags usageFlags,
        const VkImageAspectFlags aspectMask,
        const VkSampleCountFlags sampleCount,
        const uint32_t mipLevels,
        const Obdn_V_MemoryType);

void obdn_v_CopyBufferRegion(const Obdn_V_BufferRegion* src, Obdn_V_BufferRegion* dst);

void obdn_v_CopyImageToBufferRegion(const Obdn_V_Image* image, Obdn_V_BufferRegion* bufferRegion);

void obdn_v_TransferToDevice(Obdn_V_BufferRegion* pRegion);

void obdn_v_FreeImage(Obdn_V_Image* image);

void obdn_v_FreeBufferRegion(Obdn_V_BufferRegion* pRegion);

VkDeviceAddress obdn_v_GetBufferRegionAddress(const Obdn_V_BufferRegion* region);

void obdn_v_CleanUpMemory(void);

// application's job to destroy this buffer and free the memory
void obdn_v_CreateUnmanagedBuffer(const VkBufferUsageFlags bufferUsageFlags, 
        const uint32_t memorySize, const Obdn_V_MemoryType type, 
        VkDeviceMemory* pMemory, VkBuffer* pBuffer);

const VkDeviceMemory obdn_v_GetDeviceMemory(const Obdn_V_MemoryType memType);
const VkDeviceSize obdn_v_GetMemorySize(const Obdn_V_MemoryType memType);

#endif /* end of include guard: V_MEMORY_H */