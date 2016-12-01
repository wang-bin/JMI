/*
 * JMI: JNI Modern Interface
 * Copyright (C) 2016 Wang Bin - wbsecg1@gmail.com
 */
#include "jmi.h"
#include <cassert>
#include <algorithm>
#include <pthread.h>
#include <iostream>
#include <unordered_map>
#include <android/log.h>

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
        __android_log_print(ANDROID_LOG_INFO, "JMI", "JNI Modern Interface");
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
        cerr << "TLS has a JNIEnv* but not attatched!" << endl;
    status = javaVM()->AttachCurrentThread(&env, NULL);
    if (status != JNI_OK) {
        cerr << "AttachCurrentThread failed: " << status << endl;
        return nullptr;
    }
    pthread_setspecific(key_, env);
    return env;
}

jclass getClass(const std::string& class_path, bool cache)
{
    std::string path(class_path);
    std::replace(path.begin(), path.end(), '.', '/');
    typedef std::unordered_map<std::string, jclass> class_map;
    static class_map _classes;
    class_map::const_iterator it = _classes.find(path);
    if (it != _classes.end())
        return it->second;
    JNIEnv *env = getEnv();
    jclass c = (jclass)env->FindClass(path.c_str());
    if (!c) {
        env->ExceptionClear();
        return nullptr;
    }
    if (!cache)
        return c;
    c = (jclass)env->NewGlobalRef(c);
    _classes[path] = c;
    return c;
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
    JNIEnv *env = getEnv();
    class_path_ = class_path;
    std::replace(class_path_.begin(), class_path_.end(), '.', '/');
    if (class_id) {
        class_ = (jclass)env->NewGlobalRef(class_id);
    } else {
        if (obj_id)
            class_ = env->GetObjectClass(obj_id);
        else if (!class_path.empty())
            class_ = jmi::getClass(class_path_);
        class_path_.clear(); //?
    }
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
static bool fromJava(JNIEnv *env, jarray arr, T &container) {
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
template<typename T>
static bool fromJava(jarray arr, T &container) {
    return fromJava(getEnv(), arr, container);
}
// Get an element of a java array
template<typename T>
static bool fromJArrayElement(JNIEnv *env, jarray arr, size_t position, T &out);
template<typename T>
static bool fromJArrayElement(JNIEnv *env, jarray arr, size_t position, std::vector<T> &out) {
    jobject elm;
    if (!fromJArrayElement(env, arr, position, elm))
        return false;
    return fromJava(env, (jarray)elm, out);
}
template<typename T>
static bool fromJavaCollection(JNIEnv *env, jobject obj, T &out) {
    if (!obj)
        return false;
    object jcontainer(obj);
    if (!jcontainer.instance_of("java.util.Collection"))
        return false;
    out = jcontainer.call<T>("toArray", out, out);
    return true;
}

template<typename T>
static bool fromJava(JNIEnv *env, jobject obj, T &out);
template<typename T>
static bool fromJava(JNIEnv *env, jobject obj, std::vector<T> &out) {
    return fromJavaCollection(env, obj, out) || fromJava(env, (jarray)obj, out);
}
template<typename T>
static bool fromJava(JNIEnv *env, jobject obj, std::set<T> &out) {
    return fromJavaCollection(env, obj, out) || fromJava(env, (jarray)obj, out);
}
template<typename T, std::size_t N>
static bool fromJava(JNIEnv *env, jobject obj, std::array<T, N> &out) {
    return fromJavaCollection(env, obj, out) || fromJava(env, (jarray)obj, out);
}

// utility methods that return the object
template<typename T>
static T fromJava(JNIEnv *env, jobject obj) {
    T out;
    bool result = fromJava(env, obj, out);
    assert(result);
    return out;
}
template<typename T>
static T fromJava(jobject obj) {
    return fromJava<T>(getEnv(), obj);
}
template<>
bool fromJava(JNIEnv *env, jobject obj, std::string &out) {
    if (!obj) {
        out = "";
        return true;
    }
    jstring jstr = (jstring)obj;
    const char *chars = env->GetStringUTFChars(jstr, NULL);
    if (!chars)
        return false;
    out = chars;
    env->ReleaseStringUTFChars(jstr, chars);
    return true;
}
template<>
bool fromJava(JNIEnv *env, jobject obj, object &out) {
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
long object::call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return env->CallLongMethodA(obj_id, methodId, args);
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
    return fromJava<std::string>(env, call_method<jobject>(env, obj_id, methodId, args));
}
template<>
object object::call_method(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return fromJava<object>(env, call_method<jobject>(env, obj_id, methodId, args));
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
long object::call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) const {
    return env->CallStaticLongMethodA(classId, methodId, args);
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
    return fromJava<std::string>(env, call_static_method<jobject>(env, classId, methodId, args));
}
template<>
object object::call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) const {
    return fromJava<object>(env, call_static_method<jobject>(env, classId, methodId, args));
}

template<> jvalue object::to_jvalue(const bool &obj) {
    jvalue val;
    val.z = obj;
    return val;
}
template<> jvalue object::to_jvalue(const jboolean &obj) {
    jvalue val;
    val.z = obj;
    return val;
}
template<> jvalue object::to_jvalue(const jbyte &obj) {
    jvalue val;
    val.b = obj;
    return val;
}
template<> jvalue object::to_jvalue(const char &obj) {
    jvalue val;
    val.b = obj;
    return val;
}
template<> jvalue object::to_jvalue(const jchar &obj) {
    jvalue val;
    val.c = obj;
    return val;
}
template<> jvalue object::to_jvalue(const jshort &obj) {
    jvalue val;
    val.s = obj;
    return val;
}
template<> jvalue object::to_jvalue(const jint &obj) {
    jvalue val;
    val.i = obj;
    return val;
}
template<> jvalue object::to_jvalue(const unsigned int &obj) {
    jvalue val;
    val.i = obj;
    return val;
}
template<> jvalue object::to_jvalue(const long &obj) {
    jvalue val;
    val.j = obj;
    return val;
}
template<> jvalue object::to_jvalue(const jlong &obj) {
    jvalue val;
    val.j = obj;
    return val;
}
template<> jvalue object::to_jvalue(const jfloat &obj) {
    jvalue val;
    val.f = obj;
    return val;
}
template<> jvalue object::to_jvalue(const jdouble &obj) {
    jvalue val;
    val.d = obj;
    return val;
}
template<> jvalue object::to_jvalue(const jobject &obj) {
    jvalue val;
    val.l = obj;
    return val;
}
template<> jvalue object::to_jvalue(const object &obj) {
    return to_jvalue(obj.instance());
}
template<> jvalue object::to_jvalue(const jarray &obj) {
    jvalue val;
    val.l = obj;
    return val;
}
template<> jvalue object::to_jvalue(const jstring &obj) {
    jvalue val;
    val.l = obj;
    return val;
}
template<> jvalue object::to_jvalue(const std::string &obj) {
    JNIEnv *env = getEnv();
    if (!env)
        return jvalue();
    return to_jvalue(env->NewStringUTF(obj.c_str()));
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
    return env->NewByteArray(size);
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
jarray object::to_jarray(JNIEnv *env, const long&, size_t size) {
    return env->NewLongArray(size);
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
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, const jobject &elm) {
    env->SetObjectArrayElement((jobjectArray)arr, position, elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, const bool &elm) {
    const jboolean be(elm);
    env->SetBooleanArrayRegion((jbooleanArray)arr, position, 1, &be);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, const jboolean &elm) {
    const jboolean be(elm);
    env->SetBooleanArrayRegion((jbooleanArray)arr, position, 1, &be);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, const jbyte &elm) {
    env->SetByteArrayRegion((jbyteArray)arr, position, 1, &elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, const char &elm) {
    const jbyte be(elm);
    env->SetByteArrayRegion((jbyteArray)arr, position, 1, &be);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, const jchar &elm) {
    env->SetCharArrayRegion((jcharArray)arr, position, 1, &elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, const jshort &elm) {
    env->SetShortArrayRegion((jshortArray)arr, position, 1, &elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, const jint &elm) {
    env->SetIntArrayRegion((jintArray)arr, position, 1, &elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, const long &elm) {
    jlong jelm = elm;
    env->SetLongArrayRegion((jlongArray)arr, position, 1, &jelm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, const jlong &elm) {
    env->SetLongArrayRegion((jlongArray)arr, position, 1, &elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, const jfloat &elm) {
    env->SetFloatArrayRegion((jfloatArray)arr, position, 1, &elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, const jdouble &elm) {
    env->SetDoubleArrayRegion((jdoubleArray)arr, position, 1, &elm);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, const std::string &elm) {
    jobject obj = env->NewStringUTF(elm.c_str());
    set_jarray(env, arr, position, obj);
}
template<>
void object::set_jarray(JNIEnv *env, jarray arr, size_t position, const object &elm) {
    set_jarray(env, arr, position, elm.instance());
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
template<> void object::from_jvalue(const jvalue& v, long& t) { t = v.j;}
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
// TODO: std c++ types use a tmp jtype array, then assign
} //namespace jmi
