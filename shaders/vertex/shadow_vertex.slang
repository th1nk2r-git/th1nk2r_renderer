struct Input {
    [[vk::location(0)]] float3 position;
    [[vk::location(2)]] float2 texcoord;
};

struct Output {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 world_position;
    [[vk::location(1)]] float2 texcoord;
};

struct ShadowFace {
    column_major matrix<float, 4, 4> light_view_projection;
    float4 light_position_far;
};

struct ShadowDrawConstants {
    column_major matrix<float, 4, 4> transform;
    uint face_index;
    uint padding_0;
    uint padding_1;
    uint padding_2;
};

[[vk::binding(0, 0)]]
StructuredBuffer<ShadowFace> shadow_faces;

[[vk::push_constant]]
ConstantBuffer<ShadowDrawConstants> draw;

Output main(Input input) {
    Output output;
    const float4 world_position = mul(
        draw.transform,
        float4(input.position, 1.0)
    );
    output.position = mul(
        shadow_faces[draw.face_index].light_view_projection,
        world_position
    );
    output.world_position = world_position.xyz;
    output.texcoord = input.texcoord;
    return output;
}
