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

struct ViewProjection {
    column_major matrix<float, 4, 4> view;
    column_major matrix<float, 4, 4> projection;
}

struct Model {
    column_major matrix<float, 4, 4> transform;
}

[[vk::binding(0, 0)]]
ConstantBuffer<ViewProjection> camera;

[[vk::push_constant]]
ConstantBuffer<Model> entity;

Output main(Input input) {
    Output output;
    float4 in_pos = float4(input.position, 1.0);
    float4 world_pos = mul(entity.transform, in_pos);
    float4 view_pos = mul(camera.view, world_pos);
    output.position = mul(camera.projection, view_pos);
    output.color = input.color;
    output.texcoord = input.texcoord;
    return output;
}
