/*
 * Vulkan glTF model and texture loading class based on tinyglTF (https://github.com/syoyo/tinygltf)
 *
 * Copyright (C) 2018 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE

#include "VulkanglBSP.h"

VkDescriptorSetLayout vkglBSP::descriptorSetLayoutImage = VK_NULL_HANDLE;
VkDescriptorSetLayout vkglBSP::descriptorSetLayoutUbo = VK_NULL_HANDLE;
VkMemoryPropertyFlags vkglBSP::memoryPropertyFlags = 0;
uint32_t vkglBSP::descriptorBindingFlags =
    vkglBSP::DescriptorBindingFlags::ImageBaseColor;

/*
 We use a custom image loading function with tinyglTF, so we can do custom stuff loading ktx textures
 */
bool loadImageDataFunc(tinygltf::Image *image, const int imageIndex,
    std::string *error, std::string *warning, int req_width, int req_height,
    const unsigned char *bytes, int size, void *userData) {
  // KTX files will be handled by our own code
  if (image->uri.find_last_of(".") != std::string::npos) {
    if (image->uri.substr(image->uri.find_last_of(".") + 1) == "ktx") {
      return true;
    }
  }

  return tinygltf::LoadImageData(image, imageIndex, error, warning, req_width,
      req_height, bytes, size, userData);
}

bool loadImageDataFuncEmpty(tinygltf::Image *image, const int imageIndex,
    std::string *error, std::string *warning, int req_width, int req_height,
    const unsigned char *bytes, int size, void *userData) {
  // This function will be used for samples that don't require images to be loaded
  return true;
}

/*
 glTF texture loading class
 */

void vkglBSP::Texture::updateDescriptor() {
  descriptor.sampler = sampler;
  descriptor.imageView = view;
  descriptor.imageLayout = imageLayout;
}

void vkglBSP::Texture::destroy() {
  if (device) {
    vkDestroyImageView(device->logicalDevice, view, nullptr);
    vkDestroyImage(device->logicalDevice, image, nullptr);
    vkFreeMemory(device->logicalDevice, deviceMemory, nullptr);
    vkDestroySampler(device->logicalDevice, sampler, nullptr);
  }
}

void vkglBSP::Texture::fromglTfImage(tinygltf::Image &gltfimage,
    std::string path, vks::VulkanDevice *device, VkQueue copyQueue) {
  this->device = device;

  bool isKtx = false;
  // Image points to an external ktx file
  if (gltfimage.uri.find_last_of(".") != std::string::npos) {
    if (gltfimage.uri.substr(gltfimage.uri.find_last_of(".") + 1) == "ktx") {
      isKtx = true;
    }
  }

  VkFormat format;

  if (!isKtx) {
    // Texture was loaded using STB_Image

    unsigned char *buffer = nullptr;
    VkDeviceSize bufferSize = 0;
    bool deleteBuffer = false;
    if (gltfimage.component == 3) {
      // Most devices don't support RGB only on Vulkan so convert if necessary
      // TODO: Check actual format support and transform only if required
      bufferSize = gltfimage.width * gltfimage.height * 4;
      buffer = new unsigned char[bufferSize];
      unsigned char *rgba = buffer;
      unsigned char *rgb = &gltfimage.image[0];
      for (size_t i = 0; i < gltfimage.width * gltfimage.height; ++i) {
        for (int32_t j = 0; j < 3; ++j) {
          rgba[j] = rgb[j];
        }
        rgba += 4;
        rgb += 3;
      }
      deleteBuffer = true;
    } else {
      buffer = &gltfimage.image[0];
      bufferSize = gltfimage.image.size();
    }

    format = VK_FORMAT_R8G8B8A8_UNORM;

    VkFormatProperties formatProperties;

    width = gltfimage.width;
    height = gltfimage.height;
    mipLevels =
        static_cast<uint32_t>(floor(log2(std::max(width, height))) + 1.0);

    vkGetPhysicalDeviceFormatProperties(device->physicalDevice, format,
        &formatProperties);
    assert(
        formatProperties.optimalTilingFeatures
            & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
    assert(
        formatProperties.optimalTilingFeatures
            & VK_FORMAT_FEATURE_BLIT_DST_BIT);

    VkMemoryAllocateInfo memAllocInfo { };
    memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    VkMemoryRequirements memReqs { };

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo bufferCreateInfo { };
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = bufferSize;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK_RESULT(
        vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr,
            &stagingBuffer));
    vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer,
        &memReqs);
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK_RESULT(
        vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr,
            &stagingMemory));
    VK_CHECK_RESULT(
        vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory,
            0));

    uint8_t *data;
    VK_CHECK_RESULT(
        vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0,
            (void** )&data));
    memcpy(data, buffer, bufferSize);
    vkUnmapMemory(device->logicalDevice, stagingMemory);

    VkImageCreateInfo imageCreateInfo { };
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = format;
    imageCreateInfo.mipLevels = mipLevels;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.extent = { width, height, 1 };
    imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VK_CHECK_RESULT(
        vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));
    vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(
        vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr,
            &deviceMemory));
    VK_CHECK_RESULT(
        vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

    VkCommandBuffer copyCmd = device->createCommandBuffer(
        VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

    VkImageSubresourceRange subresourceRange = { };
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.levelCount = 1;
    subresourceRange.layerCount = 1;

    {
      VkImageMemoryBarrier imageMemoryBarrier { };
      imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      imageMemoryBarrier.srcAccessMask = 0;
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      imageMemoryBarrier.image = image;
      imageMemoryBarrier.subresourceRange = subresourceRange;
      vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
          &imageMemoryBarrier);
    }

    VkBufferImageCopy bufferCopyRegion = { };
    bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bufferCopyRegion.imageSubresource.mipLevel = 0;
    bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
    bufferCopyRegion.imageSubresource.layerCount = 1;
    bufferCopyRegion.imageExtent.width = width;
    bufferCopyRegion.imageExtent.height = height;
    bufferCopyRegion.imageExtent.depth = 1;

    vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

    {
      VkImageMemoryBarrier imageMemoryBarrier { };
      imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      imageMemoryBarrier.image = image;
      imageMemoryBarrier.subresourceRange = subresourceRange;
      vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
          &imageMemoryBarrier);
    }

    device->flushCommandBuffer(copyCmd, copyQueue, true);

    vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
    vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

    // Generate the mip chain (glTF uses jpg and png, so we need to create this manually)
    VkCommandBuffer blitCmd = device->createCommandBuffer(
        VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    for (uint32_t i = 1; i < mipLevels; i++) {
      VkImageBlit imageBlit { };

      imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      imageBlit.srcSubresource.layerCount = 1;
      imageBlit.srcSubresource.mipLevel = i - 1;
      imageBlit.srcOffsets[1].x = int32_t(width >> (i - 1));
      imageBlit.srcOffsets[1].y = int32_t(height >> (i - 1));
      imageBlit.srcOffsets[1].z = 1;

      imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      imageBlit.dstSubresource.layerCount = 1;
      imageBlit.dstSubresource.mipLevel = i;
      imageBlit.dstOffsets[1].x = int32_t(width >> i);
      imageBlit.dstOffsets[1].y = int32_t(height >> i);
      imageBlit.dstOffsets[1].z = 1;

      VkImageSubresourceRange mipSubRange = { };
      mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      mipSubRange.baseMipLevel = i;
      mipSubRange.levelCount = 1;
      mipSubRange.layerCount = 1;

      {
        VkImageMemoryBarrier imageMemoryBarrier { };
        imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageMemoryBarrier.srcAccessMask = 0;
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier.image = image;
        imageMemoryBarrier.subresourceRange = mipSubRange;
        vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
            &imageMemoryBarrier);
      }

      vkCmdBlitImage(blitCmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit,
          VK_FILTER_LINEAR);

      {
        VkImageMemoryBarrier imageMemoryBarrier { };
        imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        imageMemoryBarrier.image = image;
        imageMemoryBarrier.subresourceRange = mipSubRange;
        vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
            &imageMemoryBarrier);
      }
    }

    subresourceRange.levelCount = mipLevels;
    imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    {
      VkImageMemoryBarrier imageMemoryBarrier { };
      imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      imageMemoryBarrier.image = image;
      imageMemoryBarrier.subresourceRange = subresourceRange;
      vkCmdPipelineBarrier(blitCmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
          &imageMemoryBarrier);
    }

    device->flushCommandBuffer(blitCmd, copyQueue, true);
  } else {
    // Texture is stored in an external ktx file
    std::string filename = path + "/" + gltfimage.uri;

    ktxTexture *ktxTexture;

    ktxResult result = KTX_SUCCESS;
#if defined(__ANDROID__)
		AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
		if (!asset) {
			vks::tools::exitFatal("Could not load texture from " + filename + "\n\nThe file may be part of the additional asset pack.\n\nRun \"download_assets.py\" in the repository root to download the latest version.", -1);
		}
		size_t size = AAsset_getLength(asset);
		assert(size > 0);
		ktx_uint8_t* textureData = new ktx_uint8_t[size];
		AAsset_read(asset, textureData, size);
		AAsset_close(asset);
		result = ktxTexture_CreateFromMemory(textureData, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
		delete[] textureData;
#else
    if (!vks::tools::fileExists(filename)) {
      vks::tools::exitFatal(
          "Could not load texture from " + filename
              + "\n\nThe file may be part of the additional asset pack.\n\nRun \"download_assets.py\" in the repository root to download the latest version.",
          -1);
    }
    result = ktxTexture_CreateFromNamedFile(filename.c_str(),
        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
#endif		
    assert(result == KTX_SUCCESS);

    this->device = device;
    width = ktxTexture->baseWidth;
    height = ktxTexture->baseHeight;
    mipLevels = ktxTexture->numLevels;

    ktx_uint8_t *ktxTextureData = ktxTexture_GetData(ktxTexture);
    ktx_size_t ktxTextureSize = ktxTexture_GetSize(ktxTexture);
    // @todo: Use ktxTexture_GetVkFormat(ktxTexture)
    format = VK_FORMAT_R8G8B8A8_UNORM;

    // Get device properties for the requested texture format
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(device->physicalDevice, format,
        &formatProperties);

    VkCommandBuffer copyCmd = device->createCommandBuffer(
        VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo();
    bufferCreateInfo.size = ktxTextureSize;
    // This buffer is used as a transfer source for the buffer copy
    bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK_RESULT(
        vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr,
            &stagingBuffer));

    VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer,
        &memReqs);
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK_RESULT(
        vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr,
            &stagingMemory));
    VK_CHECK_RESULT(
        vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory,
            0));

    uint8_t *data;
    VK_CHECK_RESULT(
        vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0,
            (void** )&data));
    memcpy(data, ktxTextureData, ktxTextureSize);
    vkUnmapMemory(device->logicalDevice, stagingMemory);

    std::vector<VkBufferImageCopy> bufferCopyRegions;
    for (uint32_t i = 0; i < mipLevels; i++) {
      ktx_size_t offset;
      KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture, i, 0, 0,
          &offset);
      assert(result == KTX_SUCCESS);
      VkBufferImageCopy bufferCopyRegion = { };
      bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      bufferCopyRegion.imageSubresource.mipLevel = i;
      bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
      bufferCopyRegion.imageSubresource.layerCount = 1;
      bufferCopyRegion.imageExtent.width = std::max(1u,
          ktxTexture->baseWidth >> i);
      bufferCopyRegion.imageExtent.height = std::max(1u,
          ktxTexture->baseHeight >> i);
      bufferCopyRegion.imageExtent.depth = 1;
      bufferCopyRegion.bufferOffset = offset;
      bufferCopyRegions.push_back(bufferCopyRegion);
    }

    // Create optimal tiled target image
    VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = format;
    imageCreateInfo.mipLevels = mipLevels;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.extent = { width, height, 1 };
    imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VK_CHECK_RESULT(
        vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));

    vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);
    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK_RESULT(
        vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr,
            &deviceMemory));
    VK_CHECK_RESULT(
        vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

    VkImageSubresourceRange subresourceRange = { };
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = mipLevels;
    subresourceRange.layerCount = 1;

    vks::tools::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);
    vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(bufferCopyRegions.size()),
        bufferCopyRegions.data());
    vks::tools::setImageLayout(copyCmd, image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
    device->flushCommandBuffer(copyCmd, copyQueue);
    this->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
    vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

    ktxTexture_Destroy(ktxTexture);
  }

  VkSamplerCreateInfo samplerInfo { };
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
  samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  samplerInfo.maxAnisotropy = 1.0;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxLod = (float) mipLevels;
  samplerInfo.maxAnisotropy = 8.0f;
  samplerInfo.anisotropyEnable = VK_TRUE;
  VK_CHECK_RESULT(
      vkCreateSampler(device->logicalDevice, &samplerInfo, nullptr, &sampler));

  VkImageViewCreateInfo viewInfo { };
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
      VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.layerCount = 1;
  viewInfo.subresourceRange.levelCount = mipLevels;
  VK_CHECK_RESULT(
      vkCreateImageView(device->logicalDevice, &viewInfo, nullptr, &view));

  descriptor.sampler = sampler;
  descriptor.imageView = view;
  descriptor.imageLayout = imageLayout;
}

