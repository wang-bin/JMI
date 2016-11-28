/*
 * JMI: JNI Modern Interface
 * Copyright (C) 2016 Wang Bin - wbsecg1@gmail.com
 */
#include "jmi.h"
#include <algorithm>
#include <pthread.h>
#include <iostream>
#include <unordered_map>

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

object::object(const std::string &class_path, jobject obj_id, jclass class_id)
    : instance_(nullptr), class_(nullptr) {
    init(obj_id, class_id, class_path);
}

object::object(jobject obj_id, jclass class_id) : object(std::string(), obj_id, class_id) {}

object::object(const object &other) : object(other.class_path_, other.instance_, other.class_) {}
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
    if (class_path_.empty() && instance_ && class_)
        class_path_ = object("java/lang/Class", class_).call<std::string>("getName");
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
void object::callMethod(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    env->CallVoidMethodA(obj_id, methodId, args);
}
template<>
jboolean object::callMethod(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return env->CallBooleanMethodA(obj_id, methodId, args);
}
template<>
jobject object::callMethod(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return env->CallObjectMethodA(obj_id, methodId, args);
}
template<>
double object::callMethod(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
  return env->CallDoubleMethodA(obj_id, methodId, args);
}
template<>
long object::callMethod(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return env->CallLongMethodA(obj_id, methodId, args);
}
template<>
jlong object::callMethod(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return env->CallLongMethodA(obj_id, methodId, args);
}
template<>
float object::callMethod(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return env->CallFloatMethodA(obj_id, methodId, args);
}
template<>
int object::callMethod(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return env->CallIntMethodA(obj_id, methodId, args);
}
template<>
std::string object::callMethod(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return fromJava<std::string>(env, callMethod<jobject>(env, obj_id, methodId, args));
}
template<>
object object::callMethod(JNIEnv *env, jobject obj_id, jmethodID methodId, jvalue *args) const {
    return fromJava<object>(env, callMethod<jobject>(env, obj_id, methodId, args));
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
} //namespace jmi
