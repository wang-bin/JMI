/*
 * JMI: JNI Modern Interface
 * Copyright (C) 2016-2017 Wang Bin - wbsecg1@gmail.com
 * MIT License
 */
#include "jmi.h"
#include <cassert>
#include <algorithm>
#include <pthread.h>
#include <iostream>
#if defined(__ANDROID__) || defined(ANDROID)
#define OS_ANDROID
#include <android/log.h>
#endif

using namespace std;
namespace jmi {

JavaVM* javaVM(JavaVM *vm) {
    static JavaVM *jvm_ = nullptr;
    if (vm)
        jvm_ = vm;
    return jvm_;
}

JNIEnv *getEnv() {
    assert(javaVM() && "javaVM() is null");
    if (!javaVM()) {
        cerr << "java vm is null" << endl;
        return nullptr;
    }
    JNIEnv* env = nullptr;
    int status = javaVM()->GetEnv((void**)&env, JNI_VERSION_1_4);
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
        "JNI Modern Interface\n");

        pthread_key_create(&key_, [](void*){
            JNIEnv* env = nullptr;
            if (javaVM()->GetEnv((void**)&env, JNI_VERSION_1_4) == JNI_EDETACHED)
                return;
            int status = javaVM()->DetachCurrentThread();
            if (status != JNI_OK)
                cerr <<  "DetachCurrentThread error: " << status << endl;
        });
    });
    env = (JNIEnv*)pthread_getspecific(key_);
    if (env)
        cerr << "TLS has a JNIEnv* but not attatched. Maybe detatched by user." << endl;
#ifdef OS_ANDROID
    status = javaVM()->AttachCurrentThread(&env, nullptr); //JavaVMAttachArgs
#else
    status = javaVM()->AttachCurrentThread((void**)&env, nullptr);
#endif
    if (status != JNI_OK) {
        cerr << "AttachCurrentThread failed: " << status << endl;
        return nullptr;
    }
    pthread_setspecific(key_, env);
    return env;
}

std::string to_string(jstring s)
{
    const char* cs = getEnv()->GetStringUTFChars(s, 0);
    if (!cs)
        return std::string();
    std::string ss(cs);
    getEnv()->ReleaseStringUTFChars(s, cs);
    //getEnv()->DeleteLocalRef(s);
    return ss;
}

jstring from_string(const std::string &s)
{
    JNIEnv* env = getEnv();
    jclass strClass = env->FindClass("java/lang/String");
    jmethodID ctorID = env->GetMethodID(strClass, "<init>", "([B)V");
    jbyteArray bytes = env->NewByteArray(s.size());
    env->SetByteArrayRegion(bytes, 0, s.size(), (jbyte*)s.data());
    jstring encoding = env->NewStringUTF("utf-8");
    return (jstring)env->NewObject(strClass, ctorID, bytes, encoding);
}

object::object(const std::string &class_path, jclass class_id, jobject obj_id)
    : instance_(nullptr), class_(nullptr) {
    init(obj_id, class_id, class_path);
}
object::object(jclass class_id, jobject obj_id) : object(std::string(), class_id, obj_id) {}
object::object(jobject obj_id) : object(std::string(), nullptr, obj_id) {}

object::object(const object &other) : object(other.class_path_, other.class_, other.instance_) {}
object::object(object &&other) // can not use default implemention
{
    std::swap(class_path_, other.class_path_);
    std::swap(instance_, other.instance_);
    std::swap(class_, other.class_);
    std::swap(error_, other.error_);
}

object &object::operator=(const object &other) {
    reset();
    class_path_ = other.class_path_;
    init(other.instance_, other.class_, class_path_);
    return *this;
}

object &object::operator=(object &&other) {
    std::swap(class_path_, other.class_path_);
    std::swap(instance_, other.instance_);
    std::swap(class_, other.class_);
    std::swap(error_, other.error_);
    return *this;
}

bool object::operator==(const object &other) const {
    JNIEnv *env = getEnv();
    if (!env)
        return false;
    jobject a = instance();
    jobject b = other.instance();
    if (a && b)
        return env->IsSameObject(a, b);
    a = get_class();
    b = other.get_class();
    return env->IsSameObject(a, b);
}

