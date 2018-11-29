//
// Created by fgodt on 18-11-28.
// Copyright (c) 2018 fgodt <fgodt@hotmail.com>
//

#include "AudioMix.h"
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>


#define MIX_CODEC_ERROR -1
#define MIX_CODEC_EOF    0
#define MIX_CODEC_WRITE  1
#define MIX_CODEC_READ   2

#define MIX_SAMPLE_RATE 48000
#define MIX_CHANNEL     1
#define MIX_RAW_BIT     2


typedef  struct SMixCodec{
    AVFormatContext     *fmt;
    AVCodecContext      *codecContext;
    AVCodec             *codec;
    struct SwrContext   *swr;
    AVStream            *aStream;
    AVFrame             *iFrame;
    AVPacket            *ipkt;
    AVCodecParameters   *par;
    unsigned char       *sAudioBuf;
    char                tag[32];
    int                 bestAudioIndex;
    int                 bestVideoIndex;

    int                 status; // MIX_CODEC_*
    //if status == -1 error
    //if status == 0 EOF
    //if status == 1 need decode frame
    //if status == 2 need read decode data

    int                 rwpos; // read write position
    int                 nb_swr_sample;

    pthread_mutex_t     mutex;
    pthread_t           pid;

}MixCodec;

typedef struct MixContext{
    char *file1;
    char *file2;
    char *mixfile;

    long file1Stime;
    long file2Stime;
    long file1Duration;
    long file2Duration;
    long totalDuration;
    long mixpostion;
    MixCodec *file1Codec;
    MixCodec *file2Codec;
    MixCodec *mixCodec;
}MixContext;


static void get_aac_hader(unsigned char *packet, int channel, int profile, int sampleRate,int len){

    int freqIdx = sampleRate;
    int chanCfg = channel;

    packet[0] =  0xFF;
    packet[1] =  0xF9;
    packet[2] =  (((profile - 1) << 6) + (freqIdx << 2) + (chanCfg >> 2));
    packet[3] =  (((chanCfg & 3) << 6) + (len >> 11));
    packet[4] =  ((len& 0x7FF) >> 3);
    packet[5] =  (((len& 7) << 5) + 0x1F);
    packet[6] =  0xFC;
}

static void mix_audio_log(const char *fmt,...){
    va_list list;
    va_start(list,fmt);
    vprintf(fmt,list);
    va_end(list);
}

MixCodec* mix_codec_alloc(){
    MixCodec *codec = (MixCodec*)malloc(sizeof(MixCodec));
    if(codec == NULL){
        mix_audio_log("mix codec alloc error no mem \n");
        return NULL;
    }

    memset(codec,0,sizeof(MixCodec));
    return codec;
}

void mix_codec_close(MixCodec *ctx){
    if(ctx == NULL){
        return;
    }
    if(ctx->codecContext){
        avcodec_close(ctx->codecContext);
        avcodec_free_context(&ctx->codecContext);
        ctx->codecContext = NULL;
    }
    if(ctx->iFrame){
        av_frame_unref(ctx->iFrame);
        av_frame_free(&ctx->iFrame);
        ctx->iFrame = NULL;
    }
    if(ctx->ipkt){
        av_packet_unref(ctx->ipkt);
        av_packet_free(&ctx->ipkt);
        ctx->ipkt = NULL;
    }

    if(ctx->swr){
        swr_close(ctx->swr);
        swr_free(&ctx->swr);
        ctx->swr = NULL;
    }

    if(ctx->sAudioBuf){
        free(ctx->sAudioBuf);
        ctx->sAudioBuf = NULL;
    }

    if(ctx->aStream){
        ctx->aStream = NULL;
    }
    if(ctx->par){
        ctx->par = NULL;
    }

    if(ctx->fmt){
        avformat_close_input(&ctx->fmt);
        avformat_free_context(ctx->fmt);
        ctx->fmt = NULL;
    }
    free(ctx);

}


