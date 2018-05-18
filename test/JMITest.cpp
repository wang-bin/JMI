//#include <valarray>
#include <jni.h>
#include <iostream>
#include "jmi.h"
#include "JMITest.h"

#define TEST(expr) do { \
		if (!(expr)) { \
			std::cerr << __LINE__ << " test error: " << #expr << std::endl; \
			exit(1); \
		} \
	} while(false)

using namespace std;
using namespace jmi;

void JMITestCached::setX(int v)
{
	constexpr const char* MethodName = __FUNCTION__;
	struct SetX : MethodTag { static const char* name() {return MethodName;}};
	call<SetX>(v);
}

int JMITestCached::getX() const
{
	constexpr const char* MethodName = __FUNCTION__;
	struct GetX : MethodTag { static const char* name() {return MethodName;}};
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

std::vector<std::string> JMITestCached::getStrArray() const
{
	constexpr const char* MethodName = __FUNCTION__;
	struct Get : MethodTag { static const char* name() {return MethodName;}};
	return call<std::vector<std::string>, Get>();
}

std::vector<std::string> JMITestCached::getStrArrayS()
{
	constexpr const char* MethodName = __FUNCTION__;
	struct Get : MethodTag { static const char* name() {return MethodName;}};
	return callStatic<std::vector<std::string>, Get>();
}

std::valarray<int> JMITestCached::getIntArray() const
{
	constexpr const char* MethodName = __FUNCTION__;
	struct Get : MethodTag { static const char* name() {return MethodName;}};
	return call<std::valarray<int>, Get>();
}

void JMITestCached::getIntArrayAsParam(int v[2]) const
{
	// now v is int*
	//int (&out)[2] = reinterpret_cast<int(&)[2]>(v);
	int out[2];
	constexpr const char* MethodName = __FUNCTION__;
	struct Get : MethodTag { static const char* name() {return MethodName;}};
	call<Get>(std::ref(out));
	v[0] = out[0];
	v[1] = out[1];
}

void JMITestCached::getIntArrayAsParam(std::array<int, 2>& v) const
{
	constexpr const char* MethodName = __FUNCTION__;
	struct Get : MethodTag { static const char* name() {return MethodName;}};
	call<Get>(std::ref(v));
}

JMITestCached JMITestCached::getSelf() const
{
	constexpr const char* MethodName = __FUNCTION__;
	struct Get : MethodTag { static const char* name() {return MethodName;}};
	return call<JMITestCached, Get>();
}

void JMITestCached::getSelfArray(array<JMITestCached,2> &v) const
{
	constexpr const char* MethodName = __FUNCTION__;
	struct Get : MethodTag { static const char* name() {return MethodName;}};
	return call<Get>(std::ref(v));
}


void JMITestUncached::setX(int v)
{
	obj.call(__FUNCTION__, v);
}

int JMITestUncached::getX() const
{
	return obj.call<int>(__FUNCTION__);
}

void JMITestUncached::setY(int v)
{
	JObject<JMITestClassTag>::callStatic(__FUNCTION__, v);
}

int JMITestUncached::getY()
{
	return JObject<JMITestClassTag>::callStatic<int>("getY");
}

void JMITestUncached::setStr(const string& v)
{
	obj.call("setStr", v);
}

std::string JMITestUncached::getStr() const
{
	return obj.call<std::string>("getStr");
}

std::vector<std::string> JMITestUncached::getStrArrayS()
{
	return JObject<JMITestClassTag>::callStatic<std::vector<std::string>>(__FUNCTION__);
}

std::vector<std::string> JMITestUncached::getStrArray() const
{
	return obj.call<std::vector<std::string>>(__FUNCTION__);
}

std::vector<int> JMITestUncached::getIntArray() const
{
	return obj.call<std::vector<int>>(__FUNCTION__);
}

void JMITestUncached::getIntArrayAsParam(int v[2]) const
{
	// now v is int*
	int out[2];
	obj.call(__FUNCTION__, std::ref(out));
	v[0] = out[0];
	v[1] = out[1];
}

void JMITestUncached::getIntArrayAsParam(std::array<int, 2>& v) const
{
	obj.call(__FUNCTION__, std::ref(v));
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
	TEST(jstr0.create(cxxa));
	auto jstr = std::move(jstr0);
	auto js2 = jstr;
	TEST(jstr.call<jint>("length") == 3);
	TEST(jstr.error().empty());
	TEST(jstr.signature() == "Ljava/lang/String;");
	jstr.reset();
	jstr.create("abcd");
	TEST(jstr.call<jint>("length") == 4);
	jchar ccc = jstr.call<jchar>("charAt", 2);
	TEST(ccc == 'c');
	TEST(ccc == 'c');
	TEST(jstr.error().empty());
	//jmethodID mid= env->GetMethodID(js.get_class(),"charAt", "(I)C");
	//std::cout << "[2]:" <<(char)env->CallCharMethod(js.instance(), mid, 2) << endl;
	string sss = jmi::JObject<JString>::callStatic<std::string>("valueOf", 123);
	TEST(sss == "123");
	int ic = jstr.call<jint>("indexOf", std::string("c"), 1);
	TEST(ic == 2);
	struct IndexOf : jmi::MethodTag { static const char* name() {return "indexOf";} };
	ic = jstr.call<jint,IndexOf>(std::string("c"), 1);
	TEST(ic == 2);
	TEST(jstr.error().empty());
	ic = jstr.call<jint,IndexOf>(std::string("c"), 1);
	TEST(ic == 2);
	TEST(jstr.error().empty());

    //jbyte ca[] = {'a', 'b', 'c', 'd'}; // why crash? why const crash?
	jbyte *ca = (jbyte*)"abcd";
    jstr.reset();
	TEST(!jstr.create(ca));
	
	struct JMITest : public jmi::ClassTag { static std::string name() {return "JMITest";}};
	jmi::JObject<JMITest> test;
	struct Y : public jmi::FieldTag { static const char* name() { return "y";}};
	auto y = test.getStatic<Y, jint>();
	TEST(y == 168);
	TEST(jmi::JObject<JMITest>::setStatic<Y>(1258));
	auto yyy = test.getStatic<Y, jint>();
	TEST(yyy == 1258);

	struct SStr : public jmi::FieldTag { static const char* name() { return "sstr";}};
	auto sstr = jmi::JObject<JMITest>::getStatic<SStr, std::string>();
	TEST(sstr == "static text");
	TEST(jmi::JObject<JMITest>::setStatic<SStr>(std::string(":D setting static string...")));
	sss = jmi::JObject<JMITest>::getStatic<SStr,std::string>();
	TEST(sss == ":D setting static string...");

	cout << ">>>>>>>>>>>>testing Cacheable StaticField APIs..." << endl;
	auto& fsstr = jmi::JObject<JMITest>::staticField<SStr, std::string>();
	TEST(fsstr.get() == ":D setting static string...");
	jmi::JObject<JMITest>::staticField<SStr, std::string>();
	fsstr = jmi::JObject<JMITest>::staticField<SStr, std::string>();
	fsstr.set("Cacheable StaticField sstr set");
	TEST(fsstr.get() == "Cacheable StaticField sstr set");
	fsstr = "Cacheable StaticField sstr =()";
	TEST(fsstr.get() == "Cacheable StaticField sstr =()");

	cout << ">>>>>>>>>>>>testing Uncacheable StaticField APIs..." << endl;
	auto ufsstr = jmi::JObject<JMITest>::staticField<std::string>("sstr");
	TEST(ufsstr.get() == fsstr.get());
	ufsstr = jmi::JObject<JMITest>::staticField<std::string>("sstr");
	ufsstr.set("Uncacheable StaticField sstr set");
	TEST(ufsstr.get() == "Uncacheable StaticField sstr set");
	//cout << "field JMITest.sstr from Uncacheable StaticField object after set(): " << ufsstr.get() << endl;
	ufsstr.set("Uncacheable StaticField sstr =()");
	TEST(ufsstr.get() == string("Uncacheable StaticField sstr =()"));
	//cout << "field JMITest.sstr from Uncacheable StaticField object after =(): " << ufsstr.get() << endl;

	cout << ">>>>>>>>>>>>testing Cacheable field APIs..." << endl;
	test.create();
	struct X : public jmi::FieldTag { static const char* name() { return "x";}};
	int x = test.get<X, jint>();
	TEST(x == 0);
	TEST(test.set<X>(3141));

	cout << ">>>>>>>>>>>>testing Unacheable field APIs..." << endl;
	auto str = test.get<std::string>("str");
	TEST(str == "text");
	TEST(test.set("str", std::string(":D setting string...")));
	TEST(test.get<std::string>("str") == ":D setting string...");

	cout << ">>>>>>>>>>>>testing Cacheable Field APIs..." << endl;
	struct Str : public jmi::FieldTag { static const char* name() { return "str";}};
	auto fstr = test.field<Str, std::string>();
	TEST((string)fstr == string(":D setting string..."));
	fstr = test.field<Str, std::string>();
	test.field<Str, std::string>();
	fstr.set("Cacheable Field str set");
	std::string v_fstr = fstr;
	jfieldID id_fstr = fstr;
	TEST(fstr.get() == "Cacheable Field str set");
	fstr = "Cacheable Field str =()";
	TEST(fstr.get() == "Cacheable Field str =()");

	cout << ">>>>>>>>>>>>testing Uncacheable Field APIs..." << endl;
	auto ufstr = test.field<std::string>("str");
	TEST(ufstr.get() == fstr.get());
	ufstr = test.field<std::string>("str");
	TEST(ufstr.get() == fstr.get());
	ufstr.set("Uncacheable Field str =()");
	TEST(ufstr.get() == "Uncacheable Field str =()");

	auto ufself = test.field<JObject<JMITest>>("self");
	JObject<JMITest> ufselfv = ufself;
	ufstr = ufselfv.field<std::string>("str");
	TEST(ufstr.get() == fstr.get());

	cout << ">>>>>>>>>>>>testing JMITestCached APIs..." << endl;
	JMITestCached jtc;
	JMITestCached::setY(604);
	TEST(JMITestCached::getY() == 604);
	TEST(jtc.create());
	jtc.setX(2017);
	TEST(jtc.getX() == 2017);
	jtc.setStr("why");
	TEST(jtc.getStr() == "why");
	int a0[2]{};
	jtc.getIntArrayAsParam(a0);
	TEST(a0[0] == 1);
	TEST(a0[1] == 2017);
	std::array<int, 2> a1;
	jtc.getIntArrayAsParam(a1);
	TEST(a1[0] == 1);
	TEST(a1[1] == 2017);
	std::valarray<int> av0 = jtc.getIntArray();
	TEST(av0[0] == 1);
	TEST(av0[1] == 2017);
	auto sa = jtc.getStrArray();
	TEST(sa[0] == jtc.getStr());
	TEST(sa[1] == fsstr.get());
	sa = jtc.getStrArrayS();
	TEST(sa[0] == fsstr.get());

	array<std::string,1> outs;
	JMITestCached::getSStr(outs);
	TEST(outs[0] == " output  String[]");
	JMITestCached jtc_copy = jtc.getSelf();
	TEST(jtc_copy.getX() == 2017);
	jtc.setX(1231);
	TEST(jtc_copy.getX() == 1231);

	array<JMITestCached,2> selfs;
	jtc.getSelfArray(selfs);
	TEST(selfs[0].getX() == 1231);
	TEST(selfs[1].getX() == 0);

	auto ufself2 = test.field<JMITestCached>("self");
	JMITestCached ufselfv2 = ufself2;
	TEST(ufselfv2.getX() == 3141);

	cout << ">>>>>>>>>>>>testing JMITestUncached APIs..." << endl;
	JMITestUncached jtuc;
	JMITestUncached::setY(604);
	TEST(JMITestUncached::getY() == 604);
	TEST(jtuc.create());
	jtuc.setX(2017);
	TEST(jtuc.getX() == 2017);
	jtuc.setStr("why");
	TEST(jtuc.getStr() == "why");
	jtuc.getIntArrayAsParam(a0);
	TEST(a0[0] == 1);
	TEST(a0[1] == 2017);
	jtuc.getIntArrayAsParam(a1);
	TEST(a1[0] == 1);
	TEST(a1[1] == 2017);
	std::vector<int> av1 = jtuc.getIntArray();
	TEST(av1[0] == 1);
	TEST(av1[1] == 2017);
	sa = jtuc.getStrArray();
	TEST(sa[0] == jtuc.getStr());
	TEST(sa[1] == fsstr.get());
	sa = jtuc.getStrArrayS();
	TEST(sa[0] == fsstr.get());
}
} // extern "C"
