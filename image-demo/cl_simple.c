#include <CL/cl.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "cl_simple.h"
#include "cl_util.h"

/*============================================================================*/
/*========== DIRECT WRAPPERS =================================================*/
/*============================================================================*/

unsigned clSimpleCreateContext(cl_context * context, cl_device_id device_id)
{
   cl_int error;
   *context = clCreateContext(NULL, /* Properties */
                           1, /* Number of devices */
                           &device_id, /* Device pointer */
                           NULL, /* Callback for reporting errors */
                           NULL, /* User data to pass to error callback */
                           &error); /* Error code */

   if (error != CL_SUCCESS) {
      fprintf(stderr, "clCreateContext() failed: %s\n",
                      clUtilErrorString(error));
      return 0;
   }
   return 1;
}

unsigned clSimpleCreateCommandQueue(cl_command_queue * command_queue,
                               cl_context context, cl_device_id device_id)
{
   cl_int error;
   *command_queue = clCreateCommandQueue(context,
                                        device_id,
                                        0, /* Command queue properties */
                                        &error); /* Error code */

   if (error != CL_SUCCESS) {
      fprintf(stderr, "clCreateCommandQueue() failed: %s\n",
                      clUtilErrorString(error));
      return 0;
   }
   return 1;
}

unsigned clSimpleKernelSetArg(cl_kernel kernel, cl_uint index, size_t size,
                         const void * value)
{
   cl_int error;

   error = clSetKernelArg(kernel, index, size, value);

   if (error != CL_SUCCESS) {
      fprintf(stderr, "clSetKernelArg failed: %s\n", clUtilErrorString(error));
      return 0;
   }

   fprintf(stderr, "clSetKernelArg() succeeded.\n");

   return 1;
}

unsigned clSimpleCreateBuffer(cl_mem * buffer, cl_context context,
                              cl_mem_flags flags, size_t size)
{
   cl_int error;

   *buffer = clCreateBuffer(context,
                            flags, /* Flags */
                            size, /* Size of buffer */
                            NULL, /* Pointer to the data */
                            &error); /* error code */

   if (error != CL_SUCCESS) {
      fprintf(stderr, "clCreateBuffer(%p, %p, %u, %u) failed: %s\n",
                       buffer, context, flags, size,
                       clUtilErrorString(error));
      return 0;
   }

   return 1;
}

unsigned clSimpleEnqueueWriteBuffer(cl_command_queue command_queue,
      cl_mem buffer, size_t buffer_size, void * ptr)
{

   cl_int error = clEnqueueWriteBuffer(command_queue,
                                       buffer,
                                       CL_TRUE, /* Blocking write */
                                       0, /* Offset into buffer */
                                       buffer_size,
                                       ptr,
                                       0, /* Events in waiting list */
                                       NULL, /* Wait list */
                                       NULL); /* Event */
   if (error != CL_SUCCESS) {
      return 0;
   }

   return 1;
}

unsigned clSimpleEnqueueNDRangeKernel(cl_command_queue command_queue,
      cl_kernel kernel, cl_uint work_dim, const size_t * global_work_size,
      const size_t * local_work_size)
{
   cl_int error = clEnqueueNDRangeKernel(command_queue,
                                  kernel,
                                  work_dim, /* Number of dimensions */
                                  NULL, /* Global work offset */
                                  global_work_size,
                                  local_work_size, /* local work size */
                                  0, /* Events in wait list */
                                  NULL, /* Wait list */
                                  NULL); /* Event object for this event */
   if (error != CL_SUCCESS) {
      fprintf(stderr, "clEnqueueNDRangeKernel() failed: %s\n",
                      clUtilErrorString(error));
      return 0;
   }

   return 1;
}

/*============================================================================*/
/*========== CONVENIENCE WRAPPERS=============================================*/
/*============================================================================*/

unsigned clSimpleInitGpuDevice(struct cl_simple_context * context, cl_device_id * device_id)
{
   cl_int error;

   cl_uint total_platforms;
   cl_platform_id platform_id;

   cl_uint total_gpu_devices;

   error = clGetPlatformIDs(
                           1, /* Max number of platform IDs to return */
                           &platform_id, /* Pointer to platform_id */
                           &total_platforms); /* Total number of platforms
                                               * found on the system */

   if (error != CL_SUCCESS) {
      fprintf(stderr, "clGetPlatformIDs() failed: %s\n", clUtilErrorString(error));
      return 0;
   }

   fprintf(stderr, "There are %u platforms.\n", total_platforms);
   context->platform_id = platform_id; 

   error = clGetDeviceIDs(platform_id,
                          CL_DEVICE_TYPE_GPU,
                          1,
                          device_id,
                          &total_gpu_devices);

   if (error != CL_SUCCESS) {
      fprintf(stderr, "clGetDeviceIDs() failed: %s\n", clUtilErrorString(error));
      return 0;
   }

   fprintf(stderr, "There are %u GPU devices.\n", total_gpu_devices);

   return 1;
}

#define CODE_CHUNK 64

