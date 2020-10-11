#include "r_pipeline.h"
#include "v_video.h"
#include "r_render.h"
#include "m_math.h"
#include "t_def.h"
#include "v_vulkan.h"
#include <stdio.h>
#include <assert.h>

VkPipeline       pipelines[TANTO_MAX_PIPELINES];
VkDescriptorSet  descriptorSets[TANTO_MAX_DESCRIPTOR_SETS];
VkPipelineLayout pipelineLayouts[TANTO_MAX_PIPELINES];

static VkDescriptorSetLayout descriptorSetLayouts[TANTO_MAX_DESCRIPTOR_SETS]; 
static VkDescriptorPool      descriptorPool;

enum shaderStageType { VERT, FRAG };

static void initShaderModule(const char* filepath, VkShaderModule* module)
{
    VkResult r;
    int fr;
    FILE* fp;
    fp = fopen(filepath, "rb");
    fr = fseek(fp, 0, SEEK_END);
    assert( fr == 0 ); // success 
    size_t codeSize = ftell(fp);
    rewind(fp);

    unsigned char code[codeSize];
    fread(code, 1, codeSize, fp);
    fclose(fp);

    const VkShaderModuleCreateInfo shaderInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = codeSize,
        .pCode = (uint32_t*)code,
    };

    r = vkCreateShaderModule(device, &shaderInfo, NULL, module);
    assert( VK_SUCCESS == r );
}

static void createPipelineRayTrace(const Tanto_R_PipelineInfo* plInfo)
{
    VkShaderModule raygenSM;
    VkShaderModule missSM;
    VkShaderModule missShadowSM;
    VkShaderModule chitSM;

    initShaderModule(TANTO_SPVDIR"/raytrace-rgen.spv",  &raygenSM);
    initShaderModule(TANTO_SPVDIR"/raytrace-rmiss.spv", &missSM);
    initShaderModule(TANTO_SPVDIR"/raytraceShadow-rmiss.spv", &missShadowSM);
    initShaderModule(TANTO_SPVDIR"/raytrace-rchit.spv", &chitSM);

    VkPipelineShaderStageCreateInfo shaderStages[] = {{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
        .module = raygenSM,
        .pName = "main",
    },{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
        .module = missSM,
        .pName = "main",
    },{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
        .module = missShadowSM,
        .pName = "main",
    },{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        .module = chitSM,
        .pName = "main",
    }};

    VkRayTracingShaderGroupCreateInfoKHR rg = {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type  = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
        .generalShader =      0, // stage 0 in shaderStages
        .closestHitShader =   VK_SHADER_UNUSED_KHR,
        .anyHitShader =       VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
        .pShaderGroupCaptureReplayHandle = NULL
    };

    VkRayTracingShaderGroupCreateInfoKHR mg = {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type  = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
        .generalShader =      1, // stage 1 in shaderStages
        .closestHitShader =   VK_SHADER_UNUSED_KHR,
        .anyHitShader =       VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
        .pShaderGroupCaptureReplayHandle = NULL
    };

    VkRayTracingShaderGroupCreateInfoKHR msg = {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type  = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
        .generalShader =      2, // stage 1 in shaderStages
        .closestHitShader =   VK_SHADER_UNUSED_KHR,
        .anyHitShader =       VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
        .pShaderGroupCaptureReplayHandle = NULL
    };

    VkRayTracingShaderGroupCreateInfoKHR hg = {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .type  = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
        .generalShader =      VK_SHADER_UNUSED_KHR, // stage 1 in shaderStages
        .closestHitShader =   3,
        .anyHitShader =       VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR,
        .pShaderGroupCaptureReplayHandle = NULL
    };

    const VkRayTracingShaderGroupCreateInfoKHR shaderGroups[] = {rg, mg, msg, hg};

    VkResult r;

    VkRayTracingPipelineCreateInfoKHR pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
        .maxRecursionDepth = 1, 
        .layout     = pipelineLayouts[plInfo->pipelineLayoutId],
        .libraries  = {VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR},
        .groupCount = TANTO_ARRAY_SIZE(shaderGroups),
        .stageCount = TANTO_ARRAY_SIZE(shaderStages),
        .pGroups    = shaderGroups,
        .pStages    = shaderStages
    };

    r = vkCreateRayTracingPipelinesKHR(device, NULL, 1, &pipelineInfo, NULL, &pipelines[plInfo->id]);
    assert( VK_SUCCESS == r );

    vkDestroyShaderModule(device, raygenSM, NULL);
    vkDestroyShaderModule(device, chitSM, NULL);
    vkDestroyShaderModule(device, missSM, NULL);
    vkDestroyShaderModule(device, missShadowSM, NULL);
}

