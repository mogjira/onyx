#include "v_memory.h"
#include "v_video.h"
#include "v_command.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <hell/common.h>
#include <hell/debug.h>
#include "common.h"
#include "v_private.h"
#include "dtags.h"
#include <hell/minmax.h>

// HVC = Host Visible and Coherent
// DL = Device Local    
//#define MAX_BLOCKS 100000 we overflowed the stack...
#define MAX_BLOCKS 1000

typedef Obdn_V_BufferRegion BufferRegion;

static VkPhysicalDeviceMemoryProperties memoryProperties;

typedef struct obdn_V_MemBlock {
    VkDeviceSize    size;
    VkDeviceSize    offset;
    bool            inUse;
    Obdn_V_BlockId id; // global unique identifier
} Obdn_V_MemBlock;

typedef struct BlockChain {
    char               name[16]; // for debugging
    VkDeviceSize       totalSize;
    VkDeviceSize       usedSize;
    VkDeviceSize       defaultAlignment;
    VkDeviceAddress    bufferAddress;
    uint32_t           count;
    uint32_t           cur;
    uint32_t           nextBlockId;
    VkDeviceMemory     memory;
    VkBuffer           buffer;
    VkBufferUsageFlags bufferFlags;
    uint8_t*           hostData;
    Obdn_V_MemBlock    blocks[MAX_BLOCKS];
} BlockChain;

static BlockChain blockChainHostGraphicsBuffer;
static BlockChain blockChainDeviceGraphicsBuffer;
static BlockChain blockChainDeviceGraphicsImage;
static BlockChain blockChainHostTransferBuffer;
static BlockChain blockChainExternalDeviceGraphicsImage; // for sharing with external apis like opengl

static const VkPhysicalDeviceProperties* deviceProperties;

static uint32_t hostVisibleCoherentTypeIndex = 0;
static uint32_t deviceLocalTypeIndex = 0;

static uint32_t findMemoryType(uint32_t memoryTypeBitsRequirement) __attribute__ ((unused));
static void printBufferMemoryReqs(const VkMemoryRequirements* reqs) __attribute__ ((unused));
static void printBlockChainInfo(const BlockChain* chain) __attribute__ ((unused));

#define DPRINT(fmt, ...) hell_DebugPrint(OBDN_DEBUG_TAG_MEM, fmt, ##__VA_ARGS__)

static void printBufferMemoryReqs(const VkMemoryRequirements* reqs)
{
    DPRINT("Size: %zu\tAlignment: %zu\n", reqs->size, reqs->alignment);
}

static void printBlockChainInfo(const BlockChain* chain)
{
    DPRINT("BlockChain %s:\n", chain->name);
    DPRINT("totalSize: %zu\t count: %d\t cur: %d\t nextBlockId: %d\n", chain->totalSize, chain->count, chain->cur, chain->nextBlockId);
    DPRINT("memory: %p\t buffer: %p\t hostData: %p\n", chain->memory, chain->buffer, chain->hostData);
    DPRINT("Blocks: \n");
    for (int i = 0; i < chain->count; i++) 
    {
        const Obdn_V_MemBlock* block = &chain->blocks[i];
        DPRINT("{ Block %d: size = %zu, offset = %zu, inUse = %s, id: %d}, ", i, block->size, block->offset, block->inUse ? "true" : "false", block->id);
    }
    DPRINT("\n");
}

static Obdn_V_MemorySizes getDefaultMemSizes(void)
{
    return (Obdn_V_MemorySizes){
        .hostGraphicsBufferMemorySize = OBDN_256_MiB,
        .deviceGraphicsBufferMemorySize = OBDN_256_MiB,
        .deviceGraphicsImageMemorySize = OBDN_512_MiB,
        .hostTransferBufferMemorySize = 0,
        .deviceExternalGraphicsImageMemorySize = 0,
    };
}

