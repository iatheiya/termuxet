LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

BOOTSTRAP_SRC := termux-bootstrap-zip.S

LOCAL_SRC_FILES := $(BOOTSTRAP_SRC) termux-bootstrap.c
LOCAL_MODULE := libtermux-bootstrap
# Cleaned up flags, removed LOCAL_ARM_MODE to prevent forcing 32-bit/thumb on mixed builds
LOCAL_CFLAGS += -Os -fno-stack-protector -I$(LOCAL_PATH) -fvisibility=hidden -Wall -Wextra -std=c11
LOCAL_ASFLAGS += -I$(LOCAL_PATH)
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)
LOCAL_LDFLAGS += -Wl,--no-undefined

include $(BUILD_SHARED_LIBRARY)
