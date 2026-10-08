void light_material_factors(
    float3 normal, float3 view_direction, float3 base_color, float metallic, float roughness,
    out float3 diffuse_factor, out float3 specular_factor)
{
    float3 albedo = base_color * (1.0 - metallic);
    float3 f0 = lerp(float3(0.04), base_color, metallic);
    float n_dot_v = abs(dot(normal, view_direction));
    float m = saturate(roughness * roughness);
    float4 x = float4(1.0, n_dot_v, n_dot_v * n_dot_v, n_dot_v * n_dot_v * n_dot_v);
    float4 y = float4(1.0, m, m * m, m * m * m);
    const float2x2 bias_num = float2x2(0.99044, -1.28514, 1.29678, -0.755907);
    const float3x3 bias_den = float3x3(1.0, 2.92338, 59.4188, 20.3225, -27.0302, 222.592, 121.563, 626.13, 316.627);
    const float2x2 scale_num = float2x2(0.0365463, 3.32707, 9.0632, -9.04756);
    const float3x3 scale_den = float3x3(1.0, 3.59685, -1.36772, 9.04401, -16.3174, 9.22949, 5.56589, 19.7886, -20.2123);
    float bias = dot(mul(bias_num, x.xy), y.xy) / max(dot(mul(bias_den, x.xyw), y.xyw), 1e-6);
    float scale = dot(mul(scale_num, x.xy), y.xy) / max(dot(mul(scale_den, x.xzw), y.xyw), 1e-6);
    float3 environment_fresnel = saturate(f0 * scale + bias);
    diffuse_factor = lerp(float3(0.02), float3(1.0), (1.0 - environment_fresnel) * albedo);
    specular_factor = lerp(float3(0.02), float3(1.0), environment_fresnel * lerp(0.1, 1.0, roughness));
}

struct CompositeFrame
{
    column_major matrix<float, 4, 4> inverse_view_projection;
    float4 camera_position;
    uint4 viewport;
};

struct Input
{
    float4 position : SV_Position;
};

struct Output
{
    [[vk::location(0)]] float4 color;
};

[[vk::binding(0, 0)]] ConstantBuffer<CompositeFrame> frame;
[[vk::binding(1, 0)]] Texture2D<float4> diffuse_texture;
[[vk::binding(2, 0)]] Texture2D<float4> specular_texture;
[[vk::binding(3, 0)]] Texture2D<float4> base_color_ao_texture;
[[vk::binding(4, 0)]] Texture2D<float4> normal_roughness_texture;
[[vk::binding(5, 0)]] Texture2D<float> depth_texture;
[[vk::binding(6, 0)]] Texture2D<float4> emissive_metallic_texture;

Output main(Input input)
{
    Output output;
    output.color = float4(0.0, 0.0, 0.0, 1.0);
    uint2 pixel = uint2(input.position.xy);
    float depth = depth_texture.Load(int3(pixel, 0));
    if (depth >= 1.0)
    {
        return output;
    }

    float4 normal_roughness = normal_roughness_texture.Load(int3(pixel, 0));
    float3 normal = normalize(normal_roughness.xyz);
    float2 ndc = input.position.xy / float2(frame.viewport.xy) * 2.0 - 1.0;
    float4 world = mul(frame.inverse_view_projection, float4(ndc, depth, 1.0));
    float3 position = world.xyz / world.w;
    float3 view_direction = normalize(frame.camera_position.xyz - position);
    float3 base_color = max(base_color_ao_texture.Load(int3(pixel, 0)).rgb, 0.0);
    float metallic = saturate(emissive_metallic_texture.Load(int3(pixel, 0)).a);
    float roughness = clamp(normal_roughness.w, 0.045, 1.0);
    float3 diffuse_factor;
    float3 specular_factor;
    light_material_factors(normal, view_direction, base_color, metallic, roughness, diffuse_factor, specular_factor);

    float3 diffuse = diffuse_texture.Load(int3(pixel, 0)).rgb * diffuse_factor;
    float3 specular = specular_texture.Load(int3(pixel, 0)).rgb * specular_factor;
    output.color.rgb = diffuse + specular + max(emissive_metallic_texture.Load(int3(pixel, 0)).rgb, 0.0);
    return output;
}
