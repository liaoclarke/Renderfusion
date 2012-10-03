LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES :=				\
	modetest.c

LOCAL_C_INCLUDES +=				\
	$(LOCAL_PATH)/../..			\
	$(LOCAL_PATH)/../../intel		\
	$(LOCAL_PATH)/../../include/drm

LOCAL_MODULE := modetest
LOCAL_MODULE_TAGS := debug

LOCAL_SHARED_LIBRARIES :=			\
	libdrm					\
	libdrm_intel

include $(BUILD_EXECUTABLE)
