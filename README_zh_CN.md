# JMI
**_JNI Modern Interface in C++_**

[English](README.md)

[一些使用 JMI 写的 Java 类](https://github.com/wang-bin/AND.git)

[![Build status github](https://github.com/wang-bin/JMI/workflows/Build/badge.svg)](https://github.com/wang-bin/JMI/actions)

## 特性

- 编译期 JNI 签名常量
- 支持 Java 方法入参与出参（可变数组/缓冲区用 `std::ref`）
- 按类缓存 `jclass`，按方法/字段缓存 `jmethodID` / `jfieldID`
- Java 静态方法/字段提供对应的 `callStatic` / `staticField` 接口
- `JObject` 持有 **global** ref；短生命周期可用 `LocalRef` RAII（一致使用可避免局部引用泄漏）
- 在 `javaVM(vm)` 初始化后支持任意线程调用 `getEnv()`，按需 attach/detach
- 可作为参数 / 返回值 / field 的类型：JNI 基本类型（`jint`、`jlong` 等，不是裸 `int`/`long`）、`JObject`、C/C++ string 及其数组
- 常用辅助：`to_string(jstring, JNIEnv*)`、`from_string(std::string, JNIEnv*)`、`android::application()`
- 每次调用做异常检查/清理；实例方法之后可查 `error()`
- 缓存 ID 并开启 LTO 时，几乎没有额外的 C++ 包装开销

## 快速开始

在 `JNI_OnLoad` 中设置 VM：

```cpp
jmi::javaVM(vm);
```

请在任何 JMI API 之前调用，通常放在 `JNI_OnLoad` 中。

### 缓存方法

用方法名做模板参数（C++20 NTTP 或 `MethodTag`）的 `call<…>` / `callStatic<…>` 会缓存 `jmethodID`，只解析一次。热路径请优先使用。

**C++20** — `NamedClassTag`、`call<"name">`、`NamedMethodTag`、`NamedFieldTag`、`""_jmis`：

```cpp
using SurfaceTexture = jmi::NamedClassTag<"android/graphics/SurfaceTexture">;
jmi::JObject<SurfaceTexture> texture;
if (!texture.create(tex)) {
    // texture.error()
}
texture.call<"updateTexImage">();
auto t = texture.call<jlong, "getTimestamp">();

float mat4[16]; // 或 std::array / valarray
texture.call<"getTransformMatrix">(std::ref(mat4)); // std::ref = 出参 / 出入参

using Surface = jmi::NamedClassTag<"android/view/Surface">;
jmi::JObject<Surface> surface;
surface.create(texture);
```

**ClassTag + MethodTag** — 不用 C++20 字符串 NTTP，缓存效果相同：

```cpp
struct SurfaceTexture : jmi::ClassTag {
    static constexpr auto name() { return JMISTR("android/graphics/SurfaceTexture"); } // 或 JMISTR("android.graphics.SurfaceTexture")
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

### 无缓存的字符串方法名（热路径请避免）

`call("name", …)` / `callStatic("name", …)` **每次**都会 `GetMethodID` / `GetStaticMethodID`。热路径请用 `call<"name">` 或 `MethodTag`。

```cpp
texture.call("updateTexImage");
auto t = texture.call<jlong>("getTimestamp");
```

### 运行时开销

对于已缓存的方法和字段，JMI 只在首次使用时解析 ID。后续调用仍会执行 `getEnv()`、JNI 异常检查、参数/引用转换以及 JNI 调用本身。开启 LTO 后，C++ 包装层通常可以被内联，因此相比等价的手写 JNI，额外的包装开销几乎为零；JNI 操作和引用管理本身仍有正常成本。

## 线程与生命周期

- `JNIEnv*`、local `jobject` 引用和 `LocalRef` 都属于创建它们的线程，不能传给其他线程；跨线程应使用 `JObject` 这样的 global ref。
- `JObject` 持有 global ref，允许复制，但同一个 `JObject` 实例不是带同步的共享对象。建议每个线程使用自己的包装对象，或由外部同步访问。
- 非静态 `Field` 不持有额外的对象引用。使用它时，所属的 `JObject` 必须保持有效且不能被 `reset()`。
- JMI 会在线程退出时 detach 自己 attach 的 native 线程；由调用方 attach 的线程仍由调用方管理。

### 出参

需要 JNI 改写 C++ 数组/缓冲区时用 `std::ref`。参数若是 `JObject`（或其子类），一般不必 `std::ref`——句柄不变，可能只改字段：

```cpp
MediaCodec::BufferInfo bi;
bi.create();
codec.dequeueOutputBuffer(bi, timeout); // bi 为 MediaCodec::BufferInfo&
```

### 错误

`JObject::call`、`get` 和 `set` 在每次调用开始会清空 `error()`，若检测到 JNI 异常或失败再写入。错误属于该对象实例，并会被下一次实例调用覆盖，因此应及时检查。`create()` 也会通过返回值和 `error()` 报告失败。

### 引用

- `JObject` 存的是 **global** ref（`NewGlobalRef` / `DeleteGlobalRef`）。复制 `JObject` 会创建另一个 global ref；从 call 返回 `JObject` 比短命 local ref 更重。
- `jmi::LocalRef` 析构时删除 local ref，且必须在创建它的线程析构。
- `to_string(jstring)` 会删掉传入的 local ref；`from_string` / `android::application()` 返回当前线程的 local ref，需自行管理（或包进 `LocalRef`）。

## Field 接口

`FieldTag` / C++20 `NamedFieldTag<"…">` 会在函数内静态缓存 ID。`obj.field<T>("name")` 和 `staticField<T>("name")` 在构造 `Field` 对象时查找 ID，并由该对象保存。直接调用 `obj.get<T>("name")` / `set()` / `getStatic()` / `setStatic()` 时，每次调用都会查找字段 ID。

```cpp
// C++20
auto ifield = obj.field<jint, jmi::NamedFieldTag<"myIntFieldName">>();
jfieldID ifid = ifield; // 或 ifield.id()
ifield.set(1234);
jint ivalue = ifield; // 或 ifield.get()

struct MyStrFieldS : jmi::FieldTag { static const char* name() { return "myStaticStrFieldName"; } };
auto& sfield = JObject<MyClassTag>::staticField<std::string, MyStrFieldS>();
sfield.set("JMI static field test");
sfield = "assign";
std::string s = sfield;

auto plain = obj.field<jint>("myIntFieldName"); // 为此 Field 对象查找一次
```

## 给 Java 类写 C++ 包装

继承 `JObject<YourClass>`（CRTP）或 `JObject<YourClassTag>`，或把 `JObject` 当成员。每个方法通常几行即可。参见 [JMITest](test/JMITest.h) 与 [Project AND](https://github.com/wang-bin/AND.git)。

## 使用编译器生成的签名

`signature_of<T>()` / `signature_of(fn)` 可为支持的类型（不含裸 `jobject`，类在运行期才确定）、`reference_wrapper`、`void`，以及由上述类型构成的函数类型生成 JNI 签名。

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

## 为什么 `JObject` 是模板？

以便按 class tag 分别缓存各自的 `jclass` / method / field ID（各自静态存储）。

## 构建和测试

```sh
cmake -S . -B build -DBUILD_TESTS=ON -DCMAKE_CXX_STANDARD=20
cmake --build build
ctest --test-dir build --output-on-failure
```

`ctest` 适用于本机构建；Android 交叉编译应在设备或模拟器上测试。

## 编译器

需要 C++17 或更高。C++20 启用 `call<"name">`、`NamedClassTag` 等。

- g++ >= 7.0（除 8.0–8.3）
- clang >= 5.0
- msvc >= 19.14
- icc >= 18.0

## TODO

- modern C++ 类自动生成脚本

## MIT License
> Copyright (c) 2016-2026 WangBin
