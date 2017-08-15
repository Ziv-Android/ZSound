//
// Created by ziv on 2017/8/12.
//

#include "com_ziv_zsound_MainActivity.h"
#include "jni_log_util.h"

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

extern "C" {
#include <wavlib.h>
}

static const char *JAVA_IO_IOEXCEPTION = "java/io/IOException";
static const char *JAVA_LANG_OUTOFMEMORYERROR = "java/lang/OutOfMemoryError";

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))

struct PlayerContext {
    SLObjectItf engineObject;
    SLEngineItf engineEngine;
    SLObjectItf outputMixObject;
    SLObjectItf audioPlayerObject;
    SLAndroidSimpleBufferQueueItf audioPlayerBufferQueue;
    SLPlayItf audioPlayerPlay;

    WAV wav;

    unsigned char *buffer;
    size_t bufferSize;

    PlayerContext()
            : engineObject(0), engineEngine(0), outputMixObject(0), audioPlayerBufferQueue(0),
              audioPlayerPlay(0), wav(0), bufferSize(0) {}
};

/**
 * 从给定的类和消息中抛出异常
 *
 * @param env JNIEnv interface.
 * @param className class name.
 * @param message exception message.
 */
static void ThrowException(JNIEnv *env, const char *className, const char *message) {
    // 获取异常类
    jclass clazz = env->FindClass(className);

    // 获取成功
    if (0 != clazz) {
        // 抛出异常
        env->ThrowNew(clazz, message);
        env->DeleteLocalRef(clazz);
    }
}

/**
 * 打开给定的WAVE文件
 *
 * @param env JNIEnv interface
 * @param fileName file name
 * @return WAV file
 * @throws IOException
 */
static WAV OpenWavFile(JNIEnv *env, jstring fileName) {
    WAVError error = WAV_SUCCESS;
    WAV wav = 0;

    const char *cFileName = env->GetStringUTFChars(fileName, 0);
    if (0 == cFileName) {
        LOGE("Get file name failed.");
        goto exit;
    }

    // 打开WAVE文件
    wav = wav_open(cFileName, WAV_READ, &error);
    env->ReleaseStringUTFChars(fileName, cFileName);

    if (0 == wav) {
        LOGE("WAV file open failed");
        ThrowException(env, JAVA_IO_IOEXCEPTION, wav_strerror(error));
    }

    exit:
    return wav;
}

/**
 * 关闭给定的WAVE文件
 * @param wav WAV file
 * @throws IOException
 */
static void CloseWaveFile(WAV wav) {
    if (0 != wav) {
        wav_close(wav);
    }
}

/**
 * 将OpenSL ES结果码转换为字符串
 *
 * @param result result code
 * @return result string
 */
static const char *ResultToString(SLresult result) {
    const char *str = 0;
    switch (result) {
        case SL_RESULT_SUCCESS:
            str = "Success";
            break;
        case SL_RESULT_PRECONDITIONS_VIOLATED:
            str = "Preconditions violated";
            break;
        case SL_RESULT_PARAMETER_INVALID:
            str = "Parameter invalid";
            break;
        case SL_RESULT_MEMORY_FAILURE:
            str = "Memory failure";
            break;
        case SL_RESULT_RESOURCE_ERROR:
            str = "Resource error";
            break;
        case SL_RESULT_RESOURCE_LOST:
            str = "Resource lost";
            break;
        case SL_RESULT_IO_ERROR:
            str = "IO error";
            break;
        case SL_RESULT_BUFFER_INSUFFICIENT:
            str = "Buffer insufficient";
            break;
        case SL_RESULT_CONTENT_CORRUPTED:
            str = "Content corrupted";
            break;
        case SL_RESULT_CONTENT_UNSUPPORTED:
            str = "Content unsupported";
            break;
        case SL_RESULT_CONTENT_NOT_FOUND:
            str = "Content not found";
            break;
        case SL_RESULT_PERMISSION_DENIED:
            str = "Permission denied";
            break;
        case SL_RESULT_FEATURE_UNSUPPORTED:
            str = "Feature unsupported";
            break;
        case SL_RESULT_INTERNAL_ERROR:
            str = "Internal error";
            break;
        case SL_RESULT_UNKNOWN_ERROR:
            str = "Unknown error";
            break;
        case SL_RESULT_OPERATION_ABORTED:
            str = "Operation aborted";
            break;
        case SL_RESULT_CONTROL_LOST:
            str = "Control lost";
            break;
        default:
            str = "Unknown error code";
            break;
    }
    return str;
}