unsigned clSimpleCreateKernel(cl_context context, cl_device_id device_id,
                              cl_kernel * kernel, const char * kernel_name)
{
   char * filename;
   char * code = NULL;
   size_t code_len = 0;
   int fd;
   int bytes_read;

   /* +3 .cl
    * +1 NULL byte
    * --
    * +4
    */
   unsigned filename_len = strlen(kernel_name) + 4;

   /* Determine file name */
   filename = malloc (filename_len + 4);
   if (!filename) {
      fprintf(stderr, "Failed to malloc filename.\n");
      return 0;
   }

   snprintf(filename, filename_len, "%s.cl", kernel_name);

   /* Open file */
   fd = open(filename, O_RDONLY);
   if (fd < 0) {
      fprintf(stderr, "Failed to open file: %s\n", filename);
      return 0;
   }

   /* Read code */
   do {
      code = realloc(code, (code_len + CODE_CHUNK) * sizeof(unsigned char));
      if (!code) {
         fprintf(stderr, "Failed to realloc code.\n");
         return 0;
      }

      bytes_read = read(fd, code + code_len, CODE_CHUNK);
      if (bytes_read < 0) {
         fprintf(stderr, "Failed to read code.\n");
         return 0;
      }
      code_len += bytes_read;
   } while(bytes_read == CODE_CHUNK);

   return clSimpleCreateKernelString(context, device_id, kernel, kernel_name,
                                     code, code_len);
}

unsigned clSimpleCreateKernelString(cl_context context, cl_device_id device_id,
                                    cl_kernel * kernel, const char * kernel_name,
                                    const char * code, size_t code_len)
{
   cl_int error;
   cl_program program;

   /* Create program */
   program = clCreateProgramWithSource(context,
                                       1, /* Number of strings */
                                       &code,
                                       &code_len, /* String lengths, 0 means all the
                                              * strings are NULL terminated. */
                                       &error);

   if (error != CL_SUCCESS) {
      fprintf(stderr, "clCreateProgramWithSource() failed: %s\n",
                      clUtilErrorString(error));
      return 0;
   }

   fprintf(stderr, "clCreateProgramWithSource() suceeded.\n");

   /* Build program */
   error = clBuildProgram(program,
                          1, /* Number of devices */
                          &device_id,
                          NULL, /* options */
                          NULL, /* callback function when compile is complete */
                          NULL); /* user data for callback */


   if (error != CL_SUCCESS) {
      char build_str[10000];
      error = clGetProgramBuildInfo(program,
                                    device_id,
                                    CL_PROGRAM_BUILD_LOG,
                                    10000, /* Size of output string */
                                    build_str, /* pointer to write the log to */
                                    NULL); /* Number of bytes written to the log */
      if (error != CL_SUCCESS) {
         fprintf(stderr, "clGetProgramBuildInfo() failed: %s\n",
                          clUtilErrorString(error));
      } else {
         fprintf(stderr, "Build Log: \n%s\n\n", build_str);
      }
      return 0;
   }

   fprintf(stderr, "clBuildProgram() suceeded.\n");

   *kernel = clCreateKernel(program, kernel_name, &error);

   if (error != CL_SUCCESS) {
      fprintf(stderr, "clCreateKernel() failed: %s\n", clUtilErrorString(error));
      return 0;
   }

   fprintf(stderr, "clCreateKernel() suceeded.\n");

   return 1;
}

unsigned clSimpleSimpleInit(struct cl_simple_context * context, const char * kernel_name)
{
   if (!clSimpleInitGpuDevice(context, &context->device_id)) {
      return 0;
   }

   if (!clSimpleCreateContext(&context->cl_ctx, context->device_id)) {
      return 0;
   }

   if (!clSimpleCreateCommandQueue(&context->command_queue, context->cl_ctx,
                                                      context->device_id)) {
      return 0;
   }

   if (!clSimpleCreateKernel(context->cl_ctx, context->device_id, &context->kernel,
                                                           kernel_name)) {
      return 0;
   }

   return 1;
}

unsigned clSimpleSetOutputBuffer(struct cl_simple_context * context,
                               unsigned buffer_size)
{
   cl_int error;

   if (!clSimpleCreateBuffer(&context->out_buffer, context->cl_ctx,
                             CL_MEM_WRITE_ONLY, buffer_size)) {
      return 0;
   }

   if (!clSimpleKernelSetArg(context->kernel, 0, sizeof(cl_mem),
                                               &context->out_buffer)) {
      return 0;
   }

   return 1;
}

unsigned clSimpleReadOutput(struct cl_simple_context * context, void * data,
      size_t data_bytes)
{
   cl_int error = clEnqueueReadBuffer(context->command_queue,
                                      context->out_buffer,
                                      CL_TRUE, /* TRUE means it is a blocking read. */
                                      0, /* Buffer offset to read from. */
                                      data_bytes, /* Bytes to read */
                                      data, /* Pointer to store the data */
                                      0, /* Events in wait list */
                                      NULL, /* Wait list */
                                      NULL); /* Event object */

   if (error != CL_SUCCESS) {
      fprintf(stderr, "clEnqueueReadBuffer() failed: %s\n",
                      clUtilErrorString(error));
      return 0;
   }

   return 1;
}
