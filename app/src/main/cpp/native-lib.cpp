#include <jni.h>
#include <string>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

extern "C"{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
}

#define MIX_CODEC_ERROR -1
#define MIX_CODEC_EOF    0
#define MIX_CODEC_WRITE  1
#define MIX_CODEC_READ   2


typedef struct mix_codec{
    AVCodec *codec;
    AVCodecContext *codecContext;
    AVFormatContext *fmt;
    AVCodecParameters *par;
    AVStream *stream;
    AVFrame *iFrame;
    int bestAudioIndex;
    int status; // -1 for error 0 for EOF 1 for need write 2 for  need read
    pthread_mutex_t  mutex;
    pthread_t  pid;
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

static void* mix_decoder_thread(void *ctx){

    MixCodec *codec = (MixCodec*)ctx;
    AVPacket *pkt;
    pkt = av_packet_alloc();
    codec->iFrame = av_frame_alloc();

    while (codec->status!=MIX_CODEC_EOF && codec->status != MIX_CODEC_ERROR){
        pthread_mutex_lock(&codec->mutex);
        if(codec->status == MIX_CODEC_WRITE){
            read_frame:
            int ret = av_read_frame(codec->fmt,pkt);
            if(ret<0){
                codec->status = MIX_CODEC_ERROR;
                pthread_mutex_unlock(&codec->mutex);
                return  NULL;
            }
            if(pkt->stream_index!= codec->bestAudioIndex){
                av_packet_unref(pkt);
                goto read_frame;
            }
            ret = avcodec_send_packet(codec->codecContext,pkt);
            check_code:
            if(ret == AVERROR(EAGAIN)){
                pthread_mutex_unlock(&codec->mutex);
                continue;
            } else if(ret == AVERROR_EOF){
                codec->status = MIX_CODEC_EOF;
            } else if(ret == 0){
                ret = avcodec_receive_frame(codec->codecContext,codec->iFrame);
                if(ret == 0){
                    codec->status = MIX_CODEC_READ;
                } else{
                    goto check_code;
                }
            } else{
                codec->status = MIX_CODEC_ERROR;
            }

            pthread_mutex_unlock(&codec->mutex);
        } else{
            pthread_mutex_unlock(&codec->mutex);
            usleep(10000);
        }
    }


}

static void* mix_encoder_thread(void *ctx){
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

    mixCtx->file2Codec = mix_codec_alloc();
    if(mixCtx->file2Codec == NULL){
        return false;
    }

    int ret = open_file(mixCtx->file1Codec,mixCtx->file1);
    if(ret == false){
        return  -1;
    }
    ret = open_file(mixCtx->file2Codec,mixCtx->file2);
    if(ret == false){
        return  -1;
    }

    if(mixCtx->file1Codec->par->format != mixCtx->file2Codec->par->format ||
       mixCtx->file1Codec->par->sample_rate != mixCtx->file2Codec->par->sample_rate){
        //fix me use swr
        return  -1;
    }


    mixCtx->mixpostion = 0;
    if((mixCtx->totalDuration != -1 && mixCtx->totalDuration<=0)||
       (mixCtx->file1Duration != -1 && mixCtx->file1Duration<=0)||
       (mixCtx->file2Duration != -1 && mixCtx->file2Duration<=0)
            ) {
        //duration error
        return  -1;
    }
    if((mixCtx->file1Stime<0 || mixCtx->file2Stime<0)){
        //start time error
        return  -1;
    }


    pthread_mutex_init(&mixCtx->file1Codec->mutex,NULL);
    pthread_mutex_init(&mixCtx->file2Codec->mutex,NULL);

    pthread_create(&mixCtx->file1Codec->pid,NULL,mix_decoder_thread,mixCtx->file1Codec);
    pthread_create(&mixCtx->file2Codec->pid,NULL,mix_decoder_thread,mixCtx->file2Codec);

    mixCtx->file1Codec->status = MIX_CODEC_WRITE;
    mixCtx->file2Codec->status = MIX_CODEC_WRITE;

    long oneFrameTime = mixCtx->file1Codec->par->frame_size/mixCtx->file1Codec->par->sample_rate;

    long mixtime = 0;
    long file1MixDuration = 0;
    long file2MixDuration = 0;
    while(mixCtx->totalDuration == -1 || mixCtx->totalDuration<mixtime){
        if(mixtime >= mixCtx->file1Stime &&
          (file1MixDuration < mixCtx->file1Duration || mixCtx->file1Duration == -1)){
            pthread_mutex_lock(&mixCtx->file1Codec->mutex);
            if(mixCtx->file1Codec->status == MIX_CODEC_READ){
                mixCtx->file1Codec->status = MIX_CODEC_WRITE;
            }
            pthread_mutex_unlock(&mixCtx->file1Codec->mutex);
            file1MixDuration+=oneFrameTime;
        } else{
            pthread_mutex_lock(&mixCtx->file1Codec->mutex);
            mixCtx->file1Codec->status = MIX_CODEC_EOF;
            pthread_mutex_unlock(&mixCtx->file1Codec->mutex);
        }

        if(mixtime >= mixCtx->file2Stime &&
           (file2MixDuration < mixCtx->file2Duration || mixCtx->file2Duration == -1)){
            pthread_mutex_lock(&mixCtx->file2Codec->mutex);
            if(mixCtx->file2Codec->status == MIX_CODEC_READ){
                mixCtx->file2Codec->status = MIX_CODEC_WRITE;
            }
            pthread_mutex_unlock(&mixCtx->file2Codec->mutex);
            file2MixDuration+=oneFrameTime;
        } else{
            pthread_mutex_lock(&mixCtx->file2Codec->mutex);
            mixCtx->file2Codec->status = MIX_CODEC_EOF;
            pthread_mutex_unlock(&mixCtx->file2Codec->mutex);
        }
        if(mixCtx->file1Codec->status < MIX_CODEC_WRITE &&
                mixCtx->file2Codec->status < MIX_CODEC_WRITE &&
                mixCtx->totalDuration == -1){
            // done mix
            break;
        }
        mixtime += oneFrameTime;
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