/*
 glTF material
 */
void vkglBSP::Material::createDescriptorSet(VkDescriptorPool descriptorPool,
    VkDescriptorSetLayout descriptorSetLayout,
    uint32_t descriptorBindingFlags) {
  VkDescriptorSetAllocateInfo descriptorSetAllocInfo { };
  descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptorSetAllocInfo.descriptorPool = descriptorPool;
  descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;
  descriptorSetAllocInfo.descriptorSetCount = 1;
  VK_CHECK_RESULT(
      vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo,
          &descriptorSet));
  std::vector<VkDescriptorImageInfo> imageDescriptors { };
  std::vector<VkWriteDescriptorSet> writeDescriptorSets { };
  if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
    imageDescriptors.push_back(baseColorTexture->descriptor);
    VkWriteDescriptorSet writeDescriptorSet { };
    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.dstSet = descriptorSet;
    writeDescriptorSet.dstBinding =
        static_cast<uint32_t>(writeDescriptorSets.size());
    writeDescriptorSet.pImageInfo = &baseColorTexture->descriptor;
    writeDescriptorSets.push_back(writeDescriptorSet);
  }
  if (normalTexture
      && descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
    imageDescriptors.push_back(normalTexture->descriptor);
    VkWriteDescriptorSet writeDescriptorSet { };
    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.dstSet = descriptorSet;
    writeDescriptorSet.dstBinding =
        static_cast<uint32_t>(writeDescriptorSets.size());
    writeDescriptorSet.pImageInfo = &normalTexture->descriptor;
    writeDescriptorSets.push_back(writeDescriptorSet);
  }
  vkUpdateDescriptorSets(device->logicalDevice,
      static_cast<uint32_t>(writeDescriptorSets.size()),
      writeDescriptorSets.data(), 0, nullptr);
}

/*
 glTF primitive
 */
void vkglBSP::Primitive::setDimensions(glm::vec3 min, glm::vec3 max) {
  dimensions.min = min;
  dimensions.max = max;
  dimensions.size = max - min;
  dimensions.center = (min + max) / 2.0f;
  dimensions.radius = glm::distance(min, max) / 2.0f;
}

/*
 glTF mesh
 */
vkglBSP::Mesh::Mesh(vks::VulkanDevice *device, glm::mat4 matrix) {
  this->device = device;
  this->uniformBlock.matrix = matrix;
  VK_CHECK_RESULT(
      device->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
              | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(uniformBlock),
          &uniformBuffer.buffer, &uniformBuffer.memory, &uniformBlock));
  VK_CHECK_RESULT(
      vkMapMemory(device->logicalDevice, uniformBuffer.memory, 0,
          sizeof(uniformBlock), 0, &uniformBuffer.mapped));
  uniformBuffer.descriptor = { uniformBuffer.buffer, 0, sizeof(uniformBlock) };
}
;

vkglBSP::Mesh::~Mesh() {
//  vkDestroyBuffer(device->logicalDevice, uniformBuffer.buffer, nullptr);
//  vkFreeMemory(device->logicalDevice, uniformBuffer.memory, nullptr);
}

/*
 glTF node
 */
glm::mat4 vkglBSP::Node::localMatrix() {
  return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation)
      * glm::scale(glm::mat4(1.0f), scale) * matrix;
}

glm::mat4 vkglBSP::Node::getMatrix() {
  glm::mat4 m = localMatrix();
  vkglBSP::Node *p = parent;
  while (p) {
    m = p->localMatrix() * m;
    p = p->parent;
  }
  return m;
}

void vkglBSP::Node::update() {
  if (mesh) {
    glm::mat4 m = getMatrix();
    if (skin) {
      mesh->uniformBlock.matrix = m;
      // Update join matrices
      glm::mat4 inverseTransform = glm::inverse(m);
      for (size_t i = 0; i < skin->joints.size(); i++) {
        vkglBSP::Node *jointNode = skin->joints[i];
        glm::mat4 jointMat = jointNode->getMatrix()
            * skin->inverseBindMatrices[i];
        jointMat = inverseTransform * jointMat;
        mesh->uniformBlock.jointMatrix[i] = jointMat;
      }
      mesh->uniformBlock.jointcount = (float) skin->joints.size();
      memcpy(mesh->uniformBuffer.mapped, &mesh->uniformBlock,
          sizeof(mesh->uniformBlock));
    } else {
      memcpy(mesh->uniformBuffer.mapped, &m, sizeof(glm::mat4));
    }
  }

  for (auto &child : children) {
    child->update();
  }
}

vkglBSP::Node::~Node() {
  if (mesh) {
    delete mesh;
  }
  for (auto &child : children) {
    delete child;
  }
}

/*
 glTF default vertex layout with easy Vulkan mapping functions
 */

VkVertexInputBindingDescription vkglBSP::Vertex::vertexInputBindingDescription;
std::vector<VkVertexInputAttributeDescription> vkglBSP::Vertex::vertexInputAttributeDescriptions;
VkPipelineVertexInputStateCreateInfo vkglBSP::Vertex::pipelineVertexInputStateCreateInfo;

VkVertexInputBindingDescription vkglBSP::Vertex::inputBindingDescription(
    uint32_t binding) {
  return VkVertexInputBindingDescription( { binding, sizeof(Vertex),
      VK_VERTEX_INPUT_RATE_VERTEX });
}

VkVertexInputAttributeDescription vkglBSP::Vertex::inputAttributeDescription(
    uint32_t binding, uint32_t location, VertexComponent component) {
  switch (component) {
  case VertexComponent::Position:
    return VkVertexInputAttributeDescription( { location, binding,
        VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos) });
  case VertexComponent::Normal:
    return VkVertexInputAttributeDescription( { location, binding,
        VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) });
  case VertexComponent::UV:
    return VkVertexInputAttributeDescription( { location, binding,
        VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) });
  case VertexComponent::Color:
    return VkVertexInputAttributeDescription( { location, binding,
        VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color) });
  case VertexComponent::Tangent:
    return VkVertexInputAttributeDescription( { location, binding,
        VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent) });
  case VertexComponent::Joint0:
    return VkVertexInputAttributeDescription( { location, binding,
        VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, joint0) });
  case VertexComponent::Weight0:
    return VkVertexInputAttributeDescription( { location, binding,
        VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weight0) });
  default:
    return VkVertexInputAttributeDescription( { });
  }
}

std::vector<VkVertexInputAttributeDescription> vkglBSP::Vertex::inputAttributeDescriptions(
    uint32_t binding, const std::vector<VertexComponent> components) {
  std::vector<VkVertexInputAttributeDescription> result;
  uint32_t location = 0;
  for (VertexComponent component : components) {
    result.push_back(
        Vertex::inputAttributeDescription(binding, location, component));
    location++;
  }
  return result;
}

/** @brief Returns the default pipeline vertex input state create info structure for the requested vertex components */
VkPipelineVertexInputStateCreateInfo* vkglBSP::Vertex::getPipelineVertexInputState(
    const std::vector<VertexComponent> components) {
  vertexInputBindingDescription = Vertex::inputBindingDescription(0);
  Vertex::vertexInputAttributeDescriptions = Vertex::inputAttributeDescriptions(
      0, components);
  pipelineVertexInputStateCreateInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  pipelineVertexInputStateCreateInfo.vertexBindingDescriptionCount = 1;
  pipelineVertexInputStateCreateInfo.pVertexBindingDescriptions =
      &Vertex::vertexInputBindingDescription;
  pipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount =
      static_cast<uint32_t>(Vertex::vertexInputAttributeDescriptions.size());
  pipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions =
      Vertex::vertexInputAttributeDescriptions.data();
  return &pipelineVertexInputStateCreateInfo;
}

vkglBSP::Texture* vkglBSP::Model::getTexture(uint32_t index) {

  if (index < textures.size()) {
    return &textures[index];
  }
  return nullptr;
}

void vkglBSP::Model::createEmptyTexture(VkQueue transferQueue) {
  emptyTexture.device = device;
  emptyTexture.width = 1;
  emptyTexture.height = 1;
  emptyTexture.layerCount = 1;
  emptyTexture.mipLevels = 1;

  size_t bufferSize = emptyTexture.width * emptyTexture.height * 4;
  unsigned char *buffer = new unsigned char[bufferSize];
  memset(buffer, 0, bufferSize);

  VkBuffer stagingBuffer;
  VkDeviceMemory stagingMemory;
  VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo();
  bufferCreateInfo.size = bufferSize;
  // This buffer is used as a transfer source for the buffer copy
  bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VK_CHECK_RESULT(
      vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr,
          &stagingBuffer));

  VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
  memAllocInfo.allocationSize = memReqs.size;
  memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
          | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VK_CHECK_RESULT(
      vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr,
          &stagingMemory));
  VK_CHECK_RESULT(
      vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory,
          0));

  // Copy texture data into staging buffer
  uint8_t *data;
  VK_CHECK_RESULT(
      vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0,
          (void** )&data));
  memcpy(data, buffer, bufferSize);
  vkUnmapMemory(device->logicalDevice, stagingMemory);

  VkBufferImageCopy bufferCopyRegion = { };
  bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  bufferCopyRegion.imageSubresource.layerCount = 1;
  bufferCopyRegion.imageExtent.width = emptyTexture.width;
  bufferCopyRegion.imageExtent.height = emptyTexture.height;
  bufferCopyRegion.imageExtent.depth = 1;

  // Create optimal tiled target image
  VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
  imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
  imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageCreateInfo.mipLevels = 1;
  imageCreateInfo.arrayLayers = 1;
  imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageCreateInfo.extent = { emptyTexture.width, emptyTexture.height, 1 };
  imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT
      | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  VK_CHECK_RESULT(
      vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr,
          &emptyTexture.image));

  vkGetImageMemoryRequirements(device->logicalDevice, emptyTexture.image,
      &memReqs);
  memAllocInfo.allocationSize = memReqs.size;
  memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VK_CHECK_RESULT(
      vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr,
          &emptyTexture.deviceMemory));
  VK_CHECK_RESULT(
      vkBindImageMemory(device->logicalDevice, emptyTexture.image,
          emptyTexture.deviceMemory, 0));

  VkImageSubresourceRange subresourceRange { };
  subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  subresourceRange.baseMipLevel = 0;
  subresourceRange.levelCount = 1;
  subresourceRange.layerCount = 1;

  VkCommandBuffer copyCmd = device->createCommandBuffer(
      VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
  vks::tools::setImageLayout(copyCmd, emptyTexture.image,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      subresourceRange);
  vkCmdCopyBufferToImage(copyCmd, stagingBuffer, emptyTexture.image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);
  vks::tools::setImageLayout(copyCmd, emptyTexture.image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
  device->flushCommandBuffer(copyCmd, transferQueue);
  emptyTexture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  // Clean up staging resources
  vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
  vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

  VkSamplerCreateInfo samplerCreateInfo =
      vks::initializers::samplerCreateInfo();
  samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
  samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
  samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
  samplerCreateInfo.maxAnisotropy = 1.0f;
  VK_CHECK_RESULT(
      vkCreateSampler(device->logicalDevice, &samplerCreateInfo, nullptr,
          &emptyTexture.sampler));

  VkImageViewCreateInfo viewCreateInfo =
      vks::initializers::imageViewCreateInfo();
  viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
      VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
  viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  viewCreateInfo.subresourceRange.levelCount = 1;
  viewCreateInfo.image = emptyTexture.image;
  VK_CHECK_RESULT(
      vkCreateImageView(device->logicalDevice, &viewCreateInfo, nullptr,
          &emptyTexture.view));

  emptyTexture.descriptor.imageLayout =
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  emptyTexture.descriptor.imageView = emptyTexture.view;
  emptyTexture.descriptor.sampler = emptyTexture.sampler;
}

/*
 glTF model loading and rendering class
 */
vkglBSP::Model::~Model() {

  if (device) {
    vkDestroyBuffer(device->logicalDevice, loadmodel->vertexBuffer.buffer, nullptr);
    vkFreeMemory(device->logicalDevice, loadmodel->memory, nullptr);
    vkDestroyBuffer(device->logicalDevice, loadmodel->indexBuffer.buffer, nullptr);
    vkFreeMemory(device->logicalDevice, loadmodel->index_memory, nullptr);
    for (auto texture : textures) {
      texture.destroy();
    }
    for (auto node : nodes) {
      delete node;
    }
    if (descriptorSetLayoutUbo != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device->logicalDevice,
          descriptorSetLayoutUbo, nullptr);
      descriptorSetLayoutUbo = VK_NULL_HANDLE;
    }
    if (descriptorSetLayoutImage != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device->logicalDevice,
          descriptorSetLayoutImage, nullptr);
      descriptorSetLayoutImage = VK_NULL_HANDLE;
    }
    vkDestroyDescriptorPool(device->logicalDevice, descriptorPool, nullptr);
    emptyTexture.destroy();
  }

}

