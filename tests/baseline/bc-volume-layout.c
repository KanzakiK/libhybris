/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "../../hybris/vulkan/compat/bc_image.c"

int main(void)
{
    struct hybris_bc_image image;
    VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_3D, .format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
        .extent = {9, 7, 5}, .mipLevels = 4, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
    assert(hybris_bc_image_describe(&info, &image) == VK_SUCCESS);
    assert(image.block_size == 272);
    assert(image.mip_offset[1] == 240 && image.mip_offset[2] == 256 && image.mip_offset[3] == 264);
    assert(hybris_bc_image_slice_size(&image, 0) == 48);
    assert(hybris_bc_image_layer_size(&image, 0) == 240);
    assert(hybris_bc_image_extent(&image, 1).depth == 2);
    assert(hybris_bc_image_extent(&image, 3).depth == 1);
    info.extent = (VkExtent3D){1, 1, 17};
    info.mipLevels = 5;
    assert(hybris_bc_image_describe(&info, &image) == VK_SUCCESS);
    assert(image.block_size == 256); /* depth controls the mip limit too */
    info.arrayLayers = 2;
    assert(hybris_bc_image_describe(&info, &image) == VK_ERROR_FORMAT_NOT_SUPPORTED);
    info.arrayLayers = 1;
    info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    assert(hybris_bc_image_describe(&info, &image) == VK_ERROR_FORMAT_NOT_SUPPORTED);
    info.flags = 0;
    info.imageType = VK_IMAGE_TYPE_2D;
    assert(hybris_bc_image_describe(&info, &image) == VK_ERROR_FORMAT_NOT_SUPPORTED);
    info.extent = (VkExtent3D){9, 7, 1};
    info.arrayLayers = 3;
    info.mipLevels = 4;
    assert(hybris_bc_image_describe(&info, &image) == VK_SUCCESS);
    assert(image.block_size == 216);
    assert(hybris_bc_image_layer_size(&image, 0) == 48);
    assert(image.mip_offset[1] == 144 && image.mip_offset[3] == 192);
    puts("PASS: 3D mip/slice layout, invalid dimensions, 2D array regression");
}
