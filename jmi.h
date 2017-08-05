/*
 * JMI: JNI Modern Interface
 * Copyright (C) 2016-2017 Wang Bin - wbsecg1@gmail.com
 * MIT License
 */
// TODO: hidden inline; reset error before each call, reset exception after each call (Aspect pattern?)
#pragma once
#include <algorithm>
#include <array>
#include <functional> // std::ref
#include <string>
#include <type_traits>
#include <valarray>
#include <vector>
#include <jni.h>

namespace jmi {
/*************************** JMI Public APIs Begin ***************************/

// set JavaVM to vm if not null. return previous JavaVM
JavaVM* javaVM(JavaVM *vm = nullptr, jint version = JNI_VERSION_1_4);
JNIEnv *getEnv();
std::string to_string(jstring s, JNIEnv* env = nullptr);
jstring from_string(const std::string& s, JNIEnv* env = nullptr);

struct ClassTag {}; // used by JObject<Tag>. subclasses must define static std::string() name(), with or without "L ;" around
struct MethodTag {}; // used by call() and callStatic(). subclasses must define static const char* name();
struct FieldTag {}; // subclasses must define static const char* name();
template<class Tag>
using if_ClassTag = typename std::enable_if<std::is_base_of<ClassTag, Tag>::value, bool>::type;
template<class Tag>
using if_MethodTag = typename std::enable_if<std::is_base_of<MethodTag, Tag>::value, bool>::type;
template<class Tag>
using if_FieldTag = typename std::enable_if<std::is_base_of<FieldTag, Tag>::value, bool>::type;
template<class CTag, if_ClassTag<CTag>> class JObject;
//template<typename T> // jni primitive types(not all fundamental types), jobject, jstring, ..., JObject, c++ array types
//using if_jni_type = typename std::enable_if<std::is_fundamental<T>::value || if_array<T>::value ||
template<typename T> struct signature;

// object must be a class template, thus we can cache class id using static member and call FindClass() only once, and also make it possible to cache method id because method id
template<class CTag, if_ClassTag<CTag> = true>
class JObject
{
public:
    static const std::string className() {
        static std::string s(normalizeClassName(CTag::name()));
        return s;
    }
    static const std::string signature() {return "L" + className() + ";";}

    JObject() {} // TODO: from jobject
    ~JObject() { reset(); }
    JObject(const JObject &other) { *this = other; }
    JObject &operator=(const JObject &other);
    JObject(JObject &&other) = default;
    JObject &operator=(JObject &&other) = default;

    operator jobject() const { return oid_;}
    operator jclass() const { return classId();} // static?
    jobject id() const { return oid_; }
    explicit operator bool() const { return !!oid_;}
    std::string error() const {return error_;}
    void reset(JNIEnv *env = nullptr);

    template<typename... Args>
    bool create(Args&&... args);

    /* with MethodTag we can avoid calling GetMethodID() in every call()
        struct MyMethod : jmi::MethodTag { static const char* name() { return "myMethod";} };
        return call<T, MyMethod>(args...);
    */
    template<typename T, class MTag, typename... Args, if_MethodTag<MTag> = true>
    inline T call(Args&&... args);
    template<class MTag, typename... Args, if_MethodTag<MTag> = true>
    inline void call(Args&&... args);
    /* with MethodTag we can avoid calling GetStaticMethodID() in every callStatic()
        struct MyStaticMethod : jmi::MethodTag { static const char* name() { return "myStaticMethod";} };
        JObject<CT>::callStatic<R, MyStaticMethod>(args...);
    */
    template<typename T, class MTag, typename... Args, if_MethodTag<MTag> = true>
    static T callStatic(Args&&... args);
    template<class MTag, typename... Args, if_MethodTag<MTag> = true>
    static void callStatic(Args&&... args);

    // get/set field and static field
    template<class FTag, typename T, if_FieldTag<FTag> = true>
    T get();
    template<class FTag, typename T, if_FieldTag<FTag> = true>
    bool set(T&& v);
    template<class FTag, typename T, if_FieldTag<FTag> = true>
    static T getStatic();
    template<class FTag, typename T, if_FieldTag<FTag> = true>
    static bool setStatic(T&& v);

    // the following call()/callStatic() will always invoke GetMethodID()/GetStaticMethodID()
    template<typename T, typename... Args>
    T call(const std::string& methodName, Args&&... args);
    template<typename... Args>
    void call(const std::string& methodName, Args&&... args);
    template<typename T, typename... Args>
    static T callStatic(const std::string& name, Args&&... args);
    template<typename... Args>
    static void callStatic(const std::string& name, Args&&... args);

    template<typename T>
    T get(std::string&& fieldName);
    template<typename T>
    bool set(std::string&& fieldName, T&& v);
    template<typename T>
    static T getStatic(std::string&& fieldName);
    template<typename T>
    static bool setStatic(std::string&& fieldName, T&& v);

