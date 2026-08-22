# JMI
**_JNI Modern Interface in C++_**

[中文](README_zh_CN.md)

[Some Java Classes Written in JMI](https://github.com/wang-bin/AND.git)

[![Build status github](https://github.com/wang-bin/JMI/workflows/Build/badge.svg)](https://github.com/wang-bin/JMI/actions)

## Features

- Compile-time JNI signature constants
- In and out parameters for Java methods (`std::ref` for mutable arrays/buffers)
- Per-class `jclass`, per-method `jmethodID`, per-field `jfieldID` cache
- Static Java methods/fields have corresponding `callStatic` / `staticField` APIs
- `JObject` owns a **global** ref; `LocalRef` RAII for short-lived local refs (helps avoid leaks when used consistently)
- `getEnv()` from any thread after `javaVM(vm)` is initialized; attach/detach handled when needed
- Supported as parameter / return / field types: JNI primitives (`jint`, `jlong`, … — not plain `int`/`long`), `JObject`, C/C++ strings, and arrays of those
- Helpers: `to_string(jstring, JNIEnv*)`, `from_string(std::string, JNIEnv*)`, `android::application()`
- Exception check / clear on calls; inspect `error()` after instance methods
- Almost no additional C++ wrapper overhead for cached calls when LTO is enabled

## Quick start

Set the VM in `JNI_OnLoad`:

```cpp
jmi::javaVM(vm);
```

Call this before any JMI API, normally from `JNI_OnLoad`.

### Cached methods

`call<…>` / `callStatic<…>` with a method name (C++20 NTTP or `MethodTag`) cache `jmethodID` and resolve it once. Prefer this on hot paths.

**C++20** — `NamedClassTag`, `call<"name">`, `NamedMethodTag`, `NamedFieldTag`, `""_jmis`:

```cpp
using SurfaceTexture = jmi::NamedClassTag<"android/graphics/SurfaceTexture">;
jmi::JObject<SurfaceTexture> texture;
if (!texture.create(tex)) {
    // texture.error()
}
texture.call<"updateTexImage">();
auto t = texture.call<jlong, "getTimestamp">();

float mat4[16]; // or std::array / valarray
texture.call<"getTransformMatrix">(std::ref(mat4)); // std::ref = out / in-out

using Surface = jmi::NamedClassTag<"android/view/Surface">;
jmi::JObject<Surface> surface;
surface.create(texture);
```

**ClassTag + MethodTag** — same caching without C++20 string NTTP:

```cpp
struct SurfaceTexture : jmi::ClassTag {
    static constexpr auto name() { return JMISTR("android/graphics/SurfaceTexture"); } // or JMISTR("android.graphics.SurfaceTexture")
};
struct UpdateTexImage : jmi::MethodTag { static const char* name() { return "updateTexImage"; } };
struct GetTimestamp : jmi::MethodTag { static const char* name() { return "getTimestamp"; } };
struct GetTransformMatrix : jmi::MethodTag { static const char* name() { return "getTransformMatrix"; } };

jmi::JObject<SurfaceTexture> texture;
if (!texture.create(tex)) {
    // texture.error()
}
texture.call<UpdateTexImage>();
auto t = texture.call<jlong, GetTimestamp>();
texture.call<GetTransformMatrix>(std::ref(mat4));
```

### Uncached string names (avoid on hot paths)

`call("name", …)` / `callStatic("name", …)` call `GetMethodID` / `GetStaticMethodID` **every time**. Prefer `call<"name">` or `MethodTag`.

```cpp
texture.call("updateTexImage");
auto t = texture.call<jlong>("getTimestamp");
```

### Runtime overhead

For cached methods and fields, JMI resolves the ID once on first use. Subsequent calls still perform `getEnv()`, JNI exception checks, argument/reference conversions, and the JNI call itself. With LTO, the C++ wrapper layer can usually be inlined, so the additional wrapper overhead is almost zero compared with equivalent handwritten JNI. JNI operations and reference management still have their normal costs.

## Threading and lifetime

- `JNIEnv*`, local `jobject` references, and `LocalRef` are thread-local. Do not pass them to another thread; use a global reference such as a `JObject` instead.
- `JObject` owns a global reference and can be copied, but one `JObject` instance is not a synchronized shared object. Use one wrapper instance per thread or synchronize access externally.
- A non-static `Field` does not own an additional object reference. Keep its parent `JObject` alive and unchanged while using the field.
- JMI detaches native threads that it attached when they exit. Threads attached by the caller remain caller-managed.

### Out parameters

Use `std::ref` when JNI should modify a C++ array/buffer. For `JObject` (or subclass) arguments, `std::ref` is usually unnecessary — the handle stays the same; fields may change:

```cpp
MediaCodec::BufferInfo bi;
bi.create();
codec.dequeueOutputBuffer(bi, timeout); // bi is MediaCodec::BufferInfo&
```

### Errors

`JObject::call`, `get`, and `set` clear `error()` at the start of a call and set it if a JNI exception (or failure) is detected. The error belongs to that object instance and is overwritten by the next instance call, so check it promptly. `create()` also reports failure through its return value and `error()`.

### References

- `JObject` stores a **global** ref (`NewGlobalRef` / `DeleteGlobalRef`). Copying a `JObject` creates another global ref; returning one from a call is heavier than keeping a short-lived local ref.
- `jmi::LocalRef` deletes its local ref on destruction and must be destroyed on the creating thread.
- `to_string(jstring)` deletes the passed local ref; `from_string` / `android::application()` return local refs on the current thread that you must manage (or wrap in `LocalRef`).

## Field API

`FieldTag` / C++20 `NamedFieldTag<"…">` cache the ID in a function-local static. `obj.field<T>("name")` and `staticField<T>("name")` resolve the ID when the `Field` object is constructed and retain it in that object. The direct `obj.get<T>("name")` / `set()` / `getStatic()` / `setStatic()` overloads look up the field ID on each call.

```cpp
// C++20
auto ifield = obj.field<jint, jmi::NamedFieldTag<"myIntFieldName">>();
jfieldID ifid = ifield; // or ifield.id()
ifield.set(1234);
jint ivalue = ifield; // or ifield.get()

struct MyStrFieldS : jmi::FieldTag { static const char* name() { return "myStaticStrFieldName"; } };
auto& sfield = JObject<MyClassTag>::staticField<std::string, MyStrFieldS>();
sfield.set("JMI static field test");
sfield = "assign";
std::string s = sfield;

auto plain = obj.field<jint>("myIntFieldName"); // resolves once for this Field object
```

## Wrapping a Java class in C++

Inherit `JObject<YourClass>` (CRTP) or `JObject<YourClassTag>`, or store a `JObject` member. Each method is usually a few lines. See [JMITest](test/JMITest.h) and [Project AND](https://github.com/wang-bin/AND.git).

## Compiler-generated signatures

`signature_of<T>()` / `signature_of(fn)` build JNI signatures for supported types (not raw `jobject` — class is runtime-only), `reference_wrapper`, `void`, and function types over those.

```cpp
void native_test_impl(JNIEnv* env, jobject thiz, ...) {}

static const JNINativeMethod gMethods[] = {
    {"native_test", signature_of(native_test_impl).data(), native_test_impl},
};

#define DEFINE_METHOD(M) {#M, signature_of(M##_impl).data(), M##_impl}
static const JNINativeMethod gMethods2[] = {
    DEFINE_METHOD(native_test),
};
```

## Why is `JObject` a template?

So each class tag can cache its own `jclass` / method / field IDs as distinct statics.

## Build and test

```sh
cmake -S . -B build -DBUILD_TESTS=ON -DCMAKE_CXX_STANDARD=20
cmake --build build
ctest --test-dir build --output-on-failure
```

`ctest` applies to native builds; Android cross-builds should be tested on a device or emulator.

## Compilers

C++17 or later. C++20 enables `call<"name">`, `NamedClassTag`, etc.

- g++ >= 7.0 (except 8.0–8.3)
- clang >= 5.0
- msvc >= 19.14
- icc >= 18.0

## TODO

- Modern C++ class generator script

## MIT License
> Copyright (c) 2016-2026 WangBin
