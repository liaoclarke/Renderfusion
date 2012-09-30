LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    demo.c
LOCAL_C_INCLUDES := \
    external/drm/include/drm  \
    external/drm \

LOCAL_SHARED_LIBRARIES := libhardware libdrm
LOCAL_MODULE := gpu_probe
LOCAL_MODULE_TAGS := test
include $(BUILD_EXECUTABLE)
