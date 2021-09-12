#include "VulkanRaytracingSample.h"
#include "VulkanglBSP.h"
#define VERTEX_BUFFER_BIND_ID 0

// Vertex layout for this example
struct Vertex {
  float pos[3];
  float uv[2];
  float normal[3];
};

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

  struct GlobalUniform {
    glm::mat4 projection;
    glm::mat4 modelView;
    glm::vec4 viewPos;
    float lodBias = 0.0f;
  } uboVS;

  struct {
    VkPipelineVertexInputStateCreateInfo inputState;
    std::vector<VkVertexInputBindingDescription> bindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
  } vertices;

  vkglBSP::GLTexture texture;
  vks::Buffer uniformBufferVS;
  vks::Buffer ubo;

  uint32_t indexCount;

  VkPipeline prePipeline;
  VkPipelineLayout prePipelineLayout;
  VkDescriptorSet preDescriptorSet;
  VkDescriptorSetLayout preDescriptorSetLayout;
  VkDescriptorPool preDescriptorPool = VK_NULL_HANDLE;
  VkPipelineCache prePipelineCache;

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
    camera.setMovementSpeed(250.0f);
    rayQueryOnly = false;
    enableExtensions();
  }

  ~VulkanExample() {
    destroyTextureImage(texture);
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
    uniformBufferVS.destroy();
  }

  void loadTexture() {
    std::cout << "Loading texture..." << std::endl;
    // We use the Khronos texture format (https://www.khronos.org/opengles/sdk/tools/KTX/file_format_spec/)
    std::string filename = getAssetPath() + "textures/cubemap_space.ktx";
    // Texture data contains 4 channels (RGBA) with unnormalized 8-bit values, this is the most commonly supported format
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

    ktxResult result;
    ktxTexture *ktxTexture;

    if (!vks::tools::fileExists(filename)) {
      vks::tools::exitFatal(
          "Could not load texture from " + filename
              + "\n\nThe file may be part of the additional asset pack.\n\nRun \"download_assets.py\" in the repository root to download the latest version.",
          -1);
    }
    result = ktxTexture_CreateFromNamedFile(filename.c_str(),
        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
    assert(result == KTX_SUCCESS);

    // Get properties required for using and upload texture data from the ktx texture object
    texture.width = ktxTexture->baseWidth;
    texture.height = ktxTexture->baseHeight;
    texture.mipLevels = ktxTexture->numLevels;
    ktx_uint8_t *ktxTextureData = ktxTexture_GetData(ktxTexture);
    ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);

    // We prefer using staging to copy the texture data to a device local optimal image
    VkBool32 useStaging = true;

    // Only use linear tiling if forced
    bool forceLinearTiling = false;
    if (forceLinearTiling) {
      // Don't use linear if format is not supported for (linear) shader sampling
      // Get device properties for the requested texture format
      VkFormatProperties formatProperties;
      vkGetPhysicalDeviceFormatProperties(physicalDevice, format,
          &formatProperties);
      useStaging = !(formatProperties.linearTilingFeatures
          & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    }

    VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
    VkMemoryRequirements memReqs = { };

    if (useStaging) {
      // Copy data to an optimal tiled image
      // This loads the texture data into a host local buffer that is copied to the optimal tiled image on the device

      // Create a host-visible staging buffer that contains the raw image data
      // This buffer will be the data source for copying texture data to the optimal tiled image on the device
      VkBuffer stagingBuffer;
      VkDeviceMemory stagingMemory;

      VkBufferCreateInfo bufferCreateInfo =
          vks::initializers::bufferCreateInfo();
      bufferCreateInfo.size = ktxTextureSize;
      // This buffer is used as a transfer source for the buffer copy
      bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      VK_CHECK_RESULT(
          vkCreateBuffer(device, &bufferCreateInfo, nullptr, &stagingBuffer));

      // Get memory requirements for the staging buffer (alignment, memory type bits)
      vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);
      memAllocInfo.allocationSize = memReqs.size;
      // Get memory type index for a host visible buffer
      memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(
          memReqs.memoryTypeBits,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
              | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      VK_CHECK_RESULT(
          vkAllocateMemory(device, &memAllocInfo, nullptr, &stagingMemory));
      VK_CHECK_RESULT(
          vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0));

      // Copy texture data into host local staging buffer
      uint8_t *data;
      VK_CHECK_RESULT(
          vkMapMemory(device, stagingMemory, 0, memReqs.size, 0,
              (void** )&data));
      memcpy(data, ktxTextureData, ktxTextureSize);
      vkUnmapMemory(device, stagingMemory);

      // Setup buffer copy regions for each mip level
      std::vector<VkBufferImageCopy> bufferCopyRegions;
      uint32_t offset = 0;

      for (uint32_t i = 0; i < texture.mipLevels; i++) {
        // Calculate offset into staging buffer for the current mip level
        ktx_size_t offset;
        KTX_error_code ret = ktxTexture_GetImageOffset(ktxTexture, i, 0, 0,
            &offset);
        assert(ret == KTX_SUCCESS);
        // Setup a buffer image copy structure for the current mip level
        VkBufferImageCopy bufferCopyRegion = { };
        bufferCopyRegion.imageSubresource.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        bufferCopyRegion.imageSubresource.mipLevel = i;
        bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
        bufferCopyRegion.imageSubresource.layerCount = 1;
        bufferCopyRegion.imageExtent.width = ktxTexture->baseWidth >> i;
        bufferCopyRegion.imageExtent.height = ktxTexture->baseHeight >> i;
        bufferCopyRegion.imageExtent.depth = 1;
        bufferCopyRegion.bufferOffset = offset;
        bufferCopyRegions.push_back(bufferCopyRegion);
      }

      // Create optimal tiled target image on the device
      VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
      imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
      imageCreateInfo.format = format;
      imageCreateInfo.mipLevels = texture.mipLevels;
      imageCreateInfo.arrayLayers = 1;
      imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
      imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
      imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      // Set initial layout of the image to undefined
      imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      imageCreateInfo.extent = { texture.width, texture.height, 1 };
      imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT
          | VK_IMAGE_USAGE_SAMPLED_BIT;
      VK_CHECK_RESULT(
          vkCreateImage(device, &imageCreateInfo, nullptr, &texture.image));

      vkGetImageMemoryRequirements(device, texture.image, &memReqs);
      memAllocInfo.allocationSize = memReqs.size;
      memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(
          memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      VK_CHECK_RESULT(
          vkAllocateMemory(device, &memAllocInfo, nullptr,
              &texture.deviceMemory));
      VK_CHECK_RESULT(
          vkBindImageMemory(device, texture.image, texture.deviceMemory, 0));

      VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(
          VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

      // Image memory barriers for the texture image

      // The sub resource range describes the regions of the image that will be transitioned using the memory barriers below
      VkImageSubresourceRange subresourceRange = { };
      // Image only contains color data
      subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      // Start at first mip level
      subresourceRange.baseMipLevel = 0;
      // We will transition on all mip levels
      subresourceRange.levelCount = texture.mipLevels;
      // The 2D texture only has one layer
      subresourceRange.layerCount = 1;

      // Transition the texture image layout to transfer target, so we can safely copy our buffer data to it.
      VkImageMemoryBarrier imageMemoryBarrier =
          vks::initializers::imageMemoryBarrier();
      ;
      imageMemoryBarrier.image = texture.image;
      imageMemoryBarrier.subresourceRange = subresourceRange;
      imageMemoryBarrier.srcAccessMask = 0;
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

      // Insert a memory dependency at the proper pipeline stages that will execute the image layout transition
      // Source pipeline stage is host write/read execution (VK_PIPELINE_STAGE_HOST_BIT)
      // Destination pipeline stage is copy command execution (VK_PIPELINE_STAGE_TRANSFER_BIT)
      vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_HOST_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
          &imageMemoryBarrier);

      // Copy mip levels from staging buffer
      vkCmdCopyBufferToImage(copyCmd, stagingBuffer, texture.image,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          static_cast<uint32_t>(bufferCopyRegions.size()),
          bufferCopyRegions.data());

      // Once the data has been uploaded we transfer to the texture image to the shader read layout, so it can be sampled from
      imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      // Insert a memory dependency at the proper pipeline stages that will execute the image layout transition
      // Source pipeline stage is copy command execution (VK_PIPELINE_STAGE_TRANSFER_BIT)
      // Destination pipeline stage fragment shader access (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
      vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
          &imageMemoryBarrier);

      // Store current layout for later reuse
      texture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      vulkanDevice->flushCommandBuffer(copyCmd, queue, true);

      // Clean up staging resources
      vkFreeMemory(device, stagingMemory, nullptr);
      vkDestroyBuffer(device, stagingBuffer, nullptr);
    } else {
      // Copy data to a linear tiled image

      VkImage mappableImage;
      VkDeviceMemory mappableMemory;

      // Load mip map level 0 to linear tiling image
      VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
      imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
      imageCreateInfo.format = format;
      imageCreateInfo.mipLevels = 1;
      imageCreateInfo.arrayLayers = 1;
      imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
      imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
      imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
      imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
      imageCreateInfo.extent = { texture.width, texture.height, 1 };
      VK_CHECK_RESULT(
          vkCreateImage(device, &imageCreateInfo, nullptr, &mappableImage));

      // Get memory requirements for this image like size and alignment
      vkGetImageMemoryRequirements(device, mappableImage, &memReqs);
      // Set memory allocation size to required memory size
      memAllocInfo.allocationSize = memReqs.size;
      // Get memory type that can be mapped to host memory
      memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(
          memReqs.memoryTypeBits,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
              | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      VK_CHECK_RESULT(
          vkAllocateMemory(device, &memAllocInfo, nullptr, &mappableMemory));
      VK_CHECK_RESULT(
          vkBindImageMemory(device, mappableImage, mappableMemory, 0));

      // Map image memory
      void *data;
      VK_CHECK_RESULT(
          vkMapMemory(device, mappableMemory, 0, memReqs.size, 0, &data));
      // Copy image data of the first mip level into memory
      memcpy(data, ktxTextureData, memReqs.size);
      vkUnmapMemory(device, mappableMemory);

      // Linear tiled images don't need to be staged and can be directly used as textures
      texture.image = mappableImage;
      texture.deviceMemory = mappableMemory;
      texture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      // Setup image memory barrier transfer image to shader read layout
      VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(
          VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

      // The sub resource range describes the regions of the image we will be transition
      VkImageSubresourceRange subresourceRange = { };
      subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      subresourceRange.baseMipLevel = 0;
      subresourceRange.levelCount = 1;
      subresourceRange.layerCount = 1;

      // Transition the texture image layout to shader read, so it can be sampled from
      VkImageMemoryBarrier imageMemoryBarrier =
          vks::initializers::imageMemoryBarrier();
      ;
      imageMemoryBarrier.image = texture.image;
      imageMemoryBarrier.subresourceRange = subresourceRange;
      imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
      imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

      // Insert a memory dependency at the proper pipeline stages that will execute the image layout transition
      // Source pipeline stage is host write/read execution (VK_PIPELINE_STAGE_HOST_BIT)
      // Destination pipeline stage fragment shader access (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
      vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_HOST_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
          &imageMemoryBarrier);

      vulkanDevice->flushCommandBuffer(copyCmd, queue, true);
    }

    ktxTexture_Destroy(ktxTexture);

    // Create a texture sampler
    // In Vulkan textures are accessed by samplers
    // This separates all the sampling information from the texture data. This means you could have multiple sampler objects for the same texture with different settings
    // Note: Similar to the samplers available with OpenGL 3.3
    VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.mipLodBias = 0.0f;
    sampler.compareOp = VK_COMPARE_OP_NEVER;
    sampler.minLod = 0.0f;
    // Set max level-of-detail to mip level count of the texture
    sampler.maxLod = (useStaging) ? (float) texture.mipLevels : 0.0f;
    // Enable anisotropic filtering
    // This feature is optional, so we must check if it's supported on the device
    if (vulkanDevice->features.samplerAnisotropy) {
      // Use max. level of anisotropy for this example
      sampler.maxAnisotropy =
          vulkanDevice->properties.limits.maxSamplerAnisotropy;
      sampler.anisotropyEnable = VK_TRUE;
    } else {
      // The device does not support anisotropic filtering
      sampler.maxAnisotropy = 1.0;
      sampler.anisotropyEnable = VK_FALSE;
    }
    sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VK_CHECK_RESULT(
        vkCreateSampler(device, &sampler, nullptr, &texture.sampler));

    // Create image view
    // Textures are not directly accessed by the shaders and
    // are abstracted by image views containing additional
    // information and sub resource ranges
    VkImageViewCreateInfo view = vks::initializers::imageViewCreateInfo();
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
    // The subresource range describes the set of mip levels (and array layers) that can be accessed through this image view
    // It's possible to create multiple image views for a single image referring to different (and/or overlapping) ranges of the image
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.baseMipLevel = 0;
    view.subresourceRange.baseArrayLayer = 0;
    view.subresourceRange.layerCount = 1;
    // Linear tiling usually won't support mip maps
    // Only set mip map count if optimal tiling is used
    view.subresourceRange.levelCount = (useStaging) ? texture.mipLevels : 1;
    // The view will be based on the texture's image
    view.image = texture.image;
    VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &texture.view));
    std::cout << "Loaded texture " << std::endl;
  }

  // Free all Vulkan resources used by a texture object
  void destroyTextureImage(vkglBSP::GLTexture texture) {
    vkDestroyImageView(device, texture.view, nullptr);
    vkDestroyImage(device, texture.image, nullptr);
    vkDestroySampler(device, texture.sampler, nullptr);
    vkFreeMemory(device, texture.deviceMemory, nullptr);
  }

  void loadScene() {

    const uint32_t glTFLoadingFlags = vkglBSP::FileLoadingFlags::None;

    scene.loadFromFile(getAssetPath() + "models/vulkanscene_shadow.gltf",
        vulkanDevice, queue, glTFLoadingFlags);

    std::cout << "Loaded from file done " << std::endl;
  }

  /*
   *
   Create the bottom level acceleration structure contains the scene's actual geometry (vertices, triangles)
   */
  void createBottomLevelAccelerationStructure() {
    // Instead of a simple triangle, we'll be loading a more complex scene for this example
    // The shaders are accessing the vertex and index buffers of the scene, so the proper usage flag has to be set on the vertex and index buffers for the scene

    VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress { };
    VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress { };
    std::cout << "Vertex buffer = " << scene.loadmodel->vertexBuffer.buffer
        << std::endl;
    vertexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(
        scene.loadmodel->vertexBuffer.buffer);
    indexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(
        scene.loadmodel->indexBuffer.buffer);

    // Setup vertices for a single uv-mapped quad made from two triangles
    std::vector<Vertex> vertices = { { { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f }, {
        0.0f, 0.0f, 1.0f } }, { { -1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f,
        0.0f, 1.0f } }, { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f,
        1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } } };

    // Setup indices
    std::vector<uint32_t> indices = { 0, 1, 2, 2, 3, 0 };
    indexCount = static_cast<uint32_t>(indices.size());

