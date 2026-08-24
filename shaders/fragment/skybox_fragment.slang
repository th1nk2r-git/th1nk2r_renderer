struct Input {
    [[vk::location(0)]] float3 world_direction;
};

struct Output {
    [[vk::location(0)]] float4 color;
};

[[vk::binding(0, 3)]]
SamplerState ibl_sampler;

[[vk::binding(4, 3)]]
TextureCube<float4> environment_map;

static const float IBL_INTENSITY = 1.0;

Output main(Input input) {
    Output output;
    const float3 hdr_color = environment_map.SampleLevel(
        ibl_sampler,
        normalize(input.world_direction),
        0.0
    ).rgb * IBL_INTENSITY;
    const float3 mapped_color = hdr_color /
        (hdr_color + float3(1.0));
    output.color = float4(mapped_color, 1.0);
    return output;
}
