/*
 * JMI: JNI Modern Interface
 * Copyright (C) 2016-2017 Wang Bin - wbsecg1@gmail.com
 * MIT License
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
std::string to_string(jstring s);
jstring from_string(const std::string& s);

template<typename T> struct signature;
template<> struct signature<jboolean> { static const char value = 'Z';};
template<> struct signature<jbyte> { static const char value = 'B';};
template<> struct signature<jchar> { static const char value = 'C';};
template<> struct signature<jshort> { static const char value = 'S';};
template<> struct signature<jlong> { static const char value = 'J';};
template<> struct signature<jint> { static const char value = 'I';};
template<> struct signature<jfloat> { static const char value = 'F';};
template<> struct signature<jdouble> { static const char value = 'D';};
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
template<typename T, std::size_t N>
inline std::string signature_of(const std::reference_wrapper<T[N]>&) {
    return std::string({'['}).append({signature_of(T())});
    //T t[N];
    //return signature_of<T,N>(t); //aggregated initialize. FIXME: why reference_wrapper ctor is called?
    //return signature_of<T,N>((T[N]){}); //aggregated initialize. FIXME: why reference_wrapper ctor is called?
}

template<typename T, if_pointer<T> = true>
inline std::string signature_of(const T&) { return {signature<jlong>::value};}
template<typename T, std::size_t N>
inline std::string signature_of(const T(&)[N]) { return std::string({'['}).append({signature_of(T())});}


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

     // TODO: return shared_ptr?
    template<typename... Args>
    static object create(const std::string &path, Args&&... args) {
        if (path.empty())
            return object();
        JNIEnv *env = getEnv();
        if (!env)
            return object();
        std::string cpath(path);
        std::replace(cpath.begin(), cpath.end(), '.', '/');
        const jclass cid = (jclass)env->FindClass(path.c_str());
        auto checker = call_on_exit([=]{
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
            if (cid)
                env->DeleteLocalRef(cid);
        });
        if (!cid)
            return object().set_error("invalid class path: " + path);
        const std::string s(args_signature(std::forward<Args>(args)...).append(signature_of())); // void
        const jmethodID mid = env->GetMethodID(cid, "<init>", s.c_str());
        if (!mid)
            return object().set_error(std::string("Failed to find constructor '" + path + "' with signature '" + s + "'."));
        jobject obj = env->NewObjectA(cid, mid, ptr0(to_jvalues(std::forward<Args>(args)...))); // ptr0(jv) crash
        if (!obj)
            return object().set_error(std::string("Failed to call constructor '" + path + "' with signature '" + s + "'."));
        return object(path, cid, obj);
    }

    // use call_with with signature for method whose return type is object
    template<typename T, typename... Args>
    T call(const std::string &name, Args&&... args) {
        return call_with<T>(name, args_signature(std::forward<Args>(args)...).append(signature_of(T())), std::forward<Args>(args)...);
    }
    template<typename T, typename... Args>
    T call_with(const std::string &name, const std::string &signature, Args&& ...args) {
        error_.clear();
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
                env->ExceptionDescribe();
                env->ExceptionClear();
                set_error(std::string("Failed to call method '") + name + "' with signature '" + signature + "'.");
            }
        });
        const jmethodID mid = env->GetMethodID(cid, name.c_str(), signature.c_str());
        if (!mid || env->ExceptionCheck())
            return T();
        return call_method_set_ref<T>(env, oid, mid, object::ptr0(to_jvalues(std::forward<Args>(args)...)), std::forward<Args>(args)...);
    }

    template<typename... Args>
    void call(const std::string &name, Args&&... args) {
        call_with(name, args_signature(std::forward<Args>(args)...).append(signature_of()), std::forward<Args>(args)...);
    }
    template<typename... Args>
    void call_with(const std::string &name, const std::string &signature, Args&&... args) {
        return call_with<void>(name, signature, std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    T call_static(const std::string &name, Args&&... args) {
        return call_static_with<T>(name, args_signature(std::forward<Args>(args)...).append(signature_of(T())), std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    T call_static_with(const std::string &name, const std::string &signature, Args&&... args) {
        error_.clear();
        const jclass cid = get_class();
        if (!cid)
            return T();
        JNIEnv *env = getEnv();
        auto checker = call_on_exit([=]{
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
                set_error(std::string("Failed to call static method '") + name + "'' with signature '" + signature + "'.");
            }
        });
        const jmethodID mid = env->GetStaticMethodID(cid, name.c_str(), signature.c_str());
        if (!mid || env->ExceptionCheck())
            return T();
        return call_static_method_set_ref<T>(env, cid, mid, object::ptr0(to_jvalues(std::forward<Args>(args)...)), std::forward<Args>(args)...);
    }

    template<typename... Args>
    void call_static(const std::string &name, Args&&... args) {
        call_static_with(name, args_signature(std::forward<Args>(args)...).append(signature_of()), std::forward<Args>(args)...);
    }
    template<typename... Args>
    void call_static_with(const std::string &name, const std::string &signature, Args&&... args) {
        return call_static_with<void>(name, signature, std::forward<Args>(args)...);
    }

private:
    jobject instance_ = nullptr;
    jclass class_ = nullptr;
    mutable std::string error_;
    std::string class_path_; // class_

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
    inline static scope_exit_handler<F> call_on_exit(const F& f) noexcept {
        return scope_exit_handler<F>(f);
    }
    template<class F>
    inline static scope_exit_handler<F> call_on_exit(F&& f) noexcept {
        return scope_exit_handler<F>(std::forward<F>(f));
    }

    template<typename... Args>
    static std::string args_signature(Args&&... args) {
        return "(" + make_sig(std::forward<Args>(args)...) + ")";
    }

    template<typename... Args>
    static std::array<jvalue, sizeof...(Args)> to_jvalues(Args&&... args) {
        std::array<jvalue, sizeof...(Args)> jargs;
        set_jvalues(&std::get<0>(jargs), std::forward<Args>(args)...);
        //snprintf(nullptr, 0, "jargs: %p\n", &jargs[0]);fflush(0); // why this line works as magic? here or in set_jvalues()
        return jargs;
    }
    static std::array<jvalue,0> to_jvalues() { return std::array<jvalue,0>();}

    template<typename Arg, typename... Args>
    static std::string make_sig(Arg&& arg, Args&&... args) { // initializer_list + (a, b)
        //std::initializer_list({(s+=signature_of<Args>(args), 0)...});
        return signature_of(std::forward<Arg>(arg)).append(make_sig(std::forward<Args>(args)...));
    }
    static std::string make_sig() {return std::string();}

    template<typename Arg, typename... Args>
    static void set_jvalues(jvalue *jargs, Arg&& arg, Args&&... args) {
        *jargs = to_jvalue(arg);
        set_jvalues(jargs + 1, std::forward<Args>(args)...);
    }
    static void set_jvalues(jvalue*) {}

    template<typename T> static jvalue to_jvalue(const T &obj);
    template<typename T> static jvalue to_jvalue(T *obj) { return to_jvalue((jlong)obj); } // jobject is _jobject*?
    static jvalue to_jvalue(const char* obj);// { return to_jvalue(std::string(obj)); }
    template<typename T> static jvalue to_jvalue(const std::vector<T> &obj) { return to_jvalue(to_jarray(obj)); }
    template<typename T> static jvalue to_jvalue(const std::set<T> &obj) { return to_jvalue(to_jarray(obj));}
    template<typename T, std::size_t N> static jvalue to_jvalue(const std::array<T, N> &obj) { return to_jvalue(to_jarray(obj)); }
    template<typename C> static jvalue to_jvalue(const std::reference_wrapper<C>& c) { return to_jvalue(to_jarray(c.get(), true)); }
    template<typename T, std::size_t N> static jvalue to_jvalue(const std::reference_wrapper<T[N]>& c) { return to_jvalue(to_jarray<T,N>(c.get(), true)); }
#if 0
    // can not defined as template specialization because to_jvalue(T*) will be choosed
    // FIXME: why clang crashes but gcc is fine? why to_jvalue(const jlong&) works?
    // what if use jmi::object instead of jarray?
    static jvalue to_jvalue(const jobject &obj) { return jvalue{.l = obj};}
    static jvalue to_jvalue(const jarray &obj) { return jvalue{.l = obj};}
    static jvalue to_jvalue(const jstring &obj) { return jvalue{.l = obj};}
#endif
    template<typename T>
    static jarray to_jarray(JNIEnv *env, const T &element, size_t size); // element is for getting jobject class
    template<typename T, std::size_t N> static jarray to_jarray(const T(&c)[N], bool is_ref = false) {
        static_assert(N > 0, "invalide array size");
        JNIEnv *env = getEnv();
        if (!env)
            return nullptr;
        jarray arr = nullptr;
        arr = to_jarray(env, c[0], N);
        if (!is_ref) {
            if (std::is_fundamental<T>::value) {
                set_jarray(env, arr, 0, N, c[0]);
            } else {
                for (std::size_t i = 0; i < N; ++i)
                    set_jarray(env, arr, i, 1, c[i]);
            }
        }
        return arr;
    }
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
            // TODO: set once for array, vector etc
            for (typename C::const_iterator itr = c.begin(); itr != c.end(); ++itr)
                set_jarray(env, arr, i++, 1, *itr);
        }
        return arr;
    }

    template<typename T>
    static void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const T &elm);
    template<typename T, std::size_t N> static T* ptr0(std::array<T,N> a) { return &std::get<0>(a);}
    template<typename T> static T* ptr0(std::array<T,0>) {return nullptr;} // overload, not partial specialization (disallowed)

    template<typename T> static void from_jvalue(const jvalue& v, T &t);
    template<typename T> static void from_jvalue(const jvalue& v, T *t, std::size_t n = 0) { // T* and T(&)[N] is the same
        if (n <= 0)
            from_jvalue(v, (jlong&)t);
        else
            from_jarray(v, t, n);
    }
    template<typename T> static void from_jvalue(const jvalue& v, std::vector<T> &t) { from_jarray(v, t.data(), t.size()); }
    //template<typename T> static void from_jvalue(const jvalue& v, const std::set<T>: &t) { return from_jvalue(v, to_jarray(t));}
    template<typename T, std::size_t N> static void from_jvalue(const jvalue& v, std::array<T, N> &t) { return from_jarray(v, t.data(), N); }
    //template<typename T, std::size_t N> static void from_jvalue(const jvalue& v, T(&t)[N]) { return from_jarray(v, t, N); }

    template<typename T>
    static void from_jarray(const jvalue& v, T* t, std::size_t N);

    template<typename T>
    static inline void set_ref_from_jvalue(jvalue* jargs, T) {
        using Tn = typename std::remove_reference<T>::type;
        if (!std::is_fundamental<Tn>::value && !std::is_pointer<Tn>::value)
            getEnv()->DeleteLocalRef(jargs->l);
    }
    static inline void set_ref_from_jvalue(jvalue *jargs, const char* s) {
        getEnv()->DeleteLocalRef(jargs->l);
    }
    template<typename T>
    static inline void set_ref_from_jvalue(jvalue *jargs, std::reference_wrapper<T> ref) {
        from_jvalue(*jargs, ref.get());
        using Tn = typename std::remove_reference<T>::type;
        if (!std::is_fundamental<Tn>::value && !std::is_pointer<Tn>::value)
            getEnv()->DeleteLocalRef(jargs->l);
    }
    template<typename T, std::size_t N>
    static inline void set_ref_from_jvalue(jvalue *jargs, std::reference_wrapper<T[N]> ref) {
        from_jvalue(*jargs, ref.get(), N); // assume only T* and T[N]
        getEnv()->DeleteLocalRef(jargs->l);
    }

    template<typename Arg, typename... Args>
    static void ref_args_from_jvalues(jvalue *jargs, Arg& arg, Args&&... args) {
        set_ref_from_jvalue(jargs, arg);
        ref_args_from_jvalues(jargs + 1, std::forward<Args>(args)...);
    }
    static void ref_args_from_jvalues(jvalue*) {}

    template<typename T, typename... Args>
    T call_method_set_ref(JNIEnv *env, jobject oid, jmethodID mid, jvalue *jargs, Args&&... args) {
        auto setter = call_on_exit([=]{
            ref_args_from_jvalues(jargs, args...);
        });
       return call_method<T>(env, oid, mid, jargs);
    }
    template<typename T>
    T call_method(JNIEnv *env, jobject oid, jmethodID mid, jvalue *args) const;

    template<typename T, typename... Args>
    T call_static_method_set_ref(JNIEnv *env, jclass cid, jmethodID mid, jvalue *jargs, Args&&... args) {
        auto setter = call_on_exit([=]{
            ref_args_from_jvalues(jargs, args...);
        });
        return call_static_method<T>(env, cid, mid, jargs);
    }
    template<typename T>
    T call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args) const;
};

template<>
inline std::string signature_of(const object& t) { return t.signature();}
} //namespace jmi
