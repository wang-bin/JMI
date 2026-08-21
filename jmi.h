/*
 * JMI: JNI Modern Interface
 * Copyright (C) 2016-2026 Wang Bin - wbsecg1@gmail.com
 * AI participated
 * https://github.com/wang-bin/JMI
 * MIT License
 */
// requires: c++17
// TODO: reset error before each call, reset exception after each call (Aspect pattern?)
// TODO: query class path if return/parameter type is jobject
// TODO: object convert
// java template, e.g. Range<T>
// https://developer.android.com/training/articles/perf-jni#threads
//
// Alternative ct_string design (not used): template<char... Cs> struct ct_string;
//   Each string is a distinct type (e.g. "ok" -> ct_string<'o','k','\0'>), so MethodTag /
//   NamedMethodTag could be omitted and the string type itself used as the cache key.
//   Literals would come from a UDL (never hand-written char packs), e.g. "ok"_cts.
//   Pros: works without C++20 structural NTTP; content-identity at the type level;
//         natural fit for type-list / tag dispatch keyed by the string type.
//   Cons: mangled names explode on long JNI signatures; heavier compile/link; pack
//         concat / substr are noisier than array-backed ct_string<N>; overlaps what
//         C++20 already provides via template<ct_string Name> (value NTTP) and
//         call<T, "name">() / NamedMethodTag<"name"> below.
#pragma once
#include <array>
#include <functional> // std::ref
#include <string>
#include <type_traits>
#include <utility>
#include <jni.h>
#if (__cplusplus + 0) < 201703L && (_MSVC_LANG + 0) <= 201402L
#error JMI requires C++17 or later
#endif
#if (__cplusplus + 0) >= 201707L || (_MSVC_LANG+0) > 201703L
#define JMI_CXX20 1
#endif

namespace jmi {
using namespace std;
/*************************** JMI Public APIs Begin ***************************/
#define JMI_MAJOR 1
#define JMI_MINOR 0
#define JMI_MICRO 0

#define JMI_VERSION_STR JMI_STRINGIFY(JMI_MAJOR) "." JMI_STRINGIFY(JMI_MINOR) "." JMI_STRINGIFY(JMI_MICRO)

// set JavaVM to vm if not null. return previous JavaVM
JavaVM* javaVM(JavaVM *vm = nullptr, jint version = JNI_VERSION_1_4);
[[nodiscard]] JNIEnv *getEnv();
// to_string: local ref is deleted internally
[[nodiscard]] string to_string(jstring s, JNIEnv* env = nullptr);
// You have to call DeleteLocalRef() manually for the returned jstring
[[nodiscard]] jstring from_string(const string& s, JNIEnv* env = nullptr);

namespace android {
// current android/app/Application object containing a local ref
[[nodiscard]] jobject application(JNIEnv* env = nullptr); // TODO: return LocalRef
} // namespace android

// Compile-time JNI string: null-terminated (N includes '\0'). C++20: also NTTP / ""_jmis.
template<size_t N>
struct ct_string : array<char, N> {
    constexpr ct_string() noexcept = default;

    constexpr ct_string(char const (&s)[N]) noexcept : array<char, N>{} {
        for (size_t i = 0; i < N; ++i)
            (*this)[i] = s[i];
    }

    constexpr ct_string(array<char, N> s) noexcept : array<char, N>(s) {}

    // C-string length excluding trailing '\0'
    constexpr size_t length() const noexcept { return N - 1; }

    // Null-terminated substring; Count is length without the final '\0'
    template<size_t Count>
    [[nodiscard]] constexpr auto substr(size_t pos = 0) const noexcept {
        ct_string<Count + 1> out{};
        for (size_t i = 0; i < Count; ++i)
            out[i] = (*this)[i + pos];
        return out;
    }
};
template<size_t N>
ct_string(char const (&)[N]) -> ct_string<N>;

namespace detail {
template<size_t N1, size_t N2, size_t... I1, size_t... I2>
[[nodiscard]] constexpr auto ct_string_cat(ct_string<N1> a, ct_string<N2> b, index_sequence<I1...>, index_sequence<I2...>) noexcept {
    return ct_string(array{a[I1]..., b[I2]...});
}
} // namespace detail

// Null-terminated concat (drop a's trailing '\0'), like C strcpy/strcat
template<size_t N1, size_t N2>
[[nodiscard]] constexpr auto operator+(ct_string<N1> a, ct_string<N2> b) noexcept {
    return detail::ct_string_cat(a, b, make_index_sequence<N1 - 1>{}, make_index_sequence<N2>{});
}
template<size_t N1, size_t N2>
[[nodiscard]] constexpr auto operator+(char const (&a)[N1], ct_string<N2> b) noexcept {
    return ct_string(a) + b;
}
template<size_t N1, size_t N2>
[[nodiscard]] constexpr auto operator+(ct_string<N1> a, char const (&b)[N2]) noexcept {
    return a + ct_string(b);
}

// cstr must be a string literal. Explicit N: GCC 10 does not CTAD on ::jmi::ct_string(...).
// Two ways to build ct_string without qualified CTAD:
// 1) This macro: "" cstr "" adjacent-concat — only string-literal tokens work; rejects
//    const char* and named arrays (e.g. char const a[] = "x"; JMISTR(a) is ill-formed).
//    No standard type trait can tell a literal from char const[N]; the concat does.
// 2) Helper (function-template deduction): rejects pointers only; named char const[N] OK.
//   template<size_t N> constexpr auto make_ct_string(char const (&s)[N]) noexcept { return ct_string<N>(s); }
//   #define JMISTR(cstr) (::jmi::make_ct_string(cstr))  // JMISTR(a) above would compile
#define JMISTR(cstr) (::jmi::ct_string<sizeof("" cstr "")>("" cstr ""))

#if (JMI_CXX20 + 0)
template<ct_string S>
[[nodiscard]] constexpr auto operator""_jmis() noexcept {
    return S;
}
#endif

struct ClassTag {}; // used by JObject<Tag>. subclasses must define static constexpr auto name() {return JMISTR("someName");}, with or without "L ;" around someName
struct MethodTag {}; // used by call() and callStatic(). subclasses must define static const char* name() or static constexpr const char*();
struct FieldTag {}; // subclasses must define static const char* name() or static constexpr const char*();

#if (JMI_CXX20 + 0)
// C++20: using Surface = jmi::NamedClassTag<"android/view/Surface">;
template<ct_string Name>
struct NamedClassTag : ClassTag {
    static constexpr auto name() noexcept { return Name; }
};
template<ct_string Name>
struct NamedMethodTag : MethodTag {
    static constexpr const char* name() noexcept { return Name.data(); }
};
template<ct_string Name>
struct NamedFieldTag : FieldTag {
    static constexpr const char* name() noexcept { return Name.data(); }
};
#endif

namespace detail {
struct JavaLangClassTag;
template<class T>
inline constexpr bool is_jobject_v = is_base_of_v<remove_pointer_t<jobject>, remove_pointer_t<T>>;
template<class T>
struct is_jobject : bool_constant<is_jobject_v<T>> {};

template<class T>
using if_jobject = enable_if_t<is_jobject_v<T>, bool>;
template<class T>
using if_not_jobject = enable_if_t<!is_jobject_v<T>, bool>;

template<class Tag>
inline constexpr bool is_ClassTag_v = is_base_of_v<ClassTag, Tag>;
template<class Tag>
inline constexpr bool is_MethodTag_v = is_base_of_v<MethodTag, Tag>;
template<class Tag>
inline constexpr bool is_FieldTag_v = is_base_of_v<FieldTag, Tag>;
template<class Tag>
using if_ClassTag = enable_if_t<is_ClassTag_v<Tag>, bool>;
template<class Tag>
using if_MethodTag = enable_if_t<is_MethodTag_v<Tag>, bool>;
template<class Tag>
using if_not_MethodTag = enable_if_t<!is_MethodTag_v<Tag>, bool>;
template<class Tag>
using if_FieldTag = enable_if_t<is_FieldTag_v<Tag>, bool>;

// detection idiom (std::experimental::is_detected subset)
template<template<class...> class Op, class, class... Args>
struct detector : false_type {};
template<template<class...> class Op, class... Args>
struct detector<Op, void_t<Op<Args...>>, Args...> : true_type {};
template<template<class...> class Op, class... Args>
using is_detected = detector<Op, void, Args...>;
template<template<class...> class Op, class... Args>
inline constexpr bool is_detected_v = is_detected<Op, Args...>::value;

template<class T>
using jobject_signature_t = decltype(T::signature());
// JObject (or CRTP subclass) is a ClassTag and exposes signature(); plain ClassTag only has name()
template<class T>
inline constexpr bool is_JObject_v = is_base_of_v<ClassTag, T> && is_detected_v<jobject_signature_t, T>;
template<class T>
struct is_JObject : bool_constant<is_JObject_v<T>> {};
template<class T>
using if_JObject = enable_if_t<is_JObject_v<T>, bool>;
template<class T>
using if_not_JObject = enable_if_t<!is_JObject_v<T>, bool>;
}
//template<typename T> // jni primitive types(not all c++ arithmetic types?), jobject, jstring, ..., JObject, c++ array types
//using if_jni_type = enable_if_t<is_arithmetic_v<T> || is_array_like_v<T> || is_same_v<T, jobject> || ... || is_JObject_v<T>>
template<typename T, bool = is_enum_v<T>> struct signature;
template<typename T>
inline constexpr auto signature_v = signature<T, is_enum_v<T>>::value;
// signature_of<T>() returns the JNI signature of T as ct_string. signature_of() is void's signature
// signature of function ptr
template<typename R, typename... Args> constexpr auto signature_of(R(*)(Args...));

class [[nodiscard]] LocalRef {
public:
    template<typename J, detail::if_jobject<J> = true>
    LocalRef(J j, JNIEnv* env = nullptr) : j_(j), env_(env) {}