//    uint32_t numTriangles = static_cast<uint32_t>(scene.loadmodel->edges.size()
//        / 3);

    uint32_t numTriangles = 2;
    uint32_t maxVertex = 6;

    // Build
    VkAccelerationStructureGeometryKHR accelerationStructureGeometry =
        vks::initializers::accelerationStructureGeometryKHR();
    accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    accelerationStructureGeometry.geometry.triangles.sType =
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    accelerationStructureGeometry.geometry.triangles.vertexFormat =
        VK_FORMAT_R32G32B32_SFLOAT;
    accelerationStructureGeometry.geometry.triangles.vertexData =
        vertexBufferDeviceAddress;
    accelerationStructureGeometry.geometry.triangles.maxVertex = maxVertex;
    accelerationStructureGeometry.geometry.triangles.vertexStride =
        sizeof(Vertex);
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

    createAccelerationStructure(bottomLevelAS,
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        accelerationStructureBuildSizesInfo);

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

    // Build the acceleration structure on the device via a one-time command buffer submission
    // Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device builds
    VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(
        VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1,
        &accelerationBuildGeometryInfo,
        accelerationBuildStructureRangeInfos.data());
    vulkanDevice->flushCommandBuffer(commandBuffer, queue);

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
    accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
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

    std::cout << "Created TLAS" << std::endl;
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
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 }, {
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
        scene.loadmodel->vertexBuffer.buffer, 0,
        VK_WHOLE_SIZE };
    VkDescriptorBufferInfo indexBufferDescriptor {
        scene.loadmodel->indexBuffer.buffer, 0,
        VK_WHOLE_SIZE };

    VkDescriptorImageInfo textureImageDescriptor { VK_NULL_HANDLE, texture.view,
        VK_IMAGE_LAYOUT_GENERAL };

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
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4, &indexBufferDescriptor),
            // Binding 5: Texture
        vks::initializers::writeDescriptorSet(descriptorSet,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 5, &textureImageDescriptor), };
    vkUpdateDescriptorSets(device,
        static_cast<uint32_t>(writeDescriptorSets.size()),
        writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
  }