static void createPipelineRasterization(const Tanto_R_PipelineInfo* plInfo)
{
    VkShaderModule vertModule;
    VkShaderModule fragModule;

    initShaderModule(plInfo->rasterInfo.vertShader, &vertModule);
    initShaderModule(plInfo->rasterInfo.fragShader, &fragModule);

    const VkSpecializationInfo shaderSpecialInfo = {
        // TODO
    };

    const VkPipelineShaderStageCreateInfo shaderStages[2] = {
        [0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        [0].stage = VK_SHADER_STAGE_VERTEX_BIT,
        [0].module = vertModule,
        [0].pName = "main",
        [0].pSpecializationInfo = NULL,
        [1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        [1].stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        [1].module = fragModule,
        [1].pName = "main",
        [1].pSpecializationInfo = NULL,
    }; // vert and frag

    const VkVertexInputBindingDescription bindingDescriptionPos = {
        .binding = 0,
        .stride  = sizeof(Vec3), 
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    const VkVertexInputBindingDescription bindingDescriptionColor = {
        .binding = 1,
        .stride  = sizeof(Vec3), 
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    const VkVertexInputBindingDescription bindingDescriptionNormal = {
        .binding = 2,
        .stride  = sizeof(Vec3), 
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    const VkVertexInputBindingDescription bindingDescriptionUvw = {
        .binding = 3,
        .stride  = sizeof(Vec3), 
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    const VkVertexInputAttributeDescription positionAttributeDescription = {
        .binding = 0,
        .location = 0, 
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = sizeof(Vec3) * 0
    };

    const VkVertexInputAttributeDescription colorAttributeDescription = {
        .binding = 1,
        .location = 1, 
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = sizeof(Vec3) * 0,
    };

    const VkVertexInputAttributeDescription normalAttributeDescription = {
        .binding = 2,
        .location = 2, 
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = sizeof(Vec3) * 0,
    };

    const VkVertexInputAttributeDescription uvwAttributeDescription = {
        .binding = 3,
        .location = 3, 
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = sizeof(Vec3) * 0,
    };

    VkVertexInputBindingDescription vBindDescs[4] = {
        bindingDescriptionPos, bindingDescriptionColor, bindingDescriptionNormal, bindingDescriptionUvw
    };

    VkVertexInputAttributeDescription vAttrDescs[4] = {
        positionAttributeDescription, colorAttributeDescription, normalAttributeDescription, uvwAttributeDescription
    };

    const VkPipelineVertexInputStateCreateInfo vertexInput = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = TANTO_ARRAY_SIZE(vBindDescs),
        .pVertexBindingDescriptions = vBindDescs,
        .vertexAttributeDescriptionCount = TANTO_ARRAY_SIZE(vAttrDescs),
        .pVertexAttributeDescriptions = vAttrDescs 
    };

    const VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE // applies only to index calls
    };

    const VkViewport viewport = {
        .height = TANTO_WINDOW_HEIGHT,
        .width = TANTO_WINDOW_WIDTH,
        .x = 0,
        .y = 0,
        .minDepth = 0.0,
        .maxDepth = 1.0
    };

    const VkRect2D scissor = {
        .extent = {TANTO_WINDOW_WIDTH, TANTO_WINDOW_HEIGHT},
        .offset = {0, 0}
    };

    const VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .scissorCount = 1,
        .pScissors = &scissor,
        .viewportCount = 1,
        .pViewports = &viewport,
    };

    const VkPipelineRasterizationStateCreateInfo rasterizationState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE, // dunno
        .rasterizerDiscardEnable = VK_FALSE, // actually discards everything
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0
    };

    const VkPipelineMultisampleStateCreateInfo multisampleState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .sampleShadingEnable = VK_FALSE,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
        // TODO: alot more settings here. more to look into
    };

    const VkPipelineColorBlendAttachmentState attachmentState = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT, /* need this to actually
                                                                    write anything to the
                                                                    framebuffer */
        .blendEnable = VK_FALSE, // no blending for now
        .srcColorBlendFactor = 0,
        .dstColorBlendFactor = 0,
        .colorBlendOp = 0,
        .srcAlphaBlendFactor = 0,
        .dstAlphaBlendFactor = 0,
        .alphaBlendOp = 0,
    };

    const VkPipelineColorBlendStateCreateInfo colorBlendState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE, // only for integer framebuffer formats
        .logicOp = 0,
        .attachmentCount = 1,
        .pAttachments = &attachmentState /* must have independentBlending device   
            feature enabled for these to be different. each entry would correspond 
            to the blending for a different framebuffer. */
    };

    const VkPipelineDepthStencilStateCreateInfo depthStencilState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE, // allows you to only keep fragments within the depth bounds
        .stencilTestEnable = VK_FALSE,
    };

    const VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .basePipelineIndex = 0, // not used
        .basePipelineHandle = 0,
        .subpass = 0, // which subpass in the renderpass do we use this pipeline with
        .renderPass = offscreenRenderPass,
        .layout = pipelineLayouts[plInfo->pipelineLayoutId],
        .pDynamicState = NULL,
        .pColorBlendState = &colorBlendState,
        .pDepthStencilState = &depthStencilState,
        .pMultisampleState = &multisampleState,
        .pRasterizationState = &rasterizationState,
        .pViewportState = &viewportState,
        .pTessellationState = NULL, // may be able to do splines with this
        .flags = 0,
        .stageCount = TANTO_ARRAY_SIZE(shaderStages),
        .pStages = shaderStages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
    };

    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipelines[plInfo->id]);

    vkDestroyShaderModule(device, vertModule, NULL);
    vkDestroyShaderModule(device, fragModule, NULL);
}

