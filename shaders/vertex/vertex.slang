struct Input {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 color;
    [[vk::location(2)]] float2 texcoord;
    [[vk::location(3)]] float3 normal;
    [[vk::location(4)]] float4 tangent;
};

struct Output {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color;
    [[vk::location(1)]] float2 texcoord;
    [[vk::location(2)]] float3 world_position;
    [[vk::location(3)]] float3 world_normal;
    [[vk::location(4)]] float4 world_tangent;
};

struct ViewProjection {
    column_major matrix<float, 4, 4> view;
    column_major matrix<float, 4, 4> projection;
    float4 camera_position;
}

struct DrawConstants {
    column_major matrix<float, 4, 4> transform;
    float4 normal_column_0;
    float4 normal_column_1;
    float4 normal_column_2;
    uint point_light_count;
    uint padding_0;
    uint padding_1;
    uint padding_2;
}

[[vk::binding(0, 0)]]
ConstantBuffer<ViewProjection> camera;

[[vk::push_constant]]
ConstantBuffer<DrawConstants> draw;

Output main(Input input) {
    Output output;
    float4 in_pos = float4(input.position, 1.0);
    float4 world_pos = mul(draw.transform, in_pos);
    float4 view_pos = mul(camera.view, world_pos);
    output.position = mul(camera.projection, view_pos);
    output.color = input.color;
    output.texcoord = input.texcoord;
    output.world_position = world_pos.xyz;
    const float3 world_normal = normalize(
        draw.normal_column_0.xyz * input.normal.x +
        draw.normal_column_1.xyz * input.normal.y +
        draw.normal_column_2.xyz * input.normal.z
    );
    const float3 transformed_tangent = mul(
        draw.transform,
        float4(input.tangent.xyz, 0.0)
    ).xyz;
    const float3 world_tangent = normalize(
        transformed_tangent -
        world_normal * dot(world_normal, transformed_tangent)
    );
    output.world_normal = world_normal;
    output.world_tangent = float4(world_tangent, input.tangent.w);
    return output;
}
