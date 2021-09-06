#include "VulkanRaytracingSample.h"
#include "VulkanglBSP.h"

class VulkanExample: public VulkanRaytracingSample {
public:
  AccelerationStructure bottomLevelAS;
  AccelerationStructure topLevelAS;
  VkPhysicalDeviceRayQueryFeaturesKHR enabledRayQueryFeatures { };
  glm::vec3 lightPos = glm::vec3();
  std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups { };
  struct ShaderBindingTables {
    ShaderBindingTable raygen;
    ShaderBindingTable miss;
    ShaderBindingTable hit;
  } shaderBindingTables;

  struct UniformData {
    glm::mat4 viewInverse;
    glm::mat4 projInverse;
    glm::vec4 lightPos;
    int32_t vertexSize;
  } uniformData;
//
//  struct UniformData {
//    glm::mat4 projection;
//    glm::mat4 view;
//    glm::mat4 model;
//    glm::vec3 lightPos;
//  } uniformData;

  vks::Buffer ubo;

  VkPipeline pipeline;
  VkPipelineLayout pipelineLayout;
  VkDescriptorSet descriptorSet;
  VkDescriptorSetLayout descriptorSetLayout;

  vkglBSP::Model scene;

  // This sample is derived from an extended base class that saves most of the ray tracing setup boiler plate
  VulkanExample() :
      VulkanRaytracingSample() {
    title = "Ray traced BSP";
    timerSpeed *= 0.25f;
    camera.type = Camera::CameraType::firstperson;
    camera.setPerspective(60.0f, (float) width / (float) height, 0.1f, 512.0f);
    camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
    camera.setTranslation(glm::vec3(0.0f, 3.0f, -20.0f));
//    rayQueryOnly = true;
    enableExtensions();
//    enabledDeviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
  }

