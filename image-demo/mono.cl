
__kernel void mono(__global uchar4 * out, __global const uchar4 * in)
{
    size_t id = get_global_id(0);
    __const float4 gMonoMult = {0.299f, 0.587f, 0.114f, .0f};

    float4 pixel = (float4)(in[id].x, in[id].y, in[id].z);
    out[id] = (uchar4)dot(pixel, gMonoMult);
}