    /*
       Alternative Field API
       Field lifetime is bounded to JObject, it does not add object ref, when object is destroyed/reset, accessing Field will fail (TODO: how to avoid crash?)
       Use MayBeFTag to ensure jfieldID is cacheable for each field
       Usage:
        auto f = obj.field<int, MyFieldTag>(), obj.field<int>("MyField"), JObject<...>::staticField<string>("MySField");
        auto& sf = JObject<...>::staticField<string, MySFieldTag>();
        f.set(123)/get(), sf.set("test")/get();
     */
    template<typename F, class MayBeFTag, bool isStaticField, bool cacheable = std::is_base_of<FieldTag, MayBeFTag>::value>
    class Field { // JObject.classId() works in Field?
    public:
        jfieldID id() const { return fid_; }
        operator jfieldID() const { return fid_; }
        operator F() const { return get(); }
        F get() const;
        void set(F&& v);
        Field& operator=(F&& v) {
            set(v);
            return *this;
        }
    protected:
        static jfieldID cachedId(jclass cid); // usually cid is used only once
        Field(jobject oid, jclass cid) : oid_(oid) { fid_ = cachedId(cid); } // it's protected so we can sure cacheable ctor will not be called for uncacheable Field
        Field(jobject oid, jclass cid, const char* name);
        // static field
        Field(jclass cid) : cid_(cid) { fid_ = cachedId(cid); }
        Field(jclass cid, const char* name);

        union {
            jobject oid_;
            jclass cid_;
        };
        jfieldID fid_ = nullptr;
        friend class JObject<CTag>;
    };
    template<class FTag, typename T, if_FieldTag<FTag> = true>
    Field<T, FTag, false> field() const {
        return Field<T, FTag, false>(oid_, classId());
    }
    template<typename T>
    Field<T, void, false> field(std::string&& name) const {
        return Field<T, void, false>(oid_, classId(), name.c_str());
    }
    template<class FTag, typename T, if_FieldTag<FTag> = true>
    static Field<T, FTag, true>& staticField() { // cacheable and static java storage, so returning ref is better
        static Field<T, FTag, true> f(classId());
        return f;
    }
    template<typename T>
    static Field<T, void, true> staticField(std::string&& name) {
        return Field<T, void, true>(classId(), name.c_str());
    }
private:
    static jclass classId(JNIEnv* env = nullptr);
    void setError(const std::string& s) {error_ = s; }
    static std::string normalizeClassName(std::string&& name) {
        std::string s = std::forward<std::string>(name);
        if (s[0] == 'L' && s.back() == ';')
            s = s.substr(1, s.size()-2);
        replace(s.begin(), s.end(), '.', '/');
        return s;
    }

