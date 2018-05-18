/*
 * JMI: JNI Modern Interface
 * Copyright (C) 2016-2018 Wang Bin - wbsecg1@gmail.com
 * https://github.com/wang-bin/JMI
 * MIT License
 */
#include "jmi.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <pthread.h>
#if defined(__ANDROID__) || defined(ANDROID)
#define OS_ANDROID
#include <android/log.h>
#endif

using namespace std;
namespace jmi {

static jint jni_ver = JNI_VERSION_1_4;

JavaVM* javaVM(JavaVM *vm, jint v) {
    static JavaVM *jvm_ = nullptr;
    if (vm)
        jvm_ = vm;
    jni_ver = v;
    return jvm_;
}

JNIEnv *getEnv() {
    assert(javaVM() && "javaVM() is null");
    if (!javaVM()) {
        clog << "JMI ERROR: java vm is null" << endl;
        return nullptr;
    }
    JNIEnv* env = nullptr;
    int status = javaVM()->GetEnv((void**)&env, jni_ver);
    if (JNI_OK == status)
        return env;
    if (status != JNI_EDETACHED) {
        if (status == JNI_EVERSION)
            clog << "JMI ERROR: requested JNI version is not supported";
        clog << "JMI ERROR: GetEnv " << status << endl;
        return nullptr;
    }
    static pthread_key_t key_ = 0; // static var can be captured in lambda
    static pthread_once_t key_once_ = PTHREAD_ONCE_INIT;
    pthread_once(&key_once_, []{
#ifdef OS_ANDROID
        __android_log_print(ANDROID_LOG_INFO, "JMI",
#else
        printf(
#endif
        "JMI: JNI Modern Interface. Version %d.%d.%d\n", JMI_MAJOR, JMI_MINOR, JMI_PATCH);

        pthread_key_create(&key_, [](void*){
            JNIEnv* env = nullptr;
            if (javaVM()->GetEnv((void**)&env, jni_ver) == JNI_EDETACHED)
                return; //
            int status = javaVM()->DetachCurrentThread();
            if (status != JNI_OK)
                clog <<  "JMI ERROR: DetachCurrentThread " << status << endl;
        });
    });
    env = (JNIEnv*)pthread_getspecific(key_);
    if (env)
        clog << "JMI ERROR: TLS has a JNIEnv* but not attatched. Maybe detatched by user." << endl;
    JavaVMAttachArgs aa{};
    aa.version = jni_ver;
#ifdef OS_ANDROID
    status = javaVM()->AttachCurrentThread(&env, &aa);
#else
    status = javaVM()->AttachCurrentThread((void**)&env, &aa);
#endif
    if (status != JNI_OK) {
        clog << "JMI ERROR: AttachCurrentThread " << status << endl;
        return nullptr;
    }
    if (pthread_setspecific(key_, env) != 0) {
        clog << "JMI ERROR: failed to set tls JNIEnv data" << endl;
        javaVM()->DetachCurrentThread();
        return nullptr;
    }
    return env;
}

std::string to_string(jstring s, JNIEnv* env)
{
    if (!s)
        return string();
    if (!env)
        env = getEnv();
    const char* cs = env->GetStringUTFChars(s, 0);
    if (!cs)
        return string();
    string ss(cs);
    env->ReleaseStringUTFChars(s, cs);
    env->DeleteLocalRef(s);
    return ss;
}

jstring from_string(const std::string &s, JNIEnv* env)
{
    if (!env)
        env = getEnv();
    return env->NewStringUTF(s.c_str());
}

namespace android {
jobject application(JNIEnv* env)
{
    if (!env)
        env = jmi::getEnv();
    LocalRef c_at = {env->FindClass("android/app/ActivityThread"), env};
    static jmethodID m_cat = env->GetStaticMethodID(c_at, "currentActivityThread", "()Landroid/app/ActivityThread;");
    static jmethodID m_ga = env->GetMethodID(c_at, "getApplication", "()Landroid/app/Application;");
    LocalRef at = {env->CallStaticObjectMethod(c_at, m_cat), env};
    return env->CallObjectMethod(at, m_ga);
}
} // namespace android

namespace detail {
bool handle_exception(JNIEnv* env) { //'static' function 'handle_exception' declared in header file should be declared 'static inline' [-Wunneeded-internal-declaration]
    if (!env)
        env = getEnv();
    if (!env->ExceptionCheck())
        return false;
    env->ExceptionDescribe();
    env->ExceptionClear();
    return true;
}

template<>
jobject call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    return env->CallObjectMethodA(obj_id, methodId, args);
}
template<>
jboolean call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    return env->CallBooleanMethodA(obj_id, methodId, args);
}
template<>
jbyte call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    return env->CallByteMethodA(obj_id, methodId, args);
}
template<>
jchar call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    return env->CallCharMethodA(obj_id, methodId, args);
}
template<>
jint call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    return env->CallIntMethodA(obj_id, methodId, args);
}
template<>
jshort call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    return env->CallShortMethodA(obj_id, methodId, args);
}
template<>
jlong call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    return env->CallLongMethodA(obj_id, methodId, args);
}
template<>
jfloat call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    return env->CallFloatMethodA(obj_id, methodId, args);
}
template<>
jdouble call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    return env->CallDoubleMethodA(obj_id, methodId, args);
}
template<>
void call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    env->CallVoidMethodA(obj_id, methodId, args);
}
template<>
std::string call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    return to_string(static_cast<jstring>(call_method<jobject>(env, obj_id, methodId, args)), env);
}