  ~VulkanExample() {
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    deleteStorageImage();
    deleteAccelerationStructure(bottomLevelAS);
    deleteAccelerationStructure(topLevelAS);
    shaderBindingTables.raygen.destroy();
    shaderBindingTables.miss.destroy();
    shaderBindingTables.hit.destroy();
    ubo.destroy();
  }

//
//  /*
//    Create the bottom level acceleration structure contains the scene's actual geometry (vertices, triangles)
//  */
//  void createBottomLevelAccelerationStructure()
//  {
//    vkglBSP::memoryPropertyFlags = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
//    const uint32_t glTFLoadingFlags = vkglBSP::FileLoadingFlags::None;
//    scene.loadFromFile(getAssetPath() + "models/vulkanscene_shadow.gltf", vulkanDevice, queue, glTFLoadingFlags);
//
//    VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
//    VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
//    std::cout << "Vertex buffer = " << scene.loadmodel->vertex_buffer << std::endl;
//    std::cout << "Index buffer = " << scene.loadmodel->index_buffer << std::endl;
//
//    vertexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(scene.loadmodel->vertex_buffer);
//    indexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(scene.loadmodel->index_buffer);
//
//    uint32_t numTriangles = static_cast<uint32_t>(scene.loadmodel->edges.size() / 3);
//    uint32_t maxVertex = scene.loadmodel->vertexes.size();
//
//    // Build
//    VkAccelerationStructureGeometryKHR accelerationStructureGeometry = vks::initializers::accelerationStructureGeometryKHR();
//    accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
//    accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
//    accelerationStructureGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
//    accelerationStructureGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
//    accelerationStructureGeometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
//    accelerationStructureGeometry.geometry.triangles.maxVertex = maxVertex;
//    accelerationStructureGeometry.geometry.triangles.vertexStride = sizeof(vkglBSP::MVertex);
//    accelerationStructureGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
//    accelerationStructureGeometry.geometry.triangles.indexData = indexBufferDeviceAddress;
//    accelerationStructureGeometry.geometry.triangles.transformData.deviceAddress = 0;
//    accelerationStructureGeometry.geometry.triangles.transformData.hostAddress = nullptr;
//
//    // Get size info
//    VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
//    accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
//    accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
//    accelerationStructureBuildGeometryInfo.geometryCount = 1;
//    accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
//
//    VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo = vks::initializers::accelerationStructureBuildSizesInfoKHR();
//    vkGetAccelerationStructureBuildSizesKHR(
//      device,
//      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
//      &accelerationStructureBuildGeometryInfo,
//      &numTriangles,
//      &accelerationStructureBuildSizesInfo);
//
//    createAccelerationStructure(bottomLevelAS, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, accelerationStructureBuildSizesInfo);
//
//    // Create a small scratch buffer used during build of the bottom level acceleration structure
//    ScratchBuffer scratchBuffer = createScratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize);
//
//    VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
//    accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
//    accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
//    accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
//    accelerationBuildGeometryInfo.dstAccelerationStructure = bottomLevelAS.handle;
//    accelerationBuildGeometryInfo.geometryCount = 1;
//    accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
//    accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;
//
//    VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
//    accelerationStructureBuildRangeInfo.primitiveCount = numTriangles;
//    accelerationStructureBuildRangeInfo.primitiveOffset = 0;
//    accelerationStructureBuildRangeInfo.firstVertex = 0;
//    accelerationStructureBuildRangeInfo.transformOffset = 0;
//    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };
//
//    // Build the acceleration structure on the device via a one-time command buffer submission
//    // Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device builds
//    VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
//    vkCmdBuildAccelerationStructuresKHR(
//      commandBuffer,
//      1,
//      &accelerationBuildGeometryInfo,
//      accelerationBuildStructureRangeInfos.data());
//    vulkanDevice->flushCommandBuffer(commandBuffer, queue);
//
//    deleteScratchBuffer(scratchBuffer);
//  }
//
//  /*
//    The top level acceleration structure contains the scene's object instances
//  */
//  void createTopLevelAccelerationStructure()
//  {
//    VkTransformMatrixKHR transformMatrix = {
//      1.0f, 0.0f, 0.0f, 0.0f,
//      0.0f, 1.0f, 0.0f, 0.0f,
//      0.0f, 0.0f, 1.0f, 0.0f };
//
//    VkAccelerationStructureInstanceKHR instance{};
//    instance.transform = transformMatrix;
//    instance.instanceCustomIndex = 0;
//    instance.mask = 0xFF;
//    instance.instanceShaderBindingTableRecordOffset = 0;
//    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
//    instance.accelerationStructureReference = bottomLevelAS.deviceAddress;
//
//    // Buffer for instance data
//    vks::Buffer instancesBuffer;
//    VK_CHECK_RESULT(vulkanDevice->createBuffer(
//      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
//      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
//      &instancesBuffer,
//      sizeof(VkAccelerationStructureInstanceKHR),
//      &instance));
//
//    VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
//    instanceDataDeviceAddress.deviceAddress = getBufferDeviceAddress(instancesBuffer.buffer);
//
//    VkAccelerationStructureGeometryKHR accelerationStructureGeometry = vks::initializers::accelerationStructureGeometryKHR();
//    accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
//    accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
//    accelerationStructureGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
//    accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
//    accelerationStructureGeometry.geometry.instances.data = instanceDataDeviceAddress;
//
//    // Get size info
//    VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
//    accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
//    accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
//    accelerationStructureBuildGeometryInfo.geometryCount = 1;
//    accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
//
//    uint32_t primitive_count = 1;
//
//    VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo = vks::initializers::accelerationStructureBuildSizesInfoKHR();
//    vkGetAccelerationStructureBuildSizesKHR(
//      device,
//      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
//      &accelerationStructureBuildGeometryInfo,
//      &primitive_count,
//      &accelerationStructureBuildSizesInfo);
//
//    createAccelerationStructure(topLevelAS, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, accelerationStructureBuildSizesInfo);
//
//    // Create a small scratch buffer used during build of the top level acceleration structure
//    ScratchBuffer scratchBuffer = createScratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize);
//
//    VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo = vks::initializers::accelerationStructureBuildGeometryInfoKHR();
//    accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
//    accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
//    accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
//    accelerationBuildGeometryInfo.dstAccelerationStructure = topLevelAS.handle;
//    accelerationBuildGeometryInfo.geometryCount = 1;
//    accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
//    accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;
//
//    VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
//    accelerationStructureBuildRangeInfo.primitiveCount = 1;
//    accelerationStructureBuildRangeInfo.primitiveOffset = 0;
//    accelerationStructureBuildRangeInfo.firstVertex = 0;
//    accelerationStructureBuildRangeInfo.transformOffset = 0;
//    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };
//
//    // Build the acceleration structure on the device via a one-time command buffer submission
//    // Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device builds
//    VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
//    vkCmdBuildAccelerationStructuresKHR(
//      commandBuffer,
//      1,
//      &accelerationBuildGeometryInfo,
//      accelerationBuildStructureRangeInfos.data());
//    vulkanDevice->flushCommandBuffer(commandBuffer, queue);
//
//    deleteScratchBuffer(scratchBuffer);
//    instancesBuffer.destroy();
//  }
//
//  void buildCommandBuffers()
//  {
//    VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();
//
//    VkClearValue clearValues[2];
//    VkViewport viewport;
//    VkRect2D scissor;
//
//    for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
//    {
//      VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));
//
//      /*
//        Note: Explicit synchronization is not required between the render pass, as this is done implicit via sub pass dependencies
//      */
//
//      /*
//        Second pass: Scene rendering with applied shadow map
//      */
//
//      clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 1.0f } };;
//      clearValues[1].depthStencil = { 1.0f, 0 };
//
//      VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
//      renderPassBeginInfo.renderPass = renderPass;
//      renderPassBeginInfo.framebuffer = frameBuffers[i];
//      renderPassBeginInfo.renderArea.extent.width = width;
//      renderPassBeginInfo.renderArea.extent.height = height;
//      renderPassBeginInfo.clearValueCount = 2;
//      renderPassBeginInfo.pClearValues = clearValues;
//
//      vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
//
//      viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
//      vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
//
//      scissor = vks::initializers::rect2D(width, height, 0, 0);
//      vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);
//
//      // 3D scene
//      vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
//      vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
//      scene.draw(drawCmdBuffers[i]);
//
//      VulkanExampleBase::drawUI(drawCmdBuffers[i]);
//
//      vkCmdEndRenderPass(drawCmdBuffers[i]);
//
//      VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
//    }
//  }
//
//  void loadAssets()
//  {
//  }
//
//  void setupDescriptorPool()
//  {
//    std::vector<VkDescriptorPoolSize> poolSizes = {
//      vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3),
//      vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3),
//      vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 3)
//    };
//    VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 3);
//    VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
//  }
//
//  void setupDescriptorSetLayout()
//  {
//    // Shared pipeline layout for all pipelines used in this sample
//    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
//      // Binding 0 : Vertex shader uniform buffer
//      vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
//      // Binding 1 : Fragment shader image sampler (shadow map)
//      vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
//      // Binding 2: Acceleration structure
//      vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
//    };
//    VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
//    VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));
//    VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
//    VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
//  }
//
//  void setupDescriptorSets()
//  {
//    std::vector<VkWriteDescriptorSet> writeDescriptorSets;
//
//    // Debug display
//    VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
//
//    vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);
//
//    // Scene rendering with shadow map applied
//    VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
//    writeDescriptorSets = {
//      // Binding 0 : Vertex shader uniform buffer
//      vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &ubo.descriptor)
//    };
//
//    VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo = vks::initializers::writeDescriptorSetAccelerationStructureKHR();
//    descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
//    descriptorAccelerationStructureInfo.pAccelerationStructures = &topLevelAS.handle;
//
//    VkWriteDescriptorSet accelerationStructureWrite{};
//    accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
//    // The specialized acceleration structure descriptor has to be chained
//    accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
//    accelerationStructureWrite.dstSet = descriptorSet;
//    accelerationStructureWrite.dstBinding = 2;
//    accelerationStructureWrite.descriptorCount = 1;
//    accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
//
//    writeDescriptorSets.push_back(accelerationStructureWrite);
//    vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);
//  }
//
//  void preparePipelines()
//  {
//    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
//    VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE, 0);
//    VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
//    VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
//    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
//    VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
//    VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
//    std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
//    VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size(), 0);
//    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
//
//    VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass, 0);
//    pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
//    pipelineCI.pRasterizationState = &rasterizationStateCI;
//    pipelineCI.pColorBlendState = &colorBlendStateCI;
//    pipelineCI.pMultisampleState = &multisampleStateCI;
//    pipelineCI.pViewportState = &viewportStateCI;
//    pipelineCI.pDepthStencilState = &depthStencilStateCI;
//    pipelineCI.pDynamicState = &dynamicStateCI;
//    pipelineCI.stageCount = shaderStages.size();
//    pipelineCI.pStages = shaderStages.data();
//
//    // Scene rendering with ray traced shadows applied
//    pipelineCI.pVertexInputState = vkglBSP::Vertex::getPipelineVertexInputState({ vkglBSP::VertexComponent::Position, vkglBSP::VertexComponent::UV, vkglBSP::VertexComponent::Color, vkglBSP::VertexComponent::Normal });
//    rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
//    shaderStages[0] = loadShader(getShadersPath() + "rayquery/scene.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
//    shaderStages[1] = loadShader(getShadersPath() + "rayquery/scene.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
//    VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
//  }
//
//
//  // Prepare and initialize uniform buffer containing shader uniforms
//  void prepareUniformBuffers()
//  {
//    // Scene vertex shader uniform buffer block
//    VK_CHECK_RESULT(vulkanDevice->createBuffer(
//      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
//      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
//      &ubo,
//      sizeof(UniformData)));
//
//    // Map persistent
//    VK_CHECK_RESULT(ubo.map());
//
//    updateLight();
//    updateUniformBuffers();
//  }
//
//  void updateLight()
//  {
//    // Animate the light source
//    lightPos.x = cos(glm::radians(timer * 360.0f)) * 40.0f;
//    lightPos.y = -50.0f + sin(glm::radians(timer * 360.0f)) * 20.0f;
//    lightPos.z = 25.0f + sin(glm::radians(timer * 360.0f)) * 5.0f;
//  }
//
//  void getEnabledFeatures()
//  {
//    // Enable features required for ray tracing using feature chaining via pNext
//    enabledBufferDeviceAddresFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
//    enabledBufferDeviceAddresFeatures.bufferDeviceAddress = VK_TRUE;
//
//    enabledRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
//    enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
//    enabledRayTracingPipelineFeatures.pNext = &enabledBufferDeviceAddresFeatures;
//
//    enabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
//    enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
//    enabledAccelerationStructureFeatures.pNext = &enabledRayTracingPipelineFeatures;
//
//    enabledRayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
//    enabledRayQueryFeatures.rayQuery = VK_TRUE;
//    enabledRayQueryFeatures.pNext = &enabledAccelerationStructureFeatures;
//
//    deviceCreatepNextChain = &enabledRayQueryFeatures;
//  }
//
//  void draw()
//  {
//    VulkanExampleBase::prepareFrame();
//
//    // Command buffer to be submitted to the queue
//    submitInfo.commandBufferCount = 1;
//    submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
//
//    // Submit to queue
//    VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
//
//    VulkanExampleBase::submitFrame();
//  }
//
//  void prepare()
//  {
//    VulkanRaytracingSample::prepare();
//    loadAssets();
//    prepareUniformBuffers();
//    setupDescriptorSetLayout();
//    preparePipelines();
//    createBottomLevelAccelerationStructure();
//    createTopLevelAccelerationStructure();
//    setupDescriptorPool();
//    setupDescriptorSets();
//    buildCommandBuffers();
//    prepared = true;
//  }
//
//  void updateUniformBuffers()
//  {
//    uniformData.projection = camera.matrices.perspective;
//    uniformData.view = camera.matrices.view;
//    uniformData.model = glm::mat4(1.0f);
//    uniformData.lightPos = lightPos;
//    memcpy(ubo.mapped, &uniformData, sizeof(UniformData));
//  }

/// opld stuff

