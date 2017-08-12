LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := WAVPlayer
LOCAL_SRC_FILES := WAVPlayer.cpp

# 使用WAVLib静态库
LOCAL_STATIC_LIBRARIES += wavlib_static

# 与OpenSL ES链接
LOCAL_LDLIBS += -lOpenSLES

include $(BUILD_SHARED_LIBRARY)

# 引入WAVLib库模块
$(call import-module, transcode-1.1.7/avilib)