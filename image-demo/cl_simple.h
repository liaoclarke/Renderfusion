
struct cl_simple_context {
   cl_device_id device_id;
   cl_context cl_ctx;
   cl_command_queue command_queue;
   cl_mem out_buffer;
   cl_kernel kernel;
};
#ifdef __cplusplus
extern "C" {
#endif
unsigned clSimpleCreateContext(cl_context * context, cl_device_id device_id);
unsigned clSimpleCreateCommandQueue(cl_command_queue * command_queue,
                               cl_context context, cl_device_id device_id);
unsigned clSimpleKernelSetArg(cl_kernel kernel, cl_uint index, size_t size,
                         const void * value);
unsigned clSimpleCreateBuffer(cl_mem * buffer, cl_context context,
                              cl_mem_flags flags, size_t size);
unsigned clSimpleEnqueueWriteBuffer(cl_command_queue command_queue,
      cl_mem buffer, size_t buffer_size, void * ptr);
unsigned clSimpleEnqueueNDRangeKernel(cl_command_queue command_queue,
      cl_kernel kernel, cl_uint work_dim, const size_t * global_work_size,
      const size_t * local_work_size);

unsigned clSimpleInitGpuDevice(cl_device_id * device_id);
unsigned clSimpleCreateKernel(cl_context context, cl_device_id device_id,
                         cl_kernel * kernel, const char * kernel_name);
unsigned clSimpleCreateKernelString(cl_context context, cl_device_id device_id,
                                    cl_kernel * kernel, const char * kernel_name,
                                    const char * code, size_t code_len);
unsigned clSimpleSimpleInit(struct cl_simple_context * context,
                            const char * kernel_name);
unsigned clSimpleSetOutputBuffer(struct cl_simple_context * context,
                                 unsigned buffer_size);
unsigned clSimpleReadOutput(struct cl_simple_context * context, void * data,
      size_t data_bytes);
#ifdef __cplusplus
}
#endif
