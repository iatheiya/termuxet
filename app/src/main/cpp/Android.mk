LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

BOOTSTRAP_SRC := termux-bootstrap-zip.S

LOCAL_SRC_FILES := $(BOOTSTRAP_SRC) termux-bootstrap.c
LOCAL_MODULE := libtermux-bootstrap
LOCAL_CFLAGS += -Os -fno-stack-protector -I$(LOCAL_PATH) -fvisibility=hidden
LOCAL_ASFLAGS += -I$(LOCAL_PATH)
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)

include $(BUILD_SHARED_LIBRARY)