/**
 * 检查结果是否出错，并抛出错误信息的IOException
 *
 * @param env JNIEnv interface.
 * @param result result code
 * @return error occurred
 * @throws IOException
 */
static bool CheckError(JNIEnv *env, SLresult result) {
    bool isError = false;

    if (SL_RESULT_SUCCESS != result) {
        ThrowException(env, JAVA_IO_IOEXCEPTION, ResultToString(result));
        isError = true;
    }
    return isError;
}

static void CreateEngine(JNIEnv *env, SLObjectItf &engineObject) {
    // Android中OpenSL ES被设计未线程安全的，
    // 所以该选项请求将被忽略，但它应保证源代码可被一直到其他平台
    SLEngineOption engineOptions[] = {
            {(SLuint32) SL_ENGINEOPTION_THREADSAFE,
                    (SLuint32) SL_BOOLEAN_TRUE}
    };

    // 创建OpenSL ES引擎对象
    // 没有numInterfaces和SLInterfaceID这两个接口，传0即可
    SLresult result = slCreateEngine(&engineObject, ARRAY_LEN(engineOptions), engineOptions,
                                     0, 0, 0);
    // 错误检查
    CheckError(env, result);
}

/**
 * 实现给定的对象，Object在使用前应该先被实现
 *
 * @param env JNIEnv interface
 * @param object object instance
 * @throws IOException
 */
static void RealizeObject(JNIEnv *env, SLObjectItf object) {
    // 实现引擎对象 No async, blocking call
    SLresult result = (*object)->Realize(object, SL_BOOLEAN_FALSE);

    CheckError(env, result);
}

/**
 * 销毁给定的对象实例
 *
 * @param object object instance.[IN/OUT]
 */
static void DestroyObject(SLObjectItf object) {
    if (0 != object) {
        (*object)->Destroy(object);
    }
    object = 0;
}

/**
 * 从给懂的引擎对象中获取引擎接口以便从该引擎中创建其他对象
 *
 * @param env JNIEnv interface
 * @param engineObject engine object
 * @param engineEngine engine interface.[OUT]
 * @throws IOException
 */
static void GetEngineInterface(JNIEnv *env, SLObjectItf &engineObject, SLEngineItf &engineEngine) {
    // 获取引擎接口
    SLresult result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);

    CheckError(env, result);
}

/**
 * 创建和输出混合对象
 *
 * @param env JNIEnv interface.
 * @param engineEngine engine engine.
 * @param outputMixObject object to hold the output mix.[OUT]
 * @throws IOException
 */
static void CreateOutputMix(JNIEnv *env, SLEngineItf engineEngine, SLObjectItf &outputMixObject) {
    // 创建输出混合对象
    SLresult result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 0, 0, 0);

    CheckError(env, result);
}

/**
 * 释放播放器缓冲区
 *
 * @param buffers buffer instance.[OUT]
 */
static void FreePlayerBuffer(unsigned char *&buffers) {
    if (0 != buffers) {
        delete buffers;
        buffers = 0;
    }
}

/**
 * 初始化播放器缓冲区
 *
 * @param env JNIEnv interface
 * @param wav WAVE file
 * @param buffer buffer instance.[OUT]
 * @param bufferSize buffer size.[OUT]
 */
static void InitPlayerBuffer(JNIEnv *env, WAV wav, unsigned char *&buffer, size_t bufferSize) {
    // 计算缓冲区大小
    bufferSize = wav_get_channels(wav) * wav_get_rate(wav) * wav_get_bits(wav);

    // 初始化缓冲区
    buffer = new unsigned char[bufferSize];
    if (0 == buffer) {
        ThrowException(env, JAVA_LANG_OUTOFMEMORYERROR, "buffer out of memory");
    }
}

