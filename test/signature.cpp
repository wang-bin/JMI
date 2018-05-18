#include <valarray>
#include "jmi.h"
#include <iostream>
#include <type_traits>
#include <array>
#include <vector>
using namespace std;
using namespace jmi;

jint JNI_OnLoad(JavaVM* vm, void*)
{
    freopen("/sdcard/log.txt", "w", stdout);
    freopen("/sdcard/loge.txt", "w", stderr);
    std::cout << "JNI_OnLoad" << std::endl;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**) &env, JNI_VERSION_1_4) != JNI_OK || !env) {
        std::cerr << "GetEnv for JNI_VERSION_1_4 failed" << std::endl;
        return -1;
    }
    jmi::javaVM(vm);
    return JNI_VERSION_1_4;
}

int write(jfloatArray, jint, jint) {return 0;}

void test1(int, const char* const, vector<jboolean>) {}
int test2() {return 0;}
std::string test3() {return string();}

int main(int argc, char *argv[])
{
    cout << "jmi test" << endl;

    //cout << jmi::signature<decltype(&write)>::value << std::endl;
    cout << jmi::signature_of(write) << std::endl;
    cout << jmi::signature_of(1.2f) << endl;
    cout << jmi::signature_of(std::string()) << endl;
    std::valarray<float> f;
    cout << jmi::signature_of(&f) << endl;
    cout << jmi::signature_of(f) << endl;
    std::vector<std::string> s;
    cout << jmi::signature_of(std::ref(s)) << endl;
    //std::vector<std::reference_wrapper<int>> v;
    //cout << jmi::signature_of(v);
    std::array<int, 4> a;
    cout << jmi::signature_of(a) << endl;
    float mat4[16];
    cout << jmi::signature_of(mat4) << endl;
    cout << "ref(mat4): " << jmi::signature_of(std::ref(mat4)) << endl;
    //cout << "signature_of_args: " << jmi::signature_of_args<jint, jbyte, jlong>::value << endl;
    //std::unordered_map<float, string> m;
    //cout << jmi::signature_of(m);
    cout << "test1: " << signature_of(test1) << endl;
    cout << "test2: " << signature_of(test2) << endl;
    cout << "test3: " << signature_of(test3) << endl;
}
