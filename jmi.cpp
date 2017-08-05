/*
 * JMI: JNI Modern Interface
 * Copyright (C) 2016-2017 Wang Bin - wbsecg1@gmail.com
 * MIT License
 */
#include "jmi.h"
#include <cassert>
#include <iostream>
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
        cerr << "java vm is null" << endl;
        return nullptr;
    }
    JNIEnv* env = nullptr;
    int status = javaVM()->GetEnv((void**)&env, jni_ver); // TODO: version
    if (JNI_OK == status)
        return env;
    if (status != JNI_EDETACHED) {
        cerr << "GetEnv error: " << status << endl;
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
        "JMI: JNI Modern Interface\n");

        pthread_key_create(&key_, [](void*){
            JNIEnv* env = nullptr;
            if (javaVM()->GetEnv((void**)&env, jni_ver) == JNI_EDETACHED)
                return; //
            int status = javaVM()->DetachCurrentThread();
            if (status != JNI_OK)
                cerr <<  "DetachCurrentThread error: " << status << endl;
        });
    });
    env = (JNIEnv*)pthread_getspecific(key_);
    if (env)
        cerr << "TLS has a JNIEnv* but not attatched. Maybe detatched by user." << endl;
    JavaVMAttachArgs aa{};
    aa.version = jni_ver;
#ifdef OS_ANDROID
    status = javaVM()->AttachCurrentThread(&env, &aa);
#else
    status = javaVM()->AttachCurrentThread((void**)&env, &aa);
#endif
    if (status != JNI_OK) {
        cerr << "AttachCurrentThread failed: " << status << endl;
        return nullptr;
    }
    if (pthread_setspecific(key_, env) != 0) {
        cerr << "failed to set tls JNIEnv data" << endl;
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
    jclass strClass = env->FindClass("java/lang/String");
    jmethodID ctorID = env->GetMethodID(strClass, "<init>", "([B)V");
    jbyteArray bytes = env->NewByteArray(s.size());
    env->SetByteArrayRegion(bytes, 0, s.size(), (jbyte*)s.data());
    jstring encoding = env->NewStringUTF("utf-8");
    return (jstring)env->NewObject(strClass, ctorID, bytes, encoding);
}

namespace detail {

template<typename T>
static bool from_j(JNIEnv *env, jobject obj, T &out);

// utility methods that return the object
template<typename T>
static T from_j(JNIEnv *env, jobject obj) {
    T out;
    from_j(env, obj, out);
    return out;
}
template<>
bool from_j(JNIEnv *env, jobject obj, std::string &out) {
    out = to_string(static_cast<jstring>(obj), env);
    return !out.empty();
}

template<>
void call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    env->CallVoidMethodA(obj_id, methodId, args);
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
jobject call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    return env->CallObjectMethodA(obj_id, methodId, args);
}
template<>
jdouble call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    return env->CallDoubleMethodA(obj_id, methodId, args);
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
jint call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    return env->CallIntMethodA(obj_id, methodId, args);
}
template<>
std::string call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) {
    return from_j<std::string>(env, call_method<jobject>(env, obj_id, methodId, args));
}

template<>
void call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    env->CallStaticVoidMethodA(classId, methodId, args);
}
template<>
jobject call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    return env->CallStaticObjectMethodA(classId, methodId, args);
}
template<>
double call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    return env->CallStaticDoubleMethodA(classId, methodId, args);
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
jint call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    return env->CallStaticIntMethodA(classId, methodId, args);
}
template<>
std::string call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) {
    return from_j<std::string>(env, call_static_method<jobject>(env, classId, methodId, args));
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
    return to_jvalue(env->NewStringUTF(obj.c_str()), env);
}
jvalue to_jvalue(const char* s, JNIEnv* env) {
    return to_jvalue(env->NewStringUTF(s), env);
}

template<>
jarray make_jarray(JNIEnv *env, const jobject &element, size_t size) {
    return env->NewObjectArray(size, env->GetObjectClass(element), 0);
}
template<>
jarray make_jarray(JNIEnv *env, const bool&, size_t size) {
    return env->NewBooleanArray(size);
}
template<>
jarray make_jarray(JNIEnv *env, const char&, size_t size) {
    return env->NewByteArray(size); // must DeleteLocalRef
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
jarray make_jarray(JNIEnv *env, const float&, size_t size) {
    return env->NewFloatArray(size);
}
template<>
jarray make_jarray(JNIEnv *env, const double&, size_t size) {
    return env->NewDoubleArray(size);
}
template<>
jarray make_jarray(JNIEnv *env, const std::string&, size_t size) {
    return env->NewObjectArray(size, env->FindClass("java/lang/String"), 0);
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
void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const char &elm) {
    if (n == 1 || sizeof(jbyte) == sizeof(char)) {
        env->SetByteArrayRegion((jbyteArray)arr, position, n, (const jbyte*)&elm);
    } else {
        std::vector<jbyte> tmp(n);
        for (size_t i = 0; i < n; ++i)
            tmp[i] = *(&elm + i);
        env->SetByteArrayRegion((jbyteArray)arr, position, n, tmp.data());
    }
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
    jobject obj = env->NewStringUTF(elm.c_str());
    set_jarray(env, arr, position, n, obj);
}

template<> void from_jvalue(JNIEnv*, const jvalue& v, bool& t) { t = v.z;}
template<> void from_jvalue(JNIEnv*, const jvalue& v, char& t) { t = v.b;}
template<> void from_jvalue(JNIEnv*, const jvalue& v, uint8_t& t) { t = v.b;}
template<> void from_jvalue(JNIEnv*, const jvalue& v, int8_t& t) { t = v.b;}
template<> void from_jvalue(JNIEnv*, const jvalue& v, uint16_t& t) { t = v.c;} //jchar is 16bit, uint16_t or unsigned short. use wchar_t?
template<> void from_jvalue(JNIEnv*, const jvalue& v, int16_t& t) { t = v.s;}
template<> void from_jvalue(JNIEnv*, const jvalue& v, uint32_t& t) { t = v.i;}
template<> void from_jvalue(JNIEnv*, const jvalue& v, int32_t& t) { t = v.i;}
template<> void from_jvalue(JNIEnv*, const jvalue& v, uint64_t& t) { t = v.j;}
template<> void from_jvalue(JNIEnv*, const jvalue& v, jlong& t) { t = v.j;}
template<> void from_jvalue(JNIEnv*, const jvalue& v, float& t) { t = v.f;}
template<> void from_jvalue(JNIEnv*, const jvalue& v, double& t) { t = v.d;}
template<> void from_jvalue(JNIEnv* env, const jvalue& v, std::string& t)
{
    if (!env)
        env = getEnv();
    const jstring s = static_cast<jstring>(v.l);
    const char* cs = env->GetStringUTFChars(s, 0);
    if (!cs)
        return;
    t = cs;
    env->ReleaseStringUTFChars(s, cs);
}

template<> void from_jarray(JNIEnv* env, const jvalue& v, jboolean* t, std::size_t N)
{
    env->GetBooleanArrayRegion(static_cast<jbooleanArray>(v.l), 0, N, t);
}
template<> void from_jarray(JNIEnv* env, const jvalue& v, char* t, std::size_t N)
{
    env->GetByteArrayRegion(static_cast<jbyteArray>(v.l), 0, N, (jbyte*)t);
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
    set_field(env, oid, fid, jobject(env->NewStringUTF(v.c_str()))); // DeleteLocalRef?
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
    set_static_field(env, cid, fid, jobject(env->NewStringUTF(v.c_str())));
}

} // namespace detail
} //namespace jmi
