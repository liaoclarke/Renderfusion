TOP = external/mesa

PIPE_PREFIX ?= pipe_

PIPE_CPPFLAGS = \
	-DGALLIUM_RBUG \
	-DGALLIUM_TRACE \
	-DGALLIUM_GALAHAD \
	-I$(TOP)/include \
	-I$(TOP)/src/gallium/auxiliary \
	-I$(TOP)/src/gallium/drivers \
	-I$(TOP)/src/gallium/include \
	-I$(TOP)/src/gallium/winsys

PIPE_LIBS = \
	$(TOP)/src/gallium/drivers/identity/libidentity.a \
	$(TOP)/src/gallium/drivers/galahad/libgalahad.a \
	$(TOP)/src/gallium/drivers/trace/libtrace.a \
	$(TOP)/src/gallium/drivers/rbug/librbug.a \
	$(GALLIUM_AUXILIARIES)

PIPE_SYS = $(LIBDRM_LIB) -lm -lpthread $(DLOPEN_LIBS)

PIPE_CFLAGS = $(LIBDRM_CFLAGS)

PIPE_LDFLAGS = -Wl,--no-undefined

# r600 pipe driver
r600_LIBS = \
	$(TOP)/src/gallium/winsys/radeon/drm/libradeonwinsys.a \
	$(TOP)/src/gallium/drivers/r600/libr600.a
r600_SYS += -ldrm_radeon

# LLVM
ifeq ($(MESA_LLVM),1)
   PIPE_SYS += $(LLVM_LIBS)
   PIPE_LDFLAGS += $(LLVM_LDFLAGS)
endif

#TODO: include southern islands 
ifneq ($(findstring radeon/drm,$(GALLIUM_WINSYS_DIRS)),)
ifneq ($(findstring radeonsi,$(GALLIUM_DRIVERS_DIRS)),)
   _PIPE_TARGETS_CC += $(PIPE_PREFIX)radeonsi.so
   PIPE_SOURCES += pipe_radeonsi.c
endif
endif

LOCAL_PATH := $(call my-dir)

LOCAL_C_INCLUDES := \
	$(TOP)/include \
	$(TOP)/src/gallium/auxiliary \
	$(TOP)/src/gallium/drivers \
	$(TOP)/src/gallium/include \
	$(TOP)/src/gallium/winsys 

LOCAL_SRC_FILES := \
   pipe_r600.c 

LOCAL_MODULE := $(PIPE_PREFIX)r600
LOCAL_STATIC_LIBRARIES := libmesa_winsys_radeon libmesa_pipe_r600 libmesa_gallium
LOCAL_SHARED_LIBRARIES := libdrm libdrm_radeon libc libm libdl
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