MixContext* mix_context_alloc(){
    MixContext *ctx = (MixContext*)malloc(sizeof(MixContext));
    if(ctx == NULL){
        mix_audio_log("mix context alloc error no mem \n");
        return  NULL;
    }

    memset(ctx,0,sizeof(MixContext));
    return ctx;

}

void mix_context_free(MixContext *ctx){
    if(ctx == NULL)
        return;
    if(ctx->file1){
        free(ctx->file1);
        ctx->file1 = NULL;
    }

    if(ctx->file2){
        free(ctx->file2);
        ctx->file2 = NULL;
    }
    if(ctx->mixfile){
        free(ctx->mixfile);
        ctx->mixfile = NULL;
    }
    if(ctx->file1Codec){
        mix_codec_close(ctx->file1Codec);
        ctx->file1Codec = NULL;
    }
    if(ctx->file2Codec){
        mix_codec_close(ctx->file2Codec);
        ctx->file2Codec = NULL;
    }
    if(ctx->mixCodec){
        mix_codec_close(ctx->mixCodec);
        ctx->mixCodec = NULL;
    }
    free(ctx);
    ctx = NULL;
}


static void* mix_decoder_thread(void *ctx){
    MixCodec *codec = (MixCodec*)ctx;
    mix_audio_log("codec:%s thread in status:%d \n",codec->tag,codec->status);
    if(codec->ipkt == NULL){
        codec->ipkt = av_packet_alloc();
        if(codec->ipkt == NULL){
            mix_audio_log("alloc codec ipkt error no mem \n");
            return NULL;
        }
    }

    if(codec->iFrame == NULL){
        codec->iFrame = av_frame_alloc();
        if(codec->iFrame == NULL){
            mix_audio_log("alloc codec iFrame error no mem \n");
            return NULL;
        }
    }

    int ret;

    while(codec->status>MIX_CODEC_EOF){

        pthread_mutex_lock(&codec->mutex);
        //read pkt
        if(codec->status == MIX_CODEC_WRITE) {
            read_frame:
            ret = av_read_frame(codec->fmt, codec->ipkt);
            if (ret < 0) {
                codec->status = MIX_CODEC_EOF;
                pthread_mutex_unlock(&codec->mutex);
                break;
            }
            if (codec->ipkt->stream_index != codec->bestAudioIndex) {
                av_packet_unref(codec->ipkt);
                goto read_frame;
            }
            //decode pkt
            ret = avcodec_send_packet(codec->codecContext, codec->ipkt);

            check_codec:
            if (ret == AVERROR(EAGAIN)) {
                pthread_mutex_unlock(&codec->mutex);
                continue;
            } else if (ret == AVERROR_EOF) {
                codec->status = MIX_CODEC_EOF;
                pthread_mutex_unlock(&codec->mutex);
                break;
            } else if (ret == 0) {
                ret = avcodec_receive_frame(codec->codecContext, codec->iFrame);
                if (codec->sAudioBuf == NULL) {
                    //just use half buf
                    // 1024 is frame_size
                    codec->sAudioBuf = (unsigned char *) malloc(1024 * MIX_RAW_BIT * MIX_CHANNEL * 2);
                }
                //swr
                if (ret == 0) {
                    AVFrame *inf = codec->iFrame;
                    int nb_out_sample = (int) inf->nb_samples * MIX_SAMPLE_RATE /
                                        inf->sample_rate + 256;
                    ret = swr_convert(codec->swr, &codec->sAudioBuf, nb_out_sample,
                                      inf->extended_data, inf->nb_samples);
                    if (ret > 0) {
                        codec->nb_swr_sample = ret;
                        codec->rwpos = 0;
                        codec->status = MIX_CODEC_READ;
                    }

                } else {
                    goto check_codec;
                }
                pthread_mutex_unlock(&codec->mutex);
            } else {
                codec->status = MIX_CODEC_ERROR;
                pthread_mutex_unlock(&codec->mutex);
                break;
            }
        } else{
            pthread_mutex_unlock(&codec->mutex);
            usleep(5000);

        }

    }

    mix_audio_log("codec:%s quite on status:%d \n",codec->tag,codec->status);
    return NULL;
}