    LocalRef(const LocalRef&) = delete;
    LocalRef& operator=(const LocalRef&) = delete;
    LocalRef(LocalRef&& that) noexcept : j_(that.j_), env_(that.env_) { that.j_ = nullptr;}  // total ref obj is 1
    LocalRef& operator=(LocalRef&& that) noexcept {  // total ref obj is 2, or delete 1 here
        swap(j_, that.j_);
        swap(env_, that.env_);
        return *this;
    }
    ~LocalRef() {
        if (!j_)
            return;
        if (!env_)
            env_ = getEnv();
        env_->DeleteLocalRef(j_);
    }

    explicit operator bool() const { return !!j_; }
    template<typename J, detail::if_jobject<J> = true>
    operator J() const {return static_cast<J>(j_);}
    template<typename J, detail::if_jobject<J> = true>
    [[nodiscard]] J get() const {return static_cast<J>(j_);}
private:
    jobject j_ = nullptr;
    JNIEnv* env_ = nullptr;
};

// The local reference is owned by s and deleted when the temporary is destroyed.
[[nodiscard]] string to_string(LocalRef&& s, JNIEnv* env = nullptr);

// object must be a class template, thus we can cache class id using static member and call FindClass() only once, and also make it possible to cache method id because method id
template<class CTag>
class JObject : public ClassTag
{
public:
    using Tag = CTag;
    static constexpr auto className(); // ct_string
    static constexpr auto signature(); // ct_string

    // construct from an existing jobject. Usually obj is from native jni api containing a local ref, and it's local ref will be deleted if del_localref is true
    JObject(jobject obj = nullptr, bool del_localref = true) {
        JNIEnv *env = getEnv();
        reset(obj, env);
        if (obj && del_localref)
            env->DeleteLocalRef(obj);
    }
    JObject(LocalRef&& ref) : JObject((jobject)ref, false) {}
    JObject(const LocalRef& ref) = delete; // required
    JObject(const JObject &other) { reset(other.id()).setError(other.error()); }
    JObject &operator=(const JObject &other) {
        if (this == &other)
            return *this;
        return reset(other.id()).setError(other.error());
    }
    JObject(JObject &&other) { // default implementation does not reset other.oid_
        swap(oid_, other.oid_);
        swap(error_, other.error_);
    }
    JObject &operator=(JObject &&other) { // default implementation does not reset other.oid_
        swap(oid_, other.oid_);
        swap(error_, other.error_);
        return *this;
    }
    ~JObject() { reset(); }

    operator jobject() const { return oid_;}
    operator jclass() const { return classId();}
    jobject id() const { return oid_; }
    explicit operator bool() const { return !!oid_;}
    const string& error() const {return error_;}
    JObject& reset(jobject obj = nullptr, JNIEnv *env = nullptr);

    template<typename... Args>
    [[nodiscard]] bool create(Args&&... args);

    [[nodiscard]] string toString() const;
    [[nodiscard]] auto getClass() const -> JObject<detail::JavaLangClassTag>;
    [[nodiscard]] jint hashCode() const;
    template<class OtherTag>
    [[nodiscard]] bool equals(const JObject<OtherTag>& other) const;

    /* with MethodTag we can avoid calling GetMethodID() in every call()
        struct MyMethod : jmi::MethodTag { static const char* name() { return "myMethod";} };
        return call<T, MyMethod>(args...);
    */
    template<typename T, class MTag, typename... Args,  detail::if_MethodTag<MTag> = true>
    [[nodiscard]] inline T call(Args&&... args) const;
    template<class MTag, typename... Args,  detail::if_MethodTag<MTag> = true>
    inline void call(Args&&... args) const;
    /* with MethodTag we can avoid calling GetStaticMethodID() in every callStatic()
        struct MyStaticMethod : jmi::MethodTag { static const char* name() { return "myStaticMethod";} };
        JObject<CT>::callStatic<R, MyStaticMethod>(args...);
    */
    template<typename T, class MTag, typename... Args,  detail::if_MethodTag<MTag> = true>
    [[nodiscard]] static T callStatic(Args&&... args);
    template<class MTag, typename... Args,  detail::if_MethodTag<MTag> = true>
    static void callStatic(Args&&... args);

#if (JMI_CXX20 + 0)
    // C++20: cache jmethodID via ct_string NTTP (same order as MethodTag overloads).
    //   call<"updateTexImage">();  call<jlong, "getTimestamp">();
    template<typename T, ct_string Name, typename... Args>
    [[nodiscard]] inline T call(Args&&... args) const;
    template<ct_string Name, typename... Args>
    inline void call(Args&&... args) const;
    template<typename T, ct_string Name, typename... Args>
    [[nodiscard]] static T callStatic(Args&&... args);
    template<ct_string Name, typename... Args>
    static void callStatic(Args&&... args);
#endif

