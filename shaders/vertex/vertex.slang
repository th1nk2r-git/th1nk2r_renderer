struct Input {
    [[vk::location(0)]] float2 position;
    [[vk::location(1)]] float3 color;
};

struct Output {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color;
};

Output main(Input input) {
    Output output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    return output;
}