void vkglBSP::Model::loadNode(vkglBSP::Node *parent, const tinygltf::Node &node,
    uint32_t nodeIndex, const tinygltf::Model &model,
    std::vector<uint32_t> &indexBuffer, std::vector<Vertex> &vertexBuffer,
    float globalscale) {
  vkglBSP::Node *newNode = new Node { };
  newNode->index = nodeIndex;
  newNode->parent = parent;
  newNode->name = node.name;
  newNode->skinIndex = node.skin;
  newNode->matrix = glm::mat4(1.0f);

  // Generate local node matrix
  glm::vec3 translation = glm::vec3(0.0f);
  if (node.translation.size() == 3) {
    translation = glm::make_vec3(node.translation.data());
    newNode->translation = translation;
  }
  glm::mat4 rotation = glm::mat4(1.0f);
  if (node.rotation.size() == 4) {
    glm::quat q = glm::make_quat(node.rotation.data());
    newNode->rotation = glm::mat4(q);
  }
  glm::vec3 scale = glm::vec3(1.0f);
  if (node.scale.size() == 3) {
    scale = glm::make_vec3(node.scale.data());
    newNode->scale = scale;
  }
  if (node.matrix.size() == 16) {
    newNode->matrix = glm::make_mat4x4(node.matrix.data());
    if (globalscale != 1.0f) {
      //newNode->matrix = glm::scale(newNode->matrix, glm::vec3(globalscale));
    }
  };

  // Node with children
  if (node.children.size() > 0) {
    for (auto i = 0; i < node.children.size(); i++) {
      loadNode(newNode, model.nodes[node.children[i]], node.children[i], model,
          indexBuffer, vertexBuffer, globalscale);
    }
  }

  // Node contains mesh data
  if (node.mesh > -1) {
    const tinygltf::Mesh mesh = model.meshes[node.mesh];
    Mesh *newMesh = new Mesh(device, newNode->matrix);
    newMesh->name = mesh.name;
    for (size_t j = 0; j < mesh.primitives.size(); j++) {
      const tinygltf::Primitive &primitive = mesh.primitives[j];
      if (primitive.indices < 0) {
        continue;
      }
      uint32_t indexStart = static_cast<uint32_t>(indexBuffer.size());
      uint32_t vertexStart = static_cast<uint32_t>(vertexBuffer.size());
      uint32_t indexCount = 0;
      uint32_t vertexCount = 0;
      glm::vec3 posMin { };
      glm::vec3 posMax { };
      bool hasSkin = false;
      // Vertices
      {
        const float *bufferPos = nullptr;
        const float *bufferNormals = nullptr;
        const float *bufferTexCoords = nullptr;
        const float *bufferColors = nullptr;
        const float *bufferTangents = nullptr;
        uint32_t numColorComponents;
        const uint16_t *bufferJoints = nullptr;
        const float *bufferWeights = nullptr;

        // Position attribute is required
        assert(
            primitive.attributes.find("POSITION")
                != primitive.attributes.end());

        const tinygltf::Accessor &posAccessor =
            model.accessors[primitive.attributes.find("POSITION")->second];
        const tinygltf::BufferView &posView =
            model.bufferViews[posAccessor.bufferView];
        bufferPos =
            reinterpret_cast<const float*>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset
                + posView.byteOffset]));
        posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1],
            posAccessor.minValues[2]);
        posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1],
            posAccessor.maxValues[2]);

        if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
          const tinygltf::Accessor &normAccessor =
              model.accessors[primitive.attributes.find("NORMAL")->second];
          const tinygltf::BufferView &normView =
              model.bufferViews[normAccessor.bufferView];
          bufferNormals =
              reinterpret_cast<const float*>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset
                  + normView.byteOffset]));
        }

        if (primitive.attributes.find("TEXCOORD_0")
            != primitive.attributes.end()) {
          const tinygltf::Accessor &uvAccessor =
              model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
          const tinygltf::BufferView &uvView =
              model.bufferViews[uvAccessor.bufferView];
          bufferTexCoords =
              reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset
                  + uvView.byteOffset]));
        }

        if (primitive.attributes.find("COLOR_0")
            != primitive.attributes.end()) {
          const tinygltf::Accessor &colorAccessor =
              model.accessors[primitive.attributes.find("COLOR_0")->second];
          const tinygltf::BufferView &colorView =
              model.bufferViews[colorAccessor.bufferView];
          // Color buffer are either of type vec3 or vec4
          numColorComponents =
              colorAccessor.type == TINYGLTF_PARAMETER_TYPE_FLOAT_VEC3 ? 3 : 4;
          bufferColors =
              reinterpret_cast<const float*>(&(model.buffers[colorView.buffer].data[colorAccessor.byteOffset
                  + colorView.byteOffset]));
        }

        if (primitive.attributes.find("TANGENT")
            != primitive.attributes.end()) {
          const tinygltf::Accessor &tangentAccessor =
              model.accessors[primitive.attributes.find("TANGENT")->second];
          const tinygltf::BufferView &tangentView =
              model.bufferViews[tangentAccessor.bufferView];
          bufferTangents =
              reinterpret_cast<const float*>(&(model.buffers[tangentView.buffer].data[tangentAccessor.byteOffset
                  + tangentView.byteOffset]));
        }

        // Skinning
        // Joints
        if (primitive.attributes.find("JOINTS_0")
            != primitive.attributes.end()) {
          const tinygltf::Accessor &jointAccessor =
              model.accessors[primitive.attributes.find("JOINTS_0")->second];
          const tinygltf::BufferView &jointView =
              model.bufferViews[jointAccessor.bufferView];
          bufferJoints =
              reinterpret_cast<const uint16_t*>(&(model.buffers[jointView.buffer].data[jointAccessor.byteOffset
                  + jointView.byteOffset]));
        }

        if (primitive.attributes.find("WEIGHTS_0")
            != primitive.attributes.end()) {
          const tinygltf::Accessor &uvAccessor =
              model.accessors[primitive.attributes.find("WEIGHTS_0")->second];
          const tinygltf::BufferView &uvView =
              model.bufferViews[uvAccessor.bufferView];
          bufferWeights =
              reinterpret_cast<const float*>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset
                  + uvView.byteOffset]));
        }

        hasSkin = (bufferJoints && bufferWeights);

        vertexCount = static_cast<uint32_t>(posAccessor.count);

        for (size_t v = 0; v < posAccessor.count; v++) {
          Vertex vert { };
          vert.pos = glm::vec4(glm::make_vec3(&bufferPos[v * 3]), 1.0f);
          vert.normal = glm::normalize(
              glm::vec3(
                  bufferNormals ?
                      glm::make_vec3(&bufferNormals[v * 3]) : glm::vec3(0.0f)));
          vert.uv =
              bufferTexCoords ?
                  glm::make_vec2(&bufferTexCoords[v * 2]) : glm::vec3(0.0f);
          if (bufferColors) {
            switch (numColorComponents) {
            case 3:
              vert.color = glm::vec4(glm::make_vec3(&bufferColors[v * 3]),
                  1.0f);
            case 4:
              vert.color = glm::make_vec4(&bufferColors[v * 4]);
            }
          } else {
            vert.color = glm::vec4(1.0f);
          }
          vert.tangent =
              bufferTangents ?
                  glm::vec4(glm::make_vec4(&bufferTangents[v * 4])) :
                  glm::vec4(0.0f);
          vert.joint0 =
              hasSkin ?
                  glm::vec4(glm::make_vec4(&bufferJoints[v * 4])) :
                  glm::vec4(0.0f);
          vert.weight0 =
              hasSkin ? glm::make_vec4(&bufferWeights[v * 4]) : glm::vec4(0.0f);
          vertexBuffer.push_back(vert);
        }
      }
      // Indices
      {
        const tinygltf::Accessor &accessor = model.accessors[primitive.indices];
        const tinygltf::BufferView &bufferView =
            model.bufferViews[accessor.bufferView];
        const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

        indexCount = static_cast<uint32_t>(accessor.count);

        switch (accessor.componentType) {
        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
          uint32_t *buf = new uint32_t[accessor.count];
          memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset],
              accessor.count * sizeof(uint32_t));
          for (size_t index = 0; index < accessor.count; index++) {
            indexBuffer.push_back(buf[index] + vertexStart);
          }
          break;
        }
        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
          uint16_t *buf = new uint16_t[accessor.count];
          memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset],
              accessor.count * sizeof(uint16_t));
          for (size_t index = 0; index < accessor.count; index++) {
            indexBuffer.push_back(buf[index] + vertexStart);
          }
          break;
        }
        case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
          uint8_t *buf = new uint8_t[accessor.count];
          memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset],
              accessor.count * sizeof(uint8_t));
          for (size_t index = 0; index < accessor.count; index++) {
            indexBuffer.push_back(buf[index] + vertexStart);
          }
          break;
        }
        default:
          std::cerr << "Index component type " << accessor.componentType
              << " not supported!" << std::endl;
          return;
        }
      }
      Primitive *newPrimitive = new Primitive(indexStart, indexCount,
          primitive.material > -1 ?
              materials[primitive.material] : materials.back());
      newPrimitive->firstVertex = vertexStart;
      newPrimitive->vertexCount = vertexCount;
      newPrimitive->setDimensions(posMin, posMax);
      newMesh->primitives.push_back(newPrimitive);
    }
    newNode->mesh = newMesh;
  }
  if (parent) {
    parent->children.push_back(newNode);
  } else {
    nodes.push_back(newNode);
  }
  linearNodes.push_back(newNode);
}

void vkglBSP::Model::loadSkins(tinygltf::Model &gltfModel) {
  for (tinygltf::Skin &source : gltfModel.skins) {
    Skin *newSkin = new Skin { };
    newSkin->name = source.name;

    // Find skeleton root node
    if (source.skeleton > -1) {
      newSkin->skeletonRoot = nodeFromIndex(source.skeleton);
    }

    // Find joint nodes
    for (int jointIndex : source.joints) {
      Node *node = nodeFromIndex(jointIndex);
      if (node) {
        newSkin->joints.push_back(nodeFromIndex(jointIndex));
      }
    }

    // Get inverse bind matrices from buffer
    if (source.inverseBindMatrices > -1) {
      const tinygltf::Accessor &accessor =
          gltfModel.accessors[source.inverseBindMatrices];
      const tinygltf::BufferView &bufferView =
          gltfModel.bufferViews[accessor.bufferView];
      const tinygltf::Buffer &buffer = gltfModel.buffers[bufferView.buffer];
      newSkin->inverseBindMatrices.resize(accessor.count);
      memcpy(newSkin->inverseBindMatrices.data(),
          &buffer.data[accessor.byteOffset + bufferView.byteOffset],
          accessor.count * sizeof(glm::mat4));
    }

    skins.push_back(newSkin);
  }
}

