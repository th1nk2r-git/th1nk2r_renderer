struct Input {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 color;
    [[vk::location(2)]] float2 texcoord;
};

struct Output {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color;
    [[vk::location(1)]] float2 texcoord;
};

struct CameraUniforms {
    column_major matrix<float, 4, 4> view;
    column_major matrix<float, 4, 4> projection;
};

struct DrawConstants {
    column_major matrix<float, 4, 4> model;
    float4 base_color_factor;
    float alpha_cutoff;
    uint alpha_mask;
    float2 padding;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniforms> camera;

[[vk::push_constant]]
ConstantBuffer<DrawConstants> draw;

Output main(Input input) {
    Output output;
    const float4 world_position = mul(
        draw.model,
        float4(input.position, 1.0)
    );
    output.position = mul(
        camera.projection,
        mul(camera.view, world_position)
    );
    output.color = input.color;
    output.texcoord = input.texcoord;
    return output;
}