template<>
jobject call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    return env->CallStaticObjectMethodA(classId, methodId, args);
}
template<>
jboolean call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    return env->CallStaticBooleanMethodA(classId, methodId, args);
}
template<>
jchar call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    return env->CallStaticCharMethodA(classId, methodId, args);
}
template<>
jbyte call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    return env->CallStaticByteMethodA(classId, methodId, args);
}
template<>
jshort call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    return env->CallStaticShortMethodA(classId, methodId, args);
}
template<>
jint call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    return env->CallStaticIntMethodA(classId, methodId, args);
}
template<>
jlong call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    return env->CallStaticLongMethodA(classId, methodId, args);
}
template<>
jfloat call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    return env->CallStaticFloatMethodA(classId, methodId, args);
}
template<>
jdouble call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    return env->CallStaticDoubleMethodA(classId, methodId, args);
}
template<>
void call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    env->CallStaticVoidMethodA(classId, methodId, args);
}
template<>
std::string call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    return to_string(static_cast<jstring>(call_static_method<jobject>(env, classId, methodId, args)), env);
}

template<> jvalue to_jvalue(const jboolean &obj, JNIEnv* env) { return jvalue{.z = obj};}
template<> jvalue to_jvalue(const jbyte &obj, JNIEnv* env) { return jvalue{.b = obj};}
template<> jvalue to_jvalue(const jchar &obj, JNIEnv* env) { return jvalue{.c = obj};}
template<> jvalue to_jvalue(const jshort &obj, JNIEnv* env) { return jvalue{.s = obj};}
template<> jvalue to_jvalue(const jint &obj, JNIEnv* env) { return jvalue{.i = obj};}
template<> jvalue to_jvalue(const jlong &obj, JNIEnv* env) { return jvalue{.j = obj};}
template<> jvalue to_jvalue(const jfloat &obj, JNIEnv* env) { return jvalue{.f = obj};}
template<> jvalue to_jvalue(const jdouble &obj, JNIEnv* env) { return jvalue{.d = obj};}

template<> jvalue to_jvalue(const std::string &obj, JNIEnv* env) {
    return to_jvalue(obj.c_str(), env);
}
jvalue to_jvalue(const char* s, JNIEnv* env) {
    return to_jvalue(env->NewStringUTF(s), env); // local ref will be deleted in set_ref_from_jvalue
}

