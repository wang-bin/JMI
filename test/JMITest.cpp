//#include <valarray>
#include <jni.h>
#include <iostream>
#include "jmi.h"
#include "JMITest.h"

using namespace std;
using namespace jmi;

void JMITestCached::setX(int v)
{
	struct SetX : MethodTag { static const char* name() {return "setX";}};
	call<SetX>(v);
}

int JMITestCached::getX() const
{
	struct GetX : MethodTag { static const char* name() {return "getX";}};
	return call<int, GetX>();
}

void JMITestCached::setY(int v)
{
	struct Set : MethodTag { static const char* name() {return "setY";}};
	callStatic<Set>(v);
}

int JMITestCached::getY()
{
	struct Get : MethodTag { static const char* name() {return "getY";}};
	return callStatic<int, Get>();
}

void JMITestCached::setStr(const char* v)
{
	struct Set : MethodTag { static const char* name() {return "setStr";}};
	call<Set>(v);
}

std::string JMITestCached::getStr() const
{
	struct Get : MethodTag { static const char* name() {return "getStr";}};
	return call<std::string, Get>();
}

void JMITestCached::getSStr(std::array<std::string,1>& v)
{
	struct Get : MethodTag { static const char* name() {return "getSStr";}};
	return callStatic<Get>(std::ref(v));
}

void JMITestCached::getIntArray(int v[2]) const
{
	// now v is int*
	//int (&out)[2] = reinterpret_cast<int(&)[2]>(v);
	int out[2];
	struct Get : MethodTag { static const char* name() {return "getIntArray";}};
	call<Get>(std::ref(out));
	v[0] = out[0];
	v[1] = out[1];
}

void JMITestCached::getIntArray(std::array<int, 2>& v) const
{
	struct Get : MethodTag { static const char* name() {return "getIntArray";}};
	call<Get>(std::ref(v));
}

JMITestCached JMITestCached::getSelf() const
{
	struct Get : MethodTag { static const char* name() {return "getSelf";}};
	return call<JMITestCached, Get>();
}

void JMITestCached::getSelfArray(array<JMITestCached,2> &v) const
{
	struct Get : MethodTag { static const char* name() {return "getSelfArray";}};
	return call<Get>(std::ref(v));
}


void JMITestUncached::setX(int v)
{
	call("setX", v);
}

int JMITestUncached::getX() const
{
	return call<int>("getX");
}

void JMITestUncached::setY(int v)
{
	callStatic("setY", v);
}

int JMITestUncached::getY()
{
	return callStatic<int>("getY");
}

void JMITestUncached::setStr(const string& v)
{
	call("setStr", v);
}

std::string JMITestUncached::getStr() const
{
	return call<std::string>("getStr");
}

void JMITestUncached::getIntArray(int v[2]) const
{
	// now v is int*
	int out[2];
	call("getIntArray", std::ref(out));
	v[0] = out[0];
	v[1] = out[1];
}