static uint32_t findMemoryType(uint32_t memoryTypeBitsRequirement)
{
    const uint32_t memoryCount = memoryProperties.memoryTypeCount;
    for (uint32_t memoryIndex = 0; memoryIndex < memoryCount; ++memoryIndex) {
        const uint32_t memoryTypeBits = (1 << memoryIndex);
        if (memoryTypeBits & memoryTypeBitsRequirement) return memoryIndex;
    }
    return -1;
}

static void initBlockChain(
        const Obdn_V_MemoryType memType,
        const VkDeviceSize memorySize, 
        const uint32_t memTypeIndex, 
        const VkBufferUsageFlags bufferUsageFlags, 
        const bool mapBuffer,
        const char* name,
        struct BlockChain* chain)
{
    memset(chain, 0, sizeof(BlockChain));
    strcpy(chain->name, name);
    if (memorySize == 0) return; // basically saying we arent using this memory type
    if (memorySize % 0x40 != 0)
        hell_Error(HELL_ERR_FATAL, "Failed to initialize %s block chain because requested %zu bytes is not divisible by 0x40\n", name, memorySize);
    assert( memorySize % 0x40 == 0 ); // make sure memorysize is 64 byte aligned (arbitrary choice)
    chain->count = 1;
    chain->cur   = 0;
    chain->totalSize = memorySize;
    chain->usedSize  = 0;
    chain->nextBlockId = 1; 
    chain->defaultAlignment = 4;
    chain->bufferFlags = bufferUsageFlags;
    chain->blocks[0].inUse = false;
    chain->blocks[0].offset = 0;
    chain->blocks[0].size = memorySize;
    chain->blocks[0].id = 0;
    assert( strlen(name) < 16 );
    strcpy(chain->name, name);

    if (chain->totalSize == 0) return; // nothing else to do


    VkExportMemoryAllocateInfo exportMemoryAllocInfo;
    const void* pNext = NULL;
    if (memType == OBDN_V_MEMORY_EXTERNAL_DEVICE_TYPE)
    {
        exportMemoryAllocInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
        exportMemoryAllocInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        exportMemoryAllocInfo.pNext = NULL;
        pNext = &exportMemoryAllocInfo;
    }

    const VkMemoryAllocateFlagsInfo allocFlagsInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
        .pNext = pNext
    };

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &allocFlagsInfo,
        .allocationSize = memorySize,
        .memoryTypeIndex = memTypeIndex,
    };

    V_ASSERT( vkAllocateMemory(device, &allocInfo, NULL, &chain->memory) ); 

    if (bufferUsageFlags)
    {

        uint32_t queueFamilyIndex; 
        switch (memType) 
        {
            case OBDN_V_MEMORY_HOST_GRAPHICS_TYPE:          queueFamilyIndex = obdn_v_GetQueueFamilyIndex(OBDN_V_QUEUE_GRAPHICS_TYPE); break;
            case OBDN_V_MEMORY_HOST_TRANSFER_TYPE:          queueFamilyIndex = obdn_v_GetQueueFamilyIndex(OBDN_V_QUEUE_TRANSFER_TYPE); break;
            case OBDN_V_MEMORY_DEVICE_TYPE:                 queueFamilyIndex = obdn_v_GetQueueFamilyIndex(OBDN_V_QUEUE_GRAPHICS_TYPE); break;
            case OBDN_V_MEMORY_EXTERNAL_DEVICE_TYPE:        queueFamilyIndex = obdn_v_GetQueueFamilyIndex(OBDN_V_QUEUE_GRAPHICS_TYPE); break;
            default: assert(0); break;
        }

        VkBufferCreateInfo ci = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .usage = bufferUsageFlags,
            .queueFamilyIndexCount = 1,
            .pQueueFamilyIndices = &queueFamilyIndex,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE, // queue family determined by first use
            // TODO this sharing mode is why we need to expand Obdn_V_MemoryType to specify
            // queue usage as well. So we do need graphics type, transfer type, and compute types
            .size = memorySize 
        };

        V_ASSERT( vkCreateBuffer(device, &ci, NULL, &chain->buffer) );

        V_ASSERT( vkBindBufferMemory(device, chain->buffer, chain->memory, 0) );

        VkMemoryRequirements memReqs;

        vkGetBufferMemoryRequirements(device, chain->buffer, &memReqs);

        //chain->defaultAlignment = memReqs.alignment;

        DPRINT("Host Buffer ALIGNMENT: %zu\n", memReqs.alignment);

        const VkBufferDeviceAddressInfo addrInfo = {
            .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = chain->buffer
        };

        chain->bufferAddress = vkGetBufferDeviceAddress(device, &addrInfo);

        if (mapBuffer)
            V_ASSERT( vkMapMemory(device, chain->memory, 0, VK_WHOLE_SIZE, 0, (void**)&chain->hostData) );
        else 
            chain->hostData = NULL;
    }
    else
    {
        chain->buffer = VK_NULL_HANDLE;
        chain->hostData = NULL;
    }
}

