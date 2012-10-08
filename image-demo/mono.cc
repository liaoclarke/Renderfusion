#include <stdio.h>
#include <stdlib.h>

#include <CL/cl.h>
#include "cl_simple.h"
#include "imgsupport.hpp"

int main()
{
    cl_uchar * input;
    cl_uchar * output;
    size_t global_work_size[3];
    size_t local_work_size[3];
    int data_size; 
    vina::PngImage in_img, out_img;

    in_img.loadFromFile("lena.png");
    out_img.storeToFile("mono.png", in_img.getWidth(), in_img.getHeight(), 
        in_img.getBitDepth(), in_img.getColorType());

    printf("loaded image width = %u, height = %u BPP = %d RowBytes = %d\n", 
        in_img.getWidth(), in_img.getHeight(),
        in_img.getBPP(), in_img.getRowBytes());
    
    input = in_img.getData(); 
    data_size = in_img.getHeight() * in_img.getRowBytes();

#if 1
    struct cl_simple_context ctx;
    cl_mem in_buffer;
    cl_int error;

    if ( !clSimpleSimpleInit(&ctx, "mono") ) {
        printf("failed to load kernel mono\n"); 
        exit(1);
    }

    clSimpleSetOutputBuffer(&ctx, data_size);
    in_buffer = clCreateBuffer(ctx.cl_ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, data_size, input, NULL);
    clSetKernelArg(ctx.kernel, 1, sizeof(cl_mem), &in_buffer);

    global_work_size[0] = in_img.getWidth();
    global_work_size[1] = in_img.getHeight();
    local_work_size[0] = 4;
    local_work_size[1] = 4;
    if ( clSimpleEnqueueNDRangeKernel(ctx.command_queue, ctx.kernel, 2, global_work_size, local_work_size) ) {
        fprintf(stderr, "clEnqueueNDRangeKernel() suceeded.\n");
        clSimpleReadOutput(&ctx, output, data_size);
        out_img.setData(output);
        return EXIT_SUCCESS;
    }
    else {
        return EXIT_FAILURE;     
    }
#endif
}