static void createPipelinePostProcess(const Tanto_R_PipelineInfo* plInfo)
{
    VkShaderModule vertModule;
    VkShaderModule fragModule;

    initShaderModule(TANTO_SPVDIR"/post-vert.spv", &vertModule);
    initShaderModule(plInfo->rasterInfo.fragShader, &fragModule);

    const VkPipelineShaderStageCreateInfo shaderStages[2] = {
        [0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        [0].stage = VK_SHADER_STAGE_VERTEX_BIT,
        [0].module = vertModule,
        [0].pName = "main",
        [0].pSpecializationInfo = NULL,
        [1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        [1].stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        [1].module = fragModule,
        [1].pName = "main",
        [1].pSpecializationInfo = NULL,
    }; // vert and frag

    const VkPipelineVertexInputStateCreateInfo vertexInput = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = NULL,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = NULL
    };

    const VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE // applies only to index calls
    };

    const VkViewport viewport = {
        .height = TANTO_WINDOW_HEIGHT,
        .width = TANTO_WINDOW_WIDTH,
        .x = 0,
        .y = 0,
        .minDepth = 0.0,
        .maxDepth = 1.0
    };

    const VkRect2D scissor = {
        .extent = {TANTO_WINDOW_WIDTH, TANTO_WINDOW_HEIGHT},
        .offset = {0, 0}
    };

    const VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .scissorCount = 1,
        .pScissors = &scissor,
        .viewportCount = 1,
        .pViewports = &viewport,
    };

    const VkPipelineRasterizationStateCreateInfo rasterizationState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE, // dunno
        .rasterizerDiscardEnable = VK_FALSE, // actually discards everything
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0
    };

    const VkPipelineMultisampleStateCreateInfo multisampleState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .sampleShadingEnable = VK_FALSE,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
        // TODO: alot more settings here. more to look into
    };

    const VkPipelineColorBlendAttachmentState attachmentState = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT, /* need this to actually
                                                                    write anything to the
                                                                    framebuffer */
        .blendEnable = VK_FALSE, // no blending for now
        .srcColorBlendFactor = 0,
        .dstColorBlendFactor = 0,
        .colorBlendOp = 0,
        .srcAlphaBlendFactor = 0,
        .dstAlphaBlendFactor = 0,
        .alphaBlendOp = 0,
    };

    const VkPipelineColorBlendStateCreateInfo colorBlendState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE, // only for integer framebuffer formats
        .logicOp = 0,
        .attachmentCount = 1,
        .pAttachments = &attachmentState /* must have independentBlending device   
            feature enabled for these to be different. each entry would correspond 
            to the blending for a different framebuffer. */
    };

    const VkPipelineDepthStencilStateCreateInfo depthStencilState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE, // allows you to only keep fragments within the depth bounds
        .stencilTestEnable = VK_FALSE,
    };

    const VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .basePipelineIndex = 0, // not used
        .basePipelineHandle = 0,
        .subpass = 0, // which subpass in the renderpass do we use this pipeline with
        .renderPass = swapchainRenderPass,
        .layout = pipelineLayouts[plInfo->pipelineLayoutId],
        .pDynamicState = NULL,
        .pColorBlendState = &colorBlendState,
        .pDepthStencilState = &depthStencilState,
        .pMultisampleState = &multisampleState,
        .pRasterizationState = &rasterizationState,
        .pViewportState = &viewportState,
        .pTessellationState = NULL, // may be able to do splines with this
        .flags = 0,
        .stageCount = TANTO_ARRAY_SIZE(shaderStages),
        .pStages = shaderStages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
    };

    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipelines[plInfo->id]);

    vkDestroyShaderModule(device, vertModule, NULL);
    vkDestroyShaderModule(device, fragModule, NULL);
}