void vkglBSP::Model::loadImages(tinygltf::Model &gltfModel,
    vks::VulkanDevice *device, VkQueue transferQueue) {
  for (tinygltf::Image &image : gltfModel.images) {
    vkglBSP::Texture texture;
    texture.fromglTfImage(image, path, device, transferQueue);
    textures.push_back(texture);
  }
  // Create an empty texture to be used for empty material images
  createEmptyTexture(transferQueue);
}

void vkglBSP::Model::loadMaterials(tinygltf::Model &gltfModel) {
  for (tinygltf::Material &mat : gltfModel.materials) {
    vkglBSP::Material material(device);
    if (mat.values.find("baseColorTexture") != mat.values.end()) {
      material.baseColorTexture =
          getTexture(
              gltfModel.textures[mat.values["baseColorTexture"].TextureIndex()].source);
    }
    // Metallic roughness workflow
    if (mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
      material.metallicRoughnessTexture =
          getTexture(
              gltfModel.textures[mat.values["metallicRoughnessTexture"].TextureIndex()].source);
    }
    if (mat.values.find("roughnessFactor") != mat.values.end()) {
      material.roughnessFactor =
          static_cast<float>(mat.values["roughnessFactor"].Factor());
    }
    if (mat.values.find("metallicFactor") != mat.values.end()) {
      material.metallicFactor =
          static_cast<float>(mat.values["metallicFactor"].Factor());
    }
    if (mat.values.find("baseColorFactor") != mat.values.end()) {
      material.baseColorFactor = glm::make_vec4(
          mat.values["baseColorFactor"].ColorFactor().data());
    }
    if (mat.additionalValues.find("normalTexture")
        != mat.additionalValues.end()) {
      material.normalTexture =
          getTexture(
              gltfModel.textures[mat.additionalValues["normalTexture"].TextureIndex()].source);
    } else {
      material.normalTexture = &emptyTexture;
    }
    if (mat.additionalValues.find("emissiveTexture")
        != mat.additionalValues.end()) {
      material.emissiveTexture =
          getTexture(
              gltfModel.textures[mat.additionalValues["emissiveTexture"].TextureIndex()].source);
    }
    if (mat.additionalValues.find("occlusionTexture")
        != mat.additionalValues.end()) {
      material.occlusionTexture =
          getTexture(
              gltfModel.textures[mat.additionalValues["occlusionTexture"].TextureIndex()].source);
    }
    if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
      tinygltf::Parameter param = mat.additionalValues["alphaMode"];
      if (param.string_value == "BLEND") {
        material.alphaMode = Material::ALPHAMODE_BLEND;
      }
      if (param.string_value == "MASK") {
        material.alphaMode = Material::ALPHAMODE_MASK;
      }
    }
    if (mat.additionalValues.find("alphaCutoff")
        != mat.additionalValues.end()) {
      material.alphaCutoff =
          static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
    }

    materials.push_back(material);
  }
  // Push a default material at the end of the list for meshes with no material assigned
  materials.push_back(Material(device));
}

void vkglBSP::Model::loadAnimations(tinygltf::Model &gltfModel) {
  for (tinygltf::Animation &anim : gltfModel.animations) {
    vkglBSP::Animation animation { };
    animation.name = anim.name;
    if (anim.name.empty()) {
      animation.name = std::to_string(animations.size());
    }

    // Samplers
    for (auto &samp : anim.samplers) {
      vkglBSP::AnimationSampler sampler { };

      if (samp.interpolation == "LINEAR") {
        sampler.interpolation = AnimationSampler::InterpolationType::LINEAR;
      }
      if (samp.interpolation == "STEP") {
        sampler.interpolation = AnimationSampler::InterpolationType::STEP;
      }
      if (samp.interpolation == "CUBICSPLINE") {
        sampler.interpolation =
            AnimationSampler::InterpolationType::CUBICSPLINE;
      }

      // Read sampler input time values
      {
        const tinygltf::Accessor &accessor = gltfModel.accessors[samp.input];
        const tinygltf::BufferView &bufferView =
            gltfModel.bufferViews[accessor.bufferView];
        const tinygltf::Buffer &buffer = gltfModel.buffers[bufferView.buffer];

        assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

        float *buf = new float[accessor.count];
        memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset],
            accessor.count * sizeof(float));
        for (size_t index = 0; index < accessor.count; index++) {
          sampler.inputs.push_back(buf[index]);
        }

        for (auto input : sampler.inputs) {
          if (input < animation.start) {
            animation.start = input;
          };
          if (input > animation.end) {
            animation.end = input;
          }
        }
      }

      // Read sampler output T/R/S values
      {
        const tinygltf::Accessor &accessor = gltfModel.accessors[samp.output];
        const tinygltf::BufferView &bufferView =
            gltfModel.bufferViews[accessor.bufferView];
        const tinygltf::Buffer &buffer = gltfModel.buffers[bufferView.buffer];

        assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

        switch (accessor.type) {
        case TINYGLTF_TYPE_VEC3: {
          glm::vec3 *buf = new glm::vec3[accessor.count];
          memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset],
              accessor.count * sizeof(glm::vec3));
          for (size_t index = 0; index < accessor.count; index++) {
            sampler.outputsVec4.push_back(glm::vec4(buf[index], 0.0f));
          }
          break;
        }
        case TINYGLTF_TYPE_VEC4: {
          glm::vec4 *buf = new glm::vec4[accessor.count];
          memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset],
              accessor.count * sizeof(glm::vec4));
          for (size_t index = 0; index < accessor.count; index++) {
            sampler.outputsVec4.push_back(buf[index]);
          }
          break;
        }
        default: {
          std::cout << "unknown type" << std::endl;
          break;
        }
        }
      }

      animation.samplers.push_back(sampler);
    }

    // Channels
    for (auto &source : anim.channels) {
      vkglBSP::AnimationChannel channel { };

      if (source.target_path == "rotation") {
        channel.path = AnimationChannel::PathType::ROTATION;
      }
      if (source.target_path == "translation") {
        channel.path = AnimationChannel::PathType::TRANSLATION;
      }
      if (source.target_path == "scale") {
        channel.path = AnimationChannel::PathType::SCALE;
      }
      if (source.target_path == "weights") {
        std::cout << "weights not yet supported, skipping channel" << std::endl;
        continue;
      }
      channel.samplerIndex = source.sampler;
      channel.node = nodeFromIndex(source.target_node);
      if (!channel.node) {
        continue;
      }

      animation.channels.push_back(channel);
    }

    animations.push_back(animation);
  }
}

void vkglBSP::Model::loadFromFile(std::string filename,
    vks::VulkanDevice *device, VkQueue transferQueue, uint32_t fileLoadingFlags,
    float scale) {

  init();

  std::cout << "init completed" << std::endl;

  size_t vertexBufferSize = loadmodel->vertexes.size() * sizeof(MVertex);
  size_t indexBufferSize = loadmodel->edges.size() * sizeof(uint32_t);

  std::cout << "vertex buffer size = " << vertexBufferSize << std::endl;

  assert((vertexBufferSize > 0) && (indexBufferSize > 0));
//
//  struct StagingBuffer {
//    VkBuffer buffer;
//    VkDeviceMemory memory;
//  } vertexStaging, indexStaging;
//
//  // Create staging buffers
//  // Vertex data
//  std::cout << "Creating staging buffer " << std::endl;
//  VK_CHECK_RESULT(
//      device->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
//          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
//              | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexBufferSize,
//          &vertexStaging.buffer, &vertexStaging.memory,
//          loadmodel->vertexes.data()));
//
//  std::cout << "Done creating staging buffer " << std::endl;
//  // Index data
//  VK_CHECK_RESULT(
//      device->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
//          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
//              | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indexBufferSize,
//          &indexStaging.buffer, &indexStaging.memory, loadmodel->edges.data()));
//
//  // Create device local buffers
//  // Vertex buffer
//
//  VK_CHECK_RESULT(
//      device->createBuffer(
//          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
//              | memoryPropertyFlags, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
//          vertexBufferSize, &loadmodel->vertexBuffer.buffer, &loadmodel->memory,
//          loadmodel->vertexes.data()));
//
//  std::cout << "VERTEX BUFFER = " << loadmodel->vertexBuffer.buffer << std::endl;
//  // Index buffer
//  VK_CHECK_RESULT(
//      device->createBuffer(
//          VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
//              | memoryPropertyFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
//          indexBufferSize, &loadmodel->indexBuffer.buffer, &loadmodel->index_memory));
//
//  // Copy from staging buffers
//  std::cout << "Creating command buffer " << std::endl;
//  VkCommandBuffer copyCmd = device->createCommandBuffer(
//      VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
//
//  VkBufferCopy copyRegion = { };
//
//  copyRegion.size = vertexBufferSize;
//  vkCmdCopyBuffer(copyCmd, vertexStaging.buffer, loadmodel->vertexBuffer.buffer, 1,
//      &copyRegion);
//
//  std::cout << "Done Creating command buffer " << std::endl;
//
//  copyRegion.size = indexBufferSize;
//  vkCmdCopyBuffer(copyCmd, indexStaging.buffer, loadmodel->indexBuffer.buffer, 1,
//      &copyRegion);
//
//  std::cout << "Flush command buffer " << std::endl;
//
//  device->flushCommandBuffer(copyCmd, transferQueue, true);
//  std::cout << "Done Flush command buffer " << std::endl;
//
//  std::cout << "free staging buffer " << std::endl;
//  vkDestroyBuffer(device->logicalDevice, vertexStaging.buffer, nullptr);
//  vkFreeMemory(device->logicalDevice, vertexStaging.memory, nullptr);
//  std::cout << "done free staging buffer " << std::endl;
//  vkDestroyBuffer(device->logicalDevice, indexStaging.buffer, nullptr);
//  vkFreeMemory(device->logicalDevice, indexStaging.memory, nullptr);
//
////  getSceneDimensions();
//
//  std::cout << "Setup descriptors " << std::endl;
  // Setup descriptors
  uint32_t uboCount { 0 };
  uint32_t imageCount { 0 };
//  for (auto node : linearNodes) {
//    if (node->mesh) {
//      uboCount++;
//    }
//  }
//  for (auto material : materials) {
//    if (material.baseColorTexture != nullptr) {
//      imageCount++;
//    }
//  }

//  std::cout << "Image count == " << imageCount << std::endl;
//  std::vector<VkDescriptorPoolSize> poolSizes = { {
//      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uboCount }, };
//  if (imageCount > 0) {
//    if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
//      poolSizes.push_back( { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
//          imageCount });
//    }
//    if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
//      poolSizes.push_back( { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
//          imageCount });
//    }
//  }
//  VkDescriptorPoolCreateInfo descriptorPoolCI { };
//  descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
//  descriptorPoolCI.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
//  descriptorPoolCI.pPoolSizes = poolSizes.data();
//  descriptorPoolCI.maxSets = uboCount + imageCount;
//  VK_CHECK_RESULT(
//      vkCreateDescriptorPool(device->logicalDevice, &descriptorPoolCI, nullptr,
//          &descriptorPool));

  // Descriptors for per-node uniform buffers
//  {
//    // Layout is global, so only create if it hasn't already been created before
//    if (descriptorSetLayoutUbo == VK_NULL_HANDLE) {
//      std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
//          { vks::initializers::descriptorSetLayoutBinding(
//              VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0), };
//      VkDescriptorSetLayoutCreateInfo descriptorLayoutCI { };
//      descriptorLayoutCI.sType =
//          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
//      descriptorLayoutCI.bindingCount =
//          static_cast<uint32_t>(setLayoutBindings.size());
//      descriptorLayoutCI.pBindings = setLayoutBindings.data();
//      VK_CHECK_RESULT(
//          vkCreateDescriptorSetLayout(device->logicalDevice,
//              &descriptorLayoutCI, nullptr, &descriptorSetLayoutUbo));
//    }
////    for (auto node : nodes) {
////      prepareNodeDescriptor(node, descriptorSetLayoutUbo);
////    }
//  }

  // Descriptors for per-material images
//  {
  // Layout is global, so only create if it hasn't already been created before
//    if (descriptorSetLayoutImage == VK_NULL_HANDLE) {
//      std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings { };
//      if (descriptorBindingFlags & DescriptorBindingFlags::ImageBaseColor) {
//        setLayoutBindings.push_back(
//            vks::initializers::descriptorSetLayoutBinding(
//                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
//                VK_SHADER_STAGE_FRAGMENT_BIT,
//                static_cast<uint32_t>(setLayoutBindings.size())));
//      }
//      if (descriptorBindingFlags & DescriptorBindingFlags::ImageNormalMap) {
//        setLayoutBindings.push_back(
//            vks::initializers::descriptorSetLayoutBinding(
//                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
//                VK_SHADER_STAGE_FRAGMENT_BIT,
//                static_cast<uint32_t>(setLayoutBindings.size())));
//      }
//      VkDescriptorSetLayoutCreateInfo descriptorLayoutCI { };
//      descriptorLayoutCI.sType =
//          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
//      descriptorLayoutCI.bindingCount =
//          static_cast<uint32_t>(setLayoutBindings.size());
//      descriptorLayoutCI.pBindings = setLayoutBindings.data();
//      VK_CHECK_RESULT(
//          vkCreateDescriptorSetLayout(device->logicalDevice,
//              &descriptorLayoutCI, nullptr, &descriptorSetLayoutImage));
//    }
//    for (auto &material : materials) {
//      if (material.baseColorTexture != nullptr) {
//        material.createDescriptorSet(descriptorPool,
//            vkglBSP::descriptorSetLayoutImage, descriptorBindingFlags);
//      }
//    }
//  }
}

