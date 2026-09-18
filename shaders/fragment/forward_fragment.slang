struct Input {
    [[vk::location(0)]] float3 color;
    [[vk::location(1)]] float2 texcoord;
};

struct Output {
    [[vk::location(0)]] float4 color;
};

struct DrawConstants {
    column_major matrix<float, 4, 4> model;
    float4 base_color_factor;
    float alpha_cutoff;
    uint alpha_mask;
    float2 padding;
};

[[vk::push_constant]]
ConstantBuffer<DrawConstants> draw;

[[vk::binding(0, 1)]]
SamplerState material_sampler;

[[vk::binding(1, 1)]]
Texture2D<float4> base_color_texture;

Output main(Input input) {
    Output output;
    const float4 base_color = base_color_texture.Sample(
        material_sampler,
        input.texcoord
    ) * draw.base_color_factor * float4(input.color, 1.0);

    if (draw.alpha_mask != 0 && base_color.a < draw.alpha_cutoff) {
        discard;
    }

    output.color = base_color;
    return output;
}