  /*
   Create the bottom level acceleration structure contains the scene's actual geometry (vertices, triangles)
   */
  void createBottomLevelAccelerationStructure() {
    // Instead of a simple triangle, we'll be loading a more complex scene for this example
    // The shaders are accessing the vertex and index buffers of the scene, so the proper usage flag has to be set on the vertex and index buffers for the scene
    vkglBSP::memoryPropertyFlags =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
            | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const uint32_t glTFLoadingFlags = vkglBSP::FileLoadingFlags::None;
    scene.loadFromFile(getAssetPath() + "models/vulkanscene_shadow.gltf",
        vulkanDevice, queue, glTFLoadingFlags);

    std::cout << "Loaded from file done " << std::endl;
    VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress { };
    VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress { };
    std::cout << "Vertex buffer = " << scene.loadmodel->vertex_buffer
        << std::endl;
    vertexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(
        scene.loadmodel->vertex_buffer);
    indexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(
        scene.loadmodel->index_buffer);

    uint32_t numTriangles = static_cast<uint32_t>(scene.loadmodel->edges.size()
        / 3);
    uint32_t maxVertex = scene.loadmodel->vertexes.size();

    // Build
    VkAccelerationStructureGeometryKHR accelerationStructureGeometry =
        vks::initializers::accelerationStructureGeometryKHR();
    accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    accelerationStructureGeometry.geometry.triangles.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    accelerationStructureGeometry.geometry.triangles.vertexFormat =
        VK_FORMAT_R32G32B32A32_SFLOAT;
    accelerationStructureGeometry.geometry.triangles.vertexData =
        vertexBufferDeviceAddress;
    accelerationStructureGeometry.geometry.triangles.maxVertex = maxVertex;
    accelerationStructureGeometry.geometry.triangles.vertexStride =
        sizeof(vkglBSP::MVertex);
    accelerationStructureGeometry.geometry.triangles.indexType =
        VK_INDEX_TYPE_UINT32;
    accelerationStructureGeometry.geometry.triangles.indexData =
        indexBufferDeviceAddress;
    accelerationStructureGeometry.geometry.triangles.transformData.deviceAddress =
        0;
    accelerationStructureGeometry.geometry.triangles.transformData.hostAddress =
        nullptr;

    // Get size info
    VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo =
        vks::initializers::accelerationStructureBuildGeometryInfoKHR();
    accelerationStructureBuildGeometryInfo.type =
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    accelerationStructureBuildGeometryInfo.flags =
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    accelerationStructureBuildGeometryInfo.geometryCount = 1;
    accelerationStructureBuildGeometryInfo.pGeometries =
        &accelerationStructureGeometry;

    VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo =
        vks::initializers::accelerationStructureBuildSizesInfoKHR();
    vkGetAccelerationStructureBuildSizesKHR(device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &accelerationStructureBuildGeometryInfo, &numTriangles,
        &accelerationStructureBuildSizesInfo);

    std::cout << "1" << std::endl;

    createAccelerationStructure(bottomLevelAS,
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        accelerationStructureBuildSizesInfo);
    std::cout << "2" << std::endl;

    // Create a small scratch buffer used during build of the bottom level acceleration structure
    ScratchBuffer scratchBuffer = createScratchBuffer(
        accelerationStructureBuildSizesInfo.buildScratchSize);

    VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo =
        vks::initializers::accelerationStructureBuildGeometryInfoKHR();
    accelerationBuildGeometryInfo.type =
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    accelerationBuildGeometryInfo.flags =
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    accelerationBuildGeometryInfo.mode =
        VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    accelerationBuildGeometryInfo.dstAccelerationStructure =
        bottomLevelAS.handle;
    accelerationBuildGeometryInfo.geometryCount = 1;
    accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
    accelerationBuildGeometryInfo.scratchData.deviceAddress =
        scratchBuffer.deviceAddress;

    VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo { };
    accelerationStructureBuildRangeInfo.primitiveCount = numTriangles;
    accelerationStructureBuildRangeInfo.primitiveOffset = 0;
    accelerationStructureBuildRangeInfo.firstVertex = 0;
    accelerationStructureBuildRangeInfo.transformOffset = 0;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos =
        { &accelerationStructureBuildRangeInfo };

    std::cout << "3" << std::endl;
    // Build the acceleration structure on the device via a one-time command buffer submission
    // Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device builds
    VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(
        VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1,
        &accelerationBuildGeometryInfo,
        accelerationBuildStructureRangeInfos.data());
    vulkanDevice->flushCommandBuffer(commandBuffer, queue);

    std::cout << "4" << std::endl;
    deleteScratchBuffer(scratchBuffer);
    std::cout << "Done with BLAS" << std::endl;
  }
//
//	/*
//		The top level acceleration structure contains the scene's object instances
//	*/
  void createTopLevelAccelerationStructure() {
    VkTransformMatrixKHR transformMatrix = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };

