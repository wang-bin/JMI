# JMI
**_JNI Modern Interface in C++_**

[English](README.md)

[一些使用 JMI 写的 Java 类](https://github.com/wang-bin/AND.git)

[![Build status github](https://github.com/wang-bin/JMI/workflows/Build/badge.svg)](https://github.com/wang-bin/JMI/actions)

### 特性

- 编译期计算出签名常量(C++17)
- 支持 Java 方法输入、输出参数
- jclass、jmethodID、field 自动缓存
- C++ 和 Java 方法属性一致，如静态方法对应 C++ 静态成员函数
- 无需操心局部引用泄漏
- getEnv() 支持任意线程，无需关心 detach
- 编译器推导 java 类型及方法签名，并只生成一次
- 支持 JNI 原生的基本类型(jint、jlong之类而不是int、long)、jmi 的 JObject、C/C++ string 及上述类型的数组形式(vector, valarray, array等) 作为函数参数、返回值及 field 类型
- 提供了方便使用的常用函数: `to_string(jstring, JNIEnv*)`, `from_string(std::string, JNIEnv*)`, `android::application()`
- 简单易用，用户代码极简

### 例子:
- `JNI_OnLoad` 中设置 java vm: `jmi::javaVM(vm);`

- 创建 SurfaceTexture:
```
    // 在任意 jmi::JObject<SurfaceTexture> 可见范围内定义 SurfaceTexture tag 类
    struct SurfaceTexture : jmi::ClassTag { static constexpr auto name() {return JMISTRM("android/graphics/SurfaceTexture");}}; // 或 JMISTR("android.graphics.SurfaceTexture")
    ...
    GLuint tex = ...
    ...
    jmi::JObject<SurfaceTexture> texture;
    if (!texture.create(tex)) {
        // texture.error() ...
    }
```

- 从 SurfaceTexture 构造 Surface:
```
    struct Surface : jmi::ClassTag { static constexpr auto name() {return JMISTR("android.view.Surface");}}; // '.' or '/'
    ...
    jmi::JObject<Surface> surface;
    surface.create(texture);
```

- 调用 void 方法:
```
    texture.call("updateTexImage");
```

或

```
    texture.call<void>("updateTexImage");
```

- 调用含输出参数的方法:
```
    float mat4[16]; // or std::array<float, 16>, valarray<float>
    texture.call("getTransformMatrix", std::ref(mat4)); // use std::ref() if parameter should be modified by jni method
```

若出参类型是 `JObject<...>` 或其子类, 则可不使用`std::ref()`，因为对象不会改变，可能只是某些fields被方法修改了，如

```
    MediaCodec::BufferInfo bi;
    bi.create();
    codec.dequeueOutputBuffer(bi, timeout); // bi is of type MediaCodec::BufferInfo&
```

- 调用有返回值的方法:
```
    auto t = texture.call<jlong>("getTimestamp");
```

## jmethodID 缓存

`call/callStatic("methodName", ....)` 每次都会调用 `GetMethodID/GetStaticMethodID()`, 而 `call/callStatic<...MTag>(...)` 只会调用一次, 其中 `MTag` 是 `jmi:MethodTag` 的子类，实现了 `static const char* name() { return "methodName";}`.

```
    // GetMethodID() 对于每个不同的方法只会被调用一次
    struct UpdateTexImage : jmi::MethodTag { static const char* name() {return "updateTexImage";}};
    struct GetTimestamp : jmi::MethodTag { static const char* name() {return "getTimestamp";}};
    struct GetTransformMatrix : jmi::MethodTag { static const char* name() {return "getTransformMatrix";}};
    ...
    texture.call<UpdateTexImage>(); // or texture.call<void,UpdateTexImage>();
    auto t = texture.call<jlong, GetTimestamp>();
    texture.call<GetTransformMatrix>(std::ref(mat4)); // use std::ref() if parameter should be modified by jni method
```

### Field 接口

Field 接口支持可缓存和无缓存 jfieldID

通过 FieldTag 使用可缓存 jfieldID

```
    JObject<MyClassTag> obj;
    ...
    struct MyIntField : FieldTag { static const char* name() {return "myIntFieldName";} };
    auto ifield = obj.field<jint, MyIntField>();
    jfieldID ifid = ifield; // 或 ifield.id()
    ifield.set(1234);
    jint ivalue = ifield; // 或 ifield.get();

    // 静态 field 也一样，除了使用的是静态方法 JObject::staticField
    struct MyStrFieldS : FieldTag { static const char* name() {return "myStaticStrFieldName";} };
    auto& ifields = JObject<MyClassTag>::staticField<std::string, MyIntFieldS>(); // it can be an ref
    jfieldID ifids = ifields; // 或 ifield.id()
    ifields.set("JMI static field test");
    ifields = "assign";
    std::string ivalues = ifields; // 或 ifield.get();
```

通过 field 名字使用无缓存 jfieldID

```
    auto ifield = obj.field<jint>("myIntFieldName");
    ...
```

### 给 Java 类写个 C++ 类

创建 JObject<YouClassTag> 或把其对象作为成员变量，或使用 CRTP JObject<YouClass>。 每个方法是想通常不超过3行代码，也可以使用一些宏使每个方法实现只需一行代码， 参见 [JMITest](test/JMITest.h) 及  [Project AND](https://github.com/wang-bin/AND.git)

### 使用编译器生成的签名

模版 `auto signature_of<T>()` 返回类型 T 的签名. T 可以是 JMI 支持的类型(除了jobject，因为其类型是运行时确定的）、reference_wrapper、void 及由以上类型作为返回类型和参数类型的函数类型


例子:

```
    void native_test_impl(JNIEnv *env , jobject thiz, ...) {}

    staitc const JNINativeMethod gMethods[] = {
        {"native_test", signature_of(native_test_impl).data(), native_test_impl},
        ...
    };
```

也许你发现了可以用宏来简化

```
    #define DEFINE_METHOD(M) {#M, signature_of(M##_impl).data(), M##_impl}
    staitc const JNINativeMethod gMethods[] = {
        DEFINE_METHOD(native_test),
        ...
    }
```


### 已知问题

- 如果返回类型与前 n 个参数类型是一样的，需要显示指定这些类型

### 为什么 JObject 是个模版?
- 为了支持 jclass、jmethodID、jfieldID 缓存

#### 编译器
需要c++14/17

- g++ >= 4.9.0(except 8.0~8.3)
- clang >= 3.5
- msvc>= 19.0
- icc >= 17.0

### TODO
- modern C++ 类自动生成脚本

#### MIT License
>Copyright (c) 2016-2021 WangBin