static  void* mix_encoder_thread(void *ctx){
    MixCodec *codec = (MixCodec*)ctx;

    return NULL;
}

static int codecnum = '1';
/**
 * open mix decoder
 * @param codec mixcodec
 * @param path media file
 * @return >= 0 success
 */
int mix_decoder_open(MixCodec *codec,char* path){
    if(codec == NULL || path == NULL){
        return -1;
    }
    codec->tag[0] ='c';
    codec->tag[1] = codecnum;
    codecnum++;

    codec->fmt = avformat_alloc_context();
    int ret = avformat_open_input(&codec->fmt,path,NULL,NULL);
    if(ret<0){
        mix_audio_log("can not open file:%s\n",path);
        return -1;
    }
    ret = avformat_find_stream_info(codec->fmt,NULL);
    if(ret<0){
        mix_audio_log("can not find stream info at file:%s\n",path);
        return -1;
    }
    codec->bestAudioIndex = -1;
    codec->bestVideoIndex = -1;
    codec->aStream = NULL;
    codec->bestAudioIndex = av_find_best_stream(codec->fmt,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);

    if(codec->bestAudioIndex == -1) {
        mix_audio_log("can not find audio stream in file:%s \n",path);
        return -1;
    }

    codec->aStream = codec->fmt->streams[codec->bestAudioIndex];

    codec->par = codec->aStream->codecpar;
    codec->codec = NULL;
    codec->codec = avcodec_find_decoder(codec->par->codec_id);
    if(codec->codec == NULL){
        mix_audio_log("can not find decoder codec id:%d \n",codec->par->codec_id);
        return  -1;
    }

    codec->codecContext = avcodec_alloc_context3(codec->codec);
    ret = avcodec_parameters_to_context(codec->codecContext,codec->par);
    codec->swr = swr_alloc_set_opts(NULL,av_get_default_channel_layout(MIX_CHANNEL),
                                    AV_SAMPLE_FMT_S16,MIX_SAMPLE_RATE,codec->par->channel_layout,
                                    codec->par->format,codec->par->sample_rate,0,NULL);
    codec->sAudioBuf = NULL;
    ret = swr_init(codec->swr);
    if(ret<0){
        mix_audio_log("swr init error codec:%s \n",codec->tag);
        return -1;
    }

    ret = avcodec_open2(codec->codecContext,codec->codec,NULL);
    if(ret<0){
        mix_audio_log("avcodec open2 error codec:%s \n",codec->tag);
        return -1;
    }
    return 0;

}

int mix_encoder_open(MixCodec *ctx){
    if(ctx == NULL){
        return -1;
    }

    ctx->codec = NULL;
    ctx->codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if(ctx->codec == NULL){
        mix_audio_log("mix encoder open avcodec_find_encoder error \n");
        return -1;
    }


    ctx->codecContext = avcodec_alloc_context3(ctx->codec);
    ctx->codecContext->codec_type = AVMEDIA_TYPE_AUDIO;
    ctx->codecContext->sample_rate = MIX_SAMPLE_RATE;
    ctx->codecContext->channel_layout = av_get_default_channel_layout(MIX_CHANNEL);
    ctx->codecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
    ctx->codecContext->channels = MIX_CHANNEL;
    ctx->codecContext->time_base = (AVRational){1,1000};

    ctx->swr = swr_alloc_set_opts(NULL,ctx->codecContext->channel_layout,ctx->codecContext->sample_fmt,ctx->codecContext->sample_rate,
                                  ctx->codecContext->channel_layout,AV_SAMPLE_FMT_S16,MIX_SAMPLE_RATE,0,NULL);


    int r = swr_init(ctx->swr);
    if(r<0){
        mix_audio_log("mix encoder open swr_init error \n");
        return  -1;
    }
    ctx->sAudioBuf = (unsigned  char*)malloc(1024*MIX_SAMPLE_RATE*MIX_CHANNEL*2);
    ctx->iFrame = av_frame_alloc();
    ctx->iFrame->channel_layout = ctx->codecContext->channel_layout;
    ctx->iFrame->format = ctx->codecContext->sample_fmt;
    ctx->iFrame->sample_rate = ctx->codecContext->sample_rate;
    ctx->iFrame->nb_samples = 2014+256;

    int ret = av_frame_get_buffer(ctx->iFrame,0);
    if(ret<0){
        mix_audio_log("mix encoder open  av_frame_get_buffer error \n");
        return  -1;
    }

    ret = avcodec_open2(ctx->codecContext,ctx->codec,NULL);

    if(ret<0){
        mix_audio_log("mix encoder open  avcodec_open2 error \n");
        return  -1;
    }

    return  0;

}


