#include <stdio.h>
#include <stdlib.h>

#include <CL/cl.h>
#include "cl_simple.h"
#include "cl_util.h"
#include "imgsupport.hpp"
#define IMAGE_DIM_X 48 //in_img.getWidth()
#define IMAGE_DIM_Y 48 // in_img.getHeight()
int main()
{
    cl_uchar * input;
    cl_uchar * output;
    size_t global_work_size[3];
    size_t local_work_size[3];
    int data_size; 
    vina::PngImage in_img, out_img;

    in_img.loadFromFile("lena.png");
    out_img.storeToFile("mono.png", IMAGE_DIM_X, IMAGE_DIM_Y,
        in_img.getBitDepth(), in_img.getColorType());

    printf("loaded image width = %u, height = %u BPP = %d RowBytes = %d\n", 
        in_img.getWidth(), in_img.getHeight(),
        in_img.getBPP(), in_img.getRowBytes());
    
    input = in_img.getData(); 
    //data_size = in_img.getHeight() * in_img.getRowBytes();
    data_size = IMAGE_DIM_X * IMAGE_DIM_Y * 4; //3 * in_img.getHeight() * in_img.getRowBytes();

    unsigned char * buffer_v4 = (unsigned char *)malloc(data_size);
    for (int i = 0; i<IMAGE_DIM_X; ++i) for (int j=0; j<IMAGE_DIM_Y; ++j) {
        buffer_v4[i * IMAGE_DIM_X * 4 + j * 4]  = input[i * in_img.getRowBytes() + j * 3];
        buffer_v4[i * IMAGE_DIM_X * 4 + j * 4 + 1] = input[i * in_img.getRowBytes() + j * 3 + 1];
        buffer_v4[i * IMAGE_DIM_X * 4 + j * 4 + 2] = input[i * in_img.getRowBytes() + j * 3 + 2];
    }

#if 1
    struct cl_simple_context ctx;
    cl_mem in_buffer;
    cl_int error;
    char info[80];
    
    if ( !clSimpleSimpleInit(&ctx, "mono") ) {
        printf("failed to load kernel mono\n"); 
        exit(1);
    }

    if ( CL_SUCCESS == (error = clGetPlatformInfo(ctx.platform_id, CL_PLATFORM_VERSION, sizeof(info), info, NULL)) ) {
        printf("OpenCL version: %s\n", info);     
    }
    else {
        printf("FAILED: %s\n", clUtilErrorString(error));
    }
    if ( CL_SUCCESS == clGetPlatformInfo(ctx.platform_id, CL_PLATFORM_NAME, sizeof(info), info, NULL) ) {
        printf("\t name: %s\n", info);     
    }
    if ( CL_SUCCESS == clGetPlatformInfo(ctx.platform_id, CL_PLATFORM_NAME, sizeof(info), info, NULL) ) {
        printf("\t vendor: %s\n", info);     
    }

    clSimpleSetOutputBuffer(&ctx, data_size);
    in_buffer = clCreateBuffer(ctx.cl_ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, data_size, buffer_v4, NULL);
    clSetKernelArg(ctx.kernel, 1, sizeof(cl_mem), &in_buffer);

    global_work_size[0] = IMAGE_DIM_X * IMAGE_DIM_Y;
    //global_work_size[1] = 48
    local_work_size[0] = 16 * 16;
    //local_work_size[1] = 48;
    if ( clSimpleEnqueueNDRangeKernel(ctx.command_queue, ctx.kernel, 1, global_work_size, local_work_size) ) {
        fprintf(stderr, "clEnqueueNDRangeKernel() suceeded.\n");
        clSimpleReadOutput(&ctx, output, data_size);

        for (int i = 0; i<IMAGE_DIM_X; ++i) for (int j=0; j<IMAGE_DIM_Y; ++j) {
            buffer_v4[i * in_img.getRowBytes() + j * 3]  = output[i * IMAGE_DIM_X * 4 + j * 4];
            buffer_v4[i * in_img.getRowBytes() + j * 3 + 1] = output[i * IMAGE_DIM_X * 4 + j * 4 + 1];
            buffer_v4[i * in_img.getRowBytes() + j * 3 + 2] = output[i * IMAGE_DIM_X * 4 + j * 4 + 2];
        }

        out_img.setData(buffer_v4);
        return EXIT_SUCCESS;
    }
    else {
        return EXIT_FAILURE;     
    }
#endif
}