static void freeBlockChain(struct BlockChain* chain)
{
    memset(chain->blocks, 0, MAX_BLOCKS * sizeof(Obdn_V_MemBlock));
    if (chain->buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device, chain->buffer, NULL);
        if (chain->hostData != NULL)
            vkUnmapMemory(device, chain->memory);
        chain->buffer = VK_NULL_HANDLE;
    }
    vkFreeMemory(device, chain->memory, NULL);
    memset(chain, 0, sizeof(*chain));
}

static int findAvailableBlockIndex(const uint32_t size, struct BlockChain* chain)
{
    size_t cur  = 0;
    size_t init = cur;
    const size_t count = chain->count;
    DPRINT(">>> requesting block of size %d from chain %s with totalSize %zu\n", size, chain->name, chain->totalSize);
    assert( size < chain->totalSize );
    assert( count > 0 );
    assert( cur < count );
    while (chain->blocks[cur].inUse || chain->blocks[cur].size < size)
    {
        cur = (cur + 1) % count;
        if (cur == init) 
        {
            DPRINT("%s: no suitable block found in chain %s\n", __PRETTY_FUNCTION__, chain->name);
            return -1;
        }
    }
    // found a block not in use and with enough size
    return cur;
}

// from i = firstIndex to i = lastIndex - 1, swap block i with block i + 1
// note this can operate on indices beyond chain->count
static void rotateBlockUp(const size_t fromIndex, const size_t toIndex, struct BlockChain* chain)
{
    assert ( fromIndex >= 0 );
    assert ( toIndex < MAX_BLOCKS);
    assert ( toIndex > fromIndex );
    for (int i = fromIndex; i < toIndex; i++) 
    {
        Obdn_V_MemBlock temp = chain->blocks[i];
        chain->blocks[i] = chain->blocks[i + 1];
        chain->blocks[i + 1] = temp;
    }
}

static void rotateBlockDown(const size_t fromIndex, const size_t toIndex, struct BlockChain* chain)
{
    assert ( toIndex >= 0 );
    assert ( fromIndex < MAX_BLOCKS);
    assert ( fromIndex > toIndex );
    for (int i = fromIndex; i > toIndex; i--) 
    {
        Obdn_V_MemBlock temp = chain->blocks[i];
        chain->blocks[i] = chain->blocks[i - 1];
        chain->blocks[i - 1] = temp;
    }
}

static void mergeBlocks(struct BlockChain* chain)
{
    assert (chain->count > 1); // must have at least 2 blocks to defragment
    for (int i = 0; i < chain->count - 1; i++) 
    {
        Obdn_V_MemBlock* curr = &chain->blocks[i];   
        Obdn_V_MemBlock* next = &chain->blocks[i + 1];   
        if (!curr->inUse && !next->inUse)
        {
            // combine them together
            const VkDeviceSize cSize   = curr->size + next->size;
            // we could make the other id available for re-use as well
            curr->size   = cSize;
            // set next block to 0
            memset(next, 0, sizeof(Obdn_V_MemBlock));
            // rotate blocks down past i so that next goes to chain->count - 1
            // and the block at chain->size - 1 goes to chain->count - 2
            if (i + 1 != chain->count - 1)
            {
                rotateBlockUp(i + 1, chain->count - 1, chain);
            }
            // decrement the chain count
            chain->count--;
            i--;
        }
    }
}

