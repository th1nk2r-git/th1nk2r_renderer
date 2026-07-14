struct Input {
    uint id : SV_VertexID;
};

struct Output {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color;
};

static const float2 positions[3] = {
    float2(0.0, -0.5),
    float2(-0.5, 0.5),
    float2(0.5, 0.5)
};

static const float3 colors[3] = {
    float3(1, 0, 0),
    float3(0, 1, 0),
    float3(0, 0, 1),
};

Output main(Input input) {
    Output output;
    output.position = float4(positions[input.id], 0.0, 1.0);
    output.color = colors[input.id];
    return output;
}