    // get/set field and static field
    template<class FTag, typename T, detail::if_FieldTag<FTag> = true>
    [[nodiscard]] T get() const;
    template<class FTag, typename T, detail::if_FieldTag<FTag> = true>
    [[nodiscard]] bool set(T&& v);
    template<class FTag, typename T, detail::if_FieldTag<FTag> = true>
    [[nodiscard]] static T getStatic();
    template<class FTag, typename T, detail::if_FieldTag<FTag> = true>
    [[nodiscard]] static bool setStatic(T&& v);

    // the following call()/callStatic() will always invoke GetMethodID()/GetStaticMethodID()
    template<typename T, typename... Args, detail::if_not_MethodTag<T> = true>
    [[nodiscard]] T call(const char* methodName, Args&&... args) const;
    template<typename... Args>
    void call(const char* methodName, Args&&... args) const;
    template<typename T, typename... Args, detail::if_not_MethodTag<T> = true>
    [[nodiscard]] static T callStatic(const char* name, Args&&... args);
    template<typename... Args>
    static void callStatic(const char* name, Args&&... args);

    template<typename T>
    [[nodiscard]] T get(const char* fieldName) const;
    template<typename T>
    [[nodiscard]] bool set(const char* fieldName, T&& v);
    template<typename T>
    [[nodiscard]] static T getStatic(const char* fieldName);
    template<typename T>
    [[nodiscard]] static bool setStatic(const char* fieldName, T&& v);

    /*
        Field API
       Field lifetime is bounded to JObject, it does not add object ref, when object is destroyed/reset, accessing Field will fail (TODO: how to avoid crash?)
       jfieldID is cacheable if MayBeFTag is a FieldTag
       Usage:
        auto f = obj.field<int, MyFieldTag>(), obj.field<int>("MyField"), JObject<...>::staticField<string>("MySField");
        auto& sf = JObject<...>::staticField<string, MySFieldTag>();
        f.set(123)/get(), sf.set("test")/get();
        f = 345; int fv = f;
     */
    // F can be supported types: jni primitives(jint, jlong, ... not jobject because we can't know class name) and JObject
    template<typename F, class MayBeFTag, bool isStaticField>
    class [[nodiscard]] Field { // JObject.classId() works in Field?
    public:
        jfieldID id() const { return fid_; }
        operator jfieldID() const { return fid_; }
        operator F() const { return get(); }
        [[nodiscard]] F get() const;
        void set(F&& v);
        Field& operator=(F&& v) {
            set(std::forward<F>(v));
            return *this;
        }
    protected:
        static jfieldID cachedId(jclass cid); // usually cid is used only once
        // oid nullptr: static field
        // it's protected so we can sure cacheable ctor will not be called for uncacheable Field
        Field(jclass cid, jobject oid = nullptr);
        Field(jclass cid, const char* name, jobject oid = nullptr);

        union {
            jobject oid_;
            jclass cid_;
        };
        jfieldID fid_ = nullptr;
        friend class JObject<CTag>;
    };
    template<class FTag, typename T, detail::if_FieldTag<FTag> = true>
    [[nodiscard]] auto field() const->Field<T, FTag, false> {
        return Field<T, FTag, false>(classId(), oid_);
    }
    template<typename T>
    [[nodiscard]] auto field(const char* name) const->Field<T, void, false> {
        return Field<T, void, false>(classId(), name, oid_);
    }
    template<class FTag, typename T, detail::if_FieldTag<FTag> = true>
    [[nodiscard]] static auto staticField()->Field<T, FTag, true>& { // cacheable and static java storage, so returning ref is better
        static Field<T, FTag, true> f(classId());
        return f;
    }
    template<typename T>
    [[nodiscard]] static auto staticField(const char* name)->Field<T, void, true> {
        return Field<T, void, true>(classId(), name);
    }
private:
    static jclass classId(JNIEnv* env = nullptr);
    JObject& setError(const string& s) const noexcept {
        error_ = s;
        return *const_cast<JObject*>(this);
    }
    JObject& setError(string&& s) const noexcept {
        error_ = std::move(s);
        return *const_cast<JObject*>(this);
    }

    jobject oid_ = nullptr;
    mutable string error_;
};

template<class CTag>
using Object = JObject<CTag>;
/*************************** JMI Public APIs End ***************************/
} // namespace jmi

#define JMI_STRINGIFY(X) _JMI_STRINGIFY(X)
#define _JMI_STRINGIFY(X) #X