static void defragment(struct BlockChain* chain)
{
    // TODO: implement a proper defragment function
    DPRINT("Empty defragment function called\n");
}

static bool chainIsOrdered(const struct BlockChain* chain)
{
    for (int i = 0; i < chain->count - 1; i++) 
    {
        const Obdn_V_MemBlock* curr = &chain->blocks[i];
        const Obdn_V_MemBlock* next = &chain->blocks[i + 1];
        if (curr->offset > next->offset) return false;
    }
    return true;
}

static Obdn_V_MemBlock* requestBlock(const uint32_t size, const uint32_t alignment, struct BlockChain* chain)
{
    const int curIndex = findAvailableBlockIndex(size, chain);
    if (curIndex < 0) // try defragmenting. if that fails we're done.
    {
        defragment(chain); 
        const int curIndex = findAvailableBlockIndex(size, chain);
        if (curIndex < 0)
        {
            DPRINT("Memory allocation failed\n");
            exit(0);
        }
    }
    Obdn_V_MemBlock* curBlock = &chain->blocks[curIndex];
    if (curBlock->size == size && (curBlock->offset % alignment == 0)) // just reuse this block;
    {
        DPRINT("%s here %d\n", __PRETTY_FUNCTION__, curIndex);
        curBlock->inUse = true;
        chain->usedSize += curBlock->size;
        DPRINT(">> Re-using block %d of size %09zu from chain %s. %zu bytes out of %zu now in use.\n", curBlock->id, curBlock->size, chain->name, chain->usedSize, chain->totalSize);
        return curBlock;
    }
    // split the block
    const size_t newIndex = chain->count++;
    Obdn_V_MemBlock* newBlock = &chain->blocks[newIndex];
    assert( newIndex < MAX_BLOCKS );
    assert( newBlock->inUse == false );
    VkDeviceSize alignedOffset = hell_Align(curBlock->offset, alignment);
    const VkDeviceSize offsetDiff = alignedOffset - curBlock->offset;
    // take away the size lost due to alignment and the new size
    newBlock->size   = curBlock->size - offsetDiff - size;
    newBlock->offset = alignedOffset + size;
    newBlock->id = chain->nextBlockId++;
    curBlock->size   = size;
    curBlock->offset = alignedOffset;
    curBlock->inUse = true;
    if (newIndex != curIndex + 1)
        rotateBlockDown(newIndex, curIndex + 1, chain);
    assert( chainIsOrdered(chain) );
    chain->usedSize += curBlock->size;
    DPRINT(">> Alocating block %d of size %09zu from chain %s. %zu bytes out of %zu now in use.\n", curBlock->id, curBlock->size, chain->name, chain->usedSize, chain->totalSize);
    return curBlock;
}

static void freeBlock(struct BlockChain* chain, const Obdn_V_BlockId id)
{
    assert( id < chain->nextBlockId);
    const size_t blockCount = chain->count;
    int blockIndex = 0;
    for ( ; blockIndex < blockCount; blockIndex++) 
    {
        if (chain->blocks[blockIndex].id == id)
            break;
    }
    assert( blockIndex < blockCount ); // block must not have come from this chain
    Obdn_V_MemBlock* block = &chain->blocks[blockIndex];
    const VkDeviceSize size = block->size;
    block->inUse = false;
    mergeBlocks(chain);
    chain->usedSize -= size;
    DPRINT(">> Freeing block %d of size %09zu from chain %s. %zu bytes out of %zu now in use.\n", id, size, chain->name, chain->usedSize, chain->totalSize);
}