    jobject oid_ = nullptr;
    std::string error_;
};
/*************************** JMI Public APIs End ***************************/

/*************************** Below is JMI implementation and internal APIs***************************/
template<class CTag>
inline std::string signature_of(const JObject<CTag>& t) { return t.signature();}

//signature_of_args<decltype(Args)...>::value, template<typename ...A> struct signature_of_args?

template<> struct signature<jboolean> { static const char value = 'Z';};
template<> struct signature<jbyte> { static const char value = 'B';};
template<> struct signature<jchar> { static const char value = 'C';};
template<> struct signature<jshort> { static const char value = 'S';};
template<> struct signature<jlong> { static const char value = 'J';};
template<> struct signature<jint> { static const char value = 'I';};
template<> struct signature<jfloat> { static const char value = 'F';};
template<> struct signature<jdouble> { static const char value = 'D';};
// "L...;" is used in method parameter
template<> struct signature<std::string> { constexpr static const char* value = "Ljava/lang/String;";};
template<> struct signature<char*> { constexpr static const char* value = "Ljava/lang/String;";};

// T* and T(&)[N] are treated as the same. use enable_if to select 1 of them. The function parameter is (const T&), so the default implemention of signature_of(const T&) must check is_pointer too.
template<typename T> using if_pointer = typename std::enable_if<std::is_pointer<T>::value, bool>::type;
template<typename T> using if_not_pointer = typename std::enable_if<!std::is_pointer<T>::value, bool>::type;
template<typename T> using if_array = typename std::enable_if<std::is_array<T>::value, bool>::type;
template<typename T, if_not_pointer<T> = true>
inline std::string signature_of(const T&) {
    return {signature<T>::value}; // initializer supports both char and char*
}
inline std::string signature_of(const char*) { return "Ljava/lang/String;";}
inline std::string signature_of() { return {'V'};}
// for base types, {'[', signature<T>::value};

template<typename T, std::size_t N>
inline std::string signature_of(const std::array<T, N>&) {
    static const auto s = std::string({'['}).append({signature_of(T())});
    return s;
}

template<typename T, if_pointer<T> = true>
inline std::string signature_of(const T&) { return {signature<jlong>::value};}
template<typename T, std::size_t N>
inline std::string signature_of(const T(&)[N]) { 
    static const auto s = std::string({'['}).append({signature_of(T())});
    return s;
}

template<template<typename, class...> class C, typename T, class... Args> struct is_jarray;
// exclude std::basic_string etc.
template<typename T, class... Args> struct is_jarray<std::vector, T, Args...> : public std::true_type {};
template<typename T, class... Args> struct is_jarray<std::valarray, T, Args...> : public std::true_type {};
template<template<typename, class...> class C, typename T, class... Args> using if_jarray = typename std::enable_if<is_jarray<C, T, Args...>::value, bool>::type;
template<template<typename, class...> class C, typename T, class... Args, if_jarray<C, T, Args...> = true>
inline std::string signature_of(const C<T, Args...>&) {
    static const std::string s = std::string({'['}).append({signature_of(T())});
    return s;
}

// NOTE: define reference_wrapper at last. assume we only use reference_wrapper<...>, no container<reference_wrapper<...>>
template<typename T>
inline std::string signature_of(const std::reference_wrapper<T>&) {
    static const std::string s = signature_of(T()); //TODO: no construct
    return s;
}
template<typename T, std::size_t N>
inline std::string signature_of(const std::reference_wrapper<T[N]>&) {
    static const std::string s = std::string({'['}).append({signature_of(T())});
    //return signature_of<T,N>((T[N]){}); //aggregated initialize. can not use declval?
    return s;
}

namespace detail {
using namespace std;
    static inline bool handle_exception(JNIEnv* env = nullptr) { //'static' function 'handle_exception' declared in header file should be declared 'static inline' [-Wunneeded-internal-declaration]
        if (!env)
            env = getEnv();
        if (!env->ExceptionCheck())
            return false;
        env->ExceptionDescribe();
        env->ExceptionClear();
        return true;
    }

    template<class F>
    class scope_exit_handler {
        F f_;
        bool invoke_;
    public:
        scope_exit_handler(F f) noexcept : f_(std::move(f)), invoke_(true) {}
        scope_exit_handler(scope_exit_handler&& other) noexcept : f_(std::move(other.f_)), invoke_(other.invoke_) {
            other.invoke_ = false;
        }
        ~scope_exit_handler() {
            if (invoke_)
                f_();
        }
        scope_exit_handler(const scope_exit_handler&) = delete;
        scope_exit_handler& operator=(const scope_exit_handler&) = delete;
    };
    template<class F>
    scope_exit_handler<F> call_on_exit(const F& f) noexcept {
        return scope_exit_handler<F>(f);
    }
    template<class F>
    scope_exit_handler<F> call_on_exit(F&& f) noexcept {
        return scope_exit_handler<F>(std::forward<F>(f));
    }


    static inline std::string make_sig() {return std::string();}
    template<typename Arg, typename... Args>
    std::string make_sig(Arg&& arg, Args&&... args) { // initializer_list + (a, b)
        //std::initializer_list({(s+=signature_of<Args>(args), 0)...});
        return signature_of(std::forward<Arg>(arg)).append(make_sig(std::forward<Args>(args)...));
    }

    template<typename... Args>
    std::string args_signature(Args&&... args) {
        static const std::string s("(" + make_sig(std::forward<Args>(args)...) + ")");
        return s;
    }

    template<typename T>
    jarray make_jarray(JNIEnv *env, const T &element, size_t size); // element is for getting jobject class
    template<class CTag>
    jarray make_jarray(JNIEnv *env, const JObject<CTag, true> &element, size_t size);