    VkAccelerationStructureInstanceKHR instance { };
    instance.transform = transformMatrix;
    instance.instanceCustomIndex = 0;
    instance.mask = 0xFF;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = bottomLevelAS.deviceAddress;

    // Buffer for instance data
    vks::Buffer instancesBuffer;
    VK_CHECK_RESULT(
        vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &instancesBuffer,
            sizeof(VkAccelerationStructureInstanceKHR), &instance));

    VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress { };
    instanceDataDeviceAddress.deviceAddress = getBufferDeviceAddress(
        instancesBuffer.buffer);

    VkAccelerationStructureGeometryKHR accelerationStructureGeometry =
        vks::initializers::accelerationStructureGeometryKHR();
    accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    accelerationStructureGeometry.geometry.instances.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
    accelerationStructureGeometry.geometry.instances.data =
        instanceDataDeviceAddress;

    // Get size info
    VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo =
        vks::initializers::accelerationStructureBuildGeometryInfoKHR();
    accelerationStructureBuildGeometryInfo.type =
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    accelerationStructureBuildGeometryInfo.flags =
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    accelerationStructureBuildGeometryInfo.geometryCount = 1;
    accelerationStructureBuildGeometryInfo.pGeometries =
        &accelerationStructureGeometry;

    uint32_t primitive_count = 1;

    VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo =
        vks::initializers::accelerationStructureBuildSizesInfoKHR();
    vkGetAccelerationStructureBuildSizesKHR(device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &accelerationStructureBuildGeometryInfo, &primitive_count,
        &accelerationStructureBuildSizesInfo);

    // @todo: as return value?
    createAccelerationStructure(topLevelAS,
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        accelerationStructureBuildSizesInfo);

    // Create a small scratch buffer used during build of the top level acceleration structure
    ScratchBuffer scratchBuffer = createScratchBuffer(
        accelerationStructureBuildSizesInfo.buildScratchSize);

    VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo =
        vks::initializers::accelerationStructureBuildGeometryInfoKHR();
    accelerationBuildGeometryInfo.type =
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    accelerationBuildGeometryInfo.flags =
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    accelerationBuildGeometryInfo.mode =
        VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    accelerationBuildGeometryInfo.dstAccelerationStructure = topLevelAS.handle;
    accelerationBuildGeometryInfo.geometryCount = 1;
    accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
    accelerationBuildGeometryInfo.scratchData.deviceAddress =
        scratchBuffer.deviceAddress;

    VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo { };
    accelerationStructureBuildRangeInfo.primitiveCount = 1;
    accelerationStructureBuildRangeInfo.primitiveOffset = 0;
    accelerationStructureBuildRangeInfo.firstVertex = 0;
    accelerationStructureBuildRangeInfo.transformOffset = 0;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos =
        { &accelerationStructureBuildRangeInfo };

    // Build the acceleration structure on the device via a one-time command buffer submission
    // Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device builds
    VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(
        VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1,
        &accelerationBuildGeometryInfo,
        accelerationBuildStructureRangeInfos.data());
    vulkanDevice->flushCommandBuffer(commandBuffer, queue);

    deleteScratchBuffer(scratchBuffer);
    instancesBuffer.destroy();
  }
