LOCAL_PATH:= $(call my-dir)/../../src

include $(CLEAR_VARS)

LOCAL_CPP_EXTENSION := .cpp

LOCAL_C_INCLUDES += \
	$(LOCAL_PATH)/../include \
	$(LOCAL_PATH)/../../belr/include \

LOCAL_SRC_FILES := \
	belcard_addressing.cpp \
	belcard_communication.cpp \
	belcard_explanatory.cpp \
	belcard_geographical.cpp \
	belcard_organizational.cpp \
	belcard_parser.cpp \
	belcard_rfc6474.cpp \
	belcard_utils.cpp \
	belcard_calendar.cpp \
	belcard.cpp \
	belcard_general.cpp \
	belcard_identification.cpp \
	belcard_params.cpp \
	belcard_property.cpp \
	belcard_security.cpp \
	vcard_grammar.cpp \
	
LOCAL_STATIC_LIBRARIES += libbelr

LOCAL_MODULE:= libbelcard

include $(BUILD_STATIC_LIBRARY)