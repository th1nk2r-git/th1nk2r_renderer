struct GeometryFragmentInput
{
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
};

struct GeometryFragmentOutput
{
    // RGB: linear base color, A: ambient occlusion.
    [[vk::location(0)]]
    float4 base_color_ao;

    // RG: octahedral world normal, B: roughness, A: metallic.
    [[vk::location(1)]]
    float4 normal_roughness_metallic;

    // Linear HDR emissive color.
    [[vk::location(2)]]
    float3 emissive;
};

struct GeometryMaterial
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
StructuredBuffer<GeometryMaterial> materials;

[[vk::binding(1, 1)]]
SamplerState material_sampler;

[[vk::binding(2, 1)]]
Texture2D<float4> material_textures[];

float2 sign_not_zero(float2 value)
{
    return float2(
        value.x >= 0.0 ? 1.0 : -1.0,
        value.y >= 0.0 ? 1.0 : -1.0
    );
}

float2 encode_octahedral(float3 normal)
{
    normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
    float2 encoded = normal.xy;
    if (normal.z < 0.0)
    {
        encoded =
            (1.0 - abs(encoded.yx)) * sign_not_zero(encoded);
    }
    return encoded;
}

GeometryFragmentOutput main(GeometryFragmentInput input)
{
    const GeometryMaterial material = materials[input.material_index];
    float4 base_color = material_textures[
        material.base_color_texture_index
    ].Sample(material_sampler, input.texcoord) * material.base_color_factor;

    base_color.rgb *= input.color;
    const bool alpha_mask_enabled = material.flags.x != 0;
    const float alpha_cutoff = material.metallic_roughness_occlusion_alpha_cutoff.w;
    if (alpha_mask_enabled && base_color.a < alpha_cutoff)
    {
        discard;
    }

    GeometryFragmentOutput output;
    // calculate base_color_ao
    const float sampled_ao = material_textures[
        material.occlusion_texture_index
    ].Sample(material_sampler, input.texcoord).r;
    const float ao_strength = material.metallic_roughness_occlusion_alpha_cutoff.z;
    const float ao = lerp(1.0, sampled_ao, ao_strength);
    output.base_color_ao = float4(base_color.rgb, ao);

    // calculate normal_roughness_metallic
    float3 N = normalize(input.world_normal);
    float3 T = input.world_tangent.xyz;
    T = normalize(T - N * dot(N, T));
    float3 B = normalize(cross(N, T)) * input.world_tangent.w;
    float3 tangent_normal = material_textures[
        material.normal_texture_index
    ].Sample(material_sampler, input.texcoord).xyz;
    tangent_normal = tangent_normal * 2.0 - 1.0;
    float normal_scale = material.emissive_normal_scale.w;
    tangent_normal.xy *= normal_scale;
    tangent_normal = normalize(tangent_normal);
    float3 world_normal = normalize(
        T * tangent_normal.x +
        B * tangent_normal.y +
        N * tangent_normal.z
    );
    float2 encoded_normal = encode_octahedral(world_normal);
    float4 mr_sample = material_textures[
        material.metallic_roughness_texture_index
    ].Sample(material_sampler, input.texcoord);
    float roughness = saturate(
        mr_sample.g *
        material.metallic_roughness_occlusion_alpha_cutoff.y
    );
    float metallic = saturate(
        mr_sample.b *
        material.metallic_roughness_occlusion_alpha_cutoff.x
    );
    output.normal_roughness_metallic = float4(
        encoded_normal,
        roughness,
        metallic
    );

    // calculate emissive
    output.emissive = material_textures[
        material.emissive_texture_index
    ].Sample(material_sampler, input.texcoord).rgb *
        material.emissive_normal_scale.xyz;
    return output;
}
