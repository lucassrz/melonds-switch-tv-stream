#version 460

layout (location = 0) out vec4 outColor;

layout (location = 0) in vec2 inUV;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec3 coolTransparency;
// position of this pixel relative to the quad's center, in UI pixels
layout (location = 3) in vec2 inLocal;
// half width, half height, corner radius, border thickness (0 = filled)
layout (location = 4) in vec4 inShape;

layout (binding = 0) uniform sampler2D inTexture;

void main()
{
    outColor = texture(inTexture, inUV) * inColor;

    // this cool transparency stuff is incredibly hacky
    // but it looks so cool!
    outColor.a *= clamp(sqrt(coolTransparency.x), coolTransparency.y, coolTransparency.z);

    if (inShape.z > 0.0 || inShape.w > 0.0)
    {
        // signed distance to a rounded box, negative inside
        vec2 q = abs(inLocal) - (inShape.xy - vec2(inShape.z));
        float dist = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - inShape.z;
        float coverage = 1.0 - clamp(dist + 0.5, 0.0, 1.0);
        if (inShape.w > 0.0)
            coverage *= clamp(dist + inShape.w + 0.5, 0.0, 1.0);
        outColor.a *= coverage;
    }
}