//
//	/*
//		Create our ray tracing pipeline
//	*/
  void createRayTracingPipeline() {
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
            VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, 4),
            // Binding 5: Texture image
            vks::initializers::descriptorSetLayoutBinding(
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR,
                5),
    };

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI =
        vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
    VK_CHECK_RESULT(
        vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr,
            &descriptorSetLayout));

    std::vector<VkDescriptorSetLayout> rtDescSetLayouts = { descriptorSetLayout,
        preDescriptorSetLayout };

    VkPipelineLayoutCreateInfo pPipelineLayoutCI;
    pPipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pPipelineLayoutCI.setLayoutCount =
        static_cast<uint32_t>(rtDescSetLayouts.size());
    pPipelineLayoutCI.pSetLayouts = rtDescSetLayouts.data();

    VK_CHECK_RESULT(
        vkCreatePipelineLayout(device, &pPipelineLayoutCI, nullptr,
            &pipelineLayout));

//		/*
//			Setup ray tracing shader groups
//		*/
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

    // Setup Texture vert/frag shaders
//    {
//      shaderStages.push_back(
//          loadShader(getShadersPath() + "texture/texture.vert.spv",
//              VK_SHADER_STAGE_VERTEX_BIT));
//    }
//    {
//      shaderStages.push_back(
//          loadShader(getShadersPath() + "texture/texture.frag.spv",
//              VK_SHADER_STAGE_FRAGMENT_BIT));
//    }

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

    updateUniformBuffers();
  }

