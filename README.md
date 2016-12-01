# JMI
**_JNI Modern Interface in C++_**

Writing type signatures in JNI is boring and mistakeable. They are disappeared in JMI.

### Features

- Signature generation is done by compiler
- getEnv() at any thread without caring about when to detach

### Example:
- Setup java vm: `jmi::javaVM(vm);`
- Create a SurfaceTexture: 
```
    jmi:object st = jmi::object::create("android/graphics/SurfaceTexture", tex);
```
- Check error:
```
    if (!st.error().empty()) {...}
```
- Create Surface from SurfaceTexture:
```
    jmi:object s = jmi::object::create("android.view.Surface", st);
```
- Call void method:
```
    st.call("updateTexImage");
```
- Call method with output parameters:
```
    std::array<float, 16> mat4;
    st.call("getTransformMatrix", std::ref(mat4)); // use std::ref() if parameter will be modified by jni method
```

- Call method with a return type:
```
    jlong t = st.call<jlong>("getTimestamp");
```


### Known Issues

### TODO


#### MIT License
>Copyright (c) 2016 WangBin

