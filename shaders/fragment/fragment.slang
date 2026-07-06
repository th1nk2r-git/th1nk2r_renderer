struct Input {
    [[vk::location(0)]] float3 color;
};

struct Output {
    [[vk::location(0)]] float4 color;
};

Output main(Input input) {
    Output output;
    output.color = float4(input.color, 1.0);
    return output;
}