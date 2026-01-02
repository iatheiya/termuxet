LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

BOOTSTRAP_SRC := termux-bootstrap-zip.S

LOCAL_SRC_FILES := $(BOOTSTRAP_SRC) termux-bootstrap.c
LOCAL_MODULE := libtermux-bootstrap
LOCAL_CFLAGS += -Os -fno-stack-protector -I$(LOCAL_PATH) -fvisibility=hidden -Wall -Wextra -std=c11
LOCAL_ASFLAGS += -I$(LOCAL_PATH)
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)
LOCAL_LDFLAGS += -Wl,--no-undefined

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := termux_loader
LOCAL_SRC_FILES := termux_loader.c
LOCAL_CFLAGS += -fPIC -fPIE -DANDROID -Os -fvisibility=hidden -Wall -Wextra -std=c11 -fno-stack-protector
LOCAL_ASFLAGS += -I$(LOCAL_PATH)
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)
LOCAL_LDLIBS := -llog
LOCAL_LDFLAGS += -Wl,-z,now -Wl,-z,relro -Wl,--no-undefined

include $(BUILD_SHARED_LIBRARY)