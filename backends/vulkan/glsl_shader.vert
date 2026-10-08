#version 460

#extension GL_EXT_nonuniform_qualifier: require
#extension GL_EXT_scalar_block_layout: require
#extension GL_EXT_buffer_reference2: require

struct Vertex
{
    vec2 position;
    vec2 texCoord;
    vec4 colour;
};

layout (buffer_reference, scalar) readonly buffer VertexBuffer { Vertex vertices[]; };

layout (push_constant) uniform PushData
{
    VertexBuffer vertexBuffer;

    vec2 scale;
    vec2 translate;

    uint textureHeapSlot;
    uint samplerHeapSlot;
} pcs;

layout (location = 0u) out vec4 o_Colour;
layout (location = 1u) out vec2 o_TexCoord;

void main()
{
    Vertex vertex = pcs.vertexBuffer.vertices[gl_VertexIndex];

    o_Colour = vertex.colour;
    o_TexCoord = vertex.texCoord;
    gl_Position = vec4(vertex.position * pcs.scale + pcs.translate, 0.0f, 1.0f);
}
