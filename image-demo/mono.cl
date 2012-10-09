__kernel void mono(__global uchar4 * out, __global const uchar4 * in)
{
    size_t tx = get_global_id(0);
    size_t ty = get_global_id(1);
    size_t id = tx * get_global_size(1) + ty;
    __const float4 gMonoMult = {0.299f, 0.587f, 0.114f, .0f};

    float4 pixel = (float4)(in[id].x, in[id].y, in[id].z, .0f);
    out[id] = (uchar4)dot(pixel, gMonoMult);
}