template<>
jarray make_jarray(JNIEnv *env, const jobject &element, size_t size) {
    return env->NewObjectArray(size, env->GetObjectClass(element), 0);
}
template<>
jarray make_jarray(JNIEnv *env, const jboolean&, size_t size) {
    return env->NewBooleanArray(size);
}
template<>
jarray make_jarray(JNIEnv *env, const jbyte&, size_t size) {
    return env->NewByteArray(size); // must DeleteLocalRef
}
template<>
jarray make_jarray(JNIEnv *env, const jchar&, size_t size) {
    return env->NewCharArray(size);
}
template<>
jarray make_jarray(JNIEnv *env, const jshort&, size_t size) {
    return env->NewShortArray(size);
}
template<>
jarray make_jarray(JNIEnv *env, const jint&, size_t size) {
    return env->NewIntArray(size);
}
template<>
jarray make_jarray(JNIEnv *env, const jlong&, size_t size) {
    return env->NewLongArray(size);
}
template<>
jarray make_jarray(JNIEnv *env, const jfloat&, size_t size) {
    return env->NewFloatArray(size);
}
template<>
jarray make_jarray(JNIEnv *env, const jdouble&, size_t size) {
    return env->NewDoubleArray(size);
}
template<>
jarray make_jarray(JNIEnv *env, const std::string&, size_t size) {
    return env->NewObjectArray(size, env->FindClass("java/lang/String"), nullptr);
}
template<>
jarray make_jarray(JNIEnv *env, const char&, size_t size) {
    return env->NewByteArray(size); // must DeleteLocalRef
}

template<>
void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jobject &elm) {
    assert(n == 1 && "set only 1 jobject array element is allowed");
    env->SetObjectArrayElement((jobjectArray)arr, position, elm);
}
template<>
void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const bool &elm) {
    if (n == 1 || sizeof(jboolean) == sizeof(bool)) {
        env->SetBooleanArrayRegion((jbooleanArray)arr, position, n, (const jboolean*)&elm);
    } else {
        std::vector<jboolean> tmp(n);
        for (size_t i = 0; i < n; ++i)
            tmp[i] = *(&elm + i);
        env->SetBooleanArrayRegion((jbooleanArray)arr, position, n, tmp.data());
    }
}
template<>
void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jboolean &elm) {
    env->SetBooleanArrayRegion((jbooleanArray)arr, position, n, &elm);
}
template<>
void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jbyte &elm) {
    env->SetByteArrayRegion((jbyteArray)arr, position, n, &elm);
}
template<>
void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jchar &elm) {
    env->SetCharArrayRegion((jcharArray)arr, position, n, &elm);
}
template<>
void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jshort &elm) {
    env->SetShortArrayRegion((jshortArray)arr, position, n, &elm);
}
template<>
void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jint &elm) {
    env->SetIntArrayRegion((jintArray)arr, position, n, &elm);
}
template<>
void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jlong &elm) {
    env->SetLongArrayRegion((jlongArray)arr, position, n, &elm);
}
template<>
void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jfloat &elm) {
    env->SetFloatArrayRegion((jfloatArray)arr, position, n, &elm);
}
template<>
void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jdouble &elm) {
    env->SetDoubleArrayRegion((jdoubleArray)arr, position, n, &elm);
}
template<>
void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const std::string &elm) {
    for (size_t i = 0; i < n; ++i) {
        const string& s = *(&elm + i);
        set_jarray(env, arr, position + i, 1, (jobject)from_string(s, env));
    }
}

// no need to specialize other types(jchar, jint etc.) because java parameters are passed by value but not reference. specialize jobject, jarray is ok, now we use jlong for them
template<> void from_jvalue(JNIEnv*, const jvalue& v, jlong& t) { t = v.j;}

