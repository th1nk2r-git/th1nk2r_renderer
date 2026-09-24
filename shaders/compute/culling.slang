struct RenderInstance
{
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

struct DrawIndexedIndirectCommand
{
    uint index_count;
    uint instance_count;
    uint first_index;
    int vertex_offset;
    uint first_instance;
};

struct CullingConstants
{
    float4 frustum_planes[6];
    uint instance_count;
    uint max_draw_count;
    uint padding_0;
    uint padding_1;
};

[[vk::binding(0, 0)]]
StructuredBuffer<RenderInstance> instances;

[[vk::binding(1, 0)]]
RWStructuredBuffer<DrawIndexedIndirectCommand> commands;

[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> draw_count;

[[vk::push_constant]]
ConstantBuffer<CullingConstants> constants;

bool visible(RenderInstance instance)
{
    const float3 local_center =
        (instance.bounds_minimum.xyz + instance.bounds_maximum.xyz) * 0.5;
    const float3 local_extents =
        (instance.bounds_maximum.xyz - instance.bounds_minimum.xyz) * 0.5;

    const float3 world_center = mul(
        instance.model,
        float4(local_center, 1.0)
    ).xyz;
    const float3 world_extents =
        abs(mul(instance.model, float4(local_extents.x, 0.0, 0.0, 0.0)).xyz) +
        abs(mul(instance.model, float4(0.0, local_extents.y, 0.0, 0.0)).xyz) +
        abs(mul(instance.model, float4(0.0, 0.0, local_extents.z, 0.0)).xyz);

    for (uint plane_index = 0; plane_index < 6; ++plane_index)
    {
        const float4 plane = constants.frustum_planes[plane_index];
        const float radius = dot(abs(plane.xyz), world_extents);
        const float distance = dot(plane.xyz, world_center) + plane.w;
        if (distance + radius < 0.0)
        {
            return false;
        }
    }
    return true;
}

[numthreads(64, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    const uint instance_index = dispatch_thread_id.x;
    if (instance_index >= constants.instance_count)
    {
        return;
    }

    const RenderInstance instance = instances[instance_index];
    if (!visible(instance))
    {
        return;
    }

    uint command_index;
    InterlockedAdd(draw_count[0], 1, command_index);
    if (command_index >= constants.max_draw_count)
    {
        return;
    }

    commands[command_index] = DrawIndexedIndirectCommand(
        instance.index_count,
        1,
        instance.first_index,
        instance.vertex_offset,
        instance_index
    );
}
