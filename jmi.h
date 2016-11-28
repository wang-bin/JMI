/*
 * JMI: JNI Modern Interface
 * Copyright (C) 2016 Wang Bin - wbsecg1@gmail.com
 */
#pragma once

#include <array>
#include <cassert>
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
template<typename T>
inline std::string signature_of(const T&) {
    return {signature<T>::value}; // initializer supports bot char and char*
}
inline std::string signature_of() { return {'V'};};

class object {
public:
    object(const std::string &class_path, jobject jobj = nullptr, jclass class_id = nullptr);
    object(jobject jobj = nullptr, jclass class_id = nullptr);
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
    /*!
     * \brief class_path
     * classRef().getName() if no class path
     */
    const std::string &class_path() const { return class_path_;}
    jobject instance() const { return instance_;}
    bool instance_of(const std::string &class_path) const;
    std::string signature() const { return "L" + class_path_ + ";";}
    std::string error() const {return error_;}

    template<typename... Args>
    static object create(const std::string &path, Args... args) {
        const jclass cid = jmi::getClass(path);
        if (!cid)
            return object(path).set_error("invalid class path: " + path);
        const std::string s(args_signature<Args...>(args...).append(signature_of())); // void
        JNIEnv *env = getEnv();
        jmethodID mid = env->GetMethodID(cid, "<init>", s.c_str());
        if (!mid || env->ExceptionCheck()) {
            env->ExceptionClear();
            return object(path).set_error(std::string("Failed to find constructor '" + path + "' with signature '" + s + "'."));
        }
        jobject obj = env->NewObjectA(cid, mid, ptr0(to_jvalues(args...)));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return object(path).set_error(std::string("Failed to call constructor '" + path + "' with signature '" + s + "'."));
        }
        return object(path, obj, cid);
    }

    // use callSigned for return type is object
    template<typename T, typename... Args>
    T call(const std::string &name, Args ... args) {
        return callSigned<T>(name, args_signature(args...).append(signature_of(T())), args...);
    }
    template<typename T, typename... Args>
    T callSigned(const std::string &name, const std::string &signature, Args ...args) {
        const jclass cid = get_class();
        if (!cid)
            return T();
        const jobject oid = instance();
        if (!oid) {
            set_error("invalid object instance");
            return T();
        }
        JNIEnv *env = getEnv();
        auto checker = check_on_exit([=]{
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                set_error(std::string("Failed to call method '") + name + " with signature '" + signature + "'.");
            }
        });
        const jmethodID mid = env->GetMethodID(cid, name.c_str(), signature.c_str());
        if (!mid || env->ExceptionCheck())
            return T();
        return callMethod<T>(env, oid, mid, object::ptr0(to_jvalues(args...)));
        // TODO: assign output args from jvalue. release jarray/objects created in jvalue. do it in callMethodWrapper?
        //callMethodWrapper(,ags..., jvalues)
        //jvalues_to(jv, args...); // (T&): assign, otherwise do nothing/just unref jobj
    }

    template<typename... Args>
    void callVoid(const std::string &name, Args ... args) {
        callSignedVoid(name, args_signature(args...).append(signature_of()), args...);
    }
    template<typename... Args>
    void callSignedVoid(const std::string &name, const std::string &signature, Args... args) {
        return callSigned<void>(name, signature, args...);
    }

    template<typename T, typename... Args>
    T staticCall(const std::string &name, Args... args) {
        return staticCallSigned(name, args_signature(args...).append(signature_of(T())), args...);
    }

    template<typename T, typename... Args>
    T staticCallSigned(const std::string &name, const std::string &signature, Args ... args) {
        const jclass cid = get_class();
        if (!cid)
            return T();
        JNIEnv *env = getEnv();
        auto checker = check_on_exit([=]{
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                set_error(std::string("Failed to call static method '") + name + " with signature '" + signature + "'.");
            }
        });
        const jmethodID mid = env->GetStaticMethodID(cid, name.c_str(), signature.c_str());
        if (!mid || env->ExceptionCheck())
            return T();
        return callStaticMethod<T>(env, cid, mid, object::ptr0(to_jvalues(args...)));
    }

    template<typename... Args>
    void staticCallVoid(const std::string &name, Args ... args) {
        staticCallSignedVoid(name, args_signature(args...).append(signature_of()), args...);
    }
    template<typename... Args>
    void staticCallSignedVoid(const std::string &name, const std::string &signature, Args ... args) {
        return staticCallSigned<void>(name, signature, args...);
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
    inline scope_exit_handler<F> check_on_exit(const F& f) noexcept {
        return scope_exit_handler<F>(f);
    }
    template<class F>
    inline scope_exit_handler<F> check_on_exit(F&& f) noexcept {
        return scope_exit_handler<F>(std::forward<F>(f));
    }

    template<typename... Args>
    static std::string args_signature(Args... args) {
        return "(" + make_sig(args...) + ")";// + signature_of();
    }

    template<typename... Args>
    static std::array<jvalue, sizeof...(Args)> to_jvalues(Args... args) {
        std::array<jvalue, sizeof...(Args)> jargs;
        set_jvalues(&std::get<0>(jargs), args...);
        return jargs;
    }
    static std::array<jvalue,0> to_jvalues() { return std::array<jvalue,0>();}

    template<typename Arg, typename... Args>
    static std::string make_sig(Arg arg, Args... args) {
        return signature_of(arg).append(make_sig(args...));
    }
    static std::string make_sig() {return std::string();}

    template<typename Arg, typename... Args>
    static void set_jvalues(jvalue *jargs, const Arg &arg, const Args&... args) {
        *jargs = to_jvalue(arg);
        set_jvalues(jargs + 1, args...);
    }
    static void set_jvalues(jvalue*) {}

    template<typename T> static jvalue to_jvalue(const T &obj);
    template<typename T> static jvalue to_jvalue(T *obj) { return to_jvalue((jlong)obj); }
    template<typename T> static jvalue to_jvalue(const std::vector<T> &obj) { return to_jvalue(to_jarray(obj)); }
    template<typename T> static jvalue to_jvalue(const std::set<T> &obj) { return to_jvalue(to_jarray(obj));}
    template<typename T, std::size_t N> static jvalue to_jvalue(const std::array<T, N> &obj) { return to_jvalue(to_jarray(obj)); }
    template<typename C> static jvalue to_jvalue(const std::reference_wrapper<C>& c) { return to_jvalue(to_jarray(c.get()), true); }

    template<typename T>
    static jarray to_jarray(JNIEnv *env, const T &element, size_t size);
    // c++ container to jarray
    template<typename C> static jarray to_jarray(const C &obj, bool is_ref = false) {
        JNIEnv *env = getEnv();
        if (!env)
            return nullptr;
        jarray arr = nullptr;
        if (obj.empty())
            arr = to_jarray(env, typename C::value_type(), 0);
        else
            arr = to_jarray(env, *obj.begin(), obj.size());
        if (!is_ref) {
            size_t i = 0;
            for (typename C::const_iterator itr = obj.begin(); itr != obj.end(); ++itr)
                set_jarray(env, arr, i++, *itr);
        }
        return arr;
    }

    template<typename T>
    static void set_jarray(JNIEnv *env, jarray arr, size_t position, const T &elm);
    template<typename T, std::size_t N> static T* ptr0(std::array<T,N> a) { return &std::get<0>(a);}
    template<typename T> static T* ptr0(std::array<T,0>) {return nullptr;} // overload, not partial specialization (disallowed)

    template<typename T>
    T callMethod(JNIEnv *env, jobject oid, jmethodID mid, jvalue *args) const;
    template<typename T>
    T callStaticMethod(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) const;
};

