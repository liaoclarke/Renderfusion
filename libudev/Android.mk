LOCAL_PATH:= $(call my-dir)

# We need to build this for both the device (as a shared library)
# and the host (as a static library for tools to use).

common_SRC_FILES := \
	libudev-device-private.c \
	libudev-device.c \
	libudev-enumerate.c \
	libudev-list.c \
	libudev-monitor.c \
	libudev-queue-private.c \
	libudev-queue.c \
	libudev-util.c \
	libudev.c 

# For the device
# =====================================================

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(common_SRC_FILES)
LOCAL_CFLAGS += -DSYSCONFDIR=\""/etc"\"
LOCAL_C_INCLUDES += 
LOCAL_SHARED_LIBRARIES :=

LOCAL_MODULE:= libudev

include $(BUILD_STATIC_LIBRARY)
