package com.example.fftest.audiomix;




import android.media.AudioFormat;
        import android.media.AudioRecord;
        import android.media.MediaRecorder;
        import android.support.v4.content.ContextCompat;

        import java.io.File;
        import java.io.FileOutputStream;

public class PCMRecorder {

    private  int sapleRate ;
    private  int ch ;
    private  int fmt ;

    private AudioRecord pcmRecorder;


    public boolean init(){

        pcmRecorder = getAudioRecord();
        if (pcmRecorder == null)
            return  false;
        else
            return true;
    }

    public boolean start(){
        if (pcmRecorder == null){
            return false;
        }
        if(pcmRecorder.getState() == AudioRecord.STATE_UNINITIALIZED){
            return  false;
        }
        pcmRecorder.startRecording();
        return true;
    }

    public  int getPCM(byte[] buf){
        if(pcmRecorder == null){
            return 0;
        }
        int len = 0;
        if(pcmRecorder.getRecordingState() ==AudioRecord.RECORDSTATE_RECORDING ){
            len = pcmRecorder.read(buf,0,buf.length);
        }
        return len ;
    }

    public  void stop(){
        if(pcmRecorder == null){
            return;
        }

        pcmRecorder.stop();
        pcmRecorder = null;
    }

    private AudioRecord getAudioRecord(){
        int[] sampleRates = {48000,44100,32000,22050,11025};
        for (int sampleRate  : sampleRates){
            int audiofmt = AudioFormat.ENCODING_PCM_16BIT;
            int channelCfg = AudioFormat.CHANNEL_IN_STEREO;

            int bitSample = 16;

            int channels = 1;
            int bufsize = 2*AudioRecord.getMinBufferSize(sampleRate,channelCfg,audiofmt);

            AudioRecord audioRecord = new AudioRecord(MediaRecorder.AudioSource.MIC,sampleRate,channelCfg,audiofmt,bufsize);

            if(audioRecord.getState()!=AudioRecord.STATE_INITIALIZED){
                continue;
            }
            this.sapleRate = sampleRate;
            this.ch = channels;
            this.fmt = audiofmt;
            return audioRecord;
        }
        return  null;
    }


}