void obdn_v_InitMemory(const Obdn_V_MemorySizes* memSizes)
{
    hostVisibleCoherentTypeIndex = 0;
    deviceLocalTypeIndex = 0;

    deviceProperties = obdn_v_GetPhysicalDeviceProperties();

    vkGetPhysicalDeviceMemoryProperties(obdn_v_GetPhysicalDevice(), &memoryProperties);

    DPRINT("Memory Heap Info:\n");
    for (int i = 0; i < memoryProperties.memoryHeapCount; i++) 
    {
        DPRINT("Heap %d: Size %zu: %s local\n", 
                i,
                memoryProperties.memoryHeaps[i].size, 
                memoryProperties.memoryHeaps[i].flags 
                & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ? "Device" : "Host");
                // note there are other possible flags, but seem to only deal with multiple gpus
    }

    bool foundHvc = false;
    bool foundDl  = false;

    DPRINT("Memory Type Info:\n");
    for (int i = 0; i < memoryProperties.memoryTypeCount; i++) 
    {
        VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[i].propertyFlags;
        DPRINT("Type %d: Heap Index: %d Flags: | %s%s%s%s%s%s\n", 
                i, 
                memoryProperties.memoryTypes[i].heapIndex,
                flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ?  "Device Local | " : "",
                flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ?  "Host Visible | " : "",
                flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ? "Host Coherent | " : "",
                flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ?   "Host Cached | " : "",
                flags & VK_MEMORY_PROPERTY_PROTECTED_BIT ?     "Protected | "   : "",
                flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT ? "Lazily allocated | " : ""
                );   
        if ((flags & (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) &&
                !foundHvc)
        {
            hostVisibleCoherentTypeIndex = i;
            foundHvc = true;
        }
        if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) && !foundDl)
        {
            deviceLocalTypeIndex = i;
            foundDl = true;
        }
    }

    assert( foundHvc );
    assert( foundDl );
    DPRINT("Host Visible and Coherent memory type index found: %d\n", hostVisibleCoherentTypeIndex);
    DPRINT("Device local memory type index found: %d\n", deviceLocalTypeIndex);

    VkBufferUsageFlags hostGraphicsFlags = 
         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | 
         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
         VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
         VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
         VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
         VK_BUFFER_USAGE_TRANSFER_DST_BIT |
         VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VkBufferUsageFlags hostTransferFlags = 
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VkBufferUsageFlags devBufFlags = 
         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | 
         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
         VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
         VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
         VK_BUFFER_USAGE_TRANSFER_DST_BIT |
         VK_BUFFER_USAGE_TRANSFER_SRC_BIT; 

    Obdn_V_MemorySizes ms;
    if (!memSizes)
    {
        ms = getDefaultMemSizes();
        memSizes = &ms;
    }

    DPRINT("Obsidian: Initializing memory block chains...\n");
    initBlockChain(OBDN_V_MEMORY_HOST_GRAPHICS_TYPE, 
            memSizes->hostGraphicsBufferMemorySize, 
            hostVisibleCoherentTypeIndex, hostGraphicsFlags, 
            true, "hstGraphBuffer", &blockChainHostGraphicsBuffer);
    initBlockChain(OBDN_V_MEMORY_HOST_TRANSFER_TYPE, 
            memSizes->hostTransferBufferMemorySize, 
            hostVisibleCoherentTypeIndex, hostTransferFlags, 
            true, "hstTransBuffer", &blockChainHostTransferBuffer);
    initBlockChain(OBDN_V_MEMORY_DEVICE_TYPE, 
            memSizes->deviceGraphicsBufferMemorySize, 
            deviceLocalTypeIndex, devBufFlags, 
            false, "devBuffer", &blockChainDeviceGraphicsBuffer);
    initBlockChain(OBDN_V_MEMORY_DEVICE_TYPE, 
            memSizes->deviceGraphicsImageMemorySize, 
            deviceLocalTypeIndex, 0, 
            false, "devImage", &blockChainDeviceGraphicsImage);
    initBlockChain(OBDN_V_MEMORY_EXTERNAL_DEVICE_TYPE, 
            memSizes->deviceExternalGraphicsImageMemorySize, 
            deviceLocalTypeIndex, 0, 
            false, "devExtImage", &blockChainExternalDeviceGraphicsImage);
}