template<> void from_jarray(JNIEnv* env, const jvalue& v, jboolean* t, std::size_t N)
{
    env->GetBooleanArrayRegion(static_cast<jbooleanArray>(v.l), 0, N, t);
}
template<> void from_jarray(JNIEnv* env, const jvalue& v, jbyte* t, std::size_t N)
{
    env->GetByteArrayRegion(static_cast<jbyteArray>(v.l), 0, N, t);
}
template<> void from_jarray(JNIEnv* env, const jvalue& v, jchar* t, std::size_t N)
{
    env->GetCharArrayRegion(static_cast<jcharArray>(v.l), 0, N, t);
}
template<> void from_jarray(JNIEnv* env, const jvalue& v, jshort* t, std::size_t N)
{
    env->GetShortArrayRegion(static_cast<jshortArray>(v.l), 0, N, t);
}
template<> void from_jarray(JNIEnv* env, const jvalue& v, jint* t, std::size_t N)
{
    env->GetIntArrayRegion(static_cast<jintArray>(v.l), 0, N, t);
}
template<> void from_jarray(JNIEnv* env, const jvalue& v, jlong* t, std::size_t N)
{
    env->GetLongArrayRegion(static_cast<jlongArray>(v.l), 0, N, t);
}
template<> void from_jarray(JNIEnv* env, const jvalue& v, float* t, std::size_t N)
{
    env->GetFloatArrayRegion(static_cast<jfloatArray>(v.l), 0, N, t);
}
template<> void from_jarray(JNIEnv* env, const jvalue& v, double* t, std::size_t N)
{
    env->GetDoubleArrayRegion(static_cast<jdoubleArray>(v.l), 0, N, t);
}
template<> void from_jarray(JNIEnv* env, const jvalue& v, char* t, std::size_t N)
{
    env->GetByteArrayRegion(static_cast<jbyteArray>(v.l), 0, N, (jbyte*)t);
}
template<> void from_jarray(JNIEnv* env, const jvalue& v, std::string* t, std::size_t N)
{
    for (size_t i = 0; i < N; ++i) {
        LocalRef s = {env->GetObjectArrayElement(static_cast<jobjectArray>(v.l), i), env};
        *(t + i) = to_string(s);
    }
}

////////// Field //////////
template<>
jobject get_field(JNIEnv* env, jobject oid, jfieldID fid) {
    return env->GetObjectField(oid, fid);
}
template<>
jboolean get_field(JNIEnv* env, jobject oid, jfieldID fid) {
    return env->GetBooleanField(oid, fid);
}
template<>
jbyte get_field(JNIEnv* env, jobject oid, jfieldID fid) {
    return env->GetByteField(oid, fid);
}
template<>
jchar get_field(JNIEnv* env, jobject oid, jfieldID fid) {
    return env->GetCharField(oid, fid);
}
template<>
jshort get_field(JNIEnv* env, jobject oid, jfieldID fid) {
    return env->GetShortField(oid, fid);
}
template<>
jint get_field(JNIEnv* env, jobject oid, jfieldID fid) {
    return env->GetIntField(oid, fid);
}
template<>
jlong get_field(JNIEnv* env, jobject oid, jfieldID fid) {
    return env->GetLongField(oid, fid);
}
template<>
jfloat get_field(JNIEnv* env, jobject oid, jfieldID fid) {
    return env->GetFloatField(oid, fid);
}
template<>
jdouble get_field(JNIEnv* env, jobject oid, jfieldID fid) {
    return env->GetDoubleField(oid, fid);
}
template<>
std::string get_field(JNIEnv* env, jobject oid, jfieldID fid) {
    return to_string((jstring)get_field<jobject>(env, oid, fid), env);
}

template<>
void set_field(JNIEnv* env, jobject oid, jfieldID fid, jobject&& v) {
    env->SetObjectField(oid, fid, v);
}
template<>
void set_field(JNIEnv* env, jobject oid, jfieldID fid, jboolean&& v) {
    env->SetBooleanField(oid, fid, v);
}
template<>
void set_field(JNIEnv* env, jobject oid, jfieldID fid, jbyte&& v) {
    env->SetByteField(oid, fid, v);
}
template<>
void set_field(JNIEnv* env, jobject oid, jfieldID fid, jchar&& v) {
    env->SetCharField(oid, fid, v);
}
template<>
void set_field(JNIEnv* env, jobject oid, jfieldID fid, jshort&& v) {
    env->SetShortField(oid, fid, v);
}
template<>
void set_field(JNIEnv* env, jobject oid, jfieldID fid, jint&& v) {
    env->SetIntField(oid, fid, v);
}
template<>
void set_field(JNIEnv* env, jobject oid, jfieldID fid, jlong&& v) {
    env->SetLongField(oid, fid, v);
}
template<>
void set_field(JNIEnv* env, jobject oid, jfieldID fid, jfloat&& v) {
    env->SetFloatField(oid, fid, v);
}
template<>
void set_field(JNIEnv* env, jobject oid, jfieldID fid, jdouble&& v) {
    env->SetDoubleField(oid, fid, v);
}
template<>
void set_field(JNIEnv* env, jobject oid, jfieldID fid, std::string&& v) {
    LocalRef js = {from_string(v, env), env};
    set_field(env, oid, fid, jobject(js));
}