namespace jmi {

#if !(JMI_CXX20 + 0)
template< class T >
using remove_cvref_t = remove_cv_t<remove_reference_t<T>>;
#endif

namespace detail {
using namespace std;

template <typename T, typename = void>
struct is_array_like : false_type {};
template <typename T>
struct is_array_like<T, void_t<decltype(declval<T>()[0]), decltype(declval<T>().size())>> : true_type {};
template <typename T>
inline constexpr bool is_array_like_v = is_array_like<T>::value;
template <typename T, typename = void>
struct is_string : false_type {};
template <typename T>
struct is_string<T, void_t<decltype(declval<T>().substr())>> : true_type {};
template <typename T>
inline constexpr bool is_string_v = is_string<T>::value;
template <typename T>
inline constexpr bool is_jarray_cpp_v = (is_array_like_v<T> || is_array_v<T>)
    && !is_string_v<T>
    && !is_same_v<decay_t<T>, char*>
    && !is_same_v<decay_t<T>, const char*>;
template<typename T>
struct is_jarray_cpp : bool_constant<is_jarray_cpp_v<T>> {};

template<class T>
struct is_ref_wrap : false_type {};
template<class T>
struct is_ref_wrap<reference_wrapper<T>> : true_type {};
template<class T>
inline constexpr bool is_ref_wrap_v = is_ref_wrap<T>::value;

template<typename T>
using if_jarray_cpp = enable_if_t<is_jarray_cpp_v<T>, bool>;
template<typename T>
using if_not_jarray_cpp = enable_if_t<!is_jarray_cpp_v<T>, bool>;

template<class T>
inline constexpr bool is_jarray_v = is_base_of_v<remove_pointer_t<jarray>, remove_pointer_t<T>>;
template<class T>
struct is_jarray : bool_constant<is_jarray_v<T>> {};

template<typename T>
inline constexpr bool is_cstring_v = is_same_v<decay_t<T>, char*> || is_same_v<decay_t<T>, const char*>;
} // namespace detail
inline namespace impl {
    static inline string to_string(const string& s) noexcept { return s;}

    template<size_t N>
    constexpr auto norm(ct_string<N> a) noexcept {
        for (size_t i = 0; i < N - 1; ++i) { // keep trailing '\0'
            if (a[i] == '.')
                a[i] = '/';
        }
        return a;
    }

    template<size_t N>
    string to_string(ct_string<N> const& a) noexcept {
        return {a.data(), a.length()};
    }

    template<size_t N1, size_t N2>
    constexpr bool operator==(ct_string<N1> a, const char (&s)[N2]) noexcept {
        if constexpr (N1 != N2)
            return false;
        else {
            for (size_t i = 0; i < N1; ++i) {
                if (a[i] != s[i])
                    return false;
            }
            return true;
        }
    }
} // namespace impl

namespace detail {
struct JavaLangClassTag : ClassTag {
    static constexpr auto name() { return JMISTR("java/lang/Class"); }
};
} // namespace detail

/*************************** Below is JMI implementation and internal APIs***************************/

//signature_of_args<decltype(Args)...>::value, template<typename ...A> struct signature_of_args?
template<> struct signature<bool> { static constexpr ct_string value = "Z";}; // jboolean is uint8_t/uchar
template<> struct signature<jboolean> { static constexpr ct_string value = "Z";};
template<> struct signature<jbyte> { static constexpr ct_string value = "B";};
template<> struct signature<jchar> { static constexpr ct_string value = "C";};
template<> struct signature<jshort> { static constexpr ct_string value = "S";};
template<> struct signature<jlong> { static constexpr ct_string value = "J";};
template<> struct signature<jint> { static constexpr ct_string value = "I";};
template<> struct signature<jfloat> { static constexpr ct_string value = "F";};
template<> struct signature<jdouble> { static constexpr ct_string value = "D";};
template<> struct signature<jbooleanArray> { static constexpr ct_string value = "[Z";};
template<> struct signature<jbyteArray> { static constexpr ct_string value = "[B";};
template<> struct signature<jcharArray> { static constexpr ct_string value = "[C";};
template<> struct signature<jshortArray> { static constexpr ct_string value = "[S";};
template<> struct signature<jintArray> { static constexpr ct_string value = "[I";};
template<> struct signature<jlongArray> { static constexpr ct_string value = "[J";};
template<> struct signature<jfloatArray> { static constexpr ct_string value = "[F";};
template<> struct signature<jdoubleArray> { static constexpr ct_string value = "[D";};
// "L...;" is used in method parameter
template<> struct signature<string> { static constexpr ct_string value = "Ljava/lang/String;";};
template<> struct signature<char*> { static constexpr ct_string value = "Ljava/lang/String;";};

template<typename E>
struct signature<E, true> : signature<jint>{};

// if T is jobject or LocalRef, signature can get from GetObjectClass=>getName, but can not be cached
constexpr auto signature_of() { return ct_string("V");}

template<typename T>
constexpr auto signature_of() {
    using U = remove_cvref_t<T>;
    if constexpr (detail::is_ref_wrap_v<U>) { // assume no container<reference_wrapper<...>>
        using E = typename U::type;
        if constexpr (detail::is_jarray_cpp_v<E>)
            return "[" + signature_of<remove_cvref_t<decltype(E{}[0])>>();
        else
            return signature_of<E>();
    } else if constexpr (detail::is_JObject_v<U>) {
        return U::signature();
    } else if constexpr (detail::is_jarray_cpp_v<U>) {
        return "[" + signature_of<remove_cvref_t<decltype(U{}[0])>>(); // both c array and cpp containers
    } else if constexpr (detail::is_cstring_v<U>) {
        return signature_v<char*>;
    } else if constexpr (is_same_v<U, jstring>) {
        return signature_v<string>;
    } else if constexpr (detail::is_jobject_v<U> && !detail::is_jarray_v<U>) {
        return ct_string("Ljava/lang/Object;");
    } else if constexpr (is_pointer_v<U> && detail::is_jarray_v<U>) {
        return signature_v<U>;
    } else if constexpr (is_pointer_v<U>) {
        return signature_v<jlong>;
    } else {
        return signature_v<remove_cvref_t<decay_t<T>>>;
    }
}

// signature_of_no_ptr: consistent for any type, including void. so for call<T,MT>(...) T can be void. TODO: remove
template<typename T>
constexpr auto signature_of_no_ptr() {
    if constexpr (is_same_v<T, void*>)
        return signature_of();
    else
        return signature_of<remove_pointer_t<T>>();
}

namespace detail {
    template<typename... Args>
    constexpr auto args_signature() {
        // empty Args → "(" + ")" == "()"; otherwise "(" + sigs... + ")"
        return ct_string("(") + (signature_of<remove_cvref_t<Args>>() + ... + ct_string(")"));
    }
} //namespace detail


template<typename R, typename... Args>
constexpr auto signature_of(R (*)(Args...)) {
    return detail::args_signature<Args...>() + signature_of_no_ptr<add_pointer_t<R>>();
}

namespace detail {
    // Clear pending JNI exception and build an error string. Pass message as const char* pieces (not std::string)
    // so concatenation stays in jmi.cpp — reduces binary bloat from inlining string ops into every call_on_exit / scope_exit_handler destructor.
    std::string handle_exception(JNIEnv* env = nullptr,
        const char* p0 = nullptr, const char* p1 = nullptr, const char* p2 = nullptr,
        const char* p3 = nullptr, const char* p4 = nullptr) noexcept;

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
    auto call_on_exit(F&& f) noexcept {
        return scope_exit_handler{std::forward<F>(f)};
    }

    template<typename T, if_not_JObject<T> = true>
    jarray make_jarray(JNIEnv *env, const T &element, size_t size); // element is for getting jobject class
    template<class T, if_JObject<T> = true>
    jarray make_jarray(JNIEnv *env, const T &element, size_t size) {
        return env->NewObjectArray(size, jclass(element), nullptr);
    }

    template<typename T, if_not_JObject<T> = true>
    void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const T &elm);
    template<class T, if_JObject<T> = true>
    void set_jarray(JNIEnv *env, jarray arr, size_t position, size_t n, const T &elm) {
        set_jarray(env, arr, position, n, jobject(elm));
    }

    template<typename T>
    jarray to_jarray(JNIEnv* env, const T &c0, size_t N, bool is_ref = false);
    template<typename T, size_t N>
    jarray to_jarray(JNIEnv* env, const T(&c)[N], bool is_ref = false) {
        if constexpr (N == 0)
            return to_jarray(env, T{}, 0, is_ref);
        else
            return to_jarray(env, c[0], N, is_ref);
    }
    template<typename C> // c++ container (vector, valarray, array) to jarray. no if_jarray_cpp check (requires overload for both vector like and array like containers) because it's checked by to_jvalue
    jarray to_jarray(JNIEnv* env, const C &c, bool is_ref = false) {
        using T = remove_cvref_t<decltype(declval<const C&>()[0])>;
        if (c.size() == 0)
            return to_jarray(env, T{}, 0, is_ref);
        return to_jarray(env, c[0], c.size(), is_ref);
    }
    // env can be null for base types
    template<typename T>
    using if_enum = enable_if_t<is_enum_v<T>, bool>;
    template<typename T>
    using if_not_enum = enable_if_t<!is_enum_v<T>, bool>;
    template<typename T, if_not_enum<T> = true, if_not_JObject<T> = true, if_not_jobject<T> = true>
    jvalue to_jvalue(const T &obj, JNIEnv* env = nullptr);
    template<typename T, if_enum<T> = true, if_not_JObject<T> = true>
    jvalue to_jvalue(const T &obj, JNIEnv* env = nullptr) {return to_jvalue((jint)obj, env);}
    template<typename T, detail::if_not_jobject<T> = true> jvalue to_jvalue(T *obj, JNIEnv* env) { return to_jvalue((jlong)obj, env); }
    template<typename T, detail::if_jobject<T> = true>
    inline jvalue to_jvalue(T obj, JNIEnv* = nullptr) {
        jvalue v;
        v.l = obj;
        return v;
    }
    jvalue to_jvalue(const char* obj, JNIEnv* env);// { return to_jvalue(string(obj)); }
    template<typename T, if_not_enum<T> = true, if_JObject<T> = true>
    jvalue to_jvalue(const T &obj, JNIEnv* env = nullptr) { return to_jvalue(jobject(obj), env);}