//	/*
//		Command buffer generation
//	*/
  void buildCommandBuffers() {
    if (resized) {
      handleResize();
    }

    std::cout << "Command buffer executed" << std::endl;

    VkCommandBufferBeginInfo cmdBufInfo =
        vks::initializers::commandBufferBeginInfo();

    VkClearValue clearValues[2];
    clearValues[0].color = defaultClearColor;
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo renderPassBeginInfo =
        vks::initializers::renderPassBeginInfo();
    renderPassBeginInfo.renderPass = renderPass;
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent.width = width;
    renderPassBeginInfo.renderArea.extent.height = height;
    renderPassBeginInfo.clearValueCount = 2;
    renderPassBeginInfo.pClearValues = clearValues;

    VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
        1, 0, 1 };

    for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
      rayTrace(i);
//      VkCommandBufferBeginInfo cmdBufInfo =
//          vks::initializers::commandBufferBeginInfo();
//
//      VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

//                      renderPassBeginInfo.framebuffer = frameBuffers[i];
//      rayTrace(drawCmdBuffers[i]);

      //
//      VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));
//      vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
//

//      std::vector<VkDescriptorSet> descSets { descriptorSet, preDescriptorSet };

      /*
       Dispatch the ray tracing commands
       */
//      vkCmdBindPipeline(drawCmdBuffers[i],
//          VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);
//      vkCmdBindDescriptorSets(drawCmdBuffers[i],
//          VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout, 0,
//          (uint32_t) descSets.size(), descSets.data(), 0, 0);
//
//      VkStridedDeviceAddressRegionKHR emptySbtEntry = { };
//      vkCmdTraceRaysKHR(drawCmdBuffers[i],
//          &shaderBindingTables.raygen.stridedDeviceAddressRegion,
//          &shaderBindingTables.miss.stridedDeviceAddressRegion,
//          &shaderBindingTables.hit.stridedDeviceAddressRegion, &emptySbtEntry,
//          width, height, 1);
      /*
       Copy ray tracing output to swap chain image
       */

      // Prepare current swap chain image as transfer destination
//      vks::tools::setImageLayout(drawCmdBuffers[i], swapChain.images[i],
//          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
//          subresourceRange);
//
////       Prepare ray tracing output image as transfer source
//      vks::tools::setImageLayout(drawCmdBuffers[i], storageImage.image,
//          VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
//          subresourceRange);
//
//      VkImageCopy copyRegion { };
//      copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
//      copyRegion.srcOffset = { 0, 0, 0 };
//      copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
//      copyRegion.dstOffset = { 0, 0, 0 };
//      copyRegion.extent = { width, height, 1 };
//      vkCmdCopyImage(drawCmdBuffers[i], storageImage.image,
//          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChain.images[i],
//          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
//
//      // Transition swap chain image back for presentation
//      vks::tools::setImageLayout(drawCmdBuffers[i], swapChain.images[i],
//          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
//          subresourceRange);
//
//      // Transition ray tracing output image back to general layout
//      vks::tools::setImageLayout(drawCmdBuffers[i], storageImage.image,
//          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
//          subresourceRange);
//      scene.draw(drawCmdBuffers[i], pipeline, pipelineLayout);
      // Set target frame buffer
