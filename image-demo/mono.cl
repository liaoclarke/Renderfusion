
__kernel void mono(__global char3 * out, __global const char3 * in)
{
    size_t id = get_global_id(0);
    __const float4 gMonoMult = {0.299f, 0.587f, 0.114f};

    float3 pixel = (float3)(in[id].x, in[id].y, in[id].z);
    out[id] = (uchar3)dot(pixel, gMonoMult);
}
