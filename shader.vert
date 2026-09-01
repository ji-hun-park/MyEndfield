#version 450
layout(set = 0, binding = 0) uniform CameraBuffer {
    mat4 view;
    mat4 proj;
} camera;

layout(set = 2, binding = 0) uniform ObjectData {
    mat4 modelMatrix;
} obj;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragUV;

void main() {
    gl_Position = camera.proj * camera.view * obj.modelMatrix * vec4(inPosition, 1.0);
    // Invert Y for Vulkan (since Unity uses OpenGL style Y-up but Vulkan is Y-down)
    gl_Position.y = -gl_Position.y; 
    fragNormal = inNormal;
    fragUV = inUV;
}
