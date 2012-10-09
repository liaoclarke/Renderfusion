#include <stdio.h>
#include <stdlib.h>

#include <CL/cl.h>
#include "cl_simple.h"
#include "cl_util.h"
#include "imgsupport.hpp"
//FIXME: IMAGE_DIM_X != Y, the result image is broken.
#define IMAGE_DIM_X 368 //in_img.getWidth()
#define IMAGE_DIM_Y 368 //in_img.getHeight()

int main()
{
    cl_uchar * input;
    cl_uchar * output;
    size_t global_work_size[3];
    size_t local_work_size[3];
    int data_size; 
    vina::PngImage in_img, out_img;

    in_img.loadFromFile("lena.png");
    bool ret = out_img.storeToFile("mono.png", in_img.getWidth(), in_img.getHeight(),
        in_img.getBitDepth(), in_img.getColorType());
    if( !ret ) {
        fprintf(stderr, "out_img faile dto storeToFile\n");     
        exit(1);
    }

    printf("loaded image width = %u, height = %u BPP = %d RowBytes = %d\n", 
        in_img.getWidth(), in_img.getHeight(),
        in_img.getBPP(), in_img.getRowBytes());
    
    input = in_img.getData(); 
    
    data_size = IMAGE_DIM_X * IMAGE_DIM_Y * 4; //3 * in_img.getHeight() * in_img.getRowBytes();

    unsigned char * buffer_v4 = (unsigned char *)malloc(data_size);
    for (int i = 0; i<IMAGE_DIM_X; ++i) for (int j=0; j<IMAGE_DIM_Y; ++j) {
        buffer_v4[i * IMAGE_DIM_X * 4 + j * 4]  = input[i * in_img.getRowBytes() + j * 3];
        buffer_v4[i * IMAGE_DIM_X * 4 + j * 4 + 1] = input[i * in_img.getRowBytes() + j * 3 + 1];
        buffer_v4[i * IMAGE_DIM_X * 4 + j * 4 + 2] = input[i * in_img.getRowBytes() + j * 3 + 2];
    }

    struct cl_simple_context ctx;
    cl_mem in_buffer;
    cl_int error;
    char info[80];
    cl_uint info2;
    size_t info3;

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
    if ( CL_SUCCESS == clGetDeviceInfo(ctx.device_id, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(info2), &info2, NULL) ) {
        printf("dev max compute unit = %u\n", info2);
    }
    if ( CL_SUCCESS == clGetDeviceInfo(ctx.device_id, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, sizeof(info2), &info2, NULL) ) {
        printf("dev max work-item dimensions = %u\n", info2);
    }

    size_t * info_array = new size_t[info2];
    if ( CL_SUCCESS == clGetDeviceInfo(ctx.device_id, CL_DEVICE_MAX_WORK_ITEM_SIZES, sizeof(size_t) * info2, info_array, NULL) ) {
        printf("dev max work-item sizes = [");
        for (int i=0; i<info2; ++i) {
            printf("%d", info_array[i]); 
            if ( i != info2-1 ) putchar(',');
        }     
        printf("]\n");
    }
    delete info_array;

    if ( CL_SUCCESS == clGetDeviceInfo(ctx.device_id, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(info3), &info3, NULL) ) {
        printf("dev max work-group size = %d\n", info3);
    }

    clSimpleSetOutputBuffer(&ctx, data_size);
    in_buffer = clCreateBuffer(ctx.cl_ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, data_size, buffer_v4, NULL);
    clSetKernelArg(ctx.kernel, 1, sizeof(cl_mem), &in_buffer);

    global_work_size[0] = IMAGE_DIM_X;
    global_work_size[1] = IMAGE_DIM_Y;
    local_work_size[0] = 16;
    local_work_size[1] = 16;

    if ( clSimpleEnqueueNDRangeKernel(ctx.command_queue, ctx.kernel, 2, global_work_size, local_work_size) ) {
        fprintf(stderr, "clEnqueueNDRangeKernel() suceeded.\n");
        clSimpleReadOutput(&ctx, buffer_v4, data_size);

        for (int i = 0; i<IMAGE_DIM_X; ++i) for (int j=0; j<IMAGE_DIM_Y; ++j) {
            input[i * in_img.getRowBytes() + j * 3]  = buffer_v4[i * IMAGE_DIM_Y * 4 + j * 4];
            input[i * in_img.getRowBytes() + j * 3 + 1] = buffer_v4[i * IMAGE_DIM_Y * 4 + j * 4 + 1];
            input[i * in_img.getRowBytes() + j * 3 + 2] = buffer_v4[i * IMAGE_DIM_Y * 4 + j * 4 + 2];
        }

        out_img.setData(input);
        //free(buffer_v4);
        return EXIT_SUCCESS;
    }
    else {
        return EXIT_FAILURE;     
    }
}
