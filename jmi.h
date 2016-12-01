/*
 * JMI: JNI Modern Interface
 * Copyright (C) 2016 Wang Bin - wbsecg1@gmail.com
 */
#pragma once

#include <array>
#include <functional> // std::ref
#include <jni.h>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace jmi {

JavaVM* javaVM(JavaVM *vm = nullptr);
JNIEnv *getEnv();
jclass getClass(const std::string& class_path, bool cache = true);

template<typename T> struct signature;
template<> struct signature<bool> { static const char value = 'Z';};
template<> struct signature<jboolean> { static const char value = 'Z';};
template<> struct signature<jbyte> { static const char value = 'B';};
template<> struct signature<char> { static const char value = 'B';};
template<> struct signature<jchar> { static const char value = 'C';};
template<> struct signature<jshort> { static const char value = 'S';};
template<> struct signature<jlong> { static const char value = 'J';};
template<> struct signature<long> { static const char value = 'J';};
template<> struct signature<jint> { static const char value = 'I';};
template<> struct signature<unsigned> { static const char value = 'I';};
template<> struct signature<jfloat> { static const char value = 'F';};
template<> struct signature<jdouble> { static const char value = 'D';};
//template<> struct signature<jdouble> { static const char value = 'F';}; //?
template<> struct signature<std::string> { constexpr static const char* value = "Ljava/lang/String;";};

template<typename T>
inline std::string signature_of(const T&) {
    return {signature<T>::value}; // initializer supports bot char and char*
}
inline std::string signature_of() { return {'V'};};
// for base types, {'[', signature<T>::value};
// TODO: stl container forward declare only
template<typename T>
inline std::string signature_of(const std::vector<T>&) {
    return std::string({'['}).append({signature_of(T())});
}
template<typename T>
inline std::string signature_of(const std::set<T>&) {
    return std::string({'['}).append({signature_of(T())});
}
template<typename T, std::size_t N>
inline std::string signature_of(const std::array<T, N>&) {
    return std::string({'['}).append({signature_of(T())});
}
/*
template<typename T, typename V>
std::string signature_of(const std::unordered_map<T, V>&) {

}*/

// define reference_wrapper at last. assume we only use reference_wrapper<...>, no container<reference_wrapper<...>>
template<typename T>
inline std::string signature_of(const std::reference_wrapper<T>&) {
    return signature_of(T()); //TODO: no construct
}

template<typename T>
inline std::string signature_of(T*) { return {signature<jlong>::value};}
/*
//FIXME: conflict with overload (T*). use std::to_array(...)
template<typename T, std::size_t N>
inline std::string signature_of(T(&)[N]) { return std::string({'['}).append({signature_of(T())});}
*/

class object {
public:
    object(const std::string &class_path, jclass class_id = nullptr, jobject jobj = nullptr);
    object(jclass class_id, jobject jobj = nullptr);
    object(jobject jobj = nullptr);
    object(const object &other);
    object(object &&other);
    ~object() { reset();}
    object &operator=(const object &other);
    object &operator=(object &&other);
    bool operator==(const object &other) const;
    /*!
     * \brief operator bool
     * \return true if it's an jobject instance
     */
    operator bool() const { return !!instance_;}
    jclass get_class() const { return class_;}
    const std::string &class_path() const { return class_path_;}
    jobject instance() const { return instance_;}
    bool instance_of(const std::string &class_path) const;
    std::string signature() const { return "L" + class_path_ + ";";}
    std::string error() const {return error_;}