Obdn_V_BufferRegion obdn_v_RequestBufferRegionAligned(
        const size_t size, 
        uint32_t alignment, const Obdn_V_MemoryType memType)
{
    assert(size > 0);
    if( size % 4 != 0 ) // only allow for word-sized blocks
    {
        hell_Error(HELL_ERR_FATAL, "Size %zu is not 4 byte aligned.", size);
    }
    Obdn_V_MemBlock*  block = NULL;
    struct BlockChain* chain = NULL;
    switch (memType) 
    {
        case OBDN_V_MEMORY_HOST_GRAPHICS_TYPE:   chain = &blockChainHostGraphicsBuffer; break;
        case OBDN_V_MEMORY_HOST_TRANSFER_TYPE:   chain = &blockChainHostTransferBuffer; break;
        case OBDN_V_MEMORY_DEVICE_TYPE: chain = &blockChainDeviceGraphicsBuffer; break;
        default: block = NULL; assert(0);
    }

    if (0 == alignment)
        alignment = chain->defaultAlignment;
    block = requestBlock(size, alignment, chain); 

    Obdn_V_BufferRegion region = {};
    region.offset = block->offset;
    region.memBlockId = block->id;
    region.size = block->size;
    region.buffer = chain->buffer;
    region.pChain = chain;
    
    // TODO this check is very brittle. we should probably change V_MemoryType to a mask with flags for the different
    // requirements. One bit for host or device, one bit for transfer capabale, one bit for external, one bit for image or buffer
    if (memType == OBDN_V_MEMORY_HOST_TRANSFER_TYPE|| memType == OBDN_V_MEMORY_HOST_GRAPHICS_TYPE) 
        region.hostData = chain->hostData + block->offset;
    else
        region.hostData = NULL;

    return region;
}

Obdn_V_BufferRegion obdn_v_RequestBufferRegion(const size_t size, 
        const VkBufferUsageFlags flags, const Obdn_V_MemoryType memType)
{
    uint32_t alignment = 16;
    if ( VK_BUFFER_USAGE_STORAGE_BUFFER_BIT & flags)
        alignment = MAX(deviceProperties->limits.minStorageBufferOffsetAlignment, alignment);
    if ( VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT & flags)
        alignment = MAX(deviceProperties->limits.minUniformBufferOffsetAlignment, alignment);
    if ( VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR & flags)
        alignment = MAX(256, alignment); // 256 comes from spec for VkAccelerationStructureCreateInfoKHR - see offset
    return obdn_v_RequestBufferRegionAligned(size, alignment, memType); //TODO: fix this. find the maximum alignment and choose that
}

