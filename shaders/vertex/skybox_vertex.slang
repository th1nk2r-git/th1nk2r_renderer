struct Output {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 world_direction;
};

struct ViewProjection {
    column_major matrix<float, 4, 4> view;
    column_major matrix<float, 4, 4> projection;
    float4 camera_position;
};

[[vk::binding(0, 0)]]
ConstantBuffer<ViewProjection> camera;

Output main(uint vertex_id : SV_VertexID) {
    static const float3 positions[36] = {
        float3(-1.0,  1.0, -1.0),
        float3(-1.0, -1.0, -1.0),
        float3( 1.0, -1.0, -1.0),
        float3( 1.0, -1.0, -1.0),
        float3( 1.0,  1.0, -1.0),
        float3(-1.0,  1.0, -1.0),

        float3(-1.0, -1.0,  1.0),
        float3(-1.0, -1.0, -1.0),
        float3(-1.0,  1.0, -1.0),
        float3(-1.0,  1.0, -1.0),
        float3(-1.0,  1.0,  1.0),
        float3(-1.0, -1.0,  1.0),

        float3( 1.0, -1.0, -1.0),
        float3( 1.0, -1.0,  1.0),
        float3( 1.0,  1.0,  1.0),
        float3( 1.0,  1.0,  1.0),
        float3( 1.0,  1.0, -1.0),
        float3( 1.0, -1.0, -1.0),

        float3(-1.0, -1.0,  1.0),
        float3(-1.0,  1.0,  1.0),
        float3( 1.0,  1.0,  1.0),
        float3( 1.0,  1.0,  1.0),
        float3( 1.0, -1.0,  1.0),
        float3(-1.0, -1.0,  1.0),

        float3(-1.0,  1.0, -1.0),
        float3( 1.0,  1.0, -1.0),
        float3( 1.0,  1.0,  1.0),
        float3( 1.0,  1.0,  1.0),
        float3(-1.0,  1.0,  1.0),
        float3(-1.0,  1.0, -1.0),

        float3(-1.0, -1.0, -1.0),
        float3(-1.0, -1.0,  1.0),
        float3( 1.0, -1.0, -1.0),
        float3( 1.0, -1.0, -1.0),
        float3(-1.0, -1.0,  1.0),
        float3( 1.0, -1.0,  1.0)
    };

    Output output;
    const float3 cube_position = positions[vertex_id];
    const float4 view_position = mul(
        camera.view,
        float4(cube_position, 0.0)
    );
    float4 clip_position = mul(
        camera.projection,
        view_position
    );
    clip_position.z = clip_position.w;
    output.world_direction = cube_position;
    output.position = clip_position;
    return output;
}
