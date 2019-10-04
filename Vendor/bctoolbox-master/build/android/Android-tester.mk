LOCAL_PATH:= $(call my-dir)/../../src/
include $(CLEAR_VARS)

LOCAL_MODULE:= libbctoolbox_tester

LOCAL_SRC_FILES := \
	tester/utils.c

LOCAL_CFLAGS += -Wno-maybe-uninitialized -DIN_BCUNIT_SOURCES -DHAVE_CU_SET_TRACE_HANDLER=1 -DHAVE_CU_GET_SUITE=1

LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)/ \
	$(LOCAL_PATH)/../include \
	$(LOCAL_PATH)/../../externals/bcunit/BCUnit/Headers

include $(BUILD_STATIC_LIBRARY)