    template<typename... Args>
    static object create(const std::string &path, Args&&... args) {
        const jclass cid = jmi::getClass(path);
        if (!cid)
            return object(path).set_error("invalid class path: " + path);
        const std::string s(args_signature(args...).append(signature_of())); // void
        JNIEnv *env = getEnv();
        const jmethodID mid = env->GetMethodID(cid, "<init>", s.c_str());
        if (!mid || env->ExceptionCheck()) {
            env->ExceptionClear();
            return object(path).set_error(std::string("Failed to find constructor '" + path + "' with signature '" + s + "'."));
        }
        jobject obj = env->NewObjectA(cid, mid, ptr0(to_jvalues(args...)));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return object(path).set_error(std::string("Failed to call constructor '" + path + "' with signature '" + s + "'."));
        }
        return object(path, cid, obj);
    }

    // use call_with with signature for method whose return type is object
    template<typename T, typename... Args>
    T call(const std::string &name, Args&&... args) {
        return call_with<T>(name, args_signature(args...).append(signature_of(T())), args...);
    }
    template<typename T, typename... Args>
    T call_with(const std::string &name, const std::string &signature, Args&& ...args) {
        const jclass cid = get_class();
        if (!cid)
            return T();
        const jobject oid = instance();
        if (!oid) {
            set_error("invalid object instance");
            return T();
        }
        JNIEnv *env = getEnv();
        auto checker = call_on_exit([=]{
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                set_error(std::string("Failed to call method '") + name + " with signature '" + signature + "'.");
            }
        });
        const jmethodID mid = env->GetMethodID(cid, name.c_str(), signature.c_str());
        if (!mid || env->ExceptionCheck())
            return T();
        return call_method_set_ref<T>(env, oid, mid, object::ptr0(to_jvalues(args...)), args...);
    }

    template<typename... Args>
    void call(const std::string &name, Args&&... args) {
        call_with(name, args_signature(args...).append(signature_of()), args...);
    }
    template<typename... Args>
    void call_with(const std::string &name, const std::string &signature, Args&&... args) {
        return call_with<void>(name, signature, args...);
    }

    template<typename T, typename... Args>
    T call_static(const std::string &name, Args&&... args) {
        return call_static_with<T>(name, args_signature(args...).append(signature_of(T())), args...);
    }

    template<typename T, typename... Args>
    T call_static_with(const std::string &name, const std::string &signature, Args&&... args) {
        const jclass cid = get_class();
        if (!cid)
            return T();
        JNIEnv *env = getEnv();
        auto checker = call_on_exit([=]{
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                set_error(std::string("Failed to call static method '") + name + " with signature '" + signature + "'.");
            }
        });
        const jmethodID mid = env->GetStaticMethodID(cid, name.c_str(), signature.c_str());
        if (!mid || env->ExceptionCheck())
            return T();
        return call_static_method_set_ref<T>(env, cid, mid, object::ptr0(to_jvalues(args...)), args...);
    }

    template<typename... Args>
    void call_static(const std::string &name, Args&&... args) {
        call_static_with(name, args_signature(args...).append(signature_of()), args...);
    }
    template<typename... Args>
    void call_static_with(const std::string &name, const std::string &signature, Args&&... args) {
        return call_static_with<void>(name, signature, args...);
    }