    template<typename T>
    void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const T &elm);
    template<class CTag>
    void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const JObject<CTag, true> &elm);

    template<typename T>
    jarray to_jarray(JNIEnv* env, const T &c0, size_t N, bool is_ref = false);
    template<typename T, std::size_t N>
    jarray to_jarray(JNIEnv* env, const T(&c)[N], bool is_ref = false) {
        return to_jarray(env, c[0], N, is_ref);
    }
    template<typename C> // c++ container (vector, valarray, array) to jarray
    jarray to_jarray(JNIEnv* env, const C &c, bool is_ref = false) {
        return to_jarray(env, c[0], c.size(), is_ref);
    }
    // env can be null for base types
    template<typename T> jvalue to_jvalue(const T &obj, JNIEnv* env = nullptr);
    template<typename T> jvalue to_jvalue(T *obj, JNIEnv* env) { return to_jvalue((jlong)obj, env); } // jobject is _jobject*?
    jvalue to_jvalue(const char* obj, JNIEnv* env);// { return to_jvalue(std::string(obj)); }
    template<template<typename,class...> class C, typename T, class... A> jvalue to_jvalue(const C<T, A...> &c, JNIEnv* env) { return to_jvalue(to_jarray(env, c), env); }
    template<typename T, std::size_t N> jvalue to_jvalue(const std::array<T, N> &c, JNIEnv* env) { return to_jvalue(to_jarray(env, c), env); }
    template<typename C> jvalue to_jvalue(const std::reference_wrapper<C>& c, JNIEnv* env) { return to_jvalue(to_jarray(env, c.get(), true), env); }
    template<typename T, std::size_t N> jvalue to_jvalue(const std::reference_wrapper<T[N]>& c, JNIEnv* env) { return to_jvalue(to_jarray<T,N>(env, c.get(), true), env); }
    template<class CTag>
    jvalue to_jvalue(const JObject<CTag, true> &obj, JNIEnv* env);
    // T(&)[N]?
#if 0
    // can not defined as template specialization because to_jvalue(T*, JNIEnv* env) will be choosed
    // FIXME: why clang crashes but gcc is fine? why to_jvalue(const jlong&, JNIEnv* env) works?
    // what if use jmi::object instead of jarray?
    jvalue to_jvalue(const jobject &obj, JNIEnv* env) { return jvalue{.l = obj};}
    jvalue to_jvalue(const jarray &obj, JNIEnv* env) { return jvalue{.l = obj};}
    jvalue to_jvalue(const jstring &obj, JNIEnv* env) { return jvalue{.l = obj};}
