struct Input {
    [[vk::location(0)]] float2 position;
    [[vk::location(1)]] float3 color;
};

struct Output {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 color;
};

struct ObjectUniforms {
    matrix<float, 4, 4> model;
}

struct CameraUniforms {
    matrix<float, 4, 4> view;
    matrix<float, 4, 4> proj;
};

ConstantBuffer<CameraUniforms> camera : register(b0, space0);

Output main(Input input) {
    Output output;
    float4 in_pos = float4(input.position, 0.0, 1.0);
    /*float4 world_pos = mul(object.model, in_pos);*/
    float4 view_pos = mul(camera.view, in_pos);
    output.position = mul(camera.proj, view_pos);
    output.color = input.color;
    return output;
}