//
//
//	/*
//		Create the Shader Binding Tables that binds the programs and top-level acceleration structure
//
//		SBT Layout used in this sample:
//
//			/-----------\
//			| raygen    |
//			|-----------|
//			| miss      |
//			|-----------|
//			| hit       |
//			\-----------/
//
//	*/
  void createShaderBindingTables() {
    const uint32_t handleSize =
        rayTracingPipelineProperties.shaderGroupHandleSize;
    const uint32_t handleSizeAligned = vks::tools::alignedSize(
        rayTracingPipelineProperties.shaderGroupHandleSize,
        rayTracingPipelineProperties.shaderGroupHandleAlignment);
    const uint32_t groupCount = static_cast<uint32_t>(shaderGroups.size());
    const uint32_t sbtSize = groupCount * handleSizeAligned;

    std::vector<uint8_t> shaderHandleStorage(sbtSize);
    VK_CHECK_RESULT(
        vkGetRayTracingShaderGroupHandlesKHR(device, pipeline, 0, groupCount,
            sbtSize, shaderHandleStorage.data()));

    createShaderBindingTable(shaderBindingTables.raygen, 1);
    // We are using two miss shaders
    createShaderBindingTable(shaderBindingTables.miss, 2);
    createShaderBindingTable(shaderBindingTables.hit, 1);

    // Copy handles
    memcpy(shaderBindingTables.raygen.mapped, shaderHandleStorage.data(),
        handleSize);
    // We are using two miss shaders, so we need to get two handles for the miss shader binding table
    memcpy(shaderBindingTables.miss.mapped,
        shaderHandleStorage.data() + handleSizeAligned, handleSize * 2);
    memcpy(shaderBindingTables.hit.mapped,
        shaderHandleStorage.data() + handleSizeAligned * 3, handleSize);
  }
//
//	/*
//		Create the descriptor sets used for the ray tracing dispatch
//	*/
  void createDescriptorSets() {
    std::vector<VkDescriptorPoolSize> poolSizes = { {
        VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 }, {
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }, {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }, {
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 } };
    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo =
        vks::initializers::descriptorPoolCreateInfo(poolSizes, 1);
    VK_CHECK_RESULT(
        vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr,
            &descriptorPool));

    VkDescriptorSetAllocateInfo descriptorSetAllocateInfo =
        vks::initializers::descriptorSetAllocateInfo(descriptorPool,
            &descriptorSetLayout, 1);
    VK_CHECK_RESULT(
        vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo,
            &descriptorSet));

    VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo =
        vks::initializers::writeDescriptorSetAccelerationStructureKHR();
    descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
    descriptorAccelerationStructureInfo.pAccelerationStructures =
        &topLevelAS.handle;

    VkWriteDescriptorSet accelerationStructureWrite { };
    accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    // The specialized acceleration structure descriptor has to be chained
    accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
    accelerationStructureWrite.dstSet = descriptorSet;
    accelerationStructureWrite.dstBinding = 0;
    accelerationStructureWrite.descriptorCount = 1;
    accelerationStructureWrite.descriptorType =
        VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    VkDescriptorImageInfo storageImageDescriptor { VK_NULL_HANDLE,
        storageImage.view, VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorBufferInfo vertexBufferDescriptor {
        scene.loadmodel->vertex_buffer, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo indexBufferDescriptor {
        scene.loadmodel->index_buffer, 0, VK_WHOLE_SIZE };

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
    // Binding 0: Top level acceleration structure
        accelerationStructureWrite,
        // Binding 1: Ray tracing result image
        vks::initializers::writeDescriptorSet(descriptorSet,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor),
        // Binding 2: Uniform data
        vks::initializers::writeDescriptorSet(descriptorSet,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2, &ubo.descriptor),
        // Binding 3: Scene vertex buffer
        vks::initializers::writeDescriptorSet(descriptorSet,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3, &vertexBufferDescriptor),
        // Binding 4: Scene index buffer
        vks::initializers::writeDescriptorSet(descriptorSet,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4, &indexBufferDescriptor), };
    vkUpdateDescriptorSets(device,
        static_cast<uint32_t>(writeDescriptorSets.size()),
        writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
  }
//
//	/*
//		Create our ray tracing pipeline
//	*/
  void createRayTracingPipeline() {

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI =
        vks::initializers::pipelineInputAssemblyStateCreateInfo(
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, 0, VK_FALSE);

    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
    // Binding 0: Acceleration structure
        vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            VK_SHADER_STAGE_RAYGEN_BIT_KHR
                | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 0),
        // Binding 1: Storage image
        vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            1),
        // Binding 2: Uniform buffer
        vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                | VK_SHADER_STAGE_MISS_BIT_KHR, 2),
        // Binding 3: Vertex buffer
        vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 3),
        // Binding 4: Index buffer
        vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 4), };

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI =
        vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
    VK_CHECK_RESULT(
        vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr,
            &descriptorSetLayout));

    VkPipelineLayoutCreateInfo pPipelineLayoutCI =
        vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
    VK_CHECK_RESULT(
        vkCreatePipelineLayout(device, &pPipelineLayoutCI, nullptr,
            &pipelineLayout));