void object::init(jobject obj_id, jclass class_id, const std::string &class_path) {
    if (!obj_id && !class_id && class_path.empty())
        return;
    JNIEnv *env = getEnv();
    class_path_ = class_path;
    std::replace(class_path_.begin(), class_path_.end(), '.', '/');
    if (!class_id) {
        if (obj_id)
            class_id = env->GetObjectClass(obj_id); // TODO: clear class_path?
        else if (!class_path.empty())
            class_id = (jclass)env->FindClass(class_path_.data());
    }
    auto checker = call_on_exit([=]{
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        if (class_id)
            env->DeleteLocalRef(class_id);
    });
    if (!class_id)
        return;
    class_ = (jclass)env->NewGlobalRef(class_id);
    if (obj_id)
        instance_ = env->NewGlobalRef(obj_id);
    if (class_path_.empty() && instance_ && class_) // TODO: call a static method and instance_ is not required?
        class_path_ = object("java/lang/Class", class_).call_static<std::string>("getName");
    if (class_)
        return;
    std::string err("Could not find class");
    if (!class_path_.empty())
        err += " '" + class_path_ + "'.";
    set_error(err);
}

void object::reset() {
    error_.clear();
    JNIEnv *env = getEnv();
    if (class_) {
        env->DeleteGlobalRef(class_);
        class_ = nullptr;
    }
    if (instance_) {
        env->DeleteGlobalRef(instance_);
        instance_ = nullptr;
    }
}

object& object::set_error(const std::string& err) {
    error_ = err;
    return *this;
}


template<typename T>
static bool from_j(JNIEnv *env, jarray arr, T &container) {
    if (!arr)
        return false;
    jsize n = env->GetArrayLength(arr);
    for (size_t i = 0; i < n; i++) {
        typename T::value_type elm;
        fromJArrayElement(env, arr, i, elm);
        container.insert(container.end(), elm);
    }
    return true;
}
// Get an element of a java array
template<typename T>
static bool fromJArrayElement(JNIEnv *env, jarray arr, size_t position, T &out);
template<typename T>
static bool fromJArrayElement(JNIEnv *env, jarray arr, size_t position, std::vector<T> &out) {
    jobject elm;
    if (!fromJArrayElement(env, arr, position, elm))
        return false;
    return from_j(env, (jarray)elm, out);
}
template<typename T>
static bool from_jcollection(JNIEnv *env, jobject obj, T &out) {
    if (!obj)
        return false;
    object jcontainer(obj);
    if (!jcontainer.instance_of("java.util.Collection"))
        return false;
    out = jcontainer.call<T>("toArray", out, out);
    return true;
}

template<typename T>
static bool from_j(JNIEnv *env, jobject obj, T &out);
template<typename T>
static bool from_j(JNIEnv *env, jobject obj, std::vector<T> &out) {
    return from_jcollection(env, obj, out) || from_j(env, (jarray)obj, out);
}
template<typename T>
static bool from_j(JNIEnv *env, jobject obj, std::set<T> &out) {
    return from_jcollection(env, obj, out) || from_j(env, (jarray)obj, out);
}
template<typename T, std::size_t N>
static bool from_j(JNIEnv *env, jobject obj, std::array<T, N> &out) {
    return from_jcollection(env, obj, out) || from_j(env, (jarray)obj, out);
}

// utility methods that return the object
template<typename T>
static T from_j(JNIEnv *env, jobject obj) {
    T out;
    bool result = from_j(env, obj, out);
    assert(result);
    return out;
}
template<>
bool from_j(JNIEnv *env, jobject obj, std::string &out) {
    if (!obj) {
        out = "";
        return true;
    }
    jstring jstr = (jstring)obj;
    const char *chars = env->GetStringUTFChars(jstr, nullptr);
    if (!chars)
        return false;
    out = chars;
    env->ReleaseStringUTFChars(jstr, chars);
    env->DeleteLocalRef(jstr);
    return true;
}
template<>
bool from_j(JNIEnv *env, jobject obj, object &out) {
    out = obj;
    env->DeleteLocalRef(obj);
    return true;
}

template<>
void object::call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    env->CallVoidMethodA(obj_id, methodId, args);
}
template<>
jboolean object::call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return env->CallBooleanMethodA(obj_id, methodId, args);
}
template<>
jbyte object::call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return env->CallByteMethodA(obj_id, methodId, args);
}
template<>
jchar object::call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return env->CallCharMethodA(obj_id, methodId, args);
}
template<>
jobject object::call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return env->CallObjectMethodA(obj_id, methodId, args);
}
template<>
double object::call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return env->CallDoubleMethodA(obj_id, methodId, args);
}
template<>
jlong object::call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return env->CallLongMethodA(obj_id, methodId, args);
}
template<>
float object::call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return env->CallFloatMethodA(obj_id, methodId, args);
}
template<>
int object::call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return env->CallIntMethodA(obj_id, methodId, args);
}
template<>
std::string object::call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return from_j<std::string>(env, call_method<jobject>(env, obj_id, methodId, args));
}
template<>
object object::call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return from_j<object>(env, call_method<jobject>(env, obj_id, methodId, args));
}