int do_mix(MixContext *mixCtx){
    int ret = 0;
    if(mixCtx == NULL ||
       mixCtx->file1 == NULL ||
       mixCtx->file2 == NULL ||
       mixCtx->mixfile == NULL){
        ret = -1;
        goto done;
    }

    mixCtx->file1Codec = mix_codec_alloc();
    if(mixCtx->file1Codec == NULL){
        ret = -1;
        goto done;
    }


    mixCtx->file2Codec = mix_codec_alloc();
    if(mixCtx->file2Codec == NULL){
        ret = -1;
        goto done;
    }

    mixCtx->mixCodec = mix_codec_alloc();
    if(mixCtx->mixCodec == NULL){
        ret = -1;
        goto done;
    }
    ret = mix_decoder_open(mixCtx->file1Codec,mixCtx->file1);
    if(ret<0){
        goto done;
    }

    ret = mix_decoder_open(mixCtx->file2Codec,mixCtx->file2);
    if(ret<0){
        goto done;
    }
    ret = mix_encoder_open(mixCtx->mixCodec);
    if(ret<0){
        goto done;
    }

    mixCtx->mixpostion = 0;
    if((mixCtx->totalDuration != -1 && mixCtx->totalDuration<=0)||
       (mixCtx->file1Duration != -1 && mixCtx->file1Duration<=0)||
       (mixCtx->file2Duration != -1 && mixCtx->file2Duration<=0)
            ) {
        //duration error
        ret = -1;
        goto done;
    }

    if((mixCtx->file1Stime<0 || mixCtx->file2Stime<0)){
        //start time error
        ret = -1;
        goto done;
    }



    mixCtx->file1Codec->status = MIX_CODEC_WRITE;
    mixCtx->file2Codec->status = MIX_CODEC_WRITE;

    pthread_mutex_init(&mixCtx->file1Codec->mutex,NULL);
    pthread_mutex_init(&mixCtx->file2Codec->mutex,NULL);

    pthread_create(&mixCtx->file1Codec->pid,NULL,mix_decoder_thread,mixCtx->file1Codec);
    pthread_create(&mixCtx->file2Codec->pid,NULL,mix_decoder_thread,mixCtx->file2Codec);


    long oneFrameTime = 1024*1000/MIX_SAMPLE_RATE;
    long mixTime = 0;
    long mixPos  = 0;
    long file1MixDuration = 0;
    long file2MixDuration = 0;
    FILE *outputfile = NULL;
    outputfile = fopen(mixCtx->mixfile,"wb");
    if(outputfile == NULL){
        mix_audio_log("can not open mix file \n");
        ret = -1;
        goto done;
    }

    unsigned char* AACHEADER[7]= {0};

    if(mixCtx->mixCodec->ipkt == NULL){
        mixCtx->mixCodec->ipkt = av_packet_alloc();
    }

    while (mixCtx->totalDuration == -1 || mixCtx->totalDuration > mixTime){
        memset(mixCtx->mixCodec->sAudioBuf,0,1024*MIX_SAMPLE_RATE*MIX_CHANNEL*2);
        if(mixTime >= mixCtx->file1Stime &&
           (file1MixDuration<mixCtx->file1Duration ||
            mixCtx->file1Duration == -1)){
            //reset mixbuf write pos
            mixPos = 0;
            //make sure decoder thread runing
            while(mixCtx->file1Codec->status > MIX_CODEC_EOF){
                if(mixCtx->file1Codec->status == MIX_CODEC_READ){
                    pthread_mutex_lock(&mixCtx->file1Codec->mutex);

                    AVFrame *mixFrame = mixCtx->mixCodec->iFrame;
                    unsigned char *mixBuf = mixCtx->mixCodec->sAudioBuf;

                    int hlen = mixCtx->file1Codec->nb_swr_sample - mixCtx->file1Codec->rwpos;
                    // mixpo/2 one sample 2byte for s16
                    // next about 2 also
                    int len = hlen > mixFrame->nb_samples ? mixFrame->nb_samples-mixPos/2:hlen;

                    // len nb of samples
                    // len*2 total bytes
                    for(int i = 0; i < len *2; i+=2){
                        mixBuf[mixPos] = mixCtx->file1Codec->sAudioBuf[i+mixCtx->file1Codec->rwpos*2];
                        mixBuf[mixPos+1] = mixCtx->file1Codec->sAudioBuf[i+mixCtx->file1Codec->rwpos*2+1];
                        mixPos += 2;
                    }

                    mixCtx->file1Codec->rwpos += len;

                    if(mixCtx->file1Codec->status > MIX_CODEC_EOF&&
                       mixCtx->file1Codec->rwpos >= mixCtx->file1Codec->nb_swr_sample){
                        mixCtx->file1Codec->status = MIX_CODEC_WRITE;
                    }

                    pthread_mutex_unlock(&mixCtx->file1Codec->mutex);

                    //data not enough
                    if(mixPos/2 < mixFrame->nb_samples){
                        continue;
                    }

                    file1MixDuration += oneFrameTime;
                    break;
                } else{
                    usleep(5000);
                }
            }
        } else if(mixCtx->file1Stime < mixTime){
            pthread_mutex_lock(&mixCtx->file1Codec->mutex);
            mixCtx->file1Codec->status = MIX_CODEC_EOF;
            pthread_mutex_unlock(&mixCtx->file1Codec->mutex);
        }


        if(mixTime >= mixCtx->file2Stime &&
           (file2MixDuration<mixCtx->file2Duration || mixCtx->file2Duration == -1)){
            mixPos = 0;
            while (mixCtx->file2Codec->status > MIX_CODEC_EOF) {
                if (mixCtx->file2Codec->status == MIX_CODEC_READ) {
                    pthread_mutex_lock(&mixCtx->file2Codec->mutex);

                    AVFrame *mixFrame = mixCtx->mixCodec->iFrame;
                    unsigned  char *mixbuf = mixCtx->mixCodec->sAudioBuf;

                    int hlen = mixCtx->file2Codec->nb_swr_sample-mixCtx->file2Codec->rwpos;
                    int len = hlen>mixFrame->nb_samples?mixFrame->nb_samples-mixPos/2:hlen;
                    //s16 2bytes
                    for (int i = 0; i < len*2; i+=2) {
                        uint8_t tmp[2] = {0};
                        tmp[0] = mixCtx->file2Codec->sAudioBuf[mixCtx->file2Codec->rwpos*2+i];
                        tmp[1] = mixCtx->file2Codec->sAudioBuf[mixCtx->file2Codec->rwpos*2+i+1];
                        short val = tmp[0];
                        val += tmp[1]<<8;
                        tmp[0] = mixbuf[mixPos];
                        tmp[1] = mixbuf[mixPos+1];
                        short mval = tmp[0];
                        mval += tmp[1]<<8;
                        mval = mval+val;


                        mixbuf[mixPos] = (mval)&0xff;
                        mixbuf[mixPos+1] = (mval>>8)&0xff;

                        mixPos+=2;
                    }
                    mixCtx->file2Codec->rwpos+=len;


                    if(mixCtx->file2Codec->status>MIX_CODEC_EOF&&
                       mixCtx->file2Codec->rwpos == mixCtx->file2Codec->nb_swr_sample)
                    {
                        mixCtx->file2Codec->status = MIX_CODEC_WRITE;
                    }
                    pthread_mutex_unlock(&mixCtx->file2Codec->mutex);

                    if(mixPos/2<mixFrame->nb_samples){
                        continue;
                    }

                    file2MixDuration += oneFrameTime;
                    break;
                } else{
                    usleep(5000);
                }
            }

        } else if(mixCtx->file2Stime<mixTime){
            pthread_mutex_lock(&mixCtx->file2Codec->mutex);
            mixCtx->file2Codec->status = MIX_CODEC_EOF;
            pthread_mutex_unlock(&mixCtx->file2Codec->mutex);
        }//end of file2

        ret = swr_convert(mixCtx->mixCodec->swr,mixCtx->mixCodec->iFrame->extended_data,1024+256,
                          &mixCtx->mixCodec->sAudioBuf,1024);
        if(ret>0){
            mixCtx->mixCodec ->iFrame->nb_samples = ret;

            mixCtx->mixCodec->iFrame->pts = mixTime;
            avcodec_send_frame(mixCtx->mixCodec->codecContext,mixCtx->mixCodec->iFrame);

            ret = avcodec_receive_packet(mixCtx->mixCodec->codecContext,mixCtx->mixCodec->ipkt);
        }
        //got pkt
        if(ret == 0){
            get_aac_hader(AACHEADER,MIX_CHANNEL,2,3,mixCtx->mixCodec->ipkt->size+7);
            if(outputfile != NULL){
                int wsize = fwrite(AACHEADER,1,7,outputfile);
                  wsize = fwrite(mixCtx->mixCodec->ipkt->data,1,mixCtx->mixCodec->ipkt->size,outputfile);
            }
        }

        av_packet_unref(mixCtx->mixCodec->ipkt);

        if(mixCtx->file1Codec->status < MIX_CODEC_WRITE &&
           mixCtx->file2Codec->status < MIX_CODEC_WRITE &&
           mixCtx->totalDuration == -1){
            //done mix
            break;
        }
        mixTime += oneFrameTime;
        mix_audio_log("mix time:%d\n",mixTime);

    }//end of mix

    pthread_mutex_lock(&mixCtx->file1Codec->mutex);
    mixCtx->file1Codec->status = MIX_CODEC_EOF;
    pthread_mutex_unlock(&mixCtx->file1Codec->mutex);
    pthread_mutex_lock(&mixCtx->file2Codec->mutex);
    mixCtx->file2Codec->status = MIX_CODEC_EOF;
    pthread_mutex_unlock(&mixCtx->file2Codec->mutex);

    pthread_join(mixCtx->file1Codec->pid,NULL);
    pthread_join(mixCtx->file2Codec->pid,NULL);

    done:
    if(outputfile != NULL){
        fclose(outputfile);
    }
    return ret;


}




int audio_mix(const char *f1, long f1stime, long f1duration,
              const char *f2, long f2stime, long f2duration,
              const char *fmix, long totalDuration){

    MixContext *mixCtx = mix_context_alloc();

    int len = strlen(f1);
    mixCtx->file1 = (char*)malloc(++len);
    memset(mixCtx->file1,'\0',len);
    memcpy(mixCtx->file1,f1,--len);
    len = strlen(f2);
    mixCtx->file2 = (char*)malloc(++len);
    memset(mixCtx->file2,'\0',len);
    memcpy(mixCtx->file2,f2,--len);

    len = strlen(fmix);
    mixCtx->mixfile = (char*)malloc(++len);
    memset(mixCtx->mixfile,'\0',len);
    memcpy(mixCtx->mixfile,fmix,--len);


    mixCtx->file1Duration = f1duration;
    mixCtx->file1Stime = f1stime;
    mixCtx->file2Duration = f2duration;
    mixCtx->file2Stime = f2stime;
    mixCtx->totalDuration = totalDuration;

    int ret = do_mix(mixCtx);
    mix_context_free(mixCtx);
    return  ret;
}