static void CreateBuifferQueueAudioPlayer(WAV wav, SLEngineItf engineEngine,
                                          SLObjectItf outputMixObject,
                                          SLObjectItf &audioPlayerObject) {
    SLDataLocator_AndroidSimpleBufferQueue dataSourceLocator = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,    // 定位器类型
            1                                           // 缓冲区数目
    };

    // PCM数据源格式
    SLDataFormat_PCM dataSourceFormat = {
            SL_DATAFORMAT_PCM,                      // 格式类型
            wav_get_channels(wav),                  // 通道数
            (SLuint32) (wav_get_rate(wav) * 1000),  // 毫赫兹/秒的样本数
            wav_get_bits(wav),                      // 每个降本的位数
            wav_get_bits(wav),                      // 容器大小
            SL_SPEAKER_FRONT_CENTER,                // 通道屏蔽
            SL_BYTEORDER_LITTLEENDIAN               // 字节顺序
    };

    // 数据源是含有PCM格式的简单缓冲区队列
    SLDataSource dataSource = {
            &dataSourceLocator, // 数据定位器
            &dataSourceFormat   // 数据格式
    };

    // 针对数据接收器的输出混合定位器
    SLDataLocator_OutputMix dataSinkLocator = {
            SL_DATALOCATOR_OUTPUTMIX,   // 定位器类型
            outputMixObject             // 输出混合
    };

    // 数据定位器是一个输出混合
    SLDataSink dataSink = {
            &dataSinkLocator,   // 定位器
            0                   // 格式
    };

    // 需要的接口
    SLInterfaceID interfaceIds[] = {
            SL_IID_BUFFERQUEUE
    };

    // 需要的接口。如果所需要的接口没有，请求将失败
    SLboolean requiredInterfaces[] = {
            SL_BOOLEAN_TRUE // for SL_IID_BUFFERQUEUE
    };

    // 创建音频播放器对象
    SLresult result = (*engineEngine)->CreateAudioPlayer(
            engineEngine,
            &audioPlayerObject,
            &dataSource,
            &dataSink,
            ARRAY_LEN(interfaceIds),
            interfaceIds,
            requiredInterfaces
    );
}

/**
 * 获取音频播放器缓冲区队列接口
 *
 * @param env JNIEnv interface.
 * @param audioPlayerObject audio player object instance.
 * @param audioPlayerBufferQueue audio player buffer queue.[OUT]
 * @throws IOException
 */
static void GetAudioPlayerBufferQueueInterface(JNIEnv *env, SLObjectItf audioPlayerObject,
                                               SLAndroidSimpleBufferQueueItf &audioPlayerBufferQueue) {
    // 获取缓冲队列接口
    SLresult result = (*audioPlayerObject)->GetInterface(audioPlayerObject, SL_IID_BUFFERQUEUE,
                                                         &audioPlayerBufferQueue);
    CheckError(env, result);
}

/**
 * 销毁播放器上下文
 *
 * @param ctx player context.
 */
static void DestroyContext(PlayerContext *&ctx) {
    // 销毁音频播放器对象
    DestroyObject(ctx->audioPlayerObject);

    // 释放播放器缓冲区
    FreePlayerBuffer(ctx->buffer);

    // 销毁输出混合对象
    DestroyObject(ctx->outputMixObject);

    // 销毁引擎实例
    DestroyObject(ctx->engineObject);

    // 关闭WAVE文件
    CloseWaveFile(ctx->wav);

    // 释放上下文
    delete ctx;
    ctx = 0;
}

/**
 * 缓冲区完成播放时调用
 *
 * @param audioPlayerBufferQueue audio player buffer queue.
 * @param context player context
 */
static void PlayerCallback(SLAndroidSimpleBufferQueueItf audioPlayerBufferQueue, void *context) {
    // 获得播放器上下文
    PlayerContext *ctx = (PlayerContext *) context;

    // 读取数据
    ssize_t readSize = wav_read_data(ctx->wav, ctx->buffer, ctx->bufferSize);

    if (0 < readSize) {
        (*audioPlayerBufferQueue)->Enqueue(
                audioPlayerBufferQueue,
                ctx->buffer,
                readSize
        );
    } else {
        DestroyContext(ctx);
    }
}

/**
 * 注册播放器回调
 *
 * @param env JNIEnv interface
 * @param audioPlayerBufferQueue audio player buffer queue.
 * @param ctx player context
 */
static void RegisterPlayerCallback(JNIEnv *env,
                                   SLAndroidSimpleBufferQueueItf audioPlayerBufferQueue,
                                   PlayerContext *ctx) {
    SLresult result = (*audioPlayerBufferQueue)->RegisterCallback(
            audioPlayerBufferQueue,
            PlayerCallback,
            ctx); // player context

    CheckError(env, result);
}

/**
 * 获得音频播放器播放接口
 *
 * @param env JNIEnv interface
 * @param audioPlayerObject audio player object instance.
 * @param audioPlayerPlay play interface.[OUT]
 * @throws IOException
 */
static void GetAudioPlayerPlayInterface(JNIEnv *env, SLObjectItf audioPlayerObject,
                                        SLPlayItf &audioPlayerPlay) {
    SLresult result = (*audioPlayerObject)->GetInterface(audioPlayerObject, SL_IID_PLAY,
                                                         &audioPlayerPlay);
    CheckError(env, result);
}

