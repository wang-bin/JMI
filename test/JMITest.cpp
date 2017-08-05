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

    //jbyte ca[] = {'a', 'b', 'c', 'd'}; // why crash? why const crash?
	jbyte *ca = (jbyte*)"abcd";
    jstr.reset();
	jstr.create(ca);

	struct JMITest : public jmi::ClassTag { static std::string name() {return "JMITest";}};
	jmi::JObject<JMITest> test;
	struct Y : public jmi::FieldTag { static const char* name() { return "y";}};
	auto y = test.getStatic<Y, jint>();
	cout << "static field JMITest::y initial value: " << y << endl;
	if (!test.setStatic<Y>(1258)) {
		cout << "static field JMITest.y set error" << endl;
	}
	cout << "static field JMITest.y after set: " << test.getStatic<Y, jint>() << endl;

	struct SStr : public jmi::FieldTag { static const char* name() { return "sstr";}};
	auto sstr = test.getStatic<SStr, std::string>();
	cout << "static field JMITest::sstr initial value: " << sstr << endl;
	if (!test.setStatic<SStr>(std::string(":D setting static string..."))) {
		cout << "static field JMITest.sstr set error" << endl;
	}
	cout << "static field JMITest.sstr after set: " << test.getStatic<SStr, std::string>() << endl;

	test.create();
	struct X : public jmi::FieldTag { static const char* name() { return "x";}};
	int x = test.get<X, jint>();
	cout << "field JMITest.x initial value: " << x << endl;
	if (!test.set<X>(3141)) {
		cout << "field JMITest.x set error" << endl;
	}
	cout << "field JMITest.x after set: " << test.get<X, jint>() << endl;

	auto str = test.get<std::string>("str");
	cout << "field JMITest::str initial value: " << str << endl;
	if (!test.set("str", std::string(":D setting string..."))) {
		cout << "field JMITest.str set error" << endl;
	}
	cout << "field JMITest.str after set: " << test.get<std::string>("str") << endl;
}
}
