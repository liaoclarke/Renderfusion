TOP := external/mesa
LOCAL_PATH := $(call my-dir)
LOCAL_SRC_FILES := \
    compute.c
LOCAL_C_INCLUDES := \
	$(TOP)/src/gallium/include \
	$(TOP)/src/gallium/auxiliary \
	$(TOP)/src/gallium/drivers \
	$(TOP)/src/gallium/winsys 

LOCAL_MODULE := pipes
LOCAL_MODULE_TAGS := tests
LOCAL_STATIC_LIBRARIES += libmesa_gallium
LOCAL_SHARED_LIBRARIES += libc libm
include $(BUILD_EXECUTABLE)
