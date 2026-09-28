struct Input
{
    float4 position : SV_Position;
    [[vk::location(0)]]
    float3 color;
    [[vk::location(1)]]
    float2 texcoord;
    [[vk::location(2)]]
    float3 world_normal;
    [[vk::location(3)]]
    float4 world_tangent;
    [[vk::location(4)]]
    nointerpolation uint material_index;
    [[vk::location(5)]]
    float4 previous_clip;
    [[vk::location(6)]]
    nointerpolation uint motion_valid;
};

struct Output
{
    // RGB: linear base color, A: ambient occlusion.
    [[vk::location(0)]]
    float4 base_color_ao;

    // RGB: normalized world-space normal XYZ in [-1, 1], A: linear roughness.
    [[vk::location(1)]]
    float4 normal_roughness;

    // RGB: linear HDR emissive color, A: metallic.
    [[vk::location(2)]]
    float4 emissive_metallic;

    // XY: previousUV-currentUV, Z: previousViewDepth-currentViewDepth,
    // W: valid previous projection.
    [[vk::location(3)]]
    float4 motion;
};

struct Camera
{
    column_major matrix<float, 4, 4> view_projection;
    column_major matrix<float, 4, 4> previous_view_projection;
    float4 viewport_size;
};

[[vk::binding(0, 0)]]
ConstantBuffer<Camera> camera;

struct Material
{
    float4 base_color_factor;

    // XYZ: emissive factor, W: normal scale.
    float4 emissive_normal_scale;

    // X: metallic factor, Y: roughness factor,
    // Z: occlusion strength, W: alpha cutoff.
    float4 metallic_roughness_occlusion_alpha_cutoff;

    // X: non-zero when alpha masking is enabled.
    uint4 flags;

    uint base_color_texture_index;
    uint metallic_roughness_texture_index;
    uint normal_texture_index;
    uint occlusion_texture_index;
    uint emissive_texture_index;
    uint padding_0;
    uint padding_1;
    uint padding_2;
};

[[vk::binding(0, 1)]]
StructuredBuffer<Material> materials;

[[vk::binding(1, 1)]]
SamplerState sampler;

[[vk::binding(2, 1)]]
Texture2D<float4> textures[];

Output main(Input input)
{
    const Material material = materials[input.material_index];
    float4 base_color = textures[
        material.base_color_texture_index
    ].Sample(sampler, input.texcoord);

    const bool alpha_mask_enabled = material.flags.x != 0;
    const float alpha_cutoff = material.metallic_roughness_occlusion_alpha_cutoff.w;
    if (alpha_mask_enabled && base_color.a < alpha_cutoff)
    {
        discard;
    }
    Output output;
    // calculate base_color_ao
    const float sampled_occlusion = textures[
        material.occlusion_texture_index
    ].Sample(sampler, input.texcoord).r;
    const float occlusion_strength = material.metallic_roughness_occlusion_alpha_cutoff.z;
    const float ao = saturate(lerp(1.0, sampled_occlusion, occlusion_strength));
    output.base_color_ao = float4(
        base_color.rgb * input.color * material.base_color_factor.rgb, 
        ao
    );

    // calculate normal_roughness
    float3 N = normalize(input.world_normal);
    float3 T = input.world_tangent.xyz;
    T = normalize(T - N * dot(N, T));
    float3 B = normalize(cross(N, T)) * input.world_tangent.w;
    float3 tangent_normal = textures[
        material.normal_texture_index
    ].Sample(sampler, input.texcoord).xyz;
    tangent_normal = tangent_normal * 2.0 - 1.0;
    float normal_scale = material.emissive_normal_scale.w;
    tangent_normal.xy *= normal_scale;
    tangent_normal = normalize(tangent_normal);
    float3 world_normal = normalize(
        T * tangent_normal.x +
        B * tangent_normal.y +
        N * tangent_normal.z
    );
    float4 metallic_roughness = textures[
        material.metallic_roughness_texture_index
    ].Sample(sampler, input.texcoord);
    float roughness = saturate(
        metallic_roughness.g *
        material.metallic_roughness_occlusion_alpha_cutoff.y
    );
    output.normal_roughness = float4(world_normal, roughness);

    // calculate emissive_metallic
    float3 emissive = textures[
        material.emissive_texture_index
    ].Sample(sampler, input.texcoord).rgb * material.emissive_normal_scale.xyz;
    float metallic = saturate(
        metallic_roughness.b *
        material.metallic_roughness_occlusion_alpha_cutoff.x
    );
    output.emissive_metallic = float4(emissive, metallic);

    // With this RH perspective projection, fragment position.w is 1/viewDepth.
    float current_view_depth = 1.0 / input.position.w;
    output.motion = float4(0.0);
    if (input.motion_valid != 0u && input.previous_clip.w > 0.0 && input.previous_clip.z >= 0.0 && input.previous_clip.z < input.previous_clip.w)
    {
        float2 previous_uv = input.previous_clip.xy / input.previous_clip.w * 0.5 + 0.5;
        if (all(previous_uv >= 0.0) && all(previous_uv < 1.0))
        {
            float2 current_uv = input.position.xy / camera.viewport_size.xy;
            output.motion = float4(
                previous_uv - current_uv,
                input.previous_clip.w - current_view_depth,
                1.0
            );
        }
    }
    return output;
}
