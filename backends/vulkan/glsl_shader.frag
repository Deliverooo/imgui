#version 460

#extension GL_EXT_nonuniform_qualifier: require
#extension GL_EXT_descriptor_heap: require
#extension GL_EXT_scalar_block_layout: require
#extension GL_EXT_buffer_reference2: require
#extension GL_EXT_shader_explicit_arithmetic_types_int64: require

layout (location = 0u) in vec4 v_Colour;
layout (location = 1u) in vec2 v_TexCoord;

layout (descriptor_heap) uniform texture2D texture2DHeap[];
layout (descriptor_heap) uniform sampler samplerHeap[];

#define SAMPLE_TEXTURE(__textureId, __samplerId, __texCoord) texture(sampler2D(texture2DHeap[__textureId], samplerHeap[__samplerId]), __texCoord)

layout (push_constant) uniform PushData
{
    uint64_t vertexBuffer;
    
    vec2 scale;
    vec2 translate;

    uint textureHeapSlot;
    uint samplerHeapSlot;
} pcs;

layout (location = 0u) out vec4 o_Colour;

void main()
{
    o_Colour = v_Colour * SAMPLE_TEXTURE(pcs.textureHeapSlot, pcs.samplerHeapSlot, v_TexCoord);
}