template<> struct signature<bool> { static const char value = 'Z';};
template<> struct signature<jboolean> { static const char value = 'Z';};
template<> struct signature<jbyte> { static const char value = 'B';};
template<> struct signature<jchar> { static const char value = 'C';};
template<> struct signature<jshort> { static const char value = 'S';};
template<> struct signature<jlong> { static const char value = 'J';};
template<> struct signature<long> { static const char value = 'J';};
template<> struct signature<jint> { static const char value = 'I';};
template<> struct signature<unsigned> { static const char value = 'I';};
template<> struct signature<jfloat> { static const char value = 'F';};
//template<> struct signature<jdouble> { static const char value = 'F';}; //?
template<> struct signature<std::string> { constexpr static const char* value = "Ljava/lang/String;";};


#if 1
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
/* FIXME: conflict with overload (T*)
template<typename T, std::size_t N>
inline std::string signature_of(T(&)[N]) {
    return std::string({'['}).append({signature_of(T())});
}
*/
/*
template<typename T, typename V>
std::string signature_of(const std::unordered_map<T, V>&) {

}*/

// define reference_wrapper at last. assume we only use reference_wrapper<...>, no container<reference_wrapper<...>>
template<typename T>
inline std::string signature_of(const std::reference_wrapper<T>&) {
    return signature_of(T()); //TODO: no construct
}
#endif

template<typename T>
inline std::string signature_of(T*) { return {signature<jlong>::value};}
template<>
inline std::string signature_of(const object& t) { return t.signature();}

} //namespace jmi
