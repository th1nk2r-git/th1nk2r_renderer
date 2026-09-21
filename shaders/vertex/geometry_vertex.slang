struct GeometryVertexInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 color;
    [[vk::location(2)]] float2 texcoord;
    [[vk::location(3)]] float3 normal;
    [[vk::location(4)]] float4 tangent;
};

struct GeometryVertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color;
    [[vk::location(1)]] float2 texcoord;
    [[vk::location(2)]] float3 world_normal;
    [[vk::location(3)]] float4 world_tangent;
};

struct GeometryCamera {
    column_major matrix<float, 4, 4> view_projection;
};

struct GeometryDraw {
    column_major matrix<float, 4, 4> model;
    float4 normal_column_0;
    float4 normal_column_1;
    float4 normal_column_2;
};

[[vk::binding(0, 0)]]
ConstantBuffer<GeometryCamera> camera;

[[vk::push_constant]]
ConstantBuffer<GeometryDraw> draw;

GeometryVertexOutput main(GeometryVertexInput input) {
    GeometryVertexOutput output;
    output.color = input.color;
    matrix<float, 4, 4> mvp = mul(camera.view_projection, draw.model);
    output.position = mul(mvp, float4(input.position, 1));
    output.texcoord = input.texcoord;
    output.world_tangent = normalize(mul(draw.model, float4(input.tangent.xyz, 0)));
    output.world_tangent.w = input.tangent.w;
    output.world_normal = normalize(
        draw.normal_column_0.xyz * input.normal.x +
        draw.normal_column_1.xyz * input.normal.y +
        draw.normal_column_2.xyz * input.normal.z
    );
    return output;
}
