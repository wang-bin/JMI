#include "jmi.h"
#include <iostream>
#include <type_traits>

using namespace std;

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
int main(int argc, char *argv[])
{
    cout << "jmi test" << endl;

    cout << jmi::signature_of(jmi::object()) << endl;
    cout << jmi::signature_of(1.2f) << endl;
    cout << jmi::signature_of(std::string()) << endl;
    std::vector<float> f;
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
    //std::unordered_map<float, string> m;
    //cout << jmi::signature_of(m);
}
