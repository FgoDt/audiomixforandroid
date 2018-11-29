#include <jni.h>
#include <string>
extern "C"{
#include "AudioMix.h"
}


extern "C" JNIEXPORT jstring

JNICALL
Java_com_example_fftest_audiomix_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}

extern "C" JNIEXPORT jboolean

JNICALL
Java_com_example_fftest_audiomix_MainActivity_mixAudio(
       JNIEnv *env,
       jobject thzs,
       jstring file1Path,
       jlong file1Starttime,
       jlong file1Duration,
       jstring file2Path,
       jlong file2Starttime,
       jlong file2Duration,
       jstring mixPath,
       jlong totalDuration){

    int len = env->GetStringLength(file1Path);
    const char* jchar1 = env->GetStringUTFChars(file1Path,0);
    len = env->GetStringLength(file2Path);
    const char* jchar2 = env->GetStringUTFChars(file2Path,0);
    len = env->GetStringLength(mixPath);
    const char* jchar3= env->GetStringUTFChars(mixPath,0);



    int ret = audio_mix(jchar1,file1Starttime,file1Duration,
                        jchar2,file2Starttime,file2Duration,
                        jchar3,totalDuration);

    env->ReleaseStringUTFChars(file2Path,jchar3);
    env->ReleaseStringUTFChars(file2Path,jchar2);
    env->ReleaseStringUTFChars(file1Path,jchar1);

    if(ret>=0)
        return jboolean (true);
    else
        return jboolean (false);




}