//          VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));
//
//
//          VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
//          vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
//
//          VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
//          vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);
//      vkCmdBindDescriptorSets(drawCmdBuffers[i],
//          VK_PIPELINE_BIND_POINT_GRAPHICS, prePipelineLayout, 0, 1,
//          &preDescriptorSet, 0, NULL);
//      vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
//          prePipeline);
//      VkDeviceSize offsets[1] = { 0 };
//      vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1,
//          &vertexBuffer.buffer, offsets);
//      vkCmdBindIndexBuffer(drawCmdBuffers[i], indexBuffer.buffer, 0,
//          VK_INDEX_TYPE_UINT32);
//      vkCmdDrawIndexed(drawCmdBuffers[i], indexCount, 1, 0, 0, 0);
//      vkCmdEndRenderPass(drawCmdBuffers[i]);
//      drawUI(drawCmdBuffers[i], frameBuffers[i]);
      std::cout << " DRAE BUFFER end " << i << std::endl;
//      VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
//      VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));

    }
  }

  void updateUniformBuffers() {
    uniformData.projInverse = glm::inverse(camera.matrices.perspective);
    uniformData.viewInverse = glm::inverse(camera.matrices.view);
//    uniformData.lightPos = glm::vec4((cos(glm::radians(timer * 360.0f)) * 5.0f) + camera.position.x, (-50.0f + sin(glm::radians(timer * 360.0f)) * 5.0f) + camera.position.y, (25.0f + sin(glm::radians(timer * 360.0f)) * 5.0f) + camera.position.z, 0.0f);

    uniformData.lightPos = glm::vec4(camera.position.x, camera.position.y,
        camera.position.z, 0.0f);

    uniformData.lightPos = glm::vec4(-228.209f, 227.337f, 315.972, 0.0f);
    // Pass the vertex size to the shader for unpacking vertices
    uniformData.vertexSize = sizeof(vkglBSP::MVertex);
    memcpy(ubo.mapped, &uniformData, sizeof(uniformData));

  }

  void setupVertexDescriptions() {

//    // Binding description
    vertices.bindingDescriptions.resize(1);
//    vertices.bindingDescriptions[0] =
//        vks::initializers::vertexInputBindingDescription(
//        VERTEX_BUFFER_BIND_ID, sizeof(vkglBSP::QModel), VK_VERTEX_INPUT_RATE_VERTEX);
//
//    // Attribute descriptions
//    // Describes memory layout and shader positions
    vertices.attributeDescriptions.resize(3);
//    // Location 0 : Position
//    vertices.attributeDescriptions[0] =
//        vks::initializers::vertexInputAttributeDescription(
//        VERTEX_BUFFER_BIND_ID, 0, VK_FORMAT_R32G32B32_SFLOAT,
//            offsetof(vkglBSP::QModel, vertexes));
//    // Location 1 : Texture coordinates
//    vertices.attributeDescriptions[1] =
//        vks::initializers::vertexInputAttributeDescription(
//        VERTEX_BUFFER_BIND_ID, 1, VK_FORMAT_R32G32_SFLOAT,
//        offsetof(vkglBSP::QModel, texinfo));
//    // Location 2 : Vertex normal
//    vertices.attributeDescriptions[2] =
//        vks::initializers::vertexInputAttributeDescription(
//        VERTEX_BUFFER_BIND_ID, 2, VK_FORMAT_R32G32B32_SFLOAT,
//            offsetof(vkglBSP::QModel, vertexes));
//
//    vertices.inputState =
//        vks::initializers::pipelineVertexInputStateCreateInfo();
//    vertices.inputState.vertexBindingDescriptionCount =
//        static_cast<uint32_t>(vertices.bindingDescriptions.size());
//    vertices.inputState.pVertexBindingDescriptions =
//        vertices.bindingDescriptions.data();
//    vertices.inputState.vertexAttributeDescriptionCount =
//        static_cast<uint32_t>(vertices.attributeDescriptions.size());
//    vertices.inputState.pVertexAttributeDescriptions =
//        vertices.attributeDescriptions.data();

    // Binding description
    vertices.bindingDescriptions = {
        vks::initializers::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID,
            sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX) };

    // Attribute descriptions
    // Describes memory layout and shader positions
    vertices.attributeDescriptions = {
    // Location 0: Position
        vks::initializers::vertexInputAttributeDescription(
        VERTEX_BUFFER_BIND_ID, 0, VK_FORMAT_R32G32B32_SFLOAT,
            offsetof(Vertex, pos)),
        // Location 1: Texture coordinates
        vks::initializers::vertexInputAttributeDescription(
        VERTEX_BUFFER_BIND_ID, 1, VK_FORMAT_R32G32_SFLOAT,
            offsetof(Vertex, uv)),

//        //    // Location 2 : Vertex normal
        vertices.attributeDescriptions[2] =
            vks::initializers::vertexInputAttributeDescription(
            VERTEX_BUFFER_BIND_ID, 2, VK_FORMAT_R32G32B32_SFLOAT,
                offsetof(Vertex, normal)) };

    // Assign to vertex buffer

    vertices.inputState =
        vks::initializers::pipelineVertexInputStateCreateInfo();
    vertices.inputState.vertexBindingDescriptionCount =
        static_cast<uint32_t>(vertices.bindingDescriptions.size());
    vertices.inputState.pVertexBindingDescriptions =
        vertices.bindingDescriptions.data();
    vertices.inputState.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(vertices.attributeDescriptions.size());
    vertices.inputState.pVertexAttributeDescriptions =
        vertices.attributeDescriptions.data();

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

  void updateUniformBuffersPre() {
    uboVS.projection = (camera.matrices.perspective);
    uboVS.modelView = (camera.matrices.view);
    uboVS.lodBias = .5f;
//    uboVS.viewPos = glm::vec4(camera.position.x, camera.position.y,
//        camera.position.z, 0);
    uboVS.viewPos = glm::vec4(-228.209f, 227.337f, 315.972, 0.0f);
    memcpy(uniformBufferVS.mapped, &uboVS, sizeof(uboVS));
  }

  // Prepare and initialize uniform buffer containing shader uniforms
  void prepareUniformBuffers() {
    // Vertex shader uniform buffer block
    VK_CHECK_RESULT(
        vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBufferVS,
            sizeof(uboVS), &uboVS));
    VK_CHECK_RESULT(uniformBufferVS.map());
    updateUniformBuffersPre();
  }

  void setupDescriptorSetLayout() {
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
    // Binding 0 : Vertex shader uniform buffer
        vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            0),
        // Binding 1 : Fragment shader image sampler
        vks::initializers::descriptorSetLayoutBinding(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            1) };

    VkDescriptorSetLayoutCreateInfo descriptorLayout =
        vks::initializers::descriptorSetLayoutCreateInfo(
            setLayoutBindings.data(),
            static_cast<uint32_t>(setLayoutBindings.size()));

    VK_CHECK_RESULT(
        vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr,
            &preDescriptorSetLayout));

    VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
        vks::initializers::pipelineLayoutCreateInfo(&preDescriptorSetLayout, 1);

    VK_CHECK_RESULT(
        vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr,
            &prePipelineLayout));
  }

  void preparePipelines() {
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
        vks::initializers::pipelineInputAssemblyStateCreateInfo(
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0,
            VK_FALSE);

    VkPipelineRasterizationStateCreateInfo rasterizationState =
        vks::initializers::pipelineRasterizationStateCreateInfo(
            VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
            VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);

    VkPipelineColorBlendAttachmentState blendAttachmentState =
        vks::initializers::pipelineColorBlendAttachmentState(0xf,
        VK_FALSE);

    VkPipelineColorBlendStateCreateInfo colorBlendState =
        vks::initializers::pipelineColorBlendStateCreateInfo(1,
            &blendAttachmentState);

    VkPipelineDepthStencilStateCreateInfo depthStencilState =
        vks::initializers::pipelineDepthStencilStateCreateInfo(
        VK_TRUE,
        VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);

    VkPipelineViewportStateCreateInfo viewportState =
        vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

    VkPipelineMultisampleStateCreateInfo multisampleState =
        vks::initializers::pipelineMultisampleStateCreateInfo(
            VK_SAMPLE_COUNT_1_BIT, 0);

    std::vector<VkDynamicState> dynamicStateEnables = {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState =
        vks::initializers::pipelineDynamicStateCreateInfo(
            dynamicStateEnables.data(),
            static_cast<uint32_t>(dynamicStateEnables.size()), 0);

    // Load shaders
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

    shaderStages[0] = loadShader(
        getShadersPath() + "raytracingshadows/texture.vert.spv",
        VK_SHADER_STAGE_VERTEX_BIT);
    shaderStages[1] = loadShader(
        getShadersPath() + "raytracingshadows/texture.frag.spv",
        VK_SHADER_STAGE_FRAGMENT_BIT);

    VkGraphicsPipelineCreateInfo pipelineCreateInfo =
        vks::initializers::pipelineCreateInfo(prePipelineLayout, renderPass, 0);

    pipelineCreateInfo.pVertexInputState = &vertices.inputState;
    pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
    pipelineCreateInfo.pRasterizationState = &rasterizationState;
    pipelineCreateInfo.pColorBlendState = &colorBlendState;
    pipelineCreateInfo.pMultisampleState = &multisampleState;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pDepthStencilState = &depthStencilState;
    pipelineCreateInfo.pDynamicState = &dynamicState;
    pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineCreateInfo.pStages = shaderStages.data();

    VK_CHECK_RESULT(
        vkCreateGraphicsPipelines(device, prePipelineCache, 1,
            &pipelineCreateInfo, nullptr, &prePipeline));
  }

  void setupDescriptorPool() {
    // Example uses one ubo and one image sampler
    std::vector<VkDescriptorPoolSize> poolSizes = {
        vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            1), vks::initializers::descriptorPoolSize(
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1) };

    VkDescriptorPoolCreateInfo descriptorPoolInfo =
        vks::initializers::descriptorPoolCreateInfo(
            static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 2);

    VK_CHECK_RESULT(
        vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr,
            &preDescriptorPool));
  }

  void setupDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo =
        vks::initializers::descriptorSetAllocateInfo(preDescriptorPool,
            &preDescriptorSetLayout, 1);

    VK_CHECK_RESULT(
        vkAllocateDescriptorSets(device, &allocInfo, &preDescriptorSet));

    // Setup a descriptor image info for the current texture to be used as a combined image sampler
    VkDescriptorImageInfo textureDescriptor;
    textureDescriptor.imageView = texture.view; // The image's view (images are never directly accessed by the shader, but rather through views defining subresources)
    textureDescriptor.sampler = texture.sampler; // The sampler (Telling the pipeline how to sample the texture, including repeat, border, etc.)
    textureDescriptor.imageLayout = texture.imageLayout; // The current layout of the image (Note: Should always fit the actual use, e.g. shader read)

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
    // Binding 0 : Vertex shader uniform buffer
        vks::initializers::writeDescriptorSet(preDescriptorSet,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBufferVS.descriptor),
        // Binding 1 : Fragment shader texture sampler
        //  Fragment shader: layout (binding = 1) uniform sampler2D samplerColor;
        vks::initializers::writeDescriptorSet(preDescriptorSet,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // The descriptor set will use a combined image sampler (sampler and image could be split)
            1,                        // Shader binding point 1
            &textureDescriptor) // Pointer to the descriptor image for our texture
        };

    vkUpdateDescriptorSets(device,
        static_cast<uint32_t>(writeDescriptorSets.size()),
        writeDescriptorSets.data(), 0, NULL);
  }

  void prepare() {
    std::cout << "Preparing..." << std::endl;
    VulkanRaytracingSample::prepare();
    loadScene();

    /// Rasterizier
    loadTexture();
    std::cout << "Generating quad...." << std::endl;
    generateQuad();
    std::cout << "Generated quad!" << std::endl;
    setupVertexDescriptions();
    std::cout << "Setup Vertex descriptor sets" << std::endl;
    prepareUniformBuffers();
    std::cout << "Prepared uniform buffer" << std::endl;
    setupDescriptorSetLayout();
    std::cout << "Setup descriptor sets" << std::endl;
    preparePipelines();
    std::cout << "Prepared pipeline sets" << std::endl;
    setupDescriptorPool();
    std::cout << "Setup descriptor pool" << std::endl;
    setupDescriptorSet();
    std::cout << "Setup descriptor set" << std::endl;

    // Create the acceleration structures used to render the ray traced scene
    createBottomLevelAccelerationStructure();
    std::cout << "Created BLAS" << std::endl;
    createTopLevelAccelerationStructure();
    std::cout << "Created TLAS" << std::endl;
    createStorageImage(swapChain.colorFormat, { width, height, 1 });
    std::cout << "Created storage image" << std::endl;
    createUniformBuffer();
    std::cout << "Created uniform buffer" << std::endl;
    createRayTracingPipeline();
    std::cout << "Created ray tracing pipeline" << std::endl;
    createShaderBindingTables();
    std::cout << "Created shader bindings tables" << std::endl;
    createDescriptorSets();
    std::cout << "Created descriptor sets" << std::endl;

    buildCommandBuffers();
    std::cout << "Built command buffers" << std::endl;

    prepared = true;
    std::cout << "Prepared!" << std::endl;
  }

  void generateQuad() {
    VkBufferUsageFlags flag = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkBufferUsageFlags rayTracingFlags = // used also for building acceleration structures
        flag
            | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
            | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    // Setup vertices for a single uv-mapped quad made from two triangles
    std::vector<Vertex> vertices = { { { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f }, {
        0.0f, 0.0f, 1.0f } }, { { -1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f,
        0.0f, 1.0f } }, { { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f,
        1.0f } },
        { { 1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } } };

    // Setup indices
    std::vector<uint32_t> indices = { 0, 1, 2, 2, 3, 0 };
    indexCount = static_cast<uint32_t>(indices.size());

    // Create buffers
    // For the sake of simplicity we won't stage the vertex data to the gpu memory
    // Vertex buffer
    VK_CHECK_RESULT(
        vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | rayTracingFlags,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &scene.loadmodel->vertexBuffer, vertices.size() * sizeof(Vertex),
            vertices.data()));
    // Index buffer
    VK_CHECK_RESULT(
        vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | rayTracingFlags,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &scene.loadmodel->indexBuffer, indices.size() * sizeof(uint32_t),
            indices.data()));
  }