private:
    jobject instance_ = nullptr;
    jclass class_ = nullptr;
    mutable std::string error_;
    std::string class_path_;

    void init(jobject obj_id, jclass class_id, const std::string &class_path = std::string());
    object& set_error(const std::string& err);
    void reset();

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
    inline scope_exit_handler<F> call_on_exit(const F& f) noexcept {
        return scope_exit_handler<F>(f);
    }
    template<class F>
    inline scope_exit_handler<F> call_on_exit(F&& f) noexcept {
        return scope_exit_handler<F>(std::forward<F>(f));
    }

    template<typename... Args>
    static std::string args_signature(Args&&... args) {
        return "(" + make_sig(args...) + ")";// + signature_of();
    }

    template<typename... Args>
    static std::array<jvalue, sizeof...(Args)> to_jvalues(Args&&... args) {
        std::array<jvalue, sizeof...(Args)> jargs;
        set_jvalues(&std::get<0>(jargs), args...);
        return jargs;
    }
    static std::array<jvalue,0> to_jvalues() { return std::array<jvalue,0>();}

    template<typename Arg, typename... Args>
    static std::string make_sig(Arg&& arg, Args&&... args) {
        return signature_of(arg).append(make_sig(args...));
    }
    static std::string make_sig() {return std::string();}

    template<typename Arg, typename... Args>
    static void set_jvalues(jvalue *jargs, Arg&& arg, Args&&... args) {
        *jargs = to_jvalue(arg);
        set_jvalues(jargs + 1, args...);
    }
    static void set_jvalues(jvalue*) {}

    template<typename T> static jvalue to_jvalue(const T &obj);
    template<typename T> static jvalue to_jvalue(T *obj) { return to_jvalue((jlong)obj); }
    template<typename T> static jvalue to_jvalue(const std::vector<T> &obj) { return to_jvalue(to_jarray(obj)); }
    template<typename T> static jvalue to_jvalue(const std::set<T> &obj) { return to_jvalue(to_jarray(obj));}
    template<typename T, std::size_t N> static jvalue to_jvalue(const std::array<T, N> &obj) { return to_jvalue(to_jarray(obj)); }
    template<typename C> static jvalue to_jvalue(const std::reference_wrapper<C>& c) { return to_jvalue(to_jarray(c.get(), true)); }

    template<typename T>
    static jarray to_jarray(JNIEnv *env, const T &element, size_t size); // element is for getting jobject class
    // c++ container to jarray
    template<typename C> static jarray to_jarray(const C &c, bool is_ref = false) {
        JNIEnv *env = getEnv();
        if (!env)
            return nullptr;
        jarray arr = nullptr;
        if (c.empty())
            arr = to_jarray(env, typename C::value_type(), 0);
        else
            arr = to_jarray(env, *c.begin(), c.size()); // c.begin() is for getting jobject class
        if (!is_ref) {
            size_t i = 0;
            for (typename C::const_iterator itr = c.begin(); itr != c.end(); ++itr)
                set_jarray(env, arr, i++, *itr);
        }
        return arr;
    }

    template<typename T>
    static void set_jarray(JNIEnv *env, jarray arr, size_t position, const T &elm);
    template<typename T, std::size_t N> static T* ptr0(std::array<T,N> a) { return &std::get<0>(a);}
    template<typename T> static T* ptr0(std::array<T,0>) {return nullptr;} // overload, not partial specialization (disallowed)

    template<typename T> static void from_jvalue(const jvalue& v, T &t);
    template<typename T> static void from_jvalue(const jvalue& v, T *t) { from_jvalue(v, (jlong&)t); }
    template<typename T> static void from_jvalue(const jvalue& v, std::vector<T> &t) { from_jarray(v, t.data(), t.size()); }
    //template<typename T> static void from_jvalue(const jvalue& v, const std::set<T>: &t) { return from_jvalue(v, to_jarray(t));}
    template<typename T, std::size_t N> static void from_jvalue(const jvalue& v, std::array<T, N> &t) { return from_jarray(v, t.data(), N); }
    template<typename T>
    static void from_jarray(const jvalue& v, T* t, std::size_t N);

    template<typename T>
    static inline void set_ref_from_jvalue(jvalue *jargs, T) {}
    template<typename T>
    static inline void set_ref_from_jvalue(jvalue *jargs, std::reference_wrapper<T> ref) {
        from_jvalue(*jargs, ref.get());
    }

    template<typename Arg, typename... Args>
    static void ref_args_from_jvalues(jvalue *jargs, Arg& arg, Args&&... args) {
        set_ref_from_jvalue(jargs, arg);
        ref_args_from_jvalues(jargs + 1, args...);
    }
    static void ref_args_from_jvalues(jvalue*) {}

    template<typename T, typename... Args>
    T call_method_set_ref(JNIEnv *env, jobject oid, jmethodID mid, jvalue *jargs, Args&&... args) {
        auto setter = call_on_exit([=]{
            ref_args_from_jvalues(jargs, args...);
            // TODO: release jarray/objects created in jvalue?
        });
        return call_method<T>(env, oid, mid, jargs);
    }
    template<typename T>
    T call_method(JNIEnv *env, jobject oid, jmethodID mid, jvalue *args) const;

    template<typename T, typename... Args>
    T call_static_method_set_ref(JNIEnv *env, jclass cid, jmethodID mid, jvalue *jargs, Args&&... args) {
        auto setter = call_on_exit([=]{
            ref_args_from_jvalues(jargs, args...);
            // TODO: release jarray/objects created in jvalue?
        });
        return call_static_method<T>(env, cid, mid, jargs);
    }
    template<typename T>
    T call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) const;
};

template<>
inline std::string signature_of(const object& t) { return t.signature();}
} //namespace jmi
