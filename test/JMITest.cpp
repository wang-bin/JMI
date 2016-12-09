#include <jni.h>
#include <iostream>
#include "jmi.h"

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
	const jbyte cs[] = {'1', '2', '3'};
    std::array<jbyte, 3> ca({'a', 'b', 'c'});
    //jmi::object js = jmi::object::create(jmi::signature_of(std::string()), ca);//std::string("abc"));
    jmi::object js = jmi::object::create(jmi::signature_of(std::string()), "abc");//std::string("abc")); //jmi::signature_of(std::string()) is illegal on android FindClass
	//jmi::object js(jmi::from_string("abc"));
	if (!js.error().empty())
		cout << "error: " << js.error() << endl;
	cout << "string len: " << js.call<jint>("length") << js.error() << endl;
    cout << "js[2]: " << (char)js.call<jchar>("charAt", 2) << js.error() << std::endl;
	//jmethodID mid= env->GetMethodID(js.get_class(),"charAt", "(I)C");
	//std::cout << "[2]:" <<(char)env->CallCharMethod(js.instance(), mid, 2) << endl;
    cout << "valueOf: " << js.call_static<std::string>("valueOf", 123) << js.error() << std::endl;
    cout << "indexOf: " << js.call<jint>("indexOf", std::string("c"), 1) << js.error() << std::endl;

	jmi::object st = jmi::object::create("android/graphics/SurfaceTexture", 0);
    if (!st.error().empty())
        cout << st.error() << endl;
}
}