////////// Static Field //////////
template<>
jobject get_static_field(JNIEnv* env, jclass cid, jfieldID fid) {
    return env->GetStaticObjectField(cid, fid);
}
template<>
jboolean get_static_field(JNIEnv* env, jclass cid, jfieldID fid) {
    return env->GetStaticBooleanField(cid, fid);
}
template<>
jbyte get_static_field(JNIEnv* env, jclass cid, jfieldID fid) {
    return env->GetStaticByteField(cid, fid);
}
template<>
jchar get_static_field(JNIEnv* env, jclass cid, jfieldID fid) {
    return env->GetStaticCharField(cid, fid);
}
template<>
jshort get_static_field(JNIEnv* env, jclass cid, jfieldID fid) {
    return env->GetStaticShortField(cid, fid);
}
template<>
jint get_static_field(JNIEnv* env, jclass cid, jfieldID fid) {
    return env->GetStaticIntField(cid, fid);
}
template<>
jlong get_static_field(JNIEnv* env, jclass cid, jfieldID fid) {
    return env->GetStaticLongField(cid, fid);
}
template<>
jfloat get_static_field(JNIEnv* env, jclass cid, jfieldID fid) {
    return env->GetStaticFloatField(cid, fid);
}
template<>
jdouble get_static_field(JNIEnv* env, jclass cid, jfieldID fid) {
    return env->GetStaticDoubleField(cid, fid);
}
template<>
std::string get_static_field(JNIEnv* env, jclass cid, jfieldID fid) {
    return to_string((jstring)get_static_field<jobject>(env, cid, fid), env);
}

template<>
void set_static_field(JNIEnv* env, jclass cid, jfieldID fid, jobject&& v) {
    env->SetStaticObjectField(cid, fid, v);
}
template<>
void set_static_field(JNIEnv* env, jclass cid, jfieldID fid, jboolean&& v) {
    env->SetStaticBooleanField(cid, fid, v);
}
template<>
void set_static_field(JNIEnv* env, jclass cid, jfieldID fid, jbyte&& v) {
    env->SetStaticByteField(cid, fid, v);
}
template<>
void set_static_field(JNIEnv* env, jclass cid, jfieldID fid, jchar&& v) {
    env->SetStaticCharField(cid, fid, v);
}
template<>
void set_static_field(JNIEnv* env, jclass cid, jfieldID fid, jshort&& v) {
    env->SetStaticShortField(cid, fid, v);
}
template<>
void set_static_field(JNIEnv* env, jclass cid, jfieldID fid, jint&& v) {
    env->SetStaticIntField(cid, fid, v);
}
template<>
void set_static_field(JNIEnv* env, jclass cid, jfieldID fid, jlong&& v) {
    env->SetStaticLongField(cid, fid, v);
}
template<>
void set_static_field(JNIEnv* env, jclass cid, jfieldID fid, jfloat&& v) {
    env->SetStaticFloatField(cid, fid, v);
}
template<>
void set_static_field(JNIEnv* env, jclass cid, jfieldID fid, jdouble&& v) {
    env->SetStaticDoubleField(cid, fid, v);
}
template<>
void set_static_field(JNIEnv* env, jclass cid, jfieldID fid, std::string&& v) {
    LocalRef js = {from_string(v), env};
    set_static_field(env, cid, fid, (jobject)js);
}
} // namespace detail
} //namespace jmi