//  void generateQuad() {
//
//    VkBufferUsageFlags flag = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
//    VkBufferUsageFlags rayTracingFlags = // used also for building acceleration structures
//        flag
//            | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
//            | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
//
////    // Setup vertices for a single uv-mapped quad made from two triangles
////    std::vector<vkglBSP::MVertex> verts = scene.loadmodel->vertexes;
////
////    // Setup indices
////    std::vector<uint32_t> indices = scene.loadmodel->edges;
////    indexCount = static_cast<uint32_t>(indices.size());
////
////
////    std::vector<float> texCoords;
////
////    for(int i = 0; i < verts.size(); i++) {
////      texCoords.push_back(verts.at(i).position.x);
////    }
////
////    // Create buffers
////    // For the sake of simplicity we won't stage the vertex data to the gpu memory
////    // Vertex buffer
////    VK_CHECK_RESULT(
////        vulkanDevice->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | rayTracingFlags,
////            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
////                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &vertexBuffer,
////                verts.size() * sizeof(vkglBSP::MVertex), verts.data()));
////    // Index buffer
////    VK_CHECK_RESULT(
////        vulkanDevice->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | rayTracingFlags,
////            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
////                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &indexBuffer,
////            indices.size() * sizeof(uint32_t), indices.data()));
//
//    // Setup vertices for a single uv-mapped quad made from two triangles
//
//
//    std::vector<vkglBSP::MVertex> mverts = scene.loadmodel->vertexes;
//    std::vector<Vertex> verts;
//    for (auto v : mverts) {
//      Vertex v1;
//      v1.pos[0] = v.position.x;
//      v1.pos[1] = v.position.y;
//      v1.pos[2] = v.position.z;
//      v1.normal[0] = v.position.x;
//      v1.normal[1] = v.position.y;
//      v1.normal[2] = v.position.z;
//      v1.uv[0] = v.position.x;
//      v1.uv[1] = v.position.z;
//      verts.push_back(v1);
//    }
//
//    // Setup indices
//    std::vector<uint32_t> indices = scene.loadmodel->edges;
//    indexCount = static_cast<uint32_t>(indices.size());
//
//    // Create buffers
//    // For the sake of simplicity we won't stage the vertex data to the gpu memory
//    // Vertex buffer
//    VK_CHECK_RESULT(
//        vulkanDevice->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | rayTracingFlags,
//            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
//                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &scene.loadmodel->vertexBuffer,
//            verts.size() * sizeof(Vertex), verts.data()));
//    // Index buffer
//    VK_CHECK_RESULT(
//        vulkanDevice->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | rayTracingFlags,
//            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
//                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &scene.loadmodel->indexBuffer,
//            indices.size() * sizeof(uint32_t), indices.data()));
//
//  }

  void buildCommandBuffersPre() {
    VkCommandBufferBeginInfo cmdBufInfo =
        vks::initializers::commandBufferBeginInfo();

    VkClearValue clearValues[2];
    clearValues[0].color = defaultClearColor;
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo renderPassBeginInfo =
        vks::initializers::renderPassBeginInfo();
    renderPassBeginInfo.renderPass = renderPass;
    renderPassBeginInfo.renderArea.offset.x = 0;
    renderPassBeginInfo.renderArea.offset.y = 0;
    renderPassBeginInfo.renderArea.extent.width = width;
    renderPassBeginInfo.renderArea.extent.height = height;
    renderPassBeginInfo.clearValueCount = 2;
    renderPassBeginInfo.pClearValues = clearValues;

    for (int32_t i = 0; i < drawCmdBuffers.size(); ++i) {
      // Set target frame buffer
      renderPassBeginInfo.framebuffer = frameBuffers[i];

      VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

      vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo,
          VK_SUBPASS_CONTENTS_INLINE);

      VkViewport viewport = vks::initializers::viewport((float) width,
          (float) height, 0.0f, 1.0f);
      vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

      VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
      vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

      vkCmdBindDescriptorSets(drawCmdBuffers[i],
          VK_PIPELINE_BIND_POINT_GRAPHICS, prePipelineLayout, 0, 1,
          &preDescriptorSet, 0, NULL);
      vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
          prePipeline);

      VkDeviceSize offsets[1] = { 0 };
      vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1,
          &scene.loadmodel->vertexBuffer.buffer, offsets);
      vkCmdBindIndexBuffer(drawCmdBuffers[i],
          scene.loadmodel->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

      vkCmdDrawIndexed(drawCmdBuffers[i], indexCount, 1, 0, 0, 0);

      vkCmdEndRenderPass(drawCmdBuffers[i]);

      VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
    }
  }

  void rayTrace(size_t i) {

    VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
        1, 0, 1 };

    VkCommandBufferBeginInfo cmdBufInfo =
        vks::initializers::commandBufferBeginInfo();

    VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

    std::vector<VkDescriptorSet> descSets { descriptorSet, preDescriptorSet };

    /*
     Dispatch the ray tracing commands
     */
    vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
        pipeline);

    vkCmdBindDescriptorSets(drawCmdBuffers[i],
        VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout, 0,
        (uint32_t) descSets.size(), descSets.data(), 0, nullptr);

    vkCmdPushConstants(drawCmdBuffers[i], pipelineLayout,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
            | VK_SHADER_STAGE_MISS_BIT_KHR, 0, sizeof(UniformData),
        &uniformData);

    VkStridedDeviceAddressRegionKHR emptySbtEntry = { };
    vkCmdTraceRaysKHR(drawCmdBuffers[i],
        &shaderBindingTables.raygen.stridedDeviceAddressRegion,
        &shaderBindingTables.miss.stridedDeviceAddressRegion,
        &shaderBindingTables.hit.stridedDeviceAddressRegion, &emptySbtEntry,
        width, height, 1);

