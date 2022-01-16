#ifndef OBDN_V_MEMORY_H
#define OBDN_V_MEMORY_H

#include "video.h"
#include <stdbool.h>


// TODO this is brittle. we should probably change V_MemoryType to a mask with
// flags for the different
// requirements. One bit for host or device, one bit for transfer capabale, one
// bit for external, one bit for image or buffer
typedef enum {
    OBDN_MEMORY_HOST_GRAPHICS_TYPE,
    OBDN_MEMORY_HOST_TRANSFER_TYPE,
    OBDN_MEMORY_DEVICE_TYPE,
    OBDN_MEMORY_EXTERNAL_DEVICE_TYPE
} Obdn_MemoryType;

typedef struct Obdn_Memory Obdn_Memory;

struct BlockChain;

typedef struct Obdn_BufferRegion {
    VkDeviceSize       size;
    VkDeviceSize       offset;
    VkBuffer           buffer;
    uint8_t*           hostData; // if hostData not null, its mapped
    uint32_t           memBlockId;
    uint32_t           stride; // if bufferregion stores an array this is the stride between elements. otherwise its 0. if the elements need to satify an alignment this stride will satisfy that
    struct BlockChain* pChain;
} Obdn_BufferRegion;

typedef struct Obdn_Image {
    VkImage            handle;
    VkImageView        view;
    VkSampler          sampler;
    VkImageAspectFlags aspectMask;
    VkDeviceSize       size; // size in bytes. taken from GetMemReqs
    VkDeviceSize       offset;//TODO: delete this and see if its even necessary... should probably be a VkOffset3D object
    VkExtent3D         extent;
    VkImageLayout      layout;
    VkImageUsageFlags  usageFlags;
    VkFormat           format;
    uint32_t           mipLevels;
    uint32_t           queueFamily;
    uint32_t           memBlockId;
    struct BlockChain* pChain;
} Obdn_Image;

uint64_t obdn_SizeOfMemory(void);
Obdn_Memory* obdn_AllocMemory(void);
void obdn_CreateMemory(const Obdn_Instance* instance, const uint32_t hostGraphicsBufferMB,
                  const uint32_t deviceGraphicsBufferMB,
                  const uint32_t deviceGraphicsImageMB, const uint32_t hostTransferBufferMB,
                  const uint32_t deviceExternalGraphicsImageMB, Obdn_Memory* memory);

Obdn_BufferRegion obdn_RequestBufferRegion(Obdn_Memory*, size_t size,
                                             const VkBufferUsageFlags,
                                             const Obdn_MemoryType);

// Returns a buffer region with enough space for elemCount elements of elemSize size. 
// The stride member set to the size of the space between
// elements, which might be different from elemSize if physicalDevice requirements demand it.
Obdn_BufferRegion
obdn_RequestBufferRegionArray(Obdn_Memory* memory, uint32_t elemSize, uint32_t elemCount,
                              VkBufferUsageFlags flags,
                              Obdn_MemoryType  memType);

Obdn_BufferRegion obdn_RequestBufferRegionAligned(Obdn_Memory*, const size_t size,
                                                    uint32_t     alignment,
                                                    const Obdn_MemoryType);

uint32_t obdn_GetMemoryType(const Obdn_Memory*, uint32_t                    typeBits,
                            const VkMemoryPropertyFlags properties);

Obdn_Image obdn_CreateImage(Obdn_Memory*, const uint32_t width, const uint32_t height,
                              const VkFormat           format,
                              const VkImageUsageFlags  usageFlags,
                              const VkImageAspectFlags aspectMask,
                              const VkSampleCountFlags sampleCount,
                              const uint32_t           mipLevels,
                              const Obdn_MemoryType);

void obdn_CopyBufferRegion(const Obdn_BufferRegion* src,
                        Obdn_BufferRegion*       dst);

void obdn_CopyImageToBufferRegion(const Obdn_Image*  image,
                                  Obdn_BufferRegion* bufferRegion);

void obdn_TransferToDevice(Obdn_Memory* memory, Obdn_BufferRegion* pRegion);

void obdn_FreeImage(Obdn_Image* image);

void obdn_FreeBufferRegion(Obdn_BufferRegion* pRegion);

void obdn_DestroyMemory(Obdn_Memory* memory);

void obdn_MemoryReportSimple(const Obdn_Memory* memory);

VkDeviceAddress obdn_GetBufferRegionAddress(const Obdn_BufferRegion* region);

// application's job to destroy this buffer and free the memory
void obdn_CreateUnmanagedBuffer(Obdn_Memory* memory, const VkBufferUsageFlags bufferUsageFlags,
                                const uint32_t           memorySize,
                                const Obdn_MemoryType  type,
                                VkDeviceMemory* pMemory, VkBuffer* pBuffer);

VkDeviceMemory obdn_GetDeviceMemory(const Obdn_Memory* memory, const Obdn_MemoryType memType);
VkDeviceSize   obdn_GetMemorySize(const Obdn_Memory* memory, const Obdn_MemoryType memType);

const Obdn_Instance* obdn_GetMemoryInstance(const Obdn_Memory* memory);

void obdn_GetImageMemoryUsage(const Obdn_Memory* memory, uint64_t* bytes_in_use, uint64_t* total_bytes);

#ifdef WIN32
bool obdn_GetExternalMemoryWin32Handle(const Obdn_Memory* memory, HANDLE* handle, uint64_t* size);
#else
bool obdn_GetExternalMemoryFd(const Obdn_Memory* memory, int* fd, uint64_t* size);
#endif

#endif /* end of include guard: V_MEMORY_H */