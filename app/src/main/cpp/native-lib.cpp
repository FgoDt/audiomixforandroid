#include <jni.h>
#include <string>
#include <stdlib.h>
#include <string.h>

extern "C"{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
}

typedef struct mix_codec{
    AVCodec *codec;
    AVCodecContext *codecContext;
    AVFormatContext *fmt;
    AVCodecParameters *par;
    AVStream *stream;
    int bestAudioIndex;
}MixCodec;

typedef struct mix_ctx{
    char *file1;
    char *file2;
    char *mixfile;
    long file1Stime;
    long file2Stime;
    long file1Duration;
    long file2Duration;
    long totalDuration;
    long mixpostion; //ms
    MixCodec *file1Codec;
    MixCodec *file2Codec;
    MixCodec *mixCodec;
}MixCtx;

MixCtx* mixCtx;

MixCtx* mix_alloc(){
    mixCtx = (MixCtx*)malloc(sizeof(MixCtx));
    if(mixCtx == NULL){
        return mixCtx;
    }
    memset(mixCtx,0, sizeof(MixCtx));
    return mixCtx;
}

void close_mix(){
    if(mixCtx == NULL)
        return;
    if(!mixCtx->file1){
        free(mixCtx->file1);
        mixCtx->file1 = NULL;
    }
    if(!mixCtx->file2){
        free(mixCtx->file2);
        mixCtx->file2 = NULL;
    }

    free(mixCtx);

}

MixCodec *mix_codec_alloc(){
    MixCodec *ctx = (MixCodec*)malloc(sizeof(MixCodec));
    return ctx;
}

void mix_codec_close(MixCodec *codec){
    free(codec);
}


int open_file(MixCodec *codec,char *url){
    if(codec == NULL || url == NULL){
        return false;
    }
    codec->fmt = avformat_alloc_context();
    int ret = avformat_open_input(&codec->fmt,url,NULL,NULL);
    if(ret<0){
        return ret;
    }
    ret = avformat_find_stream_info(codec->fmt,NULL);
    codec->bestAudioIndex = -1;
    codec->stream = NULL;
    codec->bestAudioIndex = av_find_best_stream(codec->fmt,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);

    if(codec->bestAudioIndex>=0){
        codec->stream = codec->fmt->streams[codec->bestAudioIndex];
    }
    if(codec->stream==NULL){
        return false;
    }
    codec->par = codec->stream->codecpar;
    codec->codec = NULL;
    codec->codec = avcodec_find_decoder(codec->par->codec_id);

    if(!codec->codec){
        return false;
    }

    codec->codecContext = avcodec_alloc_context3(codec->codec);
    ret = avcodec_parameters_to_context(codec->codecContext,codec->par);
    if(ret<0){
        return false;
    }

    ret = avcodec_open2(codec->codecContext,codec->codec,NULL);
    if(ret<0){
        return false;
    }

    return  true;

}



int do_mix(){
    if(mixCtx == NULL ||
       mixCtx->file1 == NULL ||
       mixCtx->file2 == NULL ||
       mixCtx->mixfile == NULL) {
        return false;
    }

    mixCtx->file1Codec = mix_codec_alloc();
    if(mixCtx->file1Codec == NULL){
        return false;
    }

    int ret = open_file(mixCtx->file1Codec,mixCtx->file1);
    if(ret == false){
        return  -1;
    }
    ret = open_file(mixCtx->file1Codec,mixCtx->file1);
    if(ret == false){
        return  -1;
    }

    if(mixCtx->file1Codec->par->format != mixCtx->file2Codec->par->format ||
       mixCtx->file1Codec->par->sample_rate != mixCtx->file2Codec->par->sample_rate){
        //fix me use swr
        return  -1;
    }






    return 0;

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

    mixCtx = mix_alloc();
    if(mixCtx == NULL){
        return jboolean(false);
    }
    int len = env->GetStringLength(file1Path);
    mixCtx->file1 = (char*)malloc(++len);
    memset(mixCtx->file1,'\0',len);
    const char* jchar1 = env->GetStringUTFChars(file1Path,0);
    memcpy(mixCtx->file1,jchar1,--len);
    env->ReleaseStringUTFChars(file1Path,jchar1);

    len = env->GetStringLength(file2Path);
    mixCtx->file2 = (char*)malloc(++len);
    memset(mixCtx->file2,'\0',len);
    const char* jchar2 = env->GetStringUTFChars(file2Path,0);
    memcpy(mixCtx->file2,jchar2,--len);
    env->ReleaseStringUTFChars(file2Path,jchar2);

    len = env->GetStringLength(mixPath);
    mixCtx->mixfile= (char*)malloc(++len);
    memset(mixCtx->mixfile,'\0',len);
    const char* jchar3= env->GetStringUTFChars(mixPath,0);
    memcpy(mixCtx->mixfile,jchar3,--len);
    env->ReleaseStringUTFChars(file2Path,jchar3);


    mixCtx->file1Duration = (long)file1Duration;
    mixCtx->file2Duration = (long)file2Duration;
    mixCtx->file1Stime = (long)file1Starttime;
    mixCtx->file2Stime = (long)file2Starttime;
    mixCtx->totalDuration = (long)totalDuration;

    do_mix();
    return jboolean (true);




}


