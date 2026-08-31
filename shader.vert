#version 450
layout(push_constant) uniform PushConstants {
    mat4 mvpMatrix;
} pushConstants;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;

void main() {
    gl_Position = pushConstants.mvpMatrix * vec4(inPosition, 1.0);
    // Invert Y for Vulkan (since Unity uses OpenGL style Y-up but Vulkan is Y-down)
    gl_Position.y = -gl_Position.y; 
    fragNormal = inNormal;
    fragUV = inUV;
}