#endif

    template<typename T>
    void from_jarray(JNIEnv* env, const jvalue& v, T* t, std::size_t N);
    // env can be null for base types
    template<typename T> void from_jvalue(JNIEnv* env, const jvalue& v, T &t);
    template<typename T> void from_jvalue(JNIEnv* env, const jvalue& v, T *t, std::size_t n = 0) { // T* and T(&)[N] is the same
        if (n <= 0)
            from_jvalue(env, v, (jlong&)t);
        else
            from_jarray(env, v, t, n);
    }
    template<template<typename,class...> class C, typename T, class... A> void from_jvalue(JNIEnv* env, const jvalue& v, C<T, A...> &t) { from_jarray(env, v, &t[0], t.size()); }
    template<typename T, std::size_t N> void from_jvalue(JNIEnv* env, const jvalue& v, std::array<T, N> &t) { return from_jarray(env, v, t.data(), N); }
    //template<typename T, std::size_t N> void from_jvalue(JNIEnv* env, const jvalue& v, T(&t)[N]) { return from_jarray(env, v, t, N); }

    template<typename T>
    void set_ref_from_jvalue(JNIEnv* env, jvalue* jargs, T) {
        using Tn = typename std::remove_reference<T>::type;
        if (!std::is_fundamental<Tn>::value && !std::is_pointer<Tn>::value)
            env->DeleteLocalRef(jargs->l);
    }
    static inline void set_ref_from_jvalue(JNIEnv* env, jvalue *jargs, const char* s) {
        env->DeleteLocalRef(jargs->l);
    }
    template<typename T>
    void set_ref_from_jvalue(JNIEnv* env, jvalue *jargs, std::reference_wrapper<T> ref) {
        from_jvalue(env, *jargs, ref.get());
        using Tn = typename std::remove_reference<T>::type;
        if (!std::is_fundamental<Tn>::value && !std::is_pointer<Tn>::value)
            env->DeleteLocalRef(jargs->l);
    }
    template<typename T, std::size_t N>
    void set_ref_from_jvalue(JNIEnv* env, jvalue *jargs, std::reference_wrapper<T[N]> ref) {
        from_jvalue(env, *jargs, ref.get(), N); // assume only T* and T[N]
        env->DeleteLocalRef(jargs->l);
    }

    static inline void ref_args_from_jvalues(JNIEnv* env, jvalue*) {}
    template<typename Arg, typename... Args>
    void ref_args_from_jvalues(JNIEnv* env, jvalue *jargs, Arg& arg, Args&&... args) {
        set_ref_from_jvalue(env, jargs, arg);
        ref_args_from_jvalues(env, jargs + 1, std::forward<Args>(args)...);
    }

    template<typename T>
    T call_method(JNIEnv *env, jobject oid, jmethodID mid, jvalue *args);
    template<class CTag>
    JObject<CTag, true> call_method(JNIEnv *env, jobject obj, jmethodID methodId, jvalue *args);

    template<typename T, typename... Args>
    T call_method_set_ref(JNIEnv *env, jobject oid, jmethodID mid, jvalue *jargs, Args&&... args) {
        auto setter = call_on_exit([=]{
            ref_args_from_jvalues(env, jargs, args...);
        });
       return call_method<T>(env, oid, mid, jargs);
    }

    template<typename T>
    T call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args);
    template<typename T, typename... Args>
    T call_static_method_set_ref(JNIEnv *env, jclass cid, jmethodID mid, jvalue *jargs, Args&&... args) {
        auto setter = call_on_exit([=]{
            ref_args_from_jvalues(env, jargs, args...);
        });
        return call_static_method<T>(env, cid, mid, jargs);
    }

    template<typename T, typename... Args>
    T call_with_methodID(jobject oid, jclass cid, jmethodID* pmid, std::function<void(std::string err)> err_cb, const std::string& signature, const char* name, Args&&... args) {
        using namespace std;
        if (err_cb)
            err_cb(std::string());
        if (!cid)
            return T();
        if (!oid) {
            if (err_cb)
                err_cb("Invalid object instance");
            return T();
        }
        JNIEnv *env = getEnv();
        auto checker = call_on_exit([=]{
            if (handle_exception(env)) {
                if (err_cb)
                    err_cb(string("Failed to call method '") + name + "' with signature '" + signature + "'.");
            }
        });
        jmethodID mid = nullptr;
        if (pmid)
            mid = *pmid;
        if (!mid) {
            mid = env->GetMethodID(cid, name, signature.c_str());
            if (pmid)
                *pmid = mid;
        }
        if (!mid || env->ExceptionCheck())
            return T();
        return call_method_set_ref<T>(env, oid, mid, const_cast<jvalue*>(initializer_list<jvalue>({to_jvalue(forward<Args>(args), env)...}).begin()), std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    T call_static_with_methodID(jclass cid, jmethodID* pmid, std::function<void(std::string err)> err_cb, const std::string& signature, const char* name, Args&&... args) {
        using namespace std;
        if (err_cb)
            err_cb(std::string());
        if (!cid)
            return T();
        JNIEnv *env = getEnv();
        auto checker = call_on_exit([=]{
            if (handle_exception(env)) {
                if (err_cb)
                    err_cb(string("Failed to call static method '") + name + "'' with signature '" + signature + "'.");
            }
        });
        jmethodID mid = nullptr;
        if (pmid)
            mid = *pmid;
        if (!mid) {
            mid = env->GetStaticMethodID(cid, name, signature.c_str());
            if (pmid)
                *pmid = mid;
        }
        if (!mid || env->ExceptionCheck())
            return T();
        return call_static_method_set_ref<T>(env, cid, mid, const_cast<jvalue*>(initializer_list<jvalue>({to_jvalue(forward<Args>(args), env)...}).begin()), std::forward<Args>(args)...);
    }


    template<typename T>
    jfieldID get_field_id(JNIEnv* env, jclass cid, const char* name, jfieldID* pfid = nullptr) {
        jfieldID fid = nullptr;
        if (pfid)
            fid = *pfid;
        if (!fid) {
            fid = env->GetFieldID(cid, name, signature_of(T()).c_str());
            if (pfid)
                *pfid = fid;
        }
        return fid;
    }
    template<typename T>
    T get_field(JNIEnv* env, jobject oid, jfieldID fid);
    template<typename T>
    T get_field(jobject oid, jclass cid, jfieldID* pfid, const char* name) {
        JNIEnv* env = getEnv();
        // TODO: call_on_exit?
        jfieldID fid = get_field_id<T>(env, cid, name, pfid);
        if (!fid)
            return T();
        return get_field<T>(env, oid, fid);
    }
    template<typename T>
    void set_field(JNIEnv* env, jobject oid, jfieldID fid, T&& v);
    template<typename T>
    void set_field(jobject oid, jclass cid, jfieldID* pfid, const char* name, T&& v) {
        JNIEnv* env = getEnv();
        // TODO: call_on_exit?
        jfieldID fid = get_field_id<T>(env, cid, name, pfid);
        if (!fid)
            return;
        set_field<T>(env, oid, fid, std::forward<T>(v));
    }

    template<typename T>
    jfieldID get_static_field_id(JNIEnv* env, jclass cid, const char* name, jfieldID* pfid = nullptr) {
        jfieldID fid = nullptr;
        if (pfid)
            fid = *pfid;
        if (!fid) {
            fid = env->GetStaticFieldID(cid, name, signature_of(T()).c_str());
            if (pfid)
                *pfid = fid;
        }
        return fid;
    }
    template<typename T>
    T get_static_field(JNIEnv* env, jclass cid, jfieldID fid);
    template<typename T>
    T get_static_field(jclass cid, jfieldID* pfid, const char* name) {
        JNIEnv* env = getEnv();
        jfieldID fid = get_static_field_id<T>(env, cid, name, pfid);
        if (!fid)
            return T();
        return get_static_field<T>(env, cid, fid);
    }
    template<typename T>
    void set_static_field(JNIEnv* env, jclass cid, jfieldID fid, T&& v);
    template<typename T>
    void set_static_field(jclass cid, jfieldID* pfid, const char* name, T&& v) {
        JNIEnv* env = getEnv();
        jfieldID fid = get_static_field_id<T>(env, cid, name, pfid);
        if (!fid)
            return;
        set_static_field<T>(env, cid, fid, std::forward<T>(v));
    }
} // namespace detail

template<class CTag, if_ClassTag<CTag> V>
JObject<CTag, V>& JObject<CTag, V>::operator=(const JObject &other) {
    if (this == &other)
        return *this;
    JNIEnv *env = getEnv();
    reset(env);
    if (other.id())
        oid_ = env->NewGlobalRef(other.id());
    setError(other.error());
    return *this;
}

template<class CTag, if_ClassTag<CTag> V>
void JObject<CTag, V>::reset(JNIEnv *env) {
    error_.clear();
    if (!env)
        env = getEnv();
    if (!env)
        return;
    if (oid_)
        env->DeleteGlobalRef(oid_);
    oid_ = nullptr;
}

template<class CTag, if_ClassTag<CTag> V>
template<typename... Args>
bool JObject<CTag, V>::create(Args&&... args) {
    using namespace std;
    using namespace detail;
    JNIEnv* env = nullptr; // FIXME: why build error if let env be the last parameter of create()?
    if (!env) {
        env = getEnv();
        if (!env) {
            setError("No JNIEnv when creating class '" + className() + "'");
            return false;
        }
    }
    const jclass cid = classId();
    if (!cid) {
        setError("Failed to find class '" + className() + "'");
        return false;
    }
    auto checker = detail::call_on_exit([=]{ handle_exception(env); });
    static const string s(args_signature(forward<Args>(args)...).append(signature_of())); // void
    static const jmethodID mid = env->GetMethodID(cid, "<init>", s.c_str()); // can be static because class id, signature and arguments combination is unique
    if (!mid) {
        setError(string("Failed to find constructor of '" + className() + "' with signature '" + s + "'."));
        return false;
    }
    const jobject oid = env->NewObjectA(cid, mid, const_cast<jvalue*>(initializer_list<jvalue>({to_jvalue(forward<Args>(args), env)...}).begin())); // ptr0(jv) crash
    if (!oid) {
        setError(string("Failed to call constructor '" + className() + "' with signature '" + s + "'."));
        return false;
    }
    oid_ = env->NewGlobalRef(oid);
    return true;
}

template<class CTag, if_ClassTag<CTag> V>
template<typename T, class MTag, typename... Args, if_MethodTag<MTag>>
T JObject<CTag, V>::call(Args&&... args) {
    using namespace detail;
    static const auto s = args_signature(std::forward<Args>(args)...).append(signature_of(T()));
    static jmethodID mid = nullptr;
    return call_with_methodID<T>(oid_, classId(), &mid, [this](std::string err){ setError(err);}, s, MTag::name(), std::forward<Args>(args)...);
}
template<class CTag, if_ClassTag<CTag> V>
template<class MTag, typename... Args, if_MethodTag<MTag>>
void JObject<CTag, V>::call(Args&&... args) {
    using namespace detail;
    static const auto s = args_signature(std::forward<Args>(args)...).append(signature_of());
    static jmethodID mid = nullptr;
    call_with_methodID<void>(oid_, classId(), &mid, [this](std::string err){ setError(err);}, s, MTag::name(), std::forward<Args>(args)...);
}
template<class CTag, if_ClassTag<CTag> V>
template<typename T, class MTag, typename... Args, if_MethodTag<MTag>>
T JObject<CTag, V>::callStatic(Args&&... args) {
    using namespace detail;
    static const auto s = args_signature(std::forward<Args>(args)...).append(signature_of(T()));
    static jmethodID mid = nullptr;
    return call_static_with_methodID<T>(classId(), &mid, nullptr, s, MTag::name(), std::forward<Args>(args)...);
}
template<class CTag, if_ClassTag<CTag> V>
template<class MTag, typename... Args, if_MethodTag<MTag>>
void JObject<CTag, V>::callStatic(Args&&... args) {
    using namespace detail;
    static const auto s = args_signature(std::forward<Args>(args)...).append(signature_of());
    static jmethodID mid = nullptr;
    return call_static_with_methodID<void>(classId(), &mid, nullptr, s, MTag::name(), std::forward<Args>(args)...);
}

template<class CTag, if_ClassTag<CTag> V>
template<class FTag, typename T, if_FieldTag<FTag>>
T JObject<CTag, V>::get() {
    static jfieldID fid = nullptr;
    auto checker = detail::call_on_exit([=]{
        if (detail::handle_exception()) // TODO: check fid
            setError(std::string("Failed to get field '") + FTag::name() + "' with signature '" + signature_of(T()) + "'.");
    });
    return detail::get_field<T>(oid_, classId(), &fid, FTag::name());
}
template<class CTag, if_ClassTag<CTag> V>
template<class FTag, typename T, if_FieldTag<FTag>>
bool JObject<CTag, V>::set(T&& v) {
    static jfieldID fid = nullptr;
    auto checker = detail::call_on_exit([=]{
        if (detail::handle_exception())
            setError(std::string("Failed to set field '") + FTag::name() + "' with signature '" + signature_of(T()) + "'.");
    });
    detail::set_field<T>(oid_, classId(), &fid, FTag::name(), std::forward<T>(v));
    return true;
}
template<class CTag, if_ClassTag<CTag> V>
template<class FTag, typename T, if_FieldTag<FTag>>
T JObject<CTag, V>::getStatic() {
    static jfieldID fid = nullptr;
    return detail::get_static_field<T>(classId(), &fid, FTag::name());
}
template<class CTag, if_ClassTag<CTag> V>
template<class FTag, typename T, if_FieldTag<FTag>>
bool JObject<CTag, V>::setStatic(T&& v) {
    static jfieldID fid = nullptr;
    detail::set_static_field<T>(classId(), &fid, FTag::name(), std::forward<T>(v));
    return true;
}


template<class CTag, if_ClassTag<CTag> V>
template<typename T, typename... Args>
T JObject<CTag, V>::call(const std::string &methodName, Args&&... args) {
    using namespace detail;
    static const auto s = args_signature(std::forward<Args>(args)...).append(signature_of(T()));
    return call_with_methodID<T>(oid_, classId(), nullptr, [this](std::string err){ setError(err);}, s, methodName.c_str(), std::forward<Args>(args)...);
}
template<class CTag, if_ClassTag<CTag> V>
template<typename... Args>
void JObject<CTag, V>::call(const std::string &methodName, Args&&... args) {
    using namespace detail;
    static const auto s = args_signature(std::forward<Args>(args)...).append(signature_of());
    call_with_methodID<void>(oid_, classId(), nullptr, [this](std::string err){ setError(err);}, s, methodName.c_str(), std::forward<Args>(args)...);
}
template<class CTag, if_ClassTag<CTag> V>
template<typename T, typename... Args>
T JObject<CTag, V>::callStatic(const std::string &name, Args&&... args) {
    using namespace detail;
    static const auto s = args_signature(std::forward<Args>(args)...).append(signature_of(T()));
    return call_static_with_methodID<T>(classId(), nullptr, nullptr, s, name.c_str(), std::forward<Args>(args)...);
}
template<class CTag, if_ClassTag<CTag> V>
template<typename... Args>
void JObject<CTag, V>::callStatic(const std::string &name, Args&&... args) {
    using namespace detail;
    static const auto s = args_signature(std::forward<Args>(args)...).append(signature_of());
    call_static_with_methodID<void>(classId(), nullptr, nullptr, s, name.c_str(), std::forward<Args>(args)...);
}

template<class CTag, if_ClassTag<CTag> V>
template<typename T>
T JObject<CTag, V>::get(std::string&& fieldName) {
    jfieldID fid = nullptr;
    auto checker = detail::call_on_exit([=]{
        if (detail::handle_exception()) // TODO: check fid
            setError(std::string("Failed to get field '") + fieldName + "' with signature '" + signature_of(T()) + "'.");
    });
    return detail::get_field<T>(oid_, classId(), &fid, fieldName.c_str());
}
template<class CTag, if_ClassTag<CTag> V>
template<typename T>
bool JObject<CTag, V>::set(std::string&& fieldName, T&& v) {
    jfieldID fid = nullptr;
    auto checker = detail::call_on_exit([=]{
        if (detail::handle_exception())
            setError(std::string("Failed to set field '") + fieldName + "' with signature '" + signature_of(T()) + "'.");
    });
    detail::set_field<T>(oid_, classId(), &fid, fieldName.c_str(), std::forward<T>(v));
    return true;
}
template<class CTag, if_ClassTag<CTag> V>
template<typename T>
T JObject<CTag, V>::getStatic(std::string&& fieldName) {
    jfieldID fid = nullptr;
    return detail::get_static_field<T>(classId(), &fid, fieldName.c_str());
}
template<class CTag, if_ClassTag<CTag> V>
template<typename T>
bool JObject<CTag, V>::setStatic(std::string&& fieldName, T&& v) {
    jfieldID fid = nullptr;
    detail::set_static_field<T>(classId(), &fid, fieldName.c_str(), std::forward<T>(v));
    return true;
}

template<class CTag, if_ClassTag<CTag> V>
template<typename F, class MayBeFTag, bool isStaticField, bool cacheable>
F JObject<CTag, V>::Field<F, MayBeFTag, isStaticField, cacheable>::get() const
{
    auto checker = detail::call_on_exit([=]{
        detail::handle_exception();
    });
    if (isStaticField)
        return detail::get_static_field<F>(getEnv(), cid_, id());
    return detail::get_field<F>(getEnv(), oid_, id());
}

template<class CTag, if_ClassTag<CTag> V>
template<typename F, class MayBeFTag, bool isStaticField, bool cacheable>
void JObject<CTag, V>::Field<F, MayBeFTag, isStaticField, cacheable>::set(F&& v)
{
    auto checker = detail::call_on_exit([=]{
        detail::handle_exception();
    });
    if (isStaticField)
        detail::set_static_field<F>(getEnv(), cid_, id(), std::forward<F>(v));
    else
        detail::set_field<F>(getEnv(), oid_, id(), std::forward<F>(v));
}

template<class CTag, if_ClassTag<CTag> V>
template<typename F, class MayBeFTag, bool isStaticField, bool cacheable>
jfieldID JObject<CTag, V>::Field<F, MayBeFTag, isStaticField, cacheable>::cachedId(jclass cid)
{
    static jfieldID fid = nullptr;
    if (!fid) {
        if (isStaticField)
            fid = detail::get_static_field_id<F>(getEnv(), cid, MayBeFTag::name());
        else
            fid = detail::get_field_id<F>(getEnv(), cid, MayBeFTag::name());
    }
    return fid;
}

template<class CTag, if_ClassTag<CTag> V>
template<typename F, class MayBeFTag, bool isStaticField, bool cacheable>
JObject<CTag, V>::Field<F, MayBeFTag, isStaticField, cacheable>::Field(jobject oid, jclass cid, const char* name)
 : oid_(oid) {
    fid_ = detail::get_field_id<F>(getEnv(), cid, name);
}


template<class CTag, if_ClassTag<CTag> V>
template<typename F, class MayBeFTag, bool isStaticField, bool cacheable>
JObject<CTag, V>::Field<F, MayBeFTag, isStaticField, cacheable>::Field(jclass cid, const char* name)
 : cid_(cid) {
    fid_ = detail::get_static_field_id<F>(getEnv(), cid, name);
}


template<class CTag, if_ClassTag<CTag> V>
jclass JObject<CTag, V>::classId(JNIEnv* env) {
    static jclass c = nullptr;
    if (!c) {
        if (!env) {
            env = getEnv();
            if (!env)
                return c;
        }
        const jclass cid = (jclass)env->FindClass(className().c_str());
        if (cid) {
            c = (jclass)env->NewGlobalRef(cid); // cache per (c++/java)class class id
            env->DeleteLocalRef(cid); // can not use c
        }
    }
    return c;
}

namespace detail {

template<typename T>
jarray to_jarray(JNIEnv* env, const T &c0, size_t N, bool is_ref) {
    if (!env) {
        env = getEnv();
        if (!env)
            return nullptr;
    }
    jarray arr = nullptr;
    if (N == 0)
        arr = make_jarray(env, T(), 0);
    else
        arr = make_jarray(env, c0, N);
    if (!is_ref) {
        if (std::is_fundamental<T>::value) {
            set_jarray(env, arr, 0, N, c0);
        } else { // string etc. must convert to jobject
            for (std::size_t i = 0; i < N; ++i)
                set_jarray(env, arr, i, 1, *((&c0)+i));
        }
    }
    return arr;
}
template<class CTag>
jvalue to_jvalue(const JObject<CTag> &obj, JNIEnv* env) {
    return to_jvalue(jobject(obj), env);
}
template<class CTag>
jarray make_jarray(JNIEnv *env, const JObject<CTag> &element, size_t size) {
    return env->NewObjectArray(size, jclass(element), 0);
}
template<class CTag>
void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const JObject<CTag> &elm) {
    set_jarray(env, arr, position, n, jobject(elm));
}
/*
template<class CTag>
JObject<CTag> call_method(JNIEnv *env, jobject oid, jmethodID mid, jvalue *args) {
    return call_method<jobject>(env, oid, mid, args);
}
template<class CTag>
JObject<CTag call_static_method(JNIEnv *env, jclass cid, jmethodID mid, jvalue *args) {
    return call_static_method<jobject>(env, cid, mid, args);
}
template<class CTag>
JObject<CTag> get_field(JNIEnv* env, jobject oid, jfieldID fid);
template<class CTag>
JObject<CTag> get_static_field(JNIEnv* env, jclass cid, jfieldID fid);
*/
} //namespace detail
} //namespace jmi
