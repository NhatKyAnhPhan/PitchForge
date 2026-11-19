#include "AndroidAudioHost.h"

#include <jni.h>

#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string_view>

namespace {

constexpr char kHostClassName[] =
    "com/auralis/auralis_studio/AndroidAudioHost";
constexpr char kLogTag[] = "AuralisAudio";
JavaVM* gJavaVm = nullptr;
jclass gHostClass = nullptr;

class ScopedJniEnv final {
public:
    ScopedJniEnv() noexcept {
        if (gJavaVm == nullptr) return;
        const auto result = gJavaVm->GetEnv(
            reinterpret_cast<void**>(&environment_),
            JNI_VERSION_1_6);
        if (result == JNI_EDETACHED) {
            if (gJavaVm->AttachCurrentThread(&environment_, nullptr) == JNI_OK) {
                attached_ = true;
            } else {
                environment_ = nullptr;
            }
        } else if (result != JNI_OK) {
            environment_ = nullptr;
        }
    }

    ~ScopedJniEnv() {
        if (attached_ && gJavaVm != nullptr) {
            gJavaVm->DetachCurrentThread();
        }
    }

    [[nodiscard]] JNIEnv* get() const noexcept { return environment_; }

private:
    JNIEnv* environment_ = nullptr;
    bool attached_ = false;
};

void logJniFailure(std::string_view method) noexcept {
    __android_log_print(
        ANDROID_LOG_WARN,
        kLogTag,
        "Could not call AndroidAudioHost.%.*s",
        static_cast<int>(method.size()),
        method.data());
}

jclass findHostClass(JNIEnv* environment) noexcept {
    if (gHostClass == nullptr) return nullptr;
    auto localClass = static_cast<jclass>(environment->NewLocalRef(gHostClass));
    if (localClass == nullptr || environment->ExceptionCheck()) {
        environment->ExceptionClear();
        return nullptr;
    }
    return localClass;
}

bool callBooleanMethod(const char* methodName) noexcept {
    ScopedJniEnv scopedEnvironment;
    JNIEnv* environment = scopedEnvironment.get();
    if (environment == nullptr) {
        logJniFailure(methodName);
        return false;
    }

    jclass hostClass = findHostClass(environment);
    if (hostClass == nullptr) {
        logJniFailure(methodName);
        return false;
    }
    const jmethodID method = environment->GetStaticMethodID(
        hostClass,
        methodName,
        "()Z");
    if (method == nullptr || environment->ExceptionCheck()) {
        environment->ExceptionClear();
        environment->DeleteLocalRef(hostClass);
        logJniFailure(methodName);
        return false;
    }

    const bool result =
        environment->CallStaticBooleanMethod(hostClass, method) == JNI_TRUE;
    if (environment->ExceptionCheck()) {
        environment->ExceptionClear();
        environment->DeleteLocalRef(hostClass);
        logJniFailure(methodName);
        return false;
    }
    environment->DeleteLocalRef(hostClass);
    return result;
}

void callVoidMethod(const char* methodName) noexcept {
    ScopedJniEnv scopedEnvironment;
    JNIEnv* environment = scopedEnvironment.get();
    if (environment == nullptr) {
        logJniFailure(methodName);
        return;
    }

    jclass hostClass = findHostClass(environment);
    if (hostClass == nullptr) {
        logJniFailure(methodName);
        return;
    }
    const jmethodID method = environment->GetStaticMethodID(
        hostClass,
        methodName,
        "()V");
    if (method == nullptr || environment->ExceptionCheck()) {
        environment->ExceptionClear();
        environment->DeleteLocalRef(hostClass);
        logJniFailure(methodName);
        return;
    }
    environment->CallStaticVoidMethod(hostClass, method);
    if (environment->ExceptionCheck()) {
        environment->ExceptionClear();
        logJniFailure(methodName);
    }
    environment->DeleteLocalRef(hostClass);
}

std::string toUtf8(JNIEnv* environment, jstring text) {
    if (text == nullptr) return {};
    const char* characters = environment->GetStringUTFChars(text, nullptr);
    if (characters == nullptr) {
        if (environment->ExceptionCheck()) environment->ExceptionClear();
        return {};
    }
    std::string result(characters);
    environment->ReleaseStringUTFChars(text, characters);
    return result;
}

std::vector<std::string> callDeviceSnapshot(bool forInput) {
    std::vector<std::string> result;
    ScopedJniEnv scopedEnvironment;
    JNIEnv* environment = scopedEnvironment.get();
    if (environment == nullptr) return result;

    jclass hostClass = findHostClass(environment);
    if (hostClass == nullptr) return result;
    const jmethodID method = environment->GetStaticMethodID(
        hostClass,
        "deviceSnapshot",
        "(Z)[Ljava/lang/String;");
    if (method == nullptr || environment->ExceptionCheck()) {
        environment->ExceptionClear();
        environment->DeleteLocalRef(hostClass);
        return result;
    }

    auto devices = static_cast<jobjectArray>(
        environment->CallStaticObjectMethod(
            hostClass,
            method,
            forInput ? JNI_TRUE : JNI_FALSE));
    if (environment->ExceptionCheck()) {
        environment->ExceptionClear();
        environment->DeleteLocalRef(hostClass);
        return result;
    }
    if (devices == nullptr) {
        environment->DeleteLocalRef(hostClass);
        return result;
    }

    const jsize count = environment->GetArrayLength(devices);
    result.reserve(static_cast<std::size_t>(std::max<jsize>(0, count)));
    for (jsize index = 0; index < count; ++index) {
        auto entry = static_cast<jstring>(
            environment->GetObjectArrayElement(devices, index));
        result.push_back(toUtf8(environment, entry));
        if (entry != nullptr) environment->DeleteLocalRef(entry);
    }
    environment->DeleteLocalRef(devices);
    environment->DeleteLocalRef(hostClass);
    return result;
}

auralis::audio::android::DeviceDescriptor parseDevice(
    const std::string& encoded) {
    auralis::audio::android::DeviceDescriptor device;
    const auto first = encoded.find('\t');
    const auto second = first == std::string::npos
        ? std::string::npos
        : encoded.find('\t', first + 1);
    const auto third = second == std::string::npos
        ? std::string::npos
        : encoded.find('\t', second + 1);
    if (first == std::string::npos || second == std::string::npos
        || third == std::string::npos) {
        device.name = encoded;
        return device;
    }

    try {
        device.id = std::stoi(encoded.substr(0, first));
        device.channelCount = static_cast<std::uint32_t>(
            std::max(0, std::stoi(encoded.substr(first + 1, second - first - 1))));
        device.isDefault =
            std::stoi(encoded.substr(second + 1, third - second - 1)) != 0;
        device.name = encoded.substr(third + 1);
    } catch (...) {
        device.id = -1;
        device.channelCount = 0;
        device.isDefault = false;
        device.name = encoded;
    }
    return device;
}

} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(
    JavaVM* virtualMachine,
    void*) {
    gJavaVm = virtualMachine;
    JNIEnv* environment = nullptr;
    if (virtualMachine->GetEnv(
            reinterpret_cast<void**>(&environment),
            JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    jclass localHostClass = environment->FindClass(kHostClassName);
    if (localHostClass == nullptr || environment->ExceptionCheck()) {
        environment->ExceptionClear();
        return JNI_ERR;
    }
    gHostClass = static_cast<jclass>(environment->NewGlobalRef(localHostClass));
    environment->DeleteLocalRef(localHostClass);
    if (gHostClass == nullptr || environment->ExceptionCheck()) {
        environment->ExceptionClear();
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_auralis_auralis_1studio_MainActivity_nativeSetHostState(
    JNIEnv*,
    jclass,
    jboolean isForeground,
    jboolean hasAudioFocus) {
    auralis::audio::android::setHostState(
        isForeground == JNI_TRUE,
        hasAudioFocus == JNI_TRUE);
}

namespace auralis::audio::android {

bool hasRecordPermission() noexcept {
    return callBooleanMethod("hasRecordPermission");
}

bool requestAudioFocus() noexcept {
    return callBooleanMethod("requestAudioFocus");
}

void abandonAudioFocus() noexcept {
    callVoidMethod("abandonAudioFocus");
}

std::vector<DeviceDescriptor> deviceSnapshot(bool forInput) {
    std::vector<DeviceDescriptor> result;
    const auto encodedDevices = callDeviceSnapshot(forInput);
    result.reserve(encodedDevices.size());
    for (const auto& encoded : encodedDevices) {
        result.push_back(parseDevice(encoded));
    }
    return result;
}

} // namespace auralis::audio::android
