# JMI
**_JNI Modern Interface in C++_**

### Features

- Signature generation is done by compiler
- getEnv() at any thread without caring about when to detach
- Support both In & Out parameters for JNI methods

### Example:
- Setup java vm in `JNI_OnLoad`: `jmi::javaVM(vm);`
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
    std::array<float, 16> mat4; //or float mat4[16];
    st.call("getTransformMatrix", std::ref(mat4)); // use std::ref() if parameter will be modified by jni method
```

- Call method with a return type:
```
    jlong t = st.call<jlong>("getTimestamp");
```


### Known Issues

- If return type and first n arguments of call/call_static are the same, explicitly specifying return type and n arguments type is required

### TODO

#### MIT License
>Copyright (c) 2016 WangBin