template<>
void object::call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) const {
    env->CallStaticVoidMethodA(classId, methodId, args);
}
template<>
jobject object::call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) const {
    return env->CallStaticObjectMethodA(classId, methodId, args);
}
template<>
double object::call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) const {
    return env->CallStaticDoubleMethodA(classId, methodId, args);
}
template<>
jlong object::call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) const {
    return env->CallStaticLongMethodA(classId, methodId, args);
}
template<>
float object::call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) const {
    return env->CallStaticFloatMethodA(classId, methodId, args);
}
template<>
int object::call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) const {
    return env->CallStaticIntMethodA(classId, methodId, args);
}
template<>
std::string object::call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) const {
    return from_j<std::string>(env, call_static_method<jobject>(env, classId, methodId, args));
}
template<>
object object::call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) const {
    return from_j<object>(env, call_static_method<jobject>(env, classId, methodId, args));
}

template<> jvalue object::to_jvalue(const jboolean &obj) { return jvalue{.z = obj};}
template<> jvalue object::to_jvalue(const jbyte &obj) { return jvalue{.b = obj};}
template<> jvalue object::to_jvalue(const jchar &obj) { return jvalue{.c = obj};}
template<> jvalue object::to_jvalue(const jshort &obj) { return jvalue{.s = obj};}
template<> jvalue object::to_jvalue(const jint &obj) { return jvalue{.i = obj};}
template<> jvalue object::to_jvalue(const jlong &obj) { return jvalue{.j = obj};}
template<> jvalue object::to_jvalue(const jfloat &obj) { return jvalue{.f = obj};}
template<> jvalue object::to_jvalue(const jdouble &obj) { return jvalue{.d = obj};}
template<> jvalue object::to_jvalue(const object &obj) {
    return to_jvalue(obj.instance());
}
template<> jvalue object::to_jvalue(const std::string &obj) {
    return to_jvalue(getEnv()->NewStringUTF(obj.c_str()));
}
jvalue object::to_jvalue(const char* s) {
    return to_jvalue(getEnv()->NewStringUTF(s));
}

template<>
jarray object::to_jarray(JNIEnv *env, const jobject &element, size_t size) {
    return env->NewObjectArray(size, env->GetObjectClass(element), 0);
}
template<>
jarray object::to_jarray(JNIEnv *env, const bool&, size_t size) {
    return env->NewBooleanArray(size);
}
template<>
jarray object::to_jarray(JNIEnv *env, const char&, size_t size) {
    return env->NewByteArray(size); // must DeleteLocalRef
}
template<>
jarray object::to_jarray(JNIEnv *env, const jbyte&, size_t size) {
    return env->NewByteArray(size); // must DeleteLocalRef
}
template<>
jarray object::to_jarray(JNIEnv *env, const jchar&, size_t size) {
    return env->NewCharArray(size);
}
template<>
jarray object::to_jarray(JNIEnv *env, const jshort&, size_t size) {
    return env->NewShortArray(size);
}
template<>
jarray object::to_jarray(JNIEnv *env, const jint&, size_t size) {
    return env->NewIntArray(size);
}
template<>
jarray object::to_jarray(JNIEnv *env, const jlong&, size_t size) {
    return env->NewLongArray(size);
}
template<>
jarray object::to_jarray(JNIEnv *env, const float&, size_t size) {
    return env->NewFloatArray(size);
}
template<>
jarray object::to_jarray(JNIEnv *env, const double&, size_t size) {
    return env->NewDoubleArray(size);
}
template<>
jarray object::to_jarray(JNIEnv *env, const std::string&, size_t size) {
    return env->NewObjectArray(size, env->FindClass("java/lang/String"), 0);
}
template<>
jarray object::to_jarray(JNIEnv *env, const object &element, size_t size) {
    return env->NewObjectArray(size, element.get_class(), 0);
}

