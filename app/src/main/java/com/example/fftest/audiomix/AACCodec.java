package com.example.fftest.audiomix;


import android.media.AudioFormat;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.util.Log;

import java.io.IOError;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Date;

public class AACCodec {
    private MediaCodec AACEncoder = null;
    private int ch = 2;
    private int sampleRate = 44100;

    private ByteBuffer[] inputbuffer;
    private ByteBuffer[] outputbuffer;

    long preTime = 0;
    public  boolean init(){
        MediaFormat fmt = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC,sampleRate,ch);
        fmt.setInteger(MediaFormat.KEY_BIT_RATE, 64000);
        fmt.setInteger(MediaFormat.KEY_CHANNEL_COUNT, ch);
        fmt.setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC);
        fmt.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE,0);

        try{
            AACEncoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC);

            AACEncoder.configure(fmt,null,null,MediaCodec.CONFIGURE_FLAG_ENCODE);

        }catch (Exception ex){
            ex.printStackTrace();
            AACEncoder.release();
            AACEncoder = null;
            return  false;
        }

        if(AACEncoder == null){
            Log.d("AudioMux","can not open encode");
            return false;
        }

        AACEncoder.start();
        inputbuffer = AACEncoder.getInputBuffers();
        outputbuffer = AACEncoder.getOutputBuffers();

        return true;
    }

    public byte[] encodePCM(byte[] in,int count){
        int len = 0;
        if(AACEncoder == null){
            return null;
        }

        for(;;){
            int index = AACEncoder.dequeueInputBuffer(10);
            if(index>=0){
                ByteBuffer byteBuffer = inputbuffer[index];
                byteBuffer.clear();
                byteBuffer.limit(count);
                byteBuffer.put(in,0,count);
                long pts = 0;
                if(preTime == 0){
                    preTime = new Date().getTime()*1000;
                }else {
                    pts = new Date().getTime()*1000 -preTime;
                }
                AACEncoder.queueInputBuffer(index,0,count,pts,0);

                break;
            }
        }

        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        int index = AACEncoder.dequeueOutputBuffer(info, 10);
        if (index >= 0) {
            byte[] ret = new byte[info.size];
            ByteBuffer buffer = outputbuffer[index];
            buffer.limit(info.offset + info.size);
            buffer.get(ret, 0, info.size);
            AACEncoder.releaseOutputBuffer(index, false);
            return ret;
        }
        return  null;
    }

    public void  close(){
        if (AACEncoder ==null){
            return;
        }
        AACEncoder.stop();
        AACEncoder.release();
        AACEncoder = null;
    }

}