//    // Prepare current swap chain image as transfer destination
    vks::tools::setImageLayout(drawCmdBuffers[i], swapChain.images[i],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        subresourceRange);

    //       Prepare ray tracing output image as transfer source
    vks::tools::setImageLayout(drawCmdBuffers[i], storageImage.image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        subresourceRange);
//
    VkImageCopy copyRegion { };
    copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.srcOffset = { 0, 0, 0 };
    copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.dstOffset = { 0, 0, 0 };
    copyRegion.extent = { width, height, 1 };
    vkCmdCopyImage(drawCmdBuffers[i], storageImage.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChain.images[i],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
//
//    // Transition swap chain image back for presentation
    vks::tools::setImageLayout(drawCmdBuffers[i], swapChain.images[i],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        subresourceRange);

    // Transition ray tracing output image back to general layout
    vks::tools::setImageLayout(drawCmdBuffers[i], storageImage.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
        subresourceRange);
//

    VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
  }

//
  void draw() {
    VulkanExampleBase::prepareFrame();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
    rayTrace(currentBuffer);
    VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
    VulkanExampleBase::submitFrame();
  }

  virtual void render() {
    if (!prepared)
      return;

    draw();

    if (!paused || camera.updated) {
//      std::cout << camera.position.x << " " << camera.position.y << " "
//          << camera.position.z << std::endl;

      updateUniformBuffers();
      updateUniformBuffersPre();
    }

  }
};

VULKAN_EXAMPLE_MAIN()
