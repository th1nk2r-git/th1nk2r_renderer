struct Input {
    [[vk::location(0)]] float3 color;
    [[vk::location(1)]] float2 texcoord;
};

struct Output {
    [[vk::location(0)]] float4 color;
};

[[vk::binding(0, 1)]]
Texture2D<float4> base_color_texture;

[[vk::binding(1, 1)]]
SamplerState base_color_sampler;

Output main(Input input) {
    Output output;
    const float4 base_color = base_color_texture.Sample(
        base_color_sampler,
        input.texcoord
    );
    output.color = base_color * float4(input.color, 1.0);
    return output;
}