void JMITestUncached::getIntArray(std::array<int, 2>& v) const
{
	call("getIntArray", std::ref(v));
}

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
	if (!jmi::JObject<JMITest>::setStatic<Y>(1258)) {
		cout << "static field JMITest.y set error" << endl;
	}
	cout << "static field JMITest.y after set: " << test.getStatic<Y, jint>() << endl;

	struct SStr : public jmi::FieldTag { static const char* name() { return "sstr";}};
	auto sstr = jmi::JObject<JMITest>::getStatic<SStr, std::string>();
	cout << "static field JMITest::sstr initial value: " << sstr << endl;
	if (!jmi::JObject<JMITest>::setStatic<SStr>(std::string(":D setting static string..."))) {
		cout << "static field JMITest.sstr set error" << endl;
	}
	cout << "static field JMITest.sstr after set: " << jmi::JObject<JMITest>::getStatic<SStr, std::string>() << endl;

	cout << ">>>>>>>>>>>>testing Cacheable StaticField APIs..." << endl;
	auto& fsstr = jmi::JObject<JMITest>::staticField<SStr, std::string>();
	cout << "field JMITest.sstr from cacheable StaticField object: " << fsstr.get() << endl;
	jmi::JObject<JMITest>::staticField<SStr, std::string>();
	fsstr = jmi::JObject<JMITest>::staticField<SStr, std::string>();
	fsstr.set("Cacheable StaticField sstr set");
	cout << "field JMITest.sstr from Cacheable StaticField object after set(): " << fsstr.get() << endl;
	fsstr.set("Cacheable StaticField sstr =()");
	cout << "field JMITest.sstr from Cacheable StaticField object after =(): " << fsstr.get() << endl;

	cout << ">>>>>>>>>>>>testing Uncacheable StaticField APIs..." << endl;
	auto ufsstr = jmi::JObject<JMITest>::staticField<std::string>("sstr");
	cout << "field JMITest.sstr from uncacheable StaticField object: " << ufsstr.get() << endl;
	ufsstr = jmi::JObject<JMITest>::staticField<std::string>("sstr");
	ufsstr.set("Uncacheable StaticField sstr set");
	cout << "field JMITest.sstr from Uncacheable StaticField object after set(): " << ufsstr.get() << endl;
	ufsstr.set("Uncacheable StaticField sstr =()");
	cout << "field JMITest.sstr from Uncacheable StaticField object after =(): " << ufsstr.get() << endl;

	cout << ">>>>>>>>>>>>testing Cacheable field APIs..." << endl;
	test.create();
	struct X : public jmi::FieldTag { static const char* name() { return "x";}};
	int x = test.get<X, jint>();
	cout << "field JMITest.x initial value: " << x << endl;
	if (!test.set<X>(3141)) {
		cout << "field JMITest.x set error" << endl;
	}
	cout << "field JMITest.x after set: " << test.get<X, jint>() << endl;

	cout << ">>>>>>>>>>>>testing Unacheable field APIs..." << endl;
	auto str = test.get<std::string>("str");
	cout << "field JMITest::str initial value: " << str << endl;
	if (!test.set("str", std::string(":D setting string..."))) {
		cout << "field JMITest.str set error" << endl;
	}
	cout << "field JMITest.str after set: " << test.get<std::string>("str") << endl;

	cout << ">>>>>>>>>>>>testing Cacheable Field APIs..." << endl;
	struct Str : public jmi::FieldTag { static const char* name() { return "str";}};
	auto fstr = test.field<Str, std::string>();
	cout << "field JMITest.str from cacheable Field object: " << fstr.get() << endl;
	fstr = test.field<Str, std::string>();
	test.field<Str, std::string>();
	fstr.set("Cacheable Field str set");
	std::string v_fstr = fstr;
	jfieldID id_fstr = fstr;
	cout << "field JMITest.str from Cacheable Field object after set(): " << fstr.get() << endl;
	fstr.set("Cacheable Field str =()");
	cout << "field JMITest.str from Cacheable Field object after =(): " << fstr.get() << endl;

	cout << ">>>>>>>>>>>>testing Uncacheable Field APIs..." << endl;
	auto ufstr = test.field<std::string>("str");
	cout << "field JMITest.str from uncacheable Field object: " << ufstr.get() << endl;
	ufstr = test.field<std::string>("str");
	ufstr.set("Uncacheable Field str set");
	cout << "field JMITest.str from Uncacheable Field object after set(): " << ufstr.get() << endl;
	ufstr.set("Uncacheable Field str =()");
	cout << "field JMITest.str from Uncacheable Field object after =(): " << ufstr.get() << endl;

	auto ufself = test.field<JObject<JMITest>>("self");
	JObject<JMITest> ufselfv = ufself;
	ufstr = ufselfv.field<std::string>("str");
	cout << "field JMITest.self.str: " << ufstr.get() << endl;

	cout << ">>>>>>>>>>>>testing JMITestCached APIs..." << endl;
	JMITestCached jtc;
	JMITestCached::setY(604);
	cout << "JMITestCached::getY: " << JMITestCached::getY() << endl;
	if (!jtc.create())
		cerr << "JMITestCached.create() error" << endl;
	jtc.setX(2017);
	cout << "JMITestCached.getX: " << jtc.getX() << endl;
	jtc.setStr("why");
	cout << "JMITestCached.getStr: " << jtc.getStr() << endl;
	int a0[2]{};
	jtc.getIntArray(a0);
	cout << "JMITestCached.getIntArray(int[2]): [" << a0[0] << ", " << a0[1] << "]" << endl;
	std::array<int, 2> a1;
	jtc.getIntArray(a1);
	cout << "JMITestCached.getIntArray(std::array<int, 2>&): [" << a1[0] << ", " << a1[1] << "]" << endl;
	array<std::string,1> outs;
	JMITestCached::getSStr(outs);
	cout << "JMITestCached.getSStr(std::string&): " << outs[0] << endl;
	JMITestCached jtc_copy = jtc.getSelf();
	cout << "JMITestCached.getSelf().getX(): " << jtc_copy.getX() << endl;
	jtc.setX(1231);
	cout << "JMITestCached.getSelf().getX() after JMITestCached.setX(1231): " << jtc_copy.getX() << endl;

	array<JMITestCached,2> selfs;
	jtc.getSelfArray(selfs);
	cout << "JMITestCached.getSelfArray()[0].getX(): " << selfs[0].getX() << endl;
	cout << "JMITestCached.getSelfArray()[1].getX(): " << selfs[1].getX() << endl;

	auto ufself2 = test.field<JMITestCached>("self");
	JMITestCached ufselfv2 = ufself2;
	cout << "field JMITestCached.self.getX(): " << ufselfv2.getX() << endl;

	cout << ">>>>>>>>>>>>testing JMITestUncached APIs..." << endl;
	JMITestUncached jtuc;
	JMITestUncached::setY(604);
	cout << "JMITestUncached::getY: " << JMITestUncached::getY() << endl;
	if (!jtuc.create())
		cerr << "JMITestUncached.create() error" << endl;
	jtuc.setX(2017);
	cout << "JMITestUncached.getX: " << jtuc.getX() << endl;
	jtuc.setStr("why");
	cout << "JMITestUncached.getStr: " << jtuc.getStr() << endl;
	jtuc.getIntArray(a0);
	cout << "JMITestUncached.getIntArray(int[2]): [" << a0[0] << ", " << a0[1] << "]" << endl;
	jtuc.getIntArray(a1);
	cout << "JMITestUncached.getIntArray(std::array<int, 2>&): [" << a1[0] << ", " << a1[1] << "]" << endl;
}
} // extern "C"
