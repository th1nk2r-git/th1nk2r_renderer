struct GeometryVertexInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 color;
    [[vk::location(2)]] float2 texcoord;
    [[vk::location(3)]] float3 normal;
    [[vk::location(4)]] float4 tangent;
    uint instance_id : SV_InstanceID;
    uint first_instance : SV_StartInstanceLocation;
};

struct GeometryVertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color;
    [[vk::location(1)]] float2 texcoord;
    [[vk::location(2)]] float3 world_normal;
    [[vk::location(3)]] float4 world_tangent;
    [[vk::location(4)]] nointerpolation uint material_index;
};

struct GeometryCamera {
    column_major matrix<float, 4, 4> view_projection;
};

struct RenderInstance {
    column_major matrix<float, 4, 4> model;
    float4 normal_column_0;
    float4 normal_column_1;
    float4 normal_column_2;
    float4 bounds_minimum;
    float4 bounds_maximum;
    uint index_count;
    uint first_index;
    int vertex_offset;
    uint material_index;
};

[[vk::binding(0, 0)]]
ConstantBuffer<GeometryCamera> camera;

[[vk::binding(1, 0)]]
StructuredBuffer<RenderInstance> instances;

GeometryVertexOutput main(GeometryVertexInput input) {
    const RenderInstance instance = instances[
        input.first_instance + input.instance_id
    ];
    GeometryVertexOutput output;
    output.color = input.color;
    matrix<float, 4, 4> mvp = mul(camera.view_projection, instance.model);
    output.position = mul(mvp, float4(input.position, 1));
    output.texcoord = input.texcoord;
    output.world_tangent = normalize(
        mul(instance.model, float4(input.tangent.xyz, 0))
    );
    output.world_tangent.w = input.tangent.w;
    output.world_normal = normalize(
        instance.normal_column_0.xyz * input.normal.x +
        instance.normal_column_1.xyz * input.normal.y +
        instance.normal_column_2.xyz * input.normal.z
    );
    output.material_index = instance.material_index;
    return output;
}