    template<template<typename,class...> class C, typename T, class... A, if_jarray_cpp<C<T, A...>>  = true> // if_jarray_cpp: exclude string, jarray works (copy chars)
    jvalue to_jvalue(const C<T, A...> &c, JNIEnv* env) { return to_jvalue(to_jarray(env, c), env); }
    template<typename T, size_t N> jvalue to_jvalue(const array<T, N> &c, JNIEnv* env) { return to_jvalue(to_jarray(env, c), env); }

    template<typename T> jvalue to_jvalue(const reference_wrapper<T>& t, JNIEnv* env) { return to_jvalue(t.get(), env); } // TODO: no jvalue set
    template<template<typename,class...> class C, typename T, class... A, if_jarray_cpp<C<T, A...>>  = true> // if_jarray_cpp: exclude string, jarray works (copy chars)
    jvalue to_jvalue(const reference_wrapper<C<T, A...>>& c, JNIEnv* env) { return to_jvalue(to_jarray(env, c.get(), true), env); }
    template<typename T, size_t N> jvalue to_jvalue(const reference_wrapper<T[N]>& c, JNIEnv* env) { return to_jvalue(to_jarray<T,N>(env, c.get(), true), env); }
    template<class CTag>
    jvalue to_jvalue(const JObject<CTag> &obj, JNIEnv* env);
    // T(&)[N]?

    template<typename... Args>
    [[nodiscard]] auto make_jargs(JNIEnv* env, Args&&... args) {
        return array<jvalue, sizeof...(Args)>{to_jvalue(std::forward<Args>(args), env)...};
    }

// from_jvalue/array() is called if parameter of call() is of type reference_wrapper<...>
    template<typename T, if_not_JObject<T> = true>
    void from_jarray(JNIEnv* env, const jvalue& v, T* t, size_t N);
    template<typename T, if_JObject<T> = true>
    void from_jarray(JNIEnv* env, const jvalue& v, T* t, size_t N) {
        for (size_t i = 0; i < N; ++i) {
            LocalRef s = {env->GetObjectArrayElement(static_cast<jobjectArray>(v.l), i), env};
            (t + i)->reset(s);
        }
    }
    // reference_wrapper<const T> should do nothing
    template<typename T> void from_jvalue(JNIEnv* env, const jvalue& v, const T &t) {}
    // env can be null for base types
    template<typename T, if_not_JObject<T> = true> void from_jvalue(JNIEnv* env, const jvalue& v, T &t);
    // reference_wrapper<const T[]> should do nothing
    template<typename T> void from_jvalue(JNIEnv* env, const jvalue& v, const T *t, size_t n = 0) {}
    template<typename T> void from_jvalue(JNIEnv* env, const jvalue& v, T *t, size_t n = 0) { // T* and T(&)[N] is the same
        if (n <= 0)
            from_jvalue(env, v, (jlong&)t);
        else
            from_jarray(env, v, t, n);
    }
    template<typename T, if_JObject<T> = true> void from_jvalue(JNIEnv* env, const jvalue& v, T &t) {
        t.reset(v.l, env); // local ref will be deleted in caller set_ref_from_jvalue()
    }
    template<template<typename,class...> class C, typename T, class... A, if_jarray_cpp<C<T, A...>>  = true> // if_jarray_cpp: exclude string. jarray works too (copy chars)
    void from_jvalue(JNIEnv* env, const jvalue& v, C<T, A...> &t) {
        if (t.size() == 0)
            return;
        from_jarray(env, v, &t[0], t.size());
    }
    template<typename T, size_t N> void from_jvalue(JNIEnv* env, const jvalue& v, array<T, N> &t) { from_jarray(env, v, t.data(), N); }
    //template<typename T, size_t N> void from_jvalue(JNIEnv* env, const jvalue& v, T(&t)[N]) { from_jarray(env, v, t, N); }

    template<typename T>
    inline constexpr bool has_local_ref_v = !is_arithmetic_v<T> && !is_enum_v<T> && !is_pointer_v<T> && !is_JObject_v<T>; // is_jobject<T>? is_jarray_cpp?
    template<typename T>
    void set_ref_from_jvalue(JNIEnv* env, jvalue* jargs, T, bool) {
        using Tn = remove_reference_t<T>;
        if constexpr (has_local_ref_v<Tn>)
            env->DeleteLocalRef(jargs->l);
    }
    static inline void set_ref_from_jvalue(JNIEnv* env, jvalue *jargs, const char*, bool) {
        env->DeleteLocalRef(jargs->l);
    }
    template<typename T>
    void set_ref_from_jvalue(JNIEnv* env, jvalue *jargs, reference_wrapper<T> ref, bool copy_back) {  // do nothing in from_jvalue for const T
        if (copy_back)
            from_jvalue(env, *jargs, ref.get());
        using Tn = remove_reference_t<T>;
        if constexpr (has_local_ref_v<Tn>)
            env->DeleteLocalRef(jargs->l);
    }
    template<template<typename,class...> class C, typename T, class... A, if_jarray_cpp<C<T, A...>>  = true> // if_jarray_cpp: exclude string, jarray works (copy chars)
    void set_ref_from_jvalue(JNIEnv* env, jvalue *jargs, reference_wrapper<C<T, A...>> ref, bool copy_back) {
        if (copy_back)
            from_jvalue(env, *jargs, ref.get());
        env->DeleteLocalRef(jargs->l);
    }
    template<typename T, size_t N>
    void set_ref_from_jvalue(JNIEnv* env, jvalue *jargs, reference_wrapper<T[N]> ref, bool copy_back) {
        if (copy_back)
            from_jvalue(env, *jargs, ref.get(), N); // assume only T* and T[N]
        env->DeleteLocalRef(jargs->l);
    }
    template<typename T, size_t N>
    void set_ref_from_jvalue(JNIEnv* env, jvalue *jargs, reference_wrapper<array<T, N>> ref, bool copy_back) {
        if (copy_back)
            from_jvalue(env, *jargs, &ref.get()[0], N); // assume only T* and T[N]
        env->DeleteLocalRef(jargs->l);
    }