void tanto_r_InitDescriptorSets(const Tanto_R_DescriptorSet* const sets, const int count)
{
    // counters for different descriptors
    int dcUbo = 0, dcAs = 0, dcSi = 0, dcSb = 0, dcCis = 0;
    for (int i = 0; i < count; i++) 
    {
        const Tanto_R_DescriptorSet set = sets[i];
        assert(set.bindingCount > 0);
        assert(set.bindingCount <= TANTO_MAX_BINDINGS);
        assert(descriptorSets[set.id] == VK_NULL_HANDLE);
        assert(set.id == i); // we ensure that the set ids increase from with i from 0. No gaps.
        VkDescriptorSetLayoutBinding bindings[set.bindingCount];
        for (int b = 0; b < set.bindingCount; b++) 
        {
            const uint32_t dCount = set.bindings[b].descriptorCount;
            bindings[b].binding = b;
            bindings[b].descriptorCount = dCount;
            bindings[b].descriptorType  = set.bindings[b].type;
            bindings[b].stageFlags      = set.bindings[b].stageFlags;
            bindings[b].pImmutableSamplers = NULL;
            switch (set.bindings[b].type) 
            {
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:             dcUbo += dCount; break;
                case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: dcAs  += dCount; break;
                case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:              dcSi  += dCount; break;
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:             dcSb  += dCount; break;
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:     dcCis += dCount; break;
                default: assert(false);
            }
        }
        const VkDescriptorSetLayoutCreateInfo layoutInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = set.bindingCount,
            .pBindings = bindings
        };
        V_ASSERT(vkCreateDescriptorSetLayout(device, &layoutInfo, NULL, &descriptorSetLayouts[set.id]));
    }

    const VkDescriptorPoolSize poolSizes[] = {{
            .descriptorCount = dcUbo,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
        },{
            .descriptorCount = dcAs,
            .type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        },{
            .descriptorCount = dcSi,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        },{
            .descriptorCount = dcSb,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        },{
            .descriptorCount = dcCis,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
    }};

    const VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = count,
        .poolSizeCount = TANTO_ARRAY_SIZE(poolSizes),
        .pPoolSizes = poolSizes, 
    };

    V_ASSERT(vkCreateDescriptorPool(device, &poolInfo, NULL, &descriptorPool));

    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = count,
        .pSetLayouts = descriptorSetLayouts,
    };

    V_ASSERT(vkAllocateDescriptorSets(device, &allocInfo, descriptorSets));
}