uint32_t obdn_v_GetMemoryType(uint32_t typeBits, const VkMemoryPropertyFlags properties)
{
    for(uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
    {
      if(((typeBits & (1 << i)) > 0) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
      {
        return i;
      }
    }
    assert(0);
    return ~0u;
}

Obdn_V_Image obdn_v_CreateImage(
        const uint32_t width, 
        const uint32_t height,
        const VkFormat format,
        const VkImageUsageFlags usageFlags,
        const VkImageAspectFlags aspectMask,
        const VkSampleCountFlags sampleCount,
        const uint32_t mipLevels,
        const Obdn_V_MemoryType memType)
{
    assert(mipLevels > 0);
    assert(memType == OBDN_V_MEMORY_DEVICE_TYPE || memType == OBDN_V_MEMORY_EXTERNAL_DEVICE_TYPE);

    assert(deviceProperties->limits.framebufferColorSampleCounts >= sampleCount);
    assert(deviceProperties->limits.framebufferDepthSampleCounts >= sampleCount);

    void* pNext = NULL;

    VkExternalMemoryImageCreateInfo externalImageInfo;
    if (memType == OBDN_V_MEMORY_EXTERNAL_DEVICE_TYPE)
    {
        externalImageInfo.sType =
            VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        externalImageInfo.handleTypes =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        externalImageInfo.pNext = NULL;
        pNext                   = &externalImageInfo;
    }

    uint32_t queueFamilyIndex =
        obdn_v_GetQueueFamilyIndex(OBDN_V_QUEUE_GRAPHICS_TYPE);
    VkImageCreateInfo imageInfo = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                   .pNext = pNext,
                                   .imageType   = VK_IMAGE_TYPE_2D,
                                   .format      = format,
                                   .extent      = {width, height, 1},
                                   .mipLevels   = mipLevels,
                                   .arrayLayers = 1,
                                   .samples     = sampleCount,
                                   .tiling      = VK_IMAGE_TILING_OPTIMAL,
                                   .usage       = usageFlags,
                                   .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                   .queueFamilyIndexCount = 1,
                                   .pQueueFamilyIndices   = &queueFamilyIndex,
                                   .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};

    Obdn_V_Image image = {0};

    V_ASSERT( vkCreateImage(device, &imageInfo, NULL, &image.handle) );

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, image.handle, &memReqs);

    DPRINT("Requesting image of size %zu (0x%zx) \n", memReqs.size, memReqs.size);
    DPRINT("Width * Height * Format size = %d\n", width * height * 4);
    DPRINT("Required memory bits for image: %0x\n", memReqs.memoryTypeBits);

    image.size = memReqs.size;
    image.extent.depth = 1;
    image.extent.width = width;
    image.extent.height = height;
    image.mipLevels = mipLevels;

    switch (memType)
    {
        case OBDN_V_MEMORY_DEVICE_TYPE:          image.pChain = &blockChainDeviceGraphicsImage; break;
        case OBDN_V_MEMORY_EXTERNAL_DEVICE_TYPE: image.pChain = &blockChainExternalDeviceGraphicsImage; break;
        default: assert(0);
    }

    const Obdn_V_MemBlock* block = requestBlock(memReqs.size, memReqs.alignment, image.pChain);
    image.memBlockId = block->id;
    image.offset = block->offset;

    vkBindImageMemory(device, image.handle, image.pChain->memory, block->offset);

    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image.handle,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .components = {0, 0, 0, 0}, // no swizzling
        .format = format,
        .subresourceRange = {
            .aspectMask = aspectMask,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    V_ASSERT( vkCreateImageView(device, &viewInfo, NULL, &image.view) );

    image.sampler = VK_NULL_HANDLE;
    image.layout  = imageInfo.initialLayout;

    return image;
}

void obdn_v_FreeImage(Obdn_V_Image* image)
{
    assert(image->size != 0);
    if (image->sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(device, image->sampler, NULL);
    }
    vkDestroyImageView(device, image->view, NULL);
    vkDestroyImage(device, image->handle, NULL);
    freeBlock(image->pChain, image->memBlockId);
    memset(image, 0, sizeof(Obdn_V_Image));
}

void obdn_v_FreeBufferRegion(Obdn_V_BufferRegion* pRegion)
{
    assert(pRegion->size != 0);
    freeBlock(pRegion->pChain, pRegion->memBlockId);
    if (pRegion->hostData)
        memset(pRegion->hostData, 0, pRegion->size);
    memset(pRegion, 0, sizeof(Obdn_V_BufferRegion));
}

void obdn_v_CopyBufferRegion(const Obdn_V_BufferRegion* src, Obdn_V_BufferRegion* dst)
{
    Obdn_V_Command cmd = obdn_v_CreateCommand(OBDN_V_QUEUE_GRAPHICS_TYPE); // arbitrary index;

    obdn_v_BeginCommandBuffer(cmd.buffer);

    VkBufferCopy copy;
    copy.srcOffset = src->offset;
    copy.dstOffset = dst->offset;
    copy.size      = src->size;

    vkCmdCopyBuffer(cmd.buffer, src->buffer, dst->buffer, 1, &copy);

    obdn_v_EndCommandBuffer(cmd.buffer);

    obdn_v_SubmitAndWait(&cmd, 0);

    obdn_v_DestroyCommand(cmd);
}

void obdn_v_CopyImageToBufferRegion(const Obdn_V_Image* image, Obdn_V_BufferRegion* bufferRegion)
{
    // TODO
}

void obdn_v_TransferToDevice(Obdn_V_BufferRegion* pRegion)
{
    const Obdn_V_BufferRegion srcRegion = *pRegion;
    assert( srcRegion.pChain == &blockChainHostGraphicsBuffer); // only chain it makes sense to transfer from
    Obdn_V_BufferRegion destRegion = obdn_v_RequestBufferRegion(srcRegion.size, 0, OBDN_V_MEMORY_DEVICE_TYPE);

    obdn_v_CopyBufferRegion(&srcRegion, &destRegion);

    obdn_v_FreeBufferRegion(pRegion);

    *pRegion = destRegion;
}

void obdn_v_CleanUpMemory(void)
{
    freeBlockChain(&blockChainHostGraphicsBuffer);
    freeBlockChain(&blockChainHostTransferBuffer);
    freeBlockChain(&blockChainDeviceGraphicsImage);
    freeBlockChain(&blockChainDeviceGraphicsBuffer);
    freeBlockChain(&blockChainExternalDeviceGraphicsImage);
}

VkDeviceAddress obdn_v_GetBufferRegionAddress(const BufferRegion* region)
{
    assert(region->pChain->bufferAddress != 0);
    return region->pChain->bufferAddress + region->offset;
}

void obdn_v_CreateUnmanagedBuffer(const VkBufferUsageFlags bufferUsageFlags, 
        const uint32_t memorySize, const Obdn_V_MemoryType type, 
        VkDeviceMemory* pMemory, VkBuffer* pBuffer)
{
    uint32_t typeIndex;
    switch (type) 
    {
        case OBDN_V_MEMORY_HOST_GRAPHICS_TYPE:       typeIndex = hostVisibleCoherentTypeIndex; break;
        case OBDN_V_MEMORY_HOST_TRANSFER_TYPE:       typeIndex = hostVisibleCoherentTypeIndex; break;
        case OBDN_V_MEMORY_DEVICE_TYPE:              typeIndex = deviceLocalTypeIndex; break;
        case OBDN_V_MEMORY_EXTERNAL_DEVICE_TYPE:     typeIndex = deviceLocalTypeIndex; break;
    }

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memorySize,
        .memoryTypeIndex = typeIndex 
    };

    VkBufferCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .usage = bufferUsageFlags,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, // queue determined by first use
        .size = memorySize 
    };

    V_ASSERT( vkAllocateMemory(device, &allocInfo, NULL, pMemory) ); 

    V_ASSERT( vkCreateBuffer(device, &ci, NULL, pBuffer) );

    V_ASSERT( vkBindBufferMemory(device, *pBuffer, *pMemory, 0) );
}

const VkDeviceMemory obdn_v_GetDeviceMemory(const Obdn_V_MemoryType memType)
{
    switch (memType)
    {
        case OBDN_V_MEMORY_EXTERNAL_DEVICE_TYPE: return blockChainExternalDeviceGraphicsImage.memory;
        default: assert(0); //TODO
        return 0;
    }
}

const VkDeviceSize obdn_v_GetMemorySize(const Obdn_V_MemoryType memType)
{
    switch (memType)
    {
        case OBDN_V_MEMORY_EXTERNAL_DEVICE_TYPE: return blockChainExternalDeviceGraphicsImage.totalSize;
        default: assert(0); //TODO
        return 0;
    }
}