    template<typename... Args>
    void ref_args_from_jvalues([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jvalue *jargs, [[maybe_unused]] bool copy_back, Args&&... args) {
        size_t i = 0;
        ((set_ref_from_jvalue(env, jargs + i, std::forward<Args>(args), copy_back), ++i), ...);
    }

    template<typename T, if_not_JObject<T> = true, if_not_jarray_cpp<T> = true>
    T call_method(JNIEnv *env, jobject oid, jmethodID mid, jvalue *args);
    template<class T, if_JObject<T> = true>
    T call_method(JNIEnv *env, jobject oid, jmethodID mid, jvalue *args) {
        T t;
        LocalRef r = call_method<jobject>(env, oid, mid, args);
        if (!r || env->ExceptionCheck())
            return T();
        t.reset(r, env);
        return t;
    }
    template<typename T, if_jarray_cpp<T> = true>
    T call_method(JNIEnv *env, jobject oid, jmethodID mid, jvalue *args) {
        LocalRef ja = call_method<jobject>(env, oid, mid, args); // local ref will not be deleted in from_jvalue(), so manage here
        if (!ja || env->ExceptionCheck())
            return T();
        jvalue jv;
        jv.l = ja;
        T t(env->GetArrayLength(ja));
        from_jvalue(env, jv, t);
        return t;
    }

    template<typename T, typename... Args>
    T call_method_set_ref(JNIEnv *env, jobject oid, jmethodID mid, jvalue *jargs, Args&&... args) {
        auto setter = call_on_exit([=]{
            // With a pending exception, only local-reference cleanup is safe; do not copy back output arguments.
            const auto copy_back = !env->ExceptionCheck();
            ref_args_from_jvalues(env, jargs, copy_back, args...);
        });
        // Argument conversion may have raised an exception; do not invoke JNI with it pending.
        if (env->ExceptionCheck())
            return T();
       return call_method<T>(env, oid, mid, jargs);
    }

    template<typename T, if_not_JObject<T> = true, if_not_jarray_cpp<T> = true>
    T call_static_method(JNIEnv *env, jclass classId, jmethodID methodId, jvalue *args);
    template<class T, if_JObject<T> = true>
    T call_static_method(JNIEnv *env, jclass cid, jmethodID mid, jvalue *args) {
        LocalRef r = call_static_method<jobject>(env, cid, mid, args);
        if (!r || env->ExceptionCheck())
            return T();
        T t;
        t.reset(r, env);
        return t;
    }
    template<class T, if_jarray_cpp<T> = true>
    T call_static_method(JNIEnv *env, jclass cid, jmethodID mid, jvalue *args) {
        LocalRef ja = call_static_method<jobject>(env, cid, mid, args); // local ref will not be deleted in from_jvalue(), so manage here
        if (!ja || env->ExceptionCheck())
            return T();
        jvalue jv;
        jv.l = ja;
        T t(env->GetArrayLength(ja)); // TODO: array is not supported
        from_jvalue(env, jv, t);
        return t;
    }
    template<typename T, typename... Args>
    T call_static_method_set_ref(JNIEnv *env, jclass cid, jmethodID mid, jvalue *jargs, Args&&... args) {
        auto setter = call_on_exit([=]{ // std::forward?
            // With a pending exception, only local-reference cleanup is safe; do not copy back output arguments.
            const auto copy_back = !env->ExceptionCheck();
            ref_args_from_jvalues(env, jargs, copy_back, args...);
        });
        // Argument conversion may have raised an exception; do not invoke JNI with it pending.
        if (env->ExceptionCheck())
            return T();
        return call_static_method<T>(env, cid, mid, jargs);
    }

    // std::function would heap-allocate the per-call lambda; nullptr is a no-op.
    template<typename F>
    void invoke_err_cb(F&& f, string&& s) {
        if constexpr (!is_null_pointer_v<decay_t<F>>)
            invoke(std::forward<F>(f), std::move(s));
    }

    template<typename T, typename ErrCb, typename... Args>
    T call_with_methodID(jobject oid, jclass cid, jmethodID* pmid, ErrCb&& err_cb, const char* signature, const char* name, Args&&... args) {
        invoke_err_cb(err_cb, string());
        if (!cid)
            return T();
        if (!oid) {
            invoke_err_cb(err_cb, string("Invalid object instance"));
            return T();
        }
        JNIEnv *env = getEnv();
        const auto checker = call_on_exit([=, err_cb = std::forward<ErrCb>(err_cb)]{
            auto ex = handle_exception(env, "Failed to call method '", name, "' with signature '", signature, ".");
            if (!ex.empty())
                invoke_err_cb(err_cb, std::move(ex));
        });
        jmethodID mid = nullptr;
        if (pmid)
            mid = *pmid;
        if (!mid) {
            mid = env->GetMethodID(cid, name, signature);
            if (pmid)
                *pmid = mid;
        }
        if (!mid || env->ExceptionCheck())
            return T();
        auto jargs = make_jargs(env, std::forward<Args>(args)...);
        return call_method_set_ref<T>(env, oid, mid, jargs.empty() ? nullptr : jargs.data(), std::forward<Args>(args)...);
    }

    template<typename T, typename ErrCb, typename... Args>
    T call_static_with_methodID(jclass cid, jmethodID* pmid, ErrCb&& err_cb, const char* signature, const char* name, Args&&... args) {
        invoke_err_cb(err_cb, string());
        if (!cid)
            return T();
        JNIEnv *env = getEnv();
        auto checker = call_on_exit([=, err_cb = std::forward<ErrCb>(err_cb)]{
            auto ex = handle_exception(env, "Failed to call static method '", name, "' with signature '", signature, ".");
            if (!ex.empty())
                invoke_err_cb(err_cb, std::move(ex));
        });
        jmethodID mid = nullptr;
        if (pmid)
            mid = *pmid;
        if (!mid) {
            mid = env->GetStaticMethodID(cid, name, signature);
            if (pmid)
                *pmid = mid;
        }
        if (!mid || env->ExceptionCheck())
            return T();
        auto jargs = make_jargs(env, std::forward<Args>(args)...);
        return call_static_method_set_ref<T>(env, cid, mid, jargs.empty() ? nullptr : jargs.data(), std::forward<Args>(args)...);
    }


    template<typename T>
    jfieldID get_field_id(JNIEnv* env, jclass cid, const char* name, jfieldID* pfid = nullptr);

    template<class T, if_not_JObject<T> = true, if_not_jarray_cpp<T> = true>
    T get_field(JNIEnv* env, jobject oid, jfieldID fid);
    template<class T, if_JObject<T> = true>
    T get_field(JNIEnv* env, jobject oid, jfieldID fid) {
        LocalRef r = env->GetObjectField(oid, fid);
        if (!r)
            return T();
        T t;
        t.reset(r, env);
        return t;
    }
    template<class T, if_jarray_cpp<T> = true>
    T get_field(JNIEnv* env, jobject oid, jfieldID fid) {
        LocalRef ja = env->GetObjectField(oid, fid);
        if (!ja || env->ExceptionCheck())
            return T();
        jvalue jv;
        jv.l = ja;
        T t(env->GetArrayLength(ja));
        from_jvalue(env, jv, t);
        return t;
    }

    template<typename T>
    T get_field(jobject oid, jclass cid, jfieldID* pfid, const char* name) {
        JNIEnv* env = getEnv();
        // TODO: call_on_exit?
        jfieldID fid = get_field_id<T>(env, cid, name, pfid);
        if (!fid) // no exception check, already exist in get()? what about call?
            return T();
        return get_field<T>(env, oid, fid);
    }
    template<class T>
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
    jfieldID get_static_field_id(JNIEnv* env, jclass cid, const char* name, jfieldID* pfid = nullptr);
    template<typename T, if_not_JObject<T> = true, if_not_jarray_cpp<T> = true>
    T get_static_field(JNIEnv* env, jclass cid, jfieldID fid);
    template<class T, if_JObject<T> = true>
    T get_static_field(JNIEnv* env, jclass cid, jfieldID fid) {
        LocalRef r = env->GetStaticObjectField(cid, fid);
        if (!r || env->ExceptionCheck())
            return T();
        T t;
        t.reset(r, env);
        return t;
    }
    template<class T, if_jarray_cpp<T> = true>
    T get_static_field(JNIEnv* env, jclass cid, jfieldID fid) {
        LocalRef ja = env->GetStaticObjectField(cid, fid);
        if (!ja || env->ExceptionCheck())
            return T();
        jvalue jv;
        jv.l = ja;
        T t(env->GetArrayLength(ja));
        from_jvalue(env, jv, t);
        return t;
    }

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

    template<typename T>
    jfieldID get_field_id(JNIEnv* env, jclass cid, const char* name, jfieldID* pfid) {
        jfieldID fid = nullptr;
        if (pfid)
            fid = *pfid;
        if (!fid) {
            fid = env->GetFieldID(cid, name, signature_of<T>().data());
            if (pfid)
                *pfid = fid;
        }
        return fid;
    }
    template<typename T>
    jfieldID get_static_field_id(JNIEnv* env, jclass cid, const char* name, jfieldID* pfid) {
        jfieldID fid = nullptr;
        if (pfid)
            fid = *pfid;
        if (!fid) {
            fid = env->GetStaticFieldID(cid, name, signature_of<T>().data());
            if (pfid)
                *pfid = fid;
        }
        return fid;
    }
} // namespace detail

template<class CTag>
constexpr auto JObject<CTag>::className()
{
    // C++17: ct_string is array; C++20: structural ct_string / NamedClassTag
    constexpr auto raw = CTag::name();
    if constexpr (raw[0] == 'L' && raw[raw.size() - 2] == ';') // N - 1 == '\0', check N - 2
        return impl::norm(raw.template substr<raw.size() - 3>(1));
    else
        return impl::norm(raw);
}

template<class CTag>
constexpr auto JObject<CTag>::signature()
{
    return "L" + className() + ";";
}

template<class CTag>
JObject<CTag>& JObject<CTag>::reset(jobject obj, JNIEnv *env) {
    if (oid_ == obj) // same handle, including both null
        return *this;
    if (!env) {
        env = getEnv();
        if (!env)
            return setError("Invalid JNIEnv");
    }
    // Local and global refs to the same Java object have different handle values.
    if (oid_ && obj && env->IsSameObject(oid_, obj))
        return *this;
    error_.clear();
    env->DeleteGlobalRef(oid_); // can be null
    oid_ = nullptr;
    if (obj) {
        oid_ = env->NewGlobalRef(obj);
        //env->DeleteLocalRef(obj); // obj from JObject has no local ref
    }
    return *this;
}

template<class CTag>
template<typename... Args>
bool JObject<CTag>::create(Args&&... args) {
    using namespace std;
    using namespace detail;
    JNIEnv* env = nullptr; // FIXME: why build error if let env be the last parameter of create()?
    if (!env) {
        env = getEnv();
        if (!env) {
            setError("No JNIEnv when creating class '" + to_string(className()) + "'");
            return false;
        }
    }
    const jclass cid = classId(env);
    if (!cid) {
        setError("Failed to find class '" + to_string(className()) + "'");
        return false;
    }
    const auto checker = call_on_exit([=]{ handle_exception(env); });
    static constexpr auto s = args_signature<Args...>() + signature_of(); // void
    static const jmethodID mid = env->GetMethodID(cid, "<init>", s.data()); // can be static because class id, signature and arguments combination is unique
    if (!mid) {
        setError(string("Failed to find constructor of '") + className().data() + "' with signature '" + s.data() + "'.");
        return false;
    }
    auto jargs = make_jargs(env, std::forward<Args>(args)...);
    auto* const args_ptr = jargs.empty() ? nullptr : jargs.data();
    const auto cleanup = call_on_exit([=]{
        ref_args_from_jvalues(env, args_ptr, false, args...);
    });
    if (env->ExceptionCheck())
        return false;
    LocalRef oid = env->NewObjectA(cid, mid, args_ptr);
    if (!oid) {
        setError(string("Failed to call constructor '") + className().data() + "' with signature '" + s.data() + "'.");
        return false;
    }
    reset(oid, env);
    return !!oid_;
}

template<class CTag>
string JObject<CTag>::toString() const {
#if (JMI_CXX20 + 0)
    return call<string, "toString">();
#else
    struct ToString : MethodTag { static const char* name() { return "toString"; }};
    return call<string, ToString>();
#endif
}

template<class CTag>
auto JObject<CTag>::getClass() const -> JObject<detail::JavaLangClassTag> {
#if (JMI_CXX20 + 0)
    return call<JObject<detail::JavaLangClassTag>, "getClass">();
#else
    struct GetClass : MethodTag { static const char* name() { return "getClass"; }};
    return call<JObject<detail::JavaLangClassTag>, GetClass>();
#endif
}

template<class CTag>
jint JObject<CTag>::hashCode() const {
#if (JMI_CXX20 + 0)
    return call<jint, "hashCode">();
#else
    struct HashCode : MethodTag { static const char* name() { return "hashCode"; }};
    return call<jint, HashCode>();
#endif
}

template<class CTag>
template<class OtherTag>
bool JObject<CTag>::equals(const JObject<OtherTag>& other) const {
#if (JMI_CXX20 + 0)
    return call<jboolean, "equals">(other.id());
#else
    struct Equals : MethodTag { static const char* name() { return "equals"; }};
    return call<jboolean, Equals>(other.id());
#endif
}

template<class CTag>
template<typename T, class MTag, typename... Args, detail::if_MethodTag<MTag>>
T JObject<CTag>::call(Args&&... args) const {
    using namespace detail;
    static constexpr auto s = args_signature<Args...>() + signature_of_no_ptr<add_pointer_t<T>>();
    static jmethodID mid = nullptr;
    return call_with_methodID<T>(oid_, classId(), &mid, [this](string&& err){ setError(std::move(err));}, s.data(), MTag::name(), std::forward<Args>(args)...);
}
template<class CTag>
template<class MTag, typename... Args, detail::if_MethodTag<MTag>>
void JObject<CTag>::call(Args&&... args) const {
    using namespace detail;
    static constexpr auto s = args_signature<Args...>() + signature_of();
    static jmethodID mid = nullptr;
    call_with_methodID<void>(oid_, classId(), &mid, [this](string&& err){ setError(std::move(err));}, s.data(), MTag::name(), std::forward<Args>(args)...);
}
template<class CTag>
template<typename T, class MTag, typename... Args,  detail::if_MethodTag<MTag>>
T JObject<CTag>::callStatic(Args&&... args) {
    using namespace detail;
    static constexpr auto s = args_signature<Args...>() + signature_of_no_ptr<add_pointer_t<T>>();
    static jmethodID mid = nullptr;
    return call_static_with_methodID<T>(classId(), &mid, nullptr, s.data(), MTag::name(), std::forward<Args>(args)...);
}
template<class CTag>
template<class MTag, typename... Args,  detail::if_MethodTag<MTag>>
void JObject<CTag>::callStatic(Args&&... args) {
    using namespace detail;
    static constexpr auto s = args_signature<Args...>() + signature_of();
    static jmethodID mid = nullptr;
    call_static_with_methodID<void>(classId(), &mid, nullptr, s.data(), MTag::name(), std::forward<Args>(args)...);
}

#if (JMI_CXX20 + 0)
template<class CTag>
template<typename T, ct_string Name, typename... Args>
T JObject<CTag>::call(Args&&... args) const {
    return call<T, NamedMethodTag<Name>>(std::forward<Args>(args)...);
}
template<class CTag>
template<ct_string Name, typename... Args>
void JObject<CTag>::call(Args&&... args) const {
    call<NamedMethodTag<Name>>(std::forward<Args>(args)...);
}
template<class CTag>
template<typename T, ct_string Name, typename... Args>
T JObject<CTag>::callStatic(Args&&... args) {
    return callStatic<T, NamedMethodTag<Name>>(std::forward<Args>(args)...);
}
template<class CTag>
template<ct_string Name, typename... Args>
void JObject<CTag>::callStatic(Args&&... args) {
    callStatic<NamedMethodTag<Name>>(std::forward<Args>(args)...);
}
#endif

template<class CTag>
template<class FTag, typename T, detail::if_FieldTag<FTag>>
T JObject<CTag>::get() const {
    static jfieldID fid = nullptr;
    auto checker = detail::call_on_exit([this]{
        // TODO: check fid
        setError(detail::handle_exception(nullptr, "Failed to get field '", FTag::name(), "' with signature '", signature_of<T>().data(), "."));
    });
    return detail::get_field<T>(oid_, classId(), &fid, FTag::name());
}
template<class CTag>
template<class FTag, typename T, detail::if_FieldTag<FTag>>
bool JObject<CTag>::set(T&& v) {
    static jfieldID fid = nullptr;
    auto checker = detail::call_on_exit([this]{
        setError(detail::handle_exception(nullptr, "Failed to set field '", FTag::name(), "' with signature '", signature_of<T>().data(), "."));
    });
    detail::set_field<T>(oid_, classId(), &fid, FTag::name(), std::forward<T>(v));
    return true;
}
template<class CTag>
template<class FTag, typename T, detail::if_FieldTag<FTag>>
T JObject<CTag>::getStatic() {
    static jfieldID fid = nullptr;
    return detail::get_static_field<T>(classId(), &fid, FTag::name());
}
template<class CTag>
template<class FTag, typename T, detail::if_FieldTag<FTag>>
bool JObject<CTag>::setStatic(T&& v) {
    static jfieldID fid = nullptr;
    detail::set_static_field<T>(classId(), &fid, FTag::name(), std::forward<T>(v));
    return true;
}


template<class CTag>
template<typename T, typename... Args, detail::if_not_MethodTag<T>>
T JObject<CTag>::call(const char* methodName, Args&&... args) const {
    using namespace detail;
    static constexpr auto s = args_signature<Args...>() + signature_of_no_ptr<add_pointer_t<T>>();
    return call_with_methodID<T>(oid_, classId(), nullptr, [this](string&& err){ setError(std::move(err));}, s.data(), methodName, std::forward<Args>(args)...);
}
template<class CTag>
template<typename... Args>
void JObject<CTag>::call(const char* methodName, Args&&... args) const {
    using namespace detail;
    static constexpr auto s = args_signature<Args...>() + signature_of();
    call_with_methodID<void>(oid_, classId(), nullptr, [this](string&& err){ setError(std::move(err));}, s.data(), methodName, std::forward<Args>(args)...);
}
template<class CTag>
template<typename T, typename... Args, detail::if_not_MethodTag<T>>
T JObject<CTag>::callStatic(const char* name, Args&&... args) {
    using namespace detail;
    static constexpr auto s = args_signature<Args...>() + signature_of_no_ptr<add_pointer_t<T>>();
    return call_static_with_methodID<T>(classId(), nullptr, nullptr, s.data(), name, std::forward<Args>(args)...);
}
template<class CTag>
template<typename... Args>
void JObject<CTag>::callStatic(const char* name, Args&&... args) {
    using namespace detail;
    static constexpr auto s = args_signature<Args...>() + signature_of();
    call_static_with_methodID<void>(classId(), nullptr, nullptr, s.data(), name, std::forward<Args>(args)...);
}

template<class CTag>
template<typename T>
T JObject<CTag>::get(const char* fieldName) const {
    jfieldID fid = nullptr;
    auto checker = detail::call_on_exit([fieldName, this]{
        // TODO: check fid
        setError(detail::handle_exception(nullptr, "Failed to get field '", fieldName, "' with signature '", signature_of<T>().data(), "."));
    });
    return detail::get_field<T>(oid_, classId(), &fid, fieldName);
}
template<class CTag>
template<typename T>
bool JObject<CTag>::set(const char* fieldName, T&& v) {
    jfieldID fid = nullptr;
    auto checker = detail::call_on_exit([fieldName, this]{
        setError(detail::handle_exception(nullptr, "Failed to set field '", fieldName, "' with signature '", signature_of<T>().data(), "."));
    });
    detail::set_field<T>(oid_, classId(), &fid, fieldName, std::forward<T>(v));
    return true;
}
template<class CTag>
template<typename T>
T JObject<CTag>::getStatic(const char* fieldName) {
    jfieldID fid = nullptr;
    return detail::get_static_field<T>(classId(), &fid, fieldName);
}
template<class CTag>
template<typename T>
bool JObject<CTag>::setStatic(const char* fieldName, T&& v) {
    jfieldID fid = nullptr;
    detail::set_static_field<T>(classId(), &fid, fieldName, std::forward<T>(v));
    return true;
}

template<class CTag>
template<typename F, class MayBeFTag, bool isStaticField>
F JObject<CTag>::Field<F, MayBeFTag, isStaticField>::get() const
{
    auto checker = detail::call_on_exit([]{
        detail::handle_exception();
    });
    if constexpr (isStaticField)
        return detail::get_static_field<F>(getEnv(), cid_, id());
    else
        return detail::get_field<F>(getEnv(), oid_, id());
}

template<class CTag>
template<typename F, class MayBeFTag, bool isStaticField>
void JObject<CTag>::Field<F, MayBeFTag, isStaticField>::set(F&& v)
{
    auto checker = detail::call_on_exit([]{
        detail::handle_exception();
    });
    if constexpr (isStaticField)
        detail::set_static_field<F>(getEnv(), cid_, id(), std::forward<F>(v));
    else
        detail::set_field<F>(getEnv(), oid_, id(), std::forward<F>(v));
}

template<class CTag>
template<typename F, class MayBeFTag, bool isStaticField>
jfieldID JObject<CTag>::Field<F, MayBeFTag, isStaticField>::cachedId(jclass cid)
{
    static jfieldID fid = nullptr;
    if (!fid) {
        if constexpr (isStaticField)
            fid = detail::get_static_field_id<F>(getEnv(), cid, MayBeFTag::name());
        else
            fid = detail::get_field_id<F>(getEnv(), cid, MayBeFTag::name());
    }
    return fid;
}

template<class CTag>
template<typename F, class MayBeFTag, bool isStaticField>
JObject<CTag>::Field<F, MayBeFTag, isStaticField>::Field(jclass cid, jobject oid)
 : oid_(oid) {
    fid_ = cachedId(cid);
    if constexpr (isStaticField)
        cid_ = cid;
}

template<class CTag>
template<typename F, class MayBeFTag, bool isStaticField>
JObject<CTag>::Field<F, MayBeFTag, isStaticField>::Field(jclass cid, const char* name, jobject oid)
 : oid_(oid) {
    if constexpr (isStaticField) {
        fid_ = detail::get_static_field_id<F>(getEnv(), cid, name);
        cid_ = cid;
    } else {
        fid_ = detail::get_field_id<F>(getEnv(), cid, name);
    }
}


template<class CTag>
jclass JObject<CTag>::classId(JNIEnv* env) {
    static const jclass c = [&]{
        if (!env)
            env = getEnv();
        LocalRef cid(env->FindClass(className().data()), env);
        return static_cast<jclass>(env->NewGlobalRef(cid));
    }();
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
            if constexpr (is_arithmetic_v<T>) {
                set_jarray(env, arr, 0, N, c0);
            } else { // string etc. must convert to jobject
                for (size_t i = 0; i < N; ++i)
                    set_jarray(env, arr, i, 1, *((&c0)+i));
            }
        }
        return arr;
    }
    template<class CTag>
    jvalue to_jvalue(const JObject<CTag> &obj, JNIEnv* env) {
        return to_jvalue(jobject(obj), env);
    }
} // namespace detail
} //namespace jmi