void vkglBSP::Model::bindBuffers(VkCommandBuffer commandBuffer) {
  const VkDeviceSize offsets[1] = { 0 };
//  vkCmdBindVertexBuffers(commandBuffer, 0, 1, &loadmodel->vertex_buffer,
//      offsets);
  vkCmdBindIndexBuffer(commandBuffer, loadmodel->indexBuffer.buffer, 0,
      VK_INDEX_TYPE_UINT32);
  buffersBound = true;
}
//
//void vkglBSP::Model::drawNode(Node *node, VkCommandBuffer commandBuffer,
//    uint32_t renderFlags, VkPipelineLayout pipelineLayout,
//    uint32_t bindImageSet) {
////  if (node->mesh) {
////    for (Primitive *primitive : node->mesh->primitives) {
////      bool skip = false;
////      const vkglBSP::Material &material = primitive->material;
////      if (renderFlags & RenderFlags::RenderOpaqueNodes) {
////        skip = (material.alphaMode != Material::ALPHAMODE_OPAQUE);
////      }
////      if (renderFlags & RenderFlags::RenderAlphaMaskedNodes) {
////        skip = (material.alphaMode != Material::ALPHAMODE_MASK);
////      }
////      if (renderFlags & RenderFlags::RenderAlphaBlendedNodes) {
////        skip = (material.alphaMode != Material::ALPHAMODE_BLEND);
////      }
////      if (!skip) {
////        if (renderFlags & RenderFlags::BindImages) {
////          vkCmdBindDescriptorSets(commandBuffer,
////              VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, bindImageSet, 1,
////              &material.descriptorSet, 0, nullptr);
////        }
////
//////
//////
//////        vkCmdDrawIndexed(commandBuffer, primitive->indexCount, 1,
//////            primitive->firstIndex, 0, 0);
////      }
////    }
////  }
////  for (auto &child : node->children) {
////    drawNode(child, commandBuffer, renderFlags);
////  }
////
//
////  vkCmdDrawIndexed(commandBuffer, loadmodel->edges.size(), 1, 0, 0, 0);
//}

void vkglBSP::Model::draw(const VkCommandBuffer commandBuffer,
    VkPipeline pipeline, VkPipelineLayout pipelineLayout) {
//  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
//    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

//    VkDeviceSize offsets[1] = { 0 };
//    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &loadmodel->vertex_buffer, offsets);
//    vkCmdBindIndexBuffer(commandBuffer, loadmodel->index_buffer, 0, VK_INDEX_TYPE_UINT32);
//
//
//    for (auto &s : loadmodel->surfaces) {
//
//      GlPoly *p = &s.polys;
//  //    GlPoly *p = &loadmodel->surfaces.at(2).polys;
//
//      const int numverts = p->verts.size();
//      const int numtriangles = (numverts - 2);
//      const int numindices = numtriangles * 3;
//
//      for (const auto &v : p->verts) {
//        MVertex mv;
//        mv.position = v;
//        localVertex.push_back(mv);
//      }
//
//      for (int i = 0; i < numtriangles; ++i) {
//        localIndex.push_back(currentIndex);
//        currentIndex++;
//        localIndex.push_back(currentIndex);
//        currentIndex++;
//        localIndex.push_back(currentIndex);
//        currentIndex++;
//      }
//    }
//
//
//
//    for (int32_t i = 0; i < imDrawData->CmdListsCount; i++)
//    {
//      const ImDrawList* cmd_list = imDrawData->CmdLists[i];
//      for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++)
//      {
//        vkCmdDrawIndexed(commandBuffer, pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
//        indexOffset += pcmd->ElemCount;
//      }
//      vertexOffset += cmd_list->VtxBuffer.Size;
//    }
}

void vkglBSP::Model::getNodeDimensions(Node *node, glm::vec3 &min,
    glm::vec3 &max) {
  if (node->mesh) {
    for (Primitive *primitive : node->mesh->primitives) {
      glm::vec4 locMin = glm::vec4(primitive->dimensions.min, 1.0f)
          * node->getMatrix();
      glm::vec4 locMax = glm::vec4(primitive->dimensions.max, 1.0f)
          * node->getMatrix();
      if (locMin.x < min.x) {
        min.x = locMin.x;
      }
      if (locMin.y < min.y) {
        min.y = locMin.y;
      }
      if (locMin.z < min.z) {
        min.z = locMin.z;
      }
      if (locMax.x > max.x) {
        max.x = locMax.x;
      }
      if (locMax.y > max.y) {
        max.y = locMax.y;
      }
      if (locMax.z > max.z) {
        max.z = locMax.z;
      }
    }
  }
  for (auto child : node->children) {
    getNodeDimensions(child, min, max);
  }
}

void vkglBSP::Model::getSceneDimensions() {
  dimensions.min = glm::vec3(FLT_MAX);
  dimensions.max = glm::vec3(-FLT_MAX);
  for (auto node : nodes) {
    getNodeDimensions(node, dimensions.min, dimensions.max);
  }
  dimensions.size = dimensions.max - dimensions.min;
  dimensions.center = (dimensions.min + dimensions.max) / 2.0f;
  dimensions.radius = glm::distance(dimensions.min, dimensions.max) / 2.0f;
}

void vkglBSP::Model::updateAnimation(uint32_t index, float time) {
  if (index > static_cast<uint32_t>(animations.size()) - 1) {
    std::cout << "No animation with index " << index << std::endl;
    return;
  }
  Animation &animation = animations[index];

  bool updated = false;
  for (auto &channel : animation.channels) {
    vkglBSP::AnimationSampler &sampler =
        animation.samplers[channel.samplerIndex];
    if (sampler.inputs.size() > sampler.outputsVec4.size()) {
      continue;
    }

    for (auto i = 0; i < sampler.inputs.size() - 1; i++) {
      if ((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1])) {
        float u = std::max(0.0f, time - sampler.inputs[i])
            / (sampler.inputs[i + 1] - sampler.inputs[i]);
        if (u <= 1.0f) {
          switch (channel.path) {
          case vkglBSP::AnimationChannel::PathType::TRANSLATION: {
            glm::vec4 trans = glm::mix(sampler.outputsVec4[i],
                sampler.outputsVec4[i + 1], u);
            channel.node->translation = glm::vec3(trans);
            break;
          }
          case vkglBSP::AnimationChannel::PathType::SCALE: {
            glm::vec4 trans = glm::mix(sampler.outputsVec4[i],
                sampler.outputsVec4[i + 1], u);
            channel.node->scale = glm::vec3(trans);
            break;
          }
          case vkglBSP::AnimationChannel::PathType::ROTATION: {
            glm::quat q1;
            q1.x = sampler.outputsVec4[i].x;
            q1.y = sampler.outputsVec4[i].y;
            q1.z = sampler.outputsVec4[i].z;
            q1.w = sampler.outputsVec4[i].w;
            glm::quat q2;
            q2.x = sampler.outputsVec4[i + 1].x;
            q2.y = sampler.outputsVec4[i + 1].y;
            q2.z = sampler.outputsVec4[i + 1].z;
            q2.w = sampler.outputsVec4[i + 1].w;
            channel.node->rotation = glm::normalize(glm::slerp(q1, q2, u));
            break;
          }
          }
          updated = true;
        }
      }
    }
  }
  if (updated) {
    for (auto &node : nodes) {
      node->update();
    }
  }
}

/*
 Helper functions
 */
vkglBSP::Node* vkglBSP::Model::findNode(Node *parent, uint32_t index) {
  Node *nodeFound = nullptr;
  if (parent->index == index) {
    return parent;
  }
  for (auto &child : parent->children) {
    nodeFound = findNode(child, index);
    if (nodeFound) {
      break;
    }
  }
  return nodeFound;
}

vkglBSP::Node* vkglBSP::Model::nodeFromIndex(uint32_t index) {
  Node *nodeFound = nullptr;
  for (auto &node : nodes) {
    nodeFound = findNode(node, index);
    if (nodeFound) {
      break;
    }
  }
  return nodeFound;
}

void vkglBSP::Model::prepareNodeDescriptor(vkglBSP::Node *node,
    VkDescriptorSetLayout descriptorSetLayout) {
  if (node->mesh) {
    VkDescriptorSetAllocateInfo descriptorSetAllocInfo { };
    descriptorSetAllocInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorSetAllocInfo.descriptorPool = descriptorPool;
    descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;
    descriptorSetAllocInfo.descriptorSetCount = 1;
    VK_CHECK_RESULT(
        vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo,
            &node->mesh->uniformBuffer.descriptorSet));

    VkWriteDescriptorSet writeDescriptorSet { };
    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.dstSet = node->mesh->uniformBuffer.descriptorSet;
    writeDescriptorSet.dstBinding = 0;
    writeDescriptorSet.pBufferInfo = &node->mesh->uniformBuffer.descriptor;

    vkUpdateDescriptorSets(device->logicalDevice, 1, &writeDescriptorSet, 0,
        nullptr);
  }
  for (auto &child : node->children) {
    prepareNodeDescriptor(child, descriptorSetLayout);
  }
}

vkglBSP::QModel* vkglBSP::Model::modForName(const char *name, bool crash) {
  QModel *mod;

//  mod = modFindName (name);

  return modLoadModel(mod, crash);
}

/*
 ==================
 Mod_LoadModel

 Loads a model into the cache
 ==================
 */
