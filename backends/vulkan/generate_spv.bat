glslangValidator -V --target-env vulkan1.4 --vn "g_vertex_shader_bytecode" -o  "glsl_shader.vert.h" "glsl_shader.vert"
glslangValidator -V --target-env vulkan1.4 --vn "g_fragment_shader_bytecode" -o "glsl_shader.frag.h" "glsl_shader.frag"

pause