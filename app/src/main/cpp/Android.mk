LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

BOOTSTRAP_SRC := termux-bootstrap-zip.S

LOCAL_SRC_FILES := $(BOOTSTRAP_SRC) termux-bootstrap.c
LOCAL_MODULE := libtermux-bootstrap
LOCAL_CFLAGS += -Os -fno-stack-protector

include $(BUILD_SHARED_LIBRARY)
