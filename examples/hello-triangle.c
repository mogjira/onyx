#include <hell/common.h>
#include <hell/debug.h>
#include <hell/window.h>
#include <hell/cmd.h>
#include <coal/m.h>
#include "common.h"
#include "v_swapchain.h"
#include "s_scene.h"
#include "r_pipeline.h"
#include "r_pipeline.h"
#include "r_geo.h"
#include "r_renderpass.h"
#include "s_scene.h"

#define WIREFRAME 0

static Obdn_R_Primitive    triangle;

static const Hell_Window*  window;
static VkFramebuffer       framebuffer;
static VkRenderPass        renderPass;
static VkPipelineLayout    pipelineLayout;
static VkPipeline          pipeline;
static VkFramebuffer       framebuffers[2];
static Obdn_Image          depthImage;

static const VkFormat depthFormat = VK_FORMAT_D24_UNORM_S8_UINT;

static void createSurfaceDependent(void)
{
    depthImage = obdn_v_CreateImage(obdn_GetSurfaceWidth(), obdn_GetSurfaceHeight(),
                       depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                       VK_IMAGE_ASPECT_DEPTH_BIT, VK_SAMPLE_COUNT_1_BIT, 1,
                       OBDN_V_MEMORY_DEVICE_TYPE);
    VkImageView attachments_0[2] = {obdn_GetSwapchainImage(0, 0)->view, depthImage.view};
    VkImageView attachments_1[2] = {obdn_GetSwapchainImage(0, 1)->view, depthImage.view};
    obdn_CreateFramebuffer(2, attachments_0, window->width, window->height, renderPass, &framebuffers[0]);
    obdn_CreateFramebuffer(2, attachments_1, window->width, window->height, renderPass, &framebuffers[1]);
}

static void destroySurfaceDependent(void)
{
    obdn_v_FreeImage(&depthImage);
    obdn_DestroyFramebuffer(framebuffers[0]);
    obdn_DestroyFramebuffer(framebuffers[1]);
}

static void onSwapchainRecreate(void)
{
    destroySurfaceDependent();
    createSurfaceDependent();
}

void init(void)
{
    uint8_t attrSize = 3 * sizeof(float);
    triangle = obdn_r_CreatePrimitive(3, 4, 1, &attrSize);
    Coal_Vec3* verts = (Coal_Vec3*)triangle.vertexRegion.hostData;
    verts[0][0] =  0.0;
    verts[0][1] = -1.0;
    verts[0][2] =  0.0;
    verts[1][0] = -1.0;
    verts[1][1] =  1.0;
    verts[1][2] =  0.0;
    verts[2][0] =  1.0;
    verts[2][1] =  1.0;
    verts[2][2] =  0.0;
    Obdn_AttrIndex* indices = (Obdn_AttrIndex*)triangle.indexRegion.hostData;
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;
    indices[3] = 0; // this last index is so we render the full triangle in line mode

    obdn_r_PrintPrim(&triangle);

    // call this render pass joe
    obdn_r_CreateRenderPass_ColorDepth(
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
        obdn_v_GetSwapFormat(), depthFormat, &renderPass);
    Obdn_R_PipelineLayoutInfo pli = {0}; //nothin
    obdn_r_CreatePipelineLayouts(1, &pli, &pipelineLayout);
    if (WIREFRAME)
        obdn_CreateGraphicsPipeline_Taurus(renderPass, pipelineLayout, VK_POLYGON_MODE_LINE, &pipeline);
    else
        obdn_CreateGraphicsPipeline_Taurus(renderPass, pipelineLayout, VK_POLYGON_MODE_FILL, &pipeline);

    createSurfaceDependent();

    obdn_v_RegisterSwapchainRecreationFn(onSwapchainRecreate);
}

#define TARGET_RENDER_INTERVAL 10000 // render every 30 ms

void draw(void)
{
    static Hell_Tick timeOfLastRender = 0;
    static Hell_Tick timeSinceLastRender = TARGET_RENDER_INTERVAL;
    timeSinceLastRender = hell_Time() - timeOfLastRender;
    if (timeSinceLastRender < TARGET_RENDER_INTERVAL)
        return;
    timeOfLastRender = hell_Time();
    timeSinceLastRender = 0;

    static Obdn_Command drawCommands[2];
    static uint64_t     frameCounter = 0;
    static bool         initialized = false;

    if (!initialized)
    {
        drawCommands[0] = obdn_v_CreateCommand(OBDN_V_QUEUE_GRAPHICS_TYPE);
        drawCommands[1] = obdn_v_CreateCommand(OBDN_V_QUEUE_GRAPHICS_TYPE);
        initialized = true;
    }
    
    VkFence      drawFence;
    VkSemaphore  drawSemaphore;
    VkSemaphore  acquireSemaphore;
    obdn_CreateFence(&drawFence);
    obdn_CreateSemaphore(&drawSemaphore);
    obdn_CreateSemaphore(&acquireSemaphore);

    VkFence fence = VK_NULL_HANDLE;

    unsigned frameId = obdn_AcquireSwapchainImage(&fence, &acquireSemaphore);

    Obdn_Command cmd = drawCommands[frameCounter % 2];

    obdn_v_ResetCommand(&cmd);
    
    obdn_v_BeginCommandBuffer(cmd.buffer);

    obdn_CmdSetViewportScissorFull(cmd.buffer, obdn_GetSurfaceWidth(), obdn_GetSurfaceHeight());

    obdn_CmdBeginRenderPass_ColorDepth(
        cmd.buffer, renderPass, framebuffers[frameId], obdn_GetSurfaceWidth(),
        obdn_GetSurfaceHeight(), 0.0, 0.0, 0.01, 1.0);

        vkCmdBindPipeline(cmd.buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        obdn_r_DrawPrim(cmd.buffer, &triangle);

    obdn_CmdEndRenderPass(cmd.buffer);

    obdn_v_EndCommandBuffer(cmd.buffer);

    obdn_v_SubmitGraphicsCommand(0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
            acquireSemaphore, drawSemaphore, drawFence, cmd.buffer);

    obdn_PresentFrame(drawSemaphore);

    obdn_v_WaitForFence(&drawFence);

    obdn_PresentQueueWaitIdle();

    obdn_DestroyFence(drawFence);
    obdn_DestroySemaphore(acquireSemaphore);
    obdn_DestroySemaphore(drawSemaphore);

    frameCounter++;
}

int main(int argc, char *argv[])
{
    hell_Init(true, draw, 0);
    hell_c_SetVar("maxFps", "1000", 0);
    hell_Print("Starting hello triangle.");
    window = hell_OpenWindow(500, 500, 0);
    obdn_Init();
    obdn_CreateSwapchain(window);
    init();
    hell_Loop();
    return 0;
}
