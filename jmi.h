/*
 * JMI: JNI Modern Interface
 * Copyright (C) 2016-2017 Wang Bin - wbsecg1@gmail.com
 * MIT License
 */
// TODO: field, static java method/field should use static c++ member
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

// set JavaVM to vm if not null. return previous JavaVM
JavaVM* javaVM(JavaVM *vm = nullptr, jint version = JNI_VERSION_1_4);
JNIEnv *getEnv();
std::string to_string(jstring s, JNIEnv* env = nullptr);
jstring from_string(const std::string& s, JNIEnv* env = nullptr);

template<typename T> struct signature;
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
    static bool handle_exception(JNIEnv* env) {
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

    template<typename T>
    void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const T &elm);

    template<typename T>
    jarray to_jarray(JNIEnv* env, const T &c0, size_t N, bool is_ref = false) {
        if (!env)
            env = getEnv();
        if (!env)
            return nullptr;
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

} // namespace detail

struct ClassTag {}; // used by JObject<Tag>. subclasses must define static const std::string() name(), with or without "L ;" around
struct MethodTag {}; // used by call() and call_static(). subclasses must define static const char* name();
// object must be a class template, thus we can cache class id using static member and call FindClass() only once, and also make it possible to cache method id because method id
template<class CTag, typename std::enable_if<std::is_base_of<ClassTag, CTag>::value, bool>::type = true>
class JObject
{
public:
    static const std::string className() {
        static std::string s(normalizeClassName(CTag::name()));
        return s;
    }
    static const std::string signature() {return "L" + className() + ";";}

    JObject() {}
    ~JObject() { reset(); }
    JObject(const JObject &other) { *this = other; }
    JObject &operator=(const JObject &other) {
        if (this == &other)
            return *this;
        JNIEnv *env = getEnv();
        reset(env);
        if (other.id())
            oid_ = env->NewGlobalRef(other.id());
        setError(other.error());
        return *this;
    }
    JObject(JObject &&other) = default;
    JObject &operator=(JObject &&other) = default;

    operator jobject() const { return oid_;}
    operator jclass() const { return classId();} // static?
    jobject id() const { return oid_; }
    explicit operator bool() const { return !!oid_;}
    std::string error() const {return error_;}

    void reset(JNIEnv *env = nullptr) {
        error_.clear();
        if (!env)
            env = getEnv();
        if (!env)
            return;
        if (oid_)
            env->DeleteGlobalRef(oid_);
        oid_ = nullptr;
    }

