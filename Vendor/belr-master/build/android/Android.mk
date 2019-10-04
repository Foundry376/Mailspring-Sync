LOCAL_PATH:= $(call my-dir)/../../src

include $(CLEAR_VARS)

LOCAL_CPP_EXTENSION := .cc

LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)/../include \

LOCAL_SRC_FILES := \
	abnf.cc \
	belr.cc \
	grammarbuilder.cc \
	parser.cc \

LOCAL_MODULE:= libbelr

include $(BUILD_STATIC_LIBRARY)