void tanto_r_InitPipelineLayouts(const Tanto_R_PipelineLayout *const layouts, const int count)
{
    assert(count > 0 && count < TANTO_MAX_PIPELINES);
    for (int i = 0; i < count; i++) 
    {
        const Tanto_R_PipelineLayout layout = layouts[i];
        assert(layout.id == i);
        assert(layout.pushConstantCount < TANTO_MAX_PUSH_CONSTANTS);

        const int dsCount = layout.descriptorSetCount;
        assert(dsCount > 0 && dsCount < TANTO_MAX_DESCRIPTOR_SETS);

        VkDescriptorSetLayout descSetLayouts[dsCount];

        for (int j = 0; j < count; j++) 
        {
            descSetLayouts[j] = descriptorSetLayouts[layout.descriptorSetIds[j]];
        }

        VkPipelineLayoutCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .setLayoutCount = dsCount,
            .pSetLayouts = descSetLayouts,
            .pushConstantRangeCount = layout.pushConstantCount,
            .pPushConstantRanges = layout.pushConstantsRanges
        };

        V_ASSERT(vkCreatePipelineLayout(device, &info, NULL, &pipelineLayouts[layout.id]));
    }
}

void tanto_r_InitPipelines(const Tanto_R_PipelineInfo *const pipelineInfos, const int count)
{
    for (int i = 0; i < count; i++) 
    {
        const Tanto_R_PipelineInfo plInfo = pipelineInfos[i];
        switch (plInfo.type) 
        {
            case TANTO_R_PIPELINE_RASTER_TYPE: createPipelineRasterization(&plInfo); break;
            case TANTO_R_PIPELINE_RAYTRACE_TYPE: createPipelineRayTrace(&plInfo); break;
            case TANTO_R_PIPELINE_POSTPROC_TYPE: createPipelinePostProcess(&plInfo); break;
        }
    }
}

void tanto_r_CleanUpPipelines()
{
    for (int i = 0; i < TANTO_MAX_PIPELINES; i++) 
    {
        if (pipelineLayouts[i])
            vkDestroyPipelineLayout(device, pipelineLayouts[i], NULL);
        pipelineLayouts[i] = 0;
    }
    vkDestroyDescriptorPool(device, descriptorPool, NULL);
    descriptorPool = 0;
    for (int i = 0; i < TANTO_MAX_DESCRIPTOR_SETS; i++) 
    {
        if (descriptorSetLayouts[i])
            vkDestroyDescriptorSetLayout(device, descriptorSetLayouts[i], NULL);
        descriptorSetLayouts[i] = 0;
        descriptorSets[i] = 0;
    }
    for (int i = 0; i < TANTO_MAX_PIPELINES; i++) 
    {
        if (pipelines[i])
            vkDestroyPipeline(device, pipelines[i], NULL);
        pipelines[i] = 0;
    }
}
