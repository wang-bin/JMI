//#include <valarray>
#include <jni.h>
#include <iostream>
#include "jmi.h"
#include <vector>
using namespace std;
extern "C" {

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
	std::cout << "JNI_OnLoad" << std::endl;
	JNIEnv* env = nullptr;
	if (vm->GetEnv((void**) &env, JNI_VERSION_1_4) != JNI_OK || !env) {
		std::cerr << "GetEnv for JNI_VERSION_1_4 failed" << std::endl;
		return -1;
	}
    jmi::javaVM(vm);
    return JNI_VERSION_1_4;
}

JNIEXPORT void Java_JMITest_nativeTest(JNIEnv *env , jobject thiz)
{
    cout << "JMI Test" << endl;
	jmi::object o;
	const jbyte cs[] = {'1', '2', '3'};
    std::array<jbyte, 3> cxxa{'a', 'b', 'c'};
	struct JString : jmi::ClassTag {
		static std::string name() { return jmi::signature_of(std::string());}
	};
	jmi::JObject<JString> jstr0;
	if (!jstr0.create(cxxa)) {
		cout << __LINE__<< " !!!!create error: " << jstr0.error() << endl;
	}
	auto jstr = std::move(jstr0);
	auto js2 = jstr;
	cout << "JString len: " << jstr.call<jint>("length") << jstr.error() << endl;
	cout << jstr.signature() << endl;
	jstr.reset();
	jstr.create("abcd");
	cout << "JString len: " << jstr.call<jint>("length") << jstr.error() << endl;
    cout << "JString[2]: " << (char)jstr.call<jchar>("charAt", 2) << jstr.error() << std::endl;
	cout << "JString[2]: " << (char)jstr.call<jchar>("charAt", 2) << jstr.error() << std::endl;
	//jmethodID mid= env->GetMethodID(js.get_class(),"charAt", "(I)C");
	//std::cout << "[2]:" <<(char)env->CallCharMethod(js.instance(), mid, 2) << endl;
    cout << "valueOf: " << jmi::JObject<JString>::callStatic<std::string>("valueOf", 123) << jstr.error() << std::endl;
    cout << "indexOf c: " << jstr.call<jint>("indexOf", std::string("c"), 1) << jstr.error() << std::endl;
	struct IndexOf : jmi::MethodTag { static const char* name() {return "indexOf";} };
    cout << "JString.indexOf c: " << jstr.call<jint,IndexOf>(std::string("c"), 1) << jstr.error() << std::endl;
    cout << "JString.indexOf c: " << jstr.call<jint,IndexOf>(std::string("c"), 1) << jstr.error() << std::endl;
    cout << "JString.indexOf c: " << jstr.call<jint,IndexOf>(std::string("c"), 1) << jstr.error() << std::endl;

    jmi::object array_j = jmi::object::create(jmi::signature_of(std::string()), cxxa);//std::string("abc"));
	cout << array_j.signature() << endl;
    //jbyte ca[] = {'a', 'b', 'c', 'd'}; // why crash? why const crash?
	jbyte *ca = (jbyte*)"abcd";
    jmi::object carray_j = jmi::object::create(jmi::signature_of(std::string()), ca);//std::string("abc"));
	cout << carray_j.signature() << endl;
    jmi::object js = jmi::object::create(jmi::signature_of(std::string()), "abcd");//std::string("abc")); //jmi::signature_of(std::string()) is illegal on android FindClass
	//jmi::object js(jmi::from_string("abc"));
	if (!js.error().empty())
		cout << "error: " << js.error() << endl;
	cout << "string len: " << js.call<jint>("length") << js.error() << endl;
    cout << "js[2]: " << (char)js.call<jchar>("charAt", 2) << js.error() << std::endl;
    cout << "js[2]: " << (char)js.call<jchar>("charAt", 2) << js.error() << std::endl;
	//jmethodID mid= env->GetMethodID(js.get_class(),"charAt", "(I)C");
	//std::cout << "[2]:" <<(char)env->CallCharMethod(js.instance(), mid, 2) << endl;
    cout << "valueOf: " << js.call_static<std::string>("valueOf", 123) << js.error() << std::endl;
    cout << "indexOf c: " << js.call<jint>("indexOf", std::string("c"), 1) << js.error() << std::endl;

	jmi::object st = jmi::object::create("android/graphics/SurfaceTexture", 0);

    if (!st.error().empty())
        cout << st.error() << endl;
		/*
	std::valarray<int> v(1);
	jmi::object vv = jmi::object::create("android/graphics/SurfaceTexture", v);
	std::valarray<int> v2(1);
	jmi::object vv2 = jmi::object::create("android/graphics/SurfaceTexture", std::ref(v2));*/
	std::array<int, 3> v3;
	jmi::object vv3 = jmi::object::create("android/graphics/SurfaceTexture", std::ref(v3));
	int v4[4];
	jmi::object vv4r = jmi::object::create("android/graphics/SurfaceTexture", std::ref(v4));
	jmi::object vv4 = jmi::object::create("android/graphics/SurfaceTexture", v4);
}
}