vkglBSP::QModel* vkglBSP::Model::modLoadModel(vkglBSP::QModel *mod,
    bool crash) {
  byte *buf;
  byte stackbuf[1024];   // avoid dirtying the cache heap
  int mod_type;

//
// load the file
//
  buf = comLoadStackFile(mod->name, stackbuf, sizeof(stackbuf), &mod->path_id);
  if (!buf) {
    if (crash) {
      snprintf(errorBuff, sizeof(char) * 255, "modLoadModel: %s not found",
          mod->name);
      throw std::runtime_error(errorBuff);
    }
    return nullptr;
  }

//
// allocate a new model
//
  comFileBase(mod->name, loadname, sizeof(loadname));

  loadmodel = mod;

//
// fill it in
//

// call the apropriate loader
  mod->needload = false;

  mod_type = (buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
//  switch (mod_type) {
//  case IDPOLYHEADER:
//    Mod_LoadAliasModel(mod, buf);
//    break;
//
//  case IDSPRITEHEADER:
//    Mod_LoadSpriteModel(mod, buf);
//    break;

//default:
  modLoadBrushModel(mod, buf);
//    break;
//  }

  return mod;
}

// uses temp hunk if larger than bufsize
byte* vkglBSP::Model::comLoadStackFile(const char *path, void *buffer,
    int bufsize, unsigned int *path_id) {
  byte *buf;

  loadbuf = (byte*) buffer;
  loadsize = bufsize;
//  buf = comLoadFile(path, LOADFILE_STACK, path_id);

  return buf;
}

void vkglBSP::Model::comFileBase(const char *in, char *out, size_t outsize) {
  const char *dot, *slash, *s;

  s = in;
  slash = in;
  dot = NULL;
  while (*s) {
    if (*s == '/')
      slash = s + 1;
    if (*s == '.')
      dot = s;
    s++;
  }
  if (dot == NULL)
    dot = s;

  if (dot - slash < 2)
    q_strlcpy(out, "?model?", outsize);
  else {
    size_t len = dot - slash;
    if (len >= outsize)
      len = outsize - 1;
    memcpy(out, slash, len);
    out[len] = '\0';
  }
}

size_t vkglBSP::Model::q_strlcpy(char *dst, const char *src, size_t siz) {
  char *d = dst;
  const char *s = src;
  size_t n = siz;

  /* Copy as many bytes as will fit */
  if (n != 0) {
    while (--n != 0) {
      if ((*d++ = *s++) == '\0')
        break;
    }
  }

  /* Not enough room in dst, add NUL and traverse rest of src */
  if (n == 0) {
    if (siz != 0)
      *d = '\0'; /* NUL-terminate dst */
    while (*s++)
      ;
  }

  return (s - src - 1); /* count does not include NUL */
}

/*
 =================
 Mod_LoadBrushModel
 =================
 */
void vkglBSP::Model::modLoadBrushModel(QModel *mod, void *buffer) {
  int i, j;
  int bsp2;
  DHeader *header;
  DModel *bm;
  float radius; //johnfitz

  header = (DHeader*) buffer;

  //  mod->bspversion = LittleLong(header->version);
  mod->bspversion = header->version;

  std::cout << "bsp version =  " << mod->bspversion << std::endl;

  switch (mod->bspversion) {
  case BSPVERSION:
    bsp2 = false;
    break;
  case BSP2VERSION_2PSB:
    bsp2 = 1; //first iteration
    break;
  case BSP2VERSION_BSP2:
    bsp2 = 2; //sanitised revision
    break;
  default: {
    char buff[256];
    sprintf(buff,
        "Mod_LoadBrushModel: %s has wrong version number (%i should be %i)",
        mod->name, mod->bspversion, BSPVERSION);
    throw std::runtime_error(buff);
    break;
  }
  }

// swap all the lumps
  mod_base = (byte*) header;
//
//  for (i = 0; i < (int) sizeof(DHeader) / 4; i++)
//    ((int*) header)[i] = (((int*) header)[i]);

//// load into heap
//
  modLoadVertexes(&header->lumps[LUMP_VERTEXES]);
  modLoadEdges(&header->lumps[LUMP_EDGES]);
  modLoadSurfedges(&header->lumps[LUMP_SURFEDGES]);
  modLoadTextures(&header->lumps[LUMP_TEXTURES]);
//  Mod_LoadLighting(&header->lumps[LUMP_LIGHTING]);
//  Mod_LoadPlanes(&header->lumps[LUMP_PLANES]);
  modLoadTexInfo(&header->lumps[LUMP_TEXINFO]);
  modLoadFaces(&header->lumps[LUMP_FACES]);
//  Mod_LoadMarksurfaces(&header->lumps[LUMP_MARKSURFACES], bsp2);
//
//  if (!bsp2 && external_vis.value && sv.modelname[0]
//      && !q_strcasecmp(loadname, sv.name)) {
//    FILE *fvis;
//    Con_DPrintf("trying to open external vis file\n");
//    fvis = Mod_FindVisibilityExternal();
//    if (fvis) {
//      int mark = Hunk_LowMark();
//      loadmodel->leafs = NULL;
//      loadmodel->numleafs = 0;
//      Con_DPrintf("found valid external .vis file for map\n");
//      loadmodel->visdata = Mod_LoadVisibilityExternal(fvis);
//      if (loadmodel->visdata) {
//        Mod_LoadLeafsExternal(fvis);
//      }
//      fclose(fvis);
//      if (loadmodel->visdata && loadmodel->leafs && loadmodel->numleafs) {
//        goto visdone;
//      }
//      Hunk_FreeToLowMark(mark);
//      Con_DPrintf("External VIS data failed, using standard vis.\n");
//    }
//  }
//
//  Mod_LoadLeafs(&header->lumps[LUMP_LEAFS], bsp2);
//  visdone: Mod_LoadNodes(&header->lumps[LUMP_NODES], bsp2);
//  Mod_LoadClipnodes(&header->lumps[LUMP_CLIPNODES], bsp2);
//  Mod_LoadEntities(&header->lumps[LUMP_ENTITIES]);
//  Mod_LoadSubmodels(&header->lumps[LUMP_MODELS]);
//
//  Mod_PrepareSIMDData();
//  Mod_MakeHull0();
//
//  mod->numframes = 2;   // regular and alternate animation
//
//  Mod_CheckWaterVis();
//
////
//// set up the submodels (FIXME: this is confusing)
////
//
//  // johnfitz -- okay, so that i stop getting confused every time i look at this loop, here's how it works:
//  // we're looping through the submodels starting at 0.  Submodel 0 is the main model, so we don't have to
//  // worry about clobbering data the first time through, since it's the same data.  At the end of the loop,
//  // we create a new copy of the data to use the next time through.
//  for (i = 0; i < mod->numsubmodels; i++) {
//    bm = &mod->submodels[i];
//
//    mod->hulls[0].firstclipnode = bm->headnode[0];
//    for (j = 1; j < MAX_MAP_HULLS; j++) {
//      mod->hulls[j].firstclipnode = bm->headnode[j];
//      mod->hulls[j].lastclipnode = mod->numclipnodes - 1;
//    }
//
//    mod->firstmodelsurface = bm->firstface;
//    mod->nummodelsurfaces = bm->numfaces;
//
//    VectorCopy(bm->maxs, mod->maxs);
//    VectorCopy(bm->mins, mod->mins);
//
//    //johnfitz -- calculate rotate bounds and yaw bounds
//    radius = RadiusFromBounds(mod->mins, mod->maxs);
//    mod->rmaxs[0] = mod->rmaxs[1] = mod->rmaxs[2] = mod->ymaxs[0] =
//        mod->ymaxs[1] = mod->ymaxs[2] = radius;
//    mod->rmins[0] = mod->rmins[1] = mod->rmins[2] = mod->ymins[0] =
//        mod->ymins[1] = mod->ymins[2] = -radius;
//    //johnfitz
//
//    //johnfitz -- correct physics cullboxes so that outlying clip brushes on doors and stuff are handled right
//    if (i > 0 || strcmp(mod->name, sv.modelname) != 0) //skip submodel 0 of sv.worldmodel, which is the actual world
//        {
//      // start with the hull0 bounds
//      VectorCopy(mod->maxs, mod->clipmaxs);
//      VectorCopy(mod->mins, mod->clipmins);
//
//      // process hull1 (we don't need to process hull2 becuase there's
//      // no such thing as a brush that appears in hull2 but not hull1)
//      //Mod_BoundsFromClipNode (mod, 1, mod->hulls[1].firstclipnode); // (disabled for now becuase it fucks up on rotating models)
//    }
//    //johnfitz
//
//    mod->numleafs = bm->visleafs;
//
//    if (i < mod->numsubmodels - 1) { // duplicate the basic information
//      char name[10];
//
//      sprintf(name, "*%i", i + 1);
//      loadmodel = modFindName(name);
//      *loadmodel = *mod;
//      strcpy(loadmodel->name, name);
//      mod = loadmodel;
//    }
//  }
}

void vkglBSP::Model::modLoadVertexes(Lump *l) {
  DVertex *in;
  int i, count;

  in = (DVertex*) (mod_base + l->fileofs);
  if (l->filelen % sizeof(*in)) {
    snprintf(errorBuff, 255, "modLoadVertexes: funny lump size in %s",
        loadmodel->name);
    throw std::runtime_error(errorBuff);
  }

  count = l->filelen / sizeof(*in);

  for (i = 0; i < count; i++, in++) {
    MVertex v;
    v.position.x = in->point[0];
    v.position.y = -in->point[2];
    v.position.z = -in->point[1];
    loadmodel->vertexes.push_back(v);
  }

  loadmodel->numvertexes = count;
}

void vkglBSP::Model::init() {
  char pakfile[MAX_OSPATH];
  snprintf(pakfile, sizeof(pakfile), "id1/pak1.pak");
  Pack pak = comLoadPackFile(pakfile);
  pak0 = &pak;

  PackFile startBSP = comFindFile("maps/start.bsp");

  byte *bspBytes = new byte[sizeof(byte) * (startBSP.filelen + 1)];

  fseek(pak.handle, startBSP.filepos, SEEK_SET);
  std::cout << "fileln = " << startBSP.filelen;

  fread(bspBytes, 1, startBSP.filelen, pak.handle);
  std::cout << " filepos = " << startBSP.filepos << std::endl;

  int modType = (bspBytes[0] | (bspBytes[1] << 8) | (bspBytes[2] << 16)
      | (bspBytes[3] << 24));
  std::cout << "modType " << modType << std::endl;

  loadmodel = &mod;
  mod_base = bspBytes;

  modLoadBrushModel(&mod, bspBytes);

  std::vector<MVertex> localVertex;
  std::vector<uint32_t> localIndex;

  std::cout << "Surface size = " << loadmodel->surfaces.size() << std::endl;

  int baseIndex = 0;

  for (auto &s : loadmodel->surfaces) {

    GlPoly *p = &s.polys;

    baseIndex = s.firstedge;

    std::cout << "baseIndex = " << baseIndex << std::endl;

    const int numverts = p->verts.size();
    const int vertexCount = p->numverts - 2;

    std::cout << "numVerts = " << numverts << std::endl;
    std::cout << "vertexCount = " << vertexCount << std::endl;
    for (const auto &v : p->verts) {
      MVertex mv;
      mv.position = v;
      localVertex.push_back(mv);
    }

    for (int i = 0; i < vertexCount; ++i) {
      localIndex.push_back(baseIndex);
      localIndex.push_back(baseIndex + i + 1);
      localIndex.push_back(baseIndex + i + 2);
    }
  }

  loadmodel->vertexes = localVertex;
  loadmodel->edges = localIndex;
  std::cout << "Done Loading surfaces vertex size = " << localVertex.size()
      << " index size = " << localIndex.size() << std::endl;

  delete[] bspBytes;
  fclose(pak.handle);

}

vkglBSP::Pack vkglBSP::Model::comLoadPackFile(const char *packfile) {
  int packhandle;
  Pack pack;
  DPackHeader header;
  DPackFile info[MAX_FILES_IN_PACK];
  FILE *f;
  int retval;
  int numPackFiles;

  f = fopen(packfile, "rb");

  if (f == nullptr) {
    snprintf(errorBuff, sizeof(char) * 256, "sysFileOpenRead failed! %s",
        packfile);
    throw std::runtime_error(errorBuff);
  }

  retval = sysFileLength(f);

  fread((void*) &header, 1, sizeof(header), f);

  if (header.id[0] != 'P' || header.id[1] != 'A' || header.id[2] != 'C'
      || header.id[3] != 'K') {
    snprintf(errorBuff, sizeof(char) * 256, "%s is not a packfile", packfile);
    throw std::runtime_error(errorBuff);
  }

  numPackFiles = header.dirlen / sizeof(DPackFile);

  if (header.dirlen < 0 || header.dirofs < 0) {
    snprintf(errorBuff, sizeof(char) * 256,
        "Invalid packfile %s (dirlen: %i, dirofs: %i)", packfile, header.dirlen,
        header.dirofs);
    throw std::runtime_error(errorBuff);
  }

  if (!numPackFiles) {
    std::cerr << "WARNING: " << packfile << " has no files, ignored"
        << std::endl;
    fclose(f);
    return pack;
  }

  if (numPackFiles > MAX_FILES_IN_PACK) {
    snprintf(errorBuff, sizeof(char) * 256, "Max files of %d in %s pack file",
        numPackFiles, packfile);
    throw std::runtime_error(errorBuff);
  }

  fseek(f, header.dirofs, SEEK_SET);
  fread((void*) info, 1, header.dirlen, f);

  std::vector<PackFile> newFiles;
  // parse the directory
  for (int i = 0; i < numPackFiles; i++) {
    PackFile newFile;
    strncpy(newFile.name, info[i].name, sizeof(newFile.name));
    newFile.filepos = info[i].filepos;
    newFile.filelen = info[i].filelen;
    newFiles.push_back(newFile);
    std::cout << "Found: " << newFile.name << std::endl;
  }

  strncpy(pack.filename, packfile, sizeof(pack.filename));

  pack.handle = f;
  pack.numfiles = numPackFiles;
  pack.files = newFiles;

  return pack;
}

long vkglBSP::Model::sysFileLength(FILE *f) {
  long pos, end;

  pos = ftell(f);
  fseek(f, 0, SEEK_END);
  end = ftell(f);
  fseek(f, pos, SEEK_SET);

  return end;
}

vkglBSP::PackFile vkglBSP::Model::comFindFile(const char *filename) {
  for (const auto &f : this->pak0->files) {
    if (strcmp(f.name, filename) != 0) {
      continue;
    }

    return f;
  }

  std::cerr << "File " << filename << " not found" << std::endl;
  return PackFile();
}

void vkglBSP::Model::modLoadEdges(Lump *l) {
  int i, count;

  DSEdge *in = (DSEdge*) (mod_base + l->fileofs);

  if (l->filelen % sizeof(*in)) {
    snprintf(errorBuff, 255, "modLoadEdges: funny lump size in %s",
        loadmodel->name);
    throw std::runtime_error(errorBuff);
  }

  count = l->filelen / sizeof(*in);

  for (i = 0; i < count; i++, in++) {
    MEdge edge;
    edge.v[0] = in->v[0];
    edge.v[1] = in->v[1];

    loadmodel->medges.push_back(edge);
  }

  loadmodel->numedges = count;
  std::cout << "Edges computed" << std::endl;
}

void vkglBSP::Model::modLoadSurfedges(Lump *l) {
  int i, count;
  int *in;
  std::vector<int> out;

  in = (int*) (mod_base + l->fileofs);
  if (l->filelen % sizeof(*in)) {
    snprintf(errorBuff, 255, "modLoadSurfedges: funny lump size in %s",
        loadmodel->name);
    throw std::runtime_error(errorBuff);
  }
  count = l->filelen / sizeof(*in);
  loadmodel->numsurfedges = count;

  for (i = 0; i < count; i++) {
    out.push_back(in[i]);
  }

  loadmodel->surfedges = out;
  std::cout << "surface completed" << std::endl;
}

void vkglBSP::Model::modLoadFaces(Lump *l) {
  DSFace *ins;
  DLFace *inl;
  int i, count, surfnum, lofs;
  int planenum, side, texinfon;

  ins = (DSFace*) (mod_base + l->fileofs);
  inl = nullptr;

  if (l->filelen % sizeof(*ins)) {
    snprintf(errorBuff, 255, "modLoadFaces: funny lump size in %s",
        loadmodel->name);
    throw std::runtime_error(errorBuff);
  }

  count = l->filelen / sizeof(*ins);

  loadmodel->numsurfaces = count;

  MTexInfo *mti = loadmodel->texinfo.data();

  for (surfnum = 0; surfnum < count; surfnum++) {
    MSurface out;
    out.firstedge = ins->firstedge;
    out.numedges = ins->numedges;
    planenum = ins->planenum;
    side = ins->side;
    texinfon = ins->texinfo;
    lofs = ins->lightofs;

    ins++;

    out.flags = 0;

    if (side)
      out.flags |= SURF_PLANEBACK;

//    out.plane = loadmodel->planes + planenum;

    out.texinfo = mti + texinfon;

    std::cout << "modLoadFaces: " << out.texinfo->texture.name << std::endl;

//    CalcSurfaceExtents(out);

    // lighting info
//    if (lofs == -1)
//      out.samples = nullptr;
//    else
//      out.samples = loadmodel->lightdata + (lofs * 3); //johnfitz -- lit support via lordhavoc (was "+ i")

    //johnfitz -- this section rewritten

//    if (!q_strncasecmp(out.texinfo->texture.name, "sky", 3)) // sky surface //also note -- was Q_strncmp, changed to match qbsp
//        {
//      out.flags |= (SURF_DRAWSKY | SURF_DRAWTILED);
//      modPolyForUnlitSurface(&out); //no more subdivision
//
//    } else if (out.texinfo->texture.name[0] == '*') // warp surface
//        {
//      out.flags |= (SURF_DRAWTURB | SURF_DRAWTILED);
//
//      // detect special liquid types
//      if (!strncmp(out.texinfo->texture.name, "*lava", 5))
//        out.flags |= SURF_DRAWLAVA;
//      else if (!strncmp(out.texinfo->texture.name, "*slime", 6))
//        out.flags |= SURF_DRAWSLIME;
//      else if (!strncmp(out.texinfo->texture.name, "*tele", 5))
//        out.flags |= SURF_DRAWTELE;
//      else
//        out.flags |= SURF_DRAWWATER;
////
//      modPolyForUnlitSurface(&out);
//      glSubdivideSurface(&out);
//
//    } else if (out.texinfo->texture.name[0] == '{') // ericw -- fence textures
//        {
//      out.flags |= SURF_DRAWFENCE;
//    } else if (out.texinfo->flags & TEX_MISSING) // texture is missing from bsp
//    {
//      if (out.samples) //lightmapped
//      {
//        out.flags |= SURF_NOTEXTURE;
//      } else // not lightmapped
//      {
//        out.flags |= (SURF_NOTEXTURE | SURF_DRAWTILED);
//        modPolyForUnlitSurface(&out);
//      }
//    }
    if (strncmp(out.texinfo->texture.name, "trigger", 7)) {
      modPolyForUnlitSurface(&out);
      loadmodel->surfaces.push_back(out);
    }
  }

}

void vkglBSP::Model::boundPoly(int numverts, std::vector<glm::vec3> &verts,
    glm::vec3 & mins, glm::vec3 &maxs) {
  int i, j;
  float *v;

  mins[0] = mins[1] = mins[2] = FLT_MAX;
  maxs[0] = maxs[1] = maxs[2] = -FLT_MAX;
  v = &verts[0].x;
  for (i = 0; i < numverts; i++)
    for (j = 0; j < 3; j++, v++) {
      if (*v < mins[j])
        mins[j] = *v;
      if (*v > maxs[j])
        maxs[j] = *v;
    }
}

void vkglBSP::Model::subdividePolygon(int numverts,
    std::vector<glm::vec3> &verts) {
  int i, j, k;
  glm::vec3 mins, maxs;
  float m;
  float *v;
  std::vector<glm::vec3> front, back;
  int f, b;
  std::vector<glm::vec3> dist;
  float frac;
  GlPoly *poly;
  float s, t;

  if (numverts > 60) {
    snprintf(errorBuff, 255, "numverts = %i", numverts);
    throw std::runtime_error(errorBuff);
  }
  dist.resize(numverts);
  front.resize(64);
  back.resize(64);




  boundPoly(numverts, verts, mins, maxs);

  for (i = 0; i < 3; i++) {
    m = (mins[i] + maxs[i]) * 0.5;
    m = 128 * floor(m / 128 + 0.5);
    if (maxs[i] - m < 8)
      continue;
    if (m - mins[i] < 8)
      continue;

    // cut it
    v = &verts[i].x;

    float * dist2 = &dist[0].x;

    for (j = 0; j < numverts; j++, v+= 3)
      dist2[j] = *v - m;

    // wrap cases
    dist2[j] = dist2[0];

    verts.at(i) = glm::vec3(*(v+1), *(v+2), *(v+3));

    f = b = 0;

    std::cout << "2 " << std::endl;

    for (j = 0; j < numverts; j++, v+= 3) {
      if (dist2[j] >= 0) {
        front[f] =  glm::vec3(*(v+1), *(v+2), *(v+3));
        f++;
      }
      if (dist2[j] <= 0) {
        back[b] =  glm::vec3(*(v+1), *(v+2), *(v+3));
        b++;
      }
      if (dist2[j] == 0 || dist2[j + 1] == 0)
        continue;
      if ((dist2[j] > 0) != (dist2[j + 1] > 0)) {
        // clip point
        frac = dist2[j] / (dist2[j] - dist2[j + 1]);

        front[f].x = back[b].x = v[0] + frac * (v[3 + 0] - v[0]);
        front[f].y = back[b].y = v[1] + frac * (v[3 + 1] - v[1]);
        front[f].z = back[b].z = v[2] + frac * (v[3 + 2] - v[2]);

        f++;
        b++;
      }
    }
    std::cout << "3 " << std::endl;
    subdividePolygon(f, front);
    subdividePolygon(b, back);
    return;
  }

  std::cout << "4 " << std::endl;

  GlPoly poly2;
  poly2.next = warpface->polys.next;
  poly2.numverts = numverts;


  for (i = 0; i < numverts; i++) {
    std::cout << verts.size() << " - vert index = " << i << " out of " << numverts << std::endl;

    poly2.verts.push_back(glm::vec4(verts[i].x, verts[i].y, verts[i].z, 0));
    s = DotProduct(verts[i], warpface->texinfo->vecs[0]);
    t = DotProduct(verts[i], warpface->texinfo->vecs[1]);
//    poly2.verts[i][3] = s;
//    poly2.verts[i][4] = t;
  }
  std::cout << "5 " << std::endl;
  warpface->polys.next = &poly2;
  std::cout << "6 " << std::endl;
  polygons.push_back(poly2);
}

void vkglBSP::Model::glSubdivideSurface(MSurface *fa) {
  std::vector<glm::vec3> verts;
  int i;

  warpface = fa;

  //the first poly in the chain is the undivided poly for newwater rendering.
  //grab the verts from that.
  for (i = 0; i < fa->polys.numverts; i++)
    verts.push_back(fa->polys.verts[i]);

  subdividePolygon(fa->polys.numverts, verts);
}

void vkglBSP::Model::modPolyForUnlitSurface(MSurface *fa) {
  int numverts, i, lindex;

  float texscale;

//  if (fa->flags & (SURF_DRAWTURB | SURF_DRAWSKY))
//    texscale = (1.0/128.0); //warp animation repeats every 128
//  else
  texscale = (1.0 / 32.0); //to match r_notexture_mip

  // convert edges back to a normal polygon
  numverts = 0;

  std::vector<MEdge> pedges = loadmodel->medges;

  for (i = 0; i < fa->numedges; i++) {
    glm::vec4 vec;

    lindex = loadmodel->surfedges.at(fa->firstedge + i);
    if (lindex > 0) {
      MEdge idx = pedges.at(lindex);
      vec = loadmodel->vertexes.at(idx.v[1]).position;
    } else {
      MEdge idx = pedges.at(-lindex);
      vec = loadmodel->vertexes.at(idx.v[0]).position;
    }

    fa->polys.verts.push_back(vec);
    numverts++;
  }
  fa->polys.numverts = numverts;

}

void vkglBSP::Model::modLoadTextures(Lump *l) {
  int i, j, pixels, num, maxanim, altmax;
  MipTex *mt;
  QTexture tx2;
  QTexture *anims[10];
  QTexture *altanims[10];
  DMipTexLump *m;
//johnfitz -- more variables
  char texturename[64];
  int nummiptex;
  src_offset_t offset;
  int mark, fwidth, fheight;
  char filename[MAX_OSPATH], filename2[MAX_OSPATH], mapname[MAX_OSPATH];
  byte *data;
  extern byte *hunk_base;
//johnfitz

  //johnfitz -- don't return early if no textures; still need to create dummy texture
  if (!l->filelen) {
    std::cout << "modLoadTextures: no textures in bsp file" << std::endl;
    nummiptex = 0;
  } else {
    m = (DMipTexLump*) (mod_base + l->fileofs);
    nummiptex = m->nummiptex;
  }
  //johnfitz

  loadmodel->numtextures = nummiptex + 2; //johnfitz -- need 2 dummy texture chains for missing textures

  for (i = 0; i < nummiptex; i++) {
    QTexture tx;

    if (m->dataofs[i] == -1)
      continue;

    mt = (MipTex*) ((byte*) m + m->dataofs[i]);

    if ((mt->width & 15) || (mt->height & 15)) {
      snprintf(errorBuff, 255, "Texture %s is not 16 aligned", mt->name);
      throw std::runtime_error(errorBuff);
    }

    pixels = mt->width * mt->height / 64 * 85;

    memcpy(tx.name, mt->name, sizeof(tx.name));
    tx.width = mt->width;
    tx.height = mt->height;
    std::cout << "Loading texture.." << tx.name << std::endl;

    for (j = 0; j < MIPLEVELS; j++)
      tx.offsets[j] = mt->offsets[j] + sizeof(QTexture) - sizeof(MipTex);
    // the pixels immediately follow the structures

    // ericw -- check for pixels extending past the end of the lump.
    // appears in the wild; e.g. jam2_tronyn.bsp (func_mapjam2),
    // kellbase1.bsp (quoth), and can lead to a segfault if we read past
    // the end of the .bsp file buffer
    if (((byte*) (mt + 1) + pixels) > (mod_base + l->fileofs + l->filelen)) {
      std::cout << "Texture " << mt->name << " extends past end of lump"
          << std::endl;
      pixels = q_max(0,
          (mod_base + l->fileofs + l->filelen) - (byte* ) (mt + 1));
    }

//    QTexture * qt = &tx;
//    memcpy(qt + 1, mt + 1, pixels);

    tx.update_warp = false; //johnfitz
    tx.warpimage = nullptr; //johnfitz
    tx.fullbright = nullptr; //johnfitz

    //johnfitz -- lots of changes
//    if (!isDedicated) //no texture uploading for dedicated server
//    {
//      if (!q_strncasecmp(tx->name,"sky",3)) //sky texture //also note -- was Q_strncmp, changed to match qbsp
//        Sky_LoadTexture (tx);
//      else if (tx->name[0] == '*') //warping texture
//      {
//        //external textures -- first look in "textures/mapname/" then look in "textures/"
//        mark = Hunk_LowMark();
//        COM_StripExtension (loadmodel->name + 5, mapname, sizeof(mapname));
//        q_snprintf (filename, sizeof(filename), "textures/%s/#%s", mapname, tx->name+1); //this also replaces the '*' with a '#'
//        data = Image_LoadImage (filename, &fwidth, &fheight);
//        if (!data)
//        {
//          q_snprintf (filename, sizeof(filename), "textures/#%s", tx->name+1);
//          data = Image_LoadImage (filename, &fwidth, &fheight);
//        }
//
//        //now load whatever we found
//        if (data) //load external image
//        {
//          q_strlcpy (texturename, filename, sizeof(texturename));
//          tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, fwidth, fheight,
//            SRC_RGBA, data, filename, 0, TEXPREF_NONE);
//        }
//        else //use the texture from the bsp file
//        {
//          q_snprintf (texturename, sizeof(texturename), "%s:%s", loadmodel->name, tx->name);
//          offset = (src_offset_t)(mt+1) - (src_offset_t)mod_base;
//          tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, tx->width, tx->height,
//            SRC_INDEXED, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_NONE);
//        }
//
//        //now create the warpimage, using dummy data from the hunk to create the initial image
//        Hunk_Alloc (WARPIMAGESIZE*WARPIMAGESIZE*4); //make sure hunk is big enough so we don't reach an illegal address
//        Hunk_FreeToLowMark (mark);
//        q_snprintf (texturename, sizeof(texturename), "%s_warp", texturename);
//        tx->warpimage = TexMgr_LoadImage (loadmodel, texturename, WARPIMAGESIZE,
//          WARPIMAGESIZE, SRC_RGBA, hunk_base, "", (src_offset_t)hunk_base, TEXPREF_NOPICMIP | TEXPREF_WARPIMAGE);
//        tx->update_warp = true;
//      }
//      else //regular texture
//      {
//        // ericw -- fence textures
    int extraflags;
//
//        extraflags = 0;
    if (tx.name[0] == '{')
      extraflags |= TEXPREF_ALPHA;
//        // ericw
//
//        //external textures -- first look in "textures/mapname/" then look in "textures/"
//        mark = Hunk_LowMark ();
//        comStripExtension (loadmodel->name + 5, mapname, sizeof(mapname));
//        snprintf (filename, sizeof(filename), "textures/%s/%s", mapname, tx.name);
//        data = Image_LoadImage (filename, &fwidth, &fheight);
//        if (!data)
//        {
//          q_snprintf (filename, sizeof(filename), "textures/%s", tx->name);
//          data = Image_LoadImage (filename, &fwidth, &fheight);
//        }
//
//        //now load whatever we found
//        if (data) //load external image
//        {
//          tx->gltexture = TexMgr_LoadImage (loadmodel, filename, fwidth, fheight,
//            SRC_RGBA, data, filename, 0, TEXPREF_MIPMAP | extraflags );
//
//          //now try to load glow/luma image from the same place
//          Hunk_FreeToLowMark (mark);
//          q_snprintf (filename2, sizeof(filename2), "%s_glow", filename);
//          data = Image_LoadImage (filename2, &fwidth, &fheight);
//          if (!data)
//          {
//            q_snprintf (filename2, sizeof(filename2), "%s_luma", filename);
//            data = Image_LoadImage (filename2, &fwidth, &fheight);
//          }
//
//          if (data)
//            tx->fullbright = TexMgr_LoadImage (loadmodel, filename2, fwidth, fheight,
//              SRC_RGBA, data, filename, 0, TEXPREF_MIPMAP | extraflags );
//        }
//        else //use the texture from the bsp file
//        {
//          q_snprintf (texturename, sizeof(texturename), "%s:%s", loadmodel->name, tx->name);
//          offset = (src_offset_t)(mt+1) - (src_offset_t)mod_base;
//          if (Mod_CheckFullbrights ((byte *)(tx+1), pixels))
//          {
//            tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, tx->width, tx->height,
//              SRC_INDEXED, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_MIPMAP | TEXPREF_NOBRIGHT | extraflags);
//            q_snprintf (texturename, sizeof(texturename), "%s:%s_glow", loadmodel->name, tx->name);
//            tx->fullbright = TexMgr_LoadImage (loadmodel, texturename, tx->width, tx->height,
//              SRC_INDEXED, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_MIPMAP | TEXPREF_FULLBRIGHT | extraflags);
//          }
//          else
//          {
//            tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, tx->width, tx->height,
//              SRC_INDEXED, (byte *)(tx+1), loadmodel->name, offset, TEXPREF_MIPMAP | extraflags);
//          }
//        }
//        Hunk_FreeToLowMark (mark);
//      }
//    }
    //johnfitz

    loadmodel->textures.push_back(tx);
  }

  //johnfitz -- last 2 slots in array should be filled with dummy textures
//  loadmodel->textures[loadmodel->numtextures-2] = r_notexture_mip; //for lightmapped surfs
//  loadmodel->textures[loadmodel->numtextures-1] = r_notexture_mip2; //for SURF_DRAWTILED surfs
//
////
//// sequence the animations
////
//  for (i=0 ; i<nummiptex ; i++)
//  {
//    tx = loadmodel->textures[i];
//    if (!tx || tx->name[0] != '+')
//      continue;
//    if (tx->anim_next)
//      continue; // allready sequenced
//
//  // find the number of frames in the animation
//    memset (anims, 0, sizeof(anims));
//    memset (altanims, 0, sizeof(altanims));
//
//    maxanim = tx->name[1];
//    altmax = 0;
//    if (maxanim >= 'a' && maxanim <= 'z')
//      maxanim -= 'a' - 'A';
//    if (maxanim >= '0' && maxanim <= '9')
//    {
//      maxanim -= '0';
//      altmax = 0;
//      anims[maxanim] = tx;
//      maxanim++;
//    }
//    else if (maxanim >= 'A' && maxanim <= 'J')
//    {
//      altmax = maxanim - 'A';
//      maxanim = 0;
//      altanims[altmax] = tx;
//      altmax++;
//    }
//    else
//      Sys_Error ("Bad animating texture %s", tx->name);
//
//    for (j=i+1 ; j<nummiptex ; j++)
//    {
//      tx2 = loadmodel->textures[j];
//      if (!tx2 || tx2->name[0] != '+')
//        continue;
//      if (strcmp (tx2->name+2, tx->name+2))
//        continue;
//
//      num = tx2->name[1];
//      if (num >= 'a' && num <= 'z')
//        num -= 'a' - 'A';
//      if (num >= '0' && num <= '9')
//      {
//        num -= '0';
//        anims[num] = tx2;
//        if (num+1 > maxanim)
//          maxanim = num + 1;
//      }
//      else if (num >= 'A' && num <= 'J')
//      {
//        num = num - 'A';
//        altanims[num] = tx2;
//        if (num+1 > altmax)
//          altmax = num+1;
//      }
//      else
//        Sys_Error ("Bad animating texture %s", tx->name);
//    }
//
//#define ANIM_CYCLE  2
//  // link them all together
//    for (j=0 ; j<maxanim ; j++)
//    {
//      tx2 = anims[j];
//      if (!tx2)
//        Sys_Error ("Missing frame %i of %s",j, tx->name);
//      tx2->anim_total = maxanim * ANIM_CYCLE;
//      tx2->anim_min = j * ANIM_CYCLE;
//      tx2->anim_max = (j+1) * ANIM_CYCLE;
//      tx2->anim_next = anims[ (j+1)%maxanim ];
//      if (altmax)
//        tx2->alternate_anims = altanims[0];
//    }
//    for (j=0 ; j<altmax ; j++)
//    {
//      tx2 = altanims[j];
//      if (!tx2)
//        Sys_Error ("Missing frame %i of %s",j, tx->name);
//      tx2->anim_total = altmax * ANIM_CYCLE;
//      tx2->anim_min = j * ANIM_CYCLE;
//      tx2->anim_max = (j+1) * ANIM_CYCLE;
//      tx2->anim_next = altanims[ (j+1)%altmax ];
//      if (maxanim)
//        tx2->alternate_anims = anims[0];
//    }
//  }

  std::cout << "Done loading textures " << std::endl;
}

void vkglBSP::Model::modLoadTexInfo(Lump *l) {
  TexInfo *in;
  int i, j, count, miptex;
  int missing = 0; //johnfitz

  in = (TexInfo*) (mod_base + l->fileofs);
  if (l->filelen % sizeof(*in)) {
    snprintf(errorBuff, 255, "MOD_LoadBmodel: funny lump size in %s",
        loadmodel->name);
    throw std::runtime_error(errorBuff);
  }

  count = l->filelen / sizeof(*in);

  loadmodel->numtexinfo = count;

  for (i = 0; i < count; i++, in++) {
    MTexInfo out;
    for (j = 0; j < 4; j++) {
      out.vecs[0][j] = (in->vecs[0][j]);
      out.vecs[1][j] = (in->vecs[1][j]);
    }

    miptex = (in->miptex);
    out.flags = (in->flags);

    //johnfitz -- rewrote this section
    if (miptex >= loadmodel->numtextures - 1) {
      if (out.flags & TEX_SPECIAL)
        out.texture = loadmodel->textures[loadmodel->numtextures - 1];
      else
        out.texture = loadmodel->textures[loadmodel->numtextures - 2];
      out.flags |= TEX_MISSING;
      missing++;
    } else {
      out.texture = loadmodel->textures[miptex];
    }
    //johnfitz

    std::cout << "MTextInfo = " << out.vecs[0][2] << std::endl;

    loadmodel->texinfo.push_back(out);

  }

  //johnfitz: report missing textures
  if (missing && loadmodel->numtextures > 1) {
    std::cout << "Mod_LoadTexinfo: " << missing
        << " texture(s) missing from BSP file\n" << std::endl;
  }

  //johnfitz
}