//		/*
//			Setup ray tracing shader groups
//		*/
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    // Ray generation group
    {
      shaderStages.push_back(
          loadShader(getShadersPath() + "raytracingshadows/raygen.rgen.spv",
              VK_SHADER_STAGE_RAYGEN_BIT_KHR));
      VkRayTracingShaderGroupCreateInfoKHR shaderGroup { };
      shaderGroup.sType =
          VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
      shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
      shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size())
          - 1;
      shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
      shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
      shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
      shaderGroups.push_back(shaderGroup);
    }

    // Miss group
    {
      shaderStages.push_back(
          loadShader(getShadersPath() + "raytracingshadows/miss.rmiss.spv",
              VK_SHADER_STAGE_MISS_BIT_KHR));
      VkRayTracingShaderGroupCreateInfoKHR shaderGroup { };
      shaderGroup.sType =
          VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
      shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
      shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size())
          - 1;
      shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
      shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
      shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
      shaderGroups.push_back(shaderGroup);
      // Second shader for shadows
      shaderStages.push_back(
          loadShader(getShadersPath() + "raytracingshadows/shadow.rmiss.spv",
              VK_SHADER_STAGE_MISS_BIT_KHR));
      shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size())
          - 1;
      shaderGroups.push_back(shaderGroup);
    }

    // Closest hit group
    {
      shaderStages.push_back(
          loadShader(
              getShadersPath() + "raytracingshadows/closesthit.rchit.spv",
              VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR));
      VkRayTracingShaderGroupCreateInfoKHR shaderGroup { };
      shaderGroup.sType =
          VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
      shaderGroup.type =
          VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
      shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
      shaderGroup.closestHitShader = static_cast<uint32_t>(shaderStages.size())
          - 1;
      shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
      shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
      shaderGroups.push_back(shaderGroup);
    }

    VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI =
        vks::initializers::rayTracingPipelineCreateInfoKHR();
    rayTracingPipelineCI.stageCount =
        static_cast<uint32_t>(shaderStages.size());
    rayTracingPipelineCI.pStages = shaderStages.data();
    rayTracingPipelineCI.groupCount =
        static_cast<uint32_t>(shaderGroups.size());
    rayTracingPipelineCI.pGroups = shaderGroups.data();
    rayTracingPipelineCI.maxPipelineRayRecursionDepth = 2;
    rayTracingPipelineCI.layout = pipelineLayout;

    VK_CHECK_RESULT(
        vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, nullptr, &pipeline));
  }
