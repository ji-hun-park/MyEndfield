#version 450
layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    // Simple directional lighting based on normal
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(fragNormal, lightDir), 0.2); // ambient 0.2
    
    // Display normal as color for visualization, multiplied by diffuse
    vec3 color = (fragNormal * 0.5 + 0.5) * diff;
    outColor = vec4(color, 1.0);
}