template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jobject &elm) {
    assert(n == 1 && "set only 1 jobject array element is allowed");
    env->SetObjectArrayElement((jobjectArray)arr, position, elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const bool &elm) {
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
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jboolean &elm) {
    env->SetBooleanArrayRegion((jbooleanArray)arr, position, n, &elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jbyte &elm) {
    env->SetByteArrayRegion((jbyteArray)arr, position, n, &elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const char &elm) {
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
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jchar &elm) {
    env->SetCharArrayRegion((jcharArray)arr, position, n, &elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jshort &elm) {
    env->SetShortArrayRegion((jshortArray)arr, position, n, &elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jint &elm) {
    env->SetIntArrayRegion((jintArray)arr, position, n, &elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jlong &elm) {
    env->SetLongArrayRegion((jlongArray)arr, position, n, &elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jfloat &elm) {
    env->SetFloatArrayRegion((jfloatArray)arr, position, n, &elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const jdouble &elm) {
    env->SetDoubleArrayRegion((jdoubleArray)arr, position, n, &elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const std::string &elm) {
    jobject obj = env->NewStringUTF(elm.c_str());
    set_jarray(env, arr, position, n, obj);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const object &elm) {
    set_jarray(env, arr, position, n, elm.instance());
}

template<> void object::from_jvalue(const jvalue& v, bool& t) { t = v.z;}
template<> void object::from_jvalue(const jvalue& v, char& t) { t = v.b;}
template<> void object::from_jvalue(const jvalue& v, uint8_t& t) { t = v.b;}
template<> void object::from_jvalue(const jvalue& v, int8_t& t) { t = v.b;}
template<> void object::from_jvalue(const jvalue& v, uint16_t& t) { t = v.c;} //jchar is 16bit, uint16_t or unsigned short. use wchar_t?
template<> void object::from_jvalue(const jvalue& v, int16_t& t) { t = v.s;}
template<> void object::from_jvalue(const jvalue& v, uint32_t& t) { t = v.i;}
template<> void object::from_jvalue(const jvalue& v, int32_t& t) { t = v.i;}
template<> void object::from_jvalue(const jvalue& v, uint64_t& t) { t = v.j;}
template<> void object::from_jvalue(const jvalue& v, jlong& t) { t = v.j;}
template<> void object::from_jvalue(const jvalue& v, float& t) { t = v.f;}
template<> void object::from_jvalue(const jvalue& v, double& t) { t = v.d;}
template<> void object::from_jvalue(const jvalue& v, std::string& t)
{
    const jstring s = static_cast<jstring>(v.l);
    const char* cs = getEnv()->GetStringUTFChars(s, 0);
    if (!cs)
        return;
    t = cs;
    getEnv()->ReleaseStringUTFChars(s, cs);
}

template<> void object::from_jarray(const jvalue& v, jboolean* t, std::size_t N)
{
    getEnv()->GetBooleanArrayRegion(static_cast<jbooleanArray>(v.l), 0, N, t);
}
template<> void object::from_jarray(const jvalue& v, char* t, std::size_t N)
{
    getEnv()->GetByteArrayRegion(static_cast<jbyteArray>(v.l), 0, N, (jbyte*)t);
}
template<> void object::from_jarray(const jvalue& v, jbyte* t, std::size_t N)
{
    getEnv()->GetByteArrayRegion(static_cast<jbyteArray>(v.l), 0, N, t);
}
template<> void object::from_jarray(const jvalue& v, jchar* t, std::size_t N)
{
    getEnv()->GetCharArrayRegion(static_cast<jcharArray>(v.l), 0, N, t);
}
template<> void object::from_jarray(const jvalue& v, jshort* t, std::size_t N)
{
    getEnv()->GetShortArrayRegion(static_cast<jshortArray>(v.l), 0, N, t);
}
template<> void object::from_jarray(const jvalue& v, jint* t, std::size_t N)
{
    getEnv()->GetIntArrayRegion(static_cast<jintArray>(v.l), 0, N, t);
}
template<> void object::from_jarray(const jvalue& v, jlong* t, std::size_t N)
{
    getEnv()->GetLongArrayRegion(static_cast<jlongArray>(v.l), 0, N, t);
}
template<> void object::from_jarray(const jvalue& v, float* t, std::size_t N)
{
    getEnv()->GetFloatArrayRegion(static_cast<jfloatArray>(v.l), 0, N, t);
}
template<> void object::from_jarray(const jvalue& v, double* t, std::size_t N)
{
    getEnv()->GetDoubleArrayRegion(static_cast<jdoubleArray>(v.l), 0, N, t);
}
} //namespace jmi