    template<typename... Args>
    bool create(Args&&... args) {
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

    /* with MethodTag we can avoid calling GetMethodID() in every call()
        struct MyMethod : jmi::MethodTag { static const char* name() { return "myMethod";} };
        return call<T, MyMethod>(args...);
    */
    template<typename T, class MTag, typename... Args, typename std::enable_if<std::is_base_of<MethodTag, MTag>::value, bool>::type = true>
    T call(Args&&... args) {
        using namespace std;
        using namespace detail;
        static const auto s = args_signature(forward<Args>(args)...).append(signature_of(T()));
        static jmethodID mid = nullptr;
        return call_with_methodID<T>(oid_, classId(), &mid, [this](string err){ setError(err);}, s, MTag::name(), std::forward<Args>(args)...);
    }
    template<class MTag, typename... Args, typename std::enable_if<std::is_base_of<MethodTag, MTag>::value, bool>::type = true>
    void call(Args&&... args) {
        using namespace std;
        using namespace detail;
        static const auto s = args_signature(forward<Args>(args)...).append(signature_of());
        static jmethodID mid = nullptr;
        call_with_methodID<void>(oid_, classId(), &mid, [this](string err){ setError(err);}, s, MTag::name(), std::forward<Args>(args)...);
    }
    /* with MethodTag we can avoid calling GetStaticMethodID() in every call_static()
        struct MyStaticMethod : jmi::MethodTag { static const char* name() { return "myStaticMethod";} };
        return call_static<T, MyStaticMethod>(args...);
    */
    template<typename T, class MTag, typename... Args, typename std::enable_if<std::is_base_of<MethodTag, MTag>::value, bool>::type = true>
    T callStatic(Args&&... args) {
        using namespace std;
        using namespace detail;
        static const auto s = args_signature(forward<Args>(args)...).append(signature_of(T()));
        static jmethodID mid = nullptr;
        return call_static_with_methodID<T>(classId(), &mid, [this](string err){ setError(err);}, s, MTag::name(), forward<Args>(args)...);
    }
    template<class MTag, typename... Args, typename std::enable_if<std::is_base_of<MethodTag, MTag>::value, bool>::type = true>
    void callStatic(Args&&... args) {
        using namespace std;
        using namespace detail;
        static const auto s = args_signature(forward<Args>(args)...).append(signature_of());
        static jmethodID mid = nullptr;
        return detail::call_static_with_methodID<void>(classId(), &mid, [this](string err){ setError(err);}, s, MTag::name(), std::forward<Args>(args)...);
    }

    // the following call()/call_static() will always invoke GetMethodID()/GetStaticMethodID()
    template<typename T, typename... Args>
    T call(const std::string &methodName, Args&&... args) {
        using namespace std;
        using namespace detail;
        static const auto s = args_signature(forward<Args>(args)...).append(signature_of(T()));
        return detail::call_with_methodID<T>(oid_, classId(), nullptr, [this](string err){ setError(err);}, s, methodName.c_str(), forward<Args>(args)...);
    }
    template<typename... Args>
    void call(const std::string &methodName, Args&&... args) {
        using namespace std;
        using namespace detail;
        static const auto s = args_signature(forward<Args>(args)...).append(signature_of());
        call_with_methodID<void>(oid_, classId(), nullptr, [this](string err){ setError(err);}, s, methodName.c_str(), forward<Args>(args)...);
    }
    template<typename T, typename... Args>
    T callStatic(const std::string &name, Args&&... args) {
        using namespace std;
        using namespace detail;
        static const auto s = args_signature(forward<Args>(args)...).append(signature_of(T()));
        return call_static_with_methodID<T>(classId(), nullptr, [this](string err){ setError(err);}, s, name.c_str(), forward<Args>(args)...);
    }
    template<typename... Args>
    void callStatic(const std::string &name, Args&&... args) {
        using namespace std;
        using namespace detail;
        static const auto s = args_signature(forward<Args>(args)...).append(signature_of());
        call_static_with_methodID<void>(classId(), nullptr, [this](string err){ setError(err);}, s, name.c_str(), forward<Args>(args)...);
    }
private:
    static jclass& classId(JNIEnv* env = nullptr) {
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

class object {
public:
    object(const std::string &class_path, jclass class_id = nullptr, jobject jobj = nullptr, JNIEnv* env = nullptr);
    //object(jclass class_id, jobject jobj = nullptr, JNIEnv* env = nullptr);
    object(jobject jobj = nullptr, JNIEnv* env = nullptr);
    object(const object &other);
    object(object &&other);
    ~object() { reset();}
    object &operator=(const object &other);
    object &operator=(object &&other);
    bool operator==(const object &other) const;
    explicit operator bool() const { return !!instance_;}
    jclass get_class() const { return class_;}
    const std::string &class_path() const { return class_path_;}
    jobject instance() const { return instance_;}
    operator jobject() const { return instance_;}
    bool instance_of(const std::string &class_path) const;
    std::string signature() const { return "L" + class_path_ + ";";}
    std::string error() const {return error_;}

    template<typename... Args>
    static object create(const std::string &path, Args&&... args) {
        using namespace std;
        using namespace detail;
        if (path.empty())
            return object();
        JNIEnv *env = getEnv();
        if (!env)
            return object();
        string cpath(path);
        replace(cpath.begin(), cpath.end(), '.', '/');
        const jclass cid = (jclass)env->FindClass(cpath.c_str());
        auto checker = detail::call_on_exit([=]{
            handle_exception(env);
            if (cid)
                env->DeleteLocalRef(cid);
        });
        if (!cid)
            return object().set_error("invalid class path: " + cpath);
        static const string s(args_signature(forward<Args>(args)...).append(signature_of())); // void
        const jmethodID mid = env->GetMethodID(cid, "<init>", s.c_str());
        if (!mid)
            return object().set_error(string("Failed to find constructor '" + cpath + "' with signature '" + s + "'."));
        jobject obj = env->NewObjectA(cid, mid, const_cast<jvalue*>(initializer_list<jvalue>({to_jvalue(forward<Args>(args), env)...}).begin())); // ptr0(jv) crash
        if (!obj)
            return object().set_error(string("Failed to call constructor '" + cpath + "' with signature '" + s + "'."));
        return object(cpath, cid, obj, env);
    }

    template<typename T, typename... Args>
    T call(const std::string &methodName, Args&&... args) {
        using namespace detail;
        static const auto s = args_signature(std::forward<Args>(args)...).append(signature_of(T()));
        return call_with_methodID<T>(instance_, class_, nullptr, [this](string err){ set_error(err);}, s, methodName.c_str(), std::forward<Args>(args)...);
    }
    template<typename... Args>
    void call(const std::string &methodName, Args&&... args) {
        using namespace detail;
        static const auto s = args_signature(std::forward<Args>(args)...).append(signature_of());
        call_with_methodID<void>(instance_, class_, nullptr, [this](string err){ set_error(err);}, s, methodName.c_str(), std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    T call_static(const std::string &name, Args&&... args) {
        using namespace detail;
        static const auto s = args_signature(std::forward<Args>(args)...).append(signature_of(T()));
        return call_static_with_methodID<T>(class_, nullptr, [this](string err){ set_error(err);}, s, name.c_str(), std::forward<Args>(args)...);
    }
    template<typename... Args>
    void call_static(const std::string &name, Args&&... args) {
        using namespace detail;
        static const auto s = args_signature(std::forward<Args>(args)...).append(signature_of());
        call_static_with_methodID<void>(class_, nullptr, [this](string err){ set_error(err);}, s, name.c_str(), std::forward<Args>(args)...);
    }
private:
    jobject instance_ = nullptr;
    jclass class_ = nullptr;
    mutable std::string error_;
    std::string class_path_;
    void init(JNIEnv *env, jobject obj_id, jclass class_id, const std::string &class_path = std::string());
    object& set_error(const std::string& err);
    void reset(JNIEnv *env = nullptr);
};

template<>
inline std::string signature_of(const object& t) { return t.signature();}
} //namespace jmi