//
//	/*
//		Create the uniform buffer used to pass matrices to the ray tracing ray generation shader
//	*/
  void createUniformBuffer() {
    VK_CHECK_RESULT(
        vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ubo,
            sizeof(uniformData), &uniformData));
    VK_CHECK_RESULT(ubo.map());

    updateUniformBuffers();
  }

  /*
   If the window has been resized, we need to recreate the storage image and it's descriptor
   */
  void handleResize() {
    // Recreate image
    createStorageImage(swapChain.colorFormat, { width, height, 1 });
    // Update descriptor
    VkDescriptorImageInfo storageImageDescriptor { VK_NULL_HANDLE,
        storageImage.view, VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSet resultImageWrite =
        vks::initializers::writeDescriptorSet(descriptorSet,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor);
    vkUpdateDescriptorSets(device, 1, &resultImageWrite, 0, VK_NULL_HANDLE);
  }

//	/*
//		Command buffer generation
//	*/
  void buildCommandBuffers() {
    if (resized) {
      handleResize();
    }

    VkCommandBufferBeginInfo cmdBufInfo =
        vks::initializers::commandBufferBeginInfo();

    VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
        1, 0, 1 };

    for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
      VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

      /*
       Dispatch the ray tracing commands
       */
      vkCmdBindPipeline(drawCmdBuffers[i],
          VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);
      vkCmdBindDescriptorSets(drawCmdBuffers[i],
          VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout, 0, 1,
          &descriptorSet, 0, 0);

      VkStridedDeviceAddressRegionKHR emptySbtEntry = { };
      vkCmdTraceRaysKHR(drawCmdBuffers[i],
          &shaderBindingTables.raygen.stridedDeviceAddressRegion,
          &shaderBindingTables.miss.stridedDeviceAddressRegion,
          &shaderBindingTables.hit.stridedDeviceAddressRegion, &emptySbtEntry,
          width, height, 1);

      /*
       Copy ray tracing output to swap chain image
       */

      // Prepare current swap chain image as transfer destination
      vks::tools::setImageLayout(drawCmdBuffers[i], swapChain.images[i],
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          subresourceRange);

      // Prepare ray tracing output image as transfer source
      vks::tools::setImageLayout(drawCmdBuffers[i], storageImage.image,
          VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          subresourceRange);

      VkImageCopy copyRegion { };
      copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      copyRegion.srcOffset = { 0, 0, 0 };
      copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      copyRegion.dstOffset = { 0, 0, 0 };
      copyRegion.extent = { width, height, 1 };
      vkCmdCopyImage(drawCmdBuffers[i], storageImage.image,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChain.images[i],
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

      // Transition swap chain image back for presentation
      vks::tools::setImageLayout(drawCmdBuffers[i], swapChain.images[i],
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
          subresourceRange);

      // Transition ray tracing output image back to general layout
      vks::tools::setImageLayout(drawCmdBuffers[i], storageImage.image,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
          subresourceRange);

      scene.draw(drawCmdBuffers[i], pipeline, pipelineLayout);

      drawUI(drawCmdBuffers[i], frameBuffers[i]);

      VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
    }
  }

  void updateUniformBuffers() {
    uniformData.projInverse = glm::inverse(camera.matrices.perspective);
    uniformData.viewInverse = glm::inverse(camera.matrices.view);
//		uniformData.lightPos = glm::vec4(cos(glm::radians(timer * 360.0f)) * 40.0f, -50.0f + sin(glm::radians(timer * 360.0f)) * 20.0f, 25.0f + sin(glm::radians(timer * 360.0f)) * 5.0f, 0.0f);
    uniformData.lightPos = glm::vec4(camera.position, 0.0f);
    // Pass the vertex size to the shader for unpacking vertices
    uniformData.vertexSize = sizeof(vkglBSP::Vertex);
    memcpy(ubo.mapped, &uniformData, sizeof(uniformData));

  }

  void getEnabledFeatures() {
    // Enable features required for ray tracing using feature chaining via pNext
    enabledBufferDeviceAddresFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    enabledBufferDeviceAddresFeatures.bufferDeviceAddress = VK_TRUE;

    enabledRayTracingPipelineFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
    enabledRayTracingPipelineFeatures.pNext =
        &enabledBufferDeviceAddresFeatures;

    enabledAccelerationStructureFeatures.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
    enabledAccelerationStructureFeatures.pNext =
        &enabledRayTracingPipelineFeatures;

    deviceCreatepNextChain = &enabledAccelerationStructureFeatures;
  }

  void prepare() {
    VulkanRaytracingSample::prepare();

    // Create the acceleration structures used to render the ray traced scene
    createBottomLevelAccelerationStructure();
    createTopLevelAccelerationStructure();

    createStorageImage(swapChain.colorFormat, { width, height, 1 });
    createUniformBuffer();
    createRayTracingPipeline();
    createShaderBindingTables();
    createDescriptorSets();
    buildCommandBuffers();
    prepared = true;
  }
//
  void draw() {
    VulkanExampleBase::prepareFrame();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
    VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
    VulkanExampleBase::submitFrame();
  }

  virtual void render() {
    if (!prepared)
      return;
    draw();

    camera.update(1);

    if (!paused || camera.updated) {
//	    updateLight();
      updateUniformBuffers();
    }

  }
};

VULKAN_EXAMPLE_MAIN()