/**
 *
 *
 * @param env
 * @param audioPlayerPlay
 */
static void SetAudioPlayerStatePlaying(JNIEnv *env, SLPlayItf audioPlayerPlay) {
    SLresult result = (*audioPlayerPlay)->SetPlayState(
            audioPlayerPlay,
            SL_PLAYSTATE_PLAYING
    );

    CheckError(env, result);
}

/*
 * Class:     com_ziv_zsound_MainActivity
 * Method:    play
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_com_ziv_zsound_MainActivity_play
        (JNIEnv *env, jobject obj, jstring fileName) {
    PlayerContext *ctx = new PlayerContext();

    // 打开WAVE文件
    ctx->wav = OpenWavFile(env, fileName);
    LOGE("1.OpenWavFile");
    if (0 != env->ExceptionOccurred()) {
        goto exit;
    }

    // 创建OpenSL ES引擎
    CreateEngine(env, ctx->engineObject);
    LOGE("2.CreateEngine");
    if (0 != env->ExceptionOccurred()) {
        goto exit;
    }

    // 实现引擎对象
    RealizeObject(env, ctx->engineObject);
    LOGE("3.RealizeObject");
    if (0 != env->ExceptionOccurred()) {
        goto exit;
    }

    // 获取引擎接口
    GetEngineInterface(env, ctx->engineObject, ctx->engineEngine);
    LOGE("4.GetEngineInterface");
    if (0 != env->ExceptionOccurred()) {
        goto exit;
    }

    // 创建输出混合对象
    CreateOutputMix(env, ctx->engineEngine, ctx->outputMixObject);
    LOGE("5.CreateOutputMix");
    if (0 != env->ExceptionOccurred()) {
        goto exit;
    }

    // 实现输出混合对象
    RealizeObject(env, ctx->outputMixObject);
    LOGE("6.RealizeObject");
    if (0 != env->ExceptionOccurred()) {
        goto exit;
    }

    // 初始化缓冲区
    InitPlayerBuffer(env, ctx->wav, ctx->buffer, ctx->bufferSize);
    LOGE("7.InitPlayerBuffer");
    if (0 != env->ExceptionOccurred()) {
        goto exit;
    }

    // 创建缓冲区队列音频播放器对象
    CreateBuifferQueueAudioPlayer(ctx->wav, ctx->engineEngine, ctx->outputMixObject,
                                  ctx->audioPlayerObject);
    LOGE("8.CreateBuifferQueueAudioPlayer");
    if (0 != env->ExceptionOccurred()) {
        goto exit;
    }

    // 实现音频播放器对象
    RealizeObject(env, ctx->audioPlayerObject);
    LOGE("9.RealizeObject");
    if (0 != env->ExceptionOccurred()) {
        goto exit;
    }

    // 获取音频播放器缓冲区队列接口
    GetAudioPlayerBufferQueueInterface(env, ctx->audioPlayerObject, ctx->audioPlayerBufferQueue);
    LOGE("10.GetAudioPlayerBufferQueueInterface");
    if (0 != env->ExceptionOccurred()) {
        goto exit;
    }

    // 注册播放器回调函数
    RegisterPlayerCallback(env, ctx->audioPlayerBufferQueue, ctx);
    LOGE("11.RegisterPlayerCallback");
    if (0 != env->ExceptionOccurred()) {
        goto exit;
    }

    // 获取音频播放接口
    GetAudioPlayerPlayInterface(env, ctx->audioPlayerObject, ctx->audioPlayerPlay);
    LOGE("12.GetAudioPlayerPlayInterface");
    if (0 != env->ExceptionOccurred()) {
        goto exit;
    }

    // 设置音频播放器为播放状态
    SetAudioPlayerStatePlaying(env, ctx->audioPlayerPlay);
    LOGE("13.SetAudioPlayerStatePlaying");
    if (0 != env->ExceptionOccurred()) {
        goto exit;
    }
    // 将第一个缓冲区入队来启动运行
    PlayerCallback(ctx->audioPlayerBufferQueue, ctx);
    LOGE("14.PlayerCallback");

    exit:
    if (0 != env->ExceptionOccurred() && ctx != 0) {
        DestroyContext(ctx);
    }
}

/**
 * 对象：对象是针对定义明确任务所制定的抽象资源集合，
 *       对象可以对外提供一个或多个接口，对象可以通过引擎对象或对象接口创建
 * 接口：接口是对象提供的相关功能的抽象集合
 */