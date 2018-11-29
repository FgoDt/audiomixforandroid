package com.example.fftest.audiomix;

import android.Manifest;
import android.content.DialogInterface;
import android.content.pm.PackageManager;
import android.os.Environment;
import android.support.v4.app.ActivityCompat;
import android.support.v4.content.ContextCompat;
import android.support.v7.app.AlertDialog;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.VideoView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.lang.reflect.Field;

public class MainActivity extends AppCompatActivity {


    static {
        System.loadLibrary("native-lib");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        getPermission();
        // Example of a call to a native method
        TextView tv = (TextView) findViewById(R.id.sample_text);
        tv.setText(stringFromJNI());
    }

    /**
     * A native method that is implemented by the 'native-lib' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();



    /**
     * mix two media file to one aac file,
     * output aac file will be sample_rate = 48000
     *                         channels = 1
     *
     * @param file1 first media file path
     * @param file1StartTime first media file mix start time,
     *        1 for 1 millisecond.
     * @param file2Duration first media file mix duration,
     *        1 for 1 millisecond
     *        If f1duration = -1 will mix all first file
     * @param file2 Second media file path
     * @param file2StartTime  second media file mix start time,
     *        1 for 1 millisecond
     * @param file2Duration second media file mix duration,
     *        1 for 1 millisecond,
     *        If f2duration = -1 will mix all second file
     * @param mixpath mix file output path
     * @param totalDuation mix file duration,
     *        1 for 1 millisecond
     *        If totalDuration = -1
     *          totalduration = (f1stime+f1duration)>(f2stime+f2duration)?
     *                        (f1stime+f1duration):(f2stime+f2duration)
     *        end if
     *
     * @return true for success
     */

    public native boolean mixAudio(String file1,long file1StartTime, long file1Duration,
                                   String file2,long file2StartTime, long file2Duration,
                                   String mixpath,long totalDuation);



    PCMRecorder pcmRecorder = null;
    AACCodec aacCodec = null;
    private  boolean initRecoder(){
        boolean ret = pcmRecorder.init();
        aacCodec = new AACCodec();
        ret =  aacCodec.init();

        return ret;
    }

    public void onClick(View button){
        switch (button.getId()){
            case R.id.recordingA:
                recording("a.aac",button);
                break;
            case R.id.recordingB:
                recording("b.aac",button);
                break;
            case R.id.playA:
                play("a.aac");
                break;
            case R.id.playB:
                play("b.aac");
                break;
            case R.id.sub:
                sub("a.aac","b.aac");
                break;
            case R.id.playSub:
                play("c.aac");
                break;
            case R.id.mix:
                mix("a.aac","b.aac");
                break;
            case R.id.playMix:
                play("mix.aac");
                break;
            default:
                break;
        }
    }


    boolean recording = false;
    FileOutputStream fileOutputStream;
    private  void recording(final String path, View button){

        if (!hasRcordPermission)
        {
            getPermission();
            return;
        }

        Button b = (Button)button;

        if(b.getText() != getString(R.string.Recording)){
            recording = false;

            pcmRecorder.stop();
            pcmRecorder = null;
            if(fileOutputStream!=null){
                try{
                    fileOutputStream.close();
                }catch (Exception ex){
                    ex.printStackTrace();
                }
            }
            b.setText(R.string.Recording);
            return;
        }else {
            if(pcmRecorder != null)
                return;
            b.setText(R.string.Stop);
        }

        pcmRecorder = new PCMRecorder();
        boolean ret = initRecoder();
        if(pcmRecorder == null){
            return;
        }
        if(ret == false){
            return ;
        }

        pcmRecorder.start();
        recording = true;

        new Thread(new Runnable() {
            @Override
            public void run() {
                byte[] buf = new byte[1024*2];

                int len =  0;
                try{
                    File file = new File(Environment.getExternalStoragePublicDirectory(
                            Environment.DIRECTORY_MUSIC), path);
                    if (!file.createNewFile()) {
                        Log.e("MAKE FILE ", "Directory not created");
                    }

                    fileOutputStream= new FileOutputStream(file);


                }catch (Exception ex){
                    ex.printStackTrace();
                }

                while (recording){
                    len =   pcmRecorder.getPCM(buf);
                    if(len>0){
                        byte[] ret =  aacCodec.encodePCM(buf,len);
                        if(ret!=null&&fileOutputStream!=null&&ret.length>2){
                            try{
                                fileOutputStream.write(getAdtsHeader(2,4,2,ret.length+7));
                                fileOutputStream.write(ret);

                                fileOutputStream.flush();
                            }catch (Exception ex){
                                ex.printStackTrace();
                            }
                            Log.d("Recoding","get pcm"+ret.length);
                        }
                    }
                }

            }
        }).start();

    }

    private  void play(String url){
        if(!hasRWPermission)
        {
            getPermission();
            return;
        }
        VideoView vv = findViewById(R.id.videoView);
        if(vv!=null){
            File fileA = new File(Environment.getExternalStoragePublicDirectory(
                    Environment.DIRECTORY_MUSIC), url);
            if(fileA.exists())
            {
                vv.setVideoPath(fileA.getAbsolutePath());
                vv.start();
            }
        }
    }

    private void mix(String urla,String urlb){
        File fileA = new File(Environment.getExternalStoragePublicDirectory(
                Environment.DIRECTORY_MUSIC), urla);
        if(fileA.exists()) {
                urla = fileA.getAbsolutePath();
        }else {
            return;
        }

        fileA = new File(Environment.getExternalStoragePublicDirectory(
                Environment.DIRECTORY_MUSIC), urlb);
        if(fileA.exists()) {
            urlb = fileA.getAbsolutePath();
        }else {
            return;
        }
        String mixurl;
        fileA = new File(Environment.getExternalStoragePublicDirectory(
                Environment.DIRECTORY_MUSIC), "mix.aac");
        if(fileA.exists()) {
            fileA.delete();
        }
        mixurl = fileA.getAbsolutePath();


        boolean ret =  mixAudio(urla,0,-1,urlb ,0,-1,mixurl,10*1000);
        if(ret){
           Toast.makeText(this,"MIX DONE",Toast.LENGTH_SHORT).show() ;
        }
    }

    private  void sub(String urlA,String urlB){
        if(!hasRWPermission){
            getPermission();
            return;
        }
        File fileA = new File(Environment.getExternalStoragePublicDirectory(
                Environment.DIRECTORY_MUSIC), urlA);
        File fileB = new File(Environment.getExternalStoragePublicDirectory(
                Environment.DIRECTORY_MUSIC), urlB);

        if(!fileA.exists()||!fileB.exists()){
            return;
        }

        File fileC = new File(Environment.getExternalStoragePublicDirectory(
                Environment.DIRECTORY_MUSIC), "c.aac");
        try{
            FileInputStream fileInputStreamA = new FileInputStream(fileA);
            FileInputStream fileInputStreamB = new FileInputStream(fileB);

            fileC.createNewFile();
            FileOutputStream fileOutputStream= new FileOutputStream(fileC);

            byte[] tmp = new byte[1024];
            int ret = 0;
            while ((ret = fileInputStreamA.read(tmp))>0){
                fileOutputStream.write(tmp,0,ret);
                fileOutputStream.flush();
            }
            fileInputStreamA.close();
            while ((ret = fileInputStreamB.read(tmp))>0){
                fileOutputStream.write(tmp,0,ret);
                fileOutputStream.flush();
            }
            fileInputStreamB.close();
            fileOutputStream.close();
            Toast.makeText(this, "sub aac done", Toast.LENGTH_SHORT).show();

        }catch (Exception ex){
            ex.printStackTrace();
        }

    }




    private byte[] getAdtsHeader (int profile, int freqIdx, int chCfg, int packetLen) {
        byte[] header = new byte[7];
        header[0] = (byte)0xFF;
        header[1] = (byte)0xF9;
        header[2] = (byte)(((profile-1)<<6) + (freqIdx<<2) +(chCfg>>2));
        header[3] = (byte)(((chCfg&3)<<6) + (packetLen>>11));
        header[4] = (byte)((packetLen&0x7FF) >> 3);
        header[5] = (byte)(((packetLen&7)<<5) + 0x1F);
        header[6] = (byte)0xFC;
        return header;
    }


    private boolean hasRWPermission = false;
    private boolean hasRcordPermission = false;

    private  void getPermission(){
        int has = ContextCompat.checkSelfPermission(getApplication(), Manifest.permission.WRITE_EXTERNAL_STORAGE);
        if (has == PackageManager.PERMISSION_GRANTED){
            hasRWPermission = true;
        }else {
            ActivityCompat.requestPermissions(this,new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE  },1);
        }

        has = ContextCompat.checkSelfPermission(getApplication(), Manifest.permission.RECORD_AUDIO);
        if (has == PackageManager.PERMISSION_GRANTED){
            hasRcordPermission = true;
        }else {
            ActivityCompat.requestPermissions(this,new String[]{Manifest.permission.RECORD_AUDIO},2);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults){
        if(requestCode == 1){
            if(grantResults.length>0&&grantResults[0] == PackageManager.PERMISSION_GRANTED){
                hasRWPermission = true;
            }else {
                if(ActivityCompat.shouldShowRequestPermissionRationale(this,Manifest.permission.WRITE_EXTERNAL_STORAGE)){
                    new AlertDialog.Builder(this)
                            .setMessage("need read write permisson")
                            .setPositiveButton("ok",(d,w)->{
                                if (w == DialogInterface.BUTTON_POSITIVE){
                                    ActivityCompat.requestPermissions(this,new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE},1);
                                }
                            })
                            .setNegativeButton("cancle",null)
                            .setTitle(R.string.NeedRW)
                            .create()
                            .show();
                }
            }
        }
        if (requestCode == 2){
             if(grantResults.length>0&&grantResults[0] == PackageManager.PERMISSION_GRANTED){
                hasRcordPermission = true;
            }else {
                if(ActivityCompat.shouldShowRequestPermissionRationale(this,Manifest.permission.RECORD_AUDIO)){
                    new AlertDialog.Builder(this)
                            .setMessage("need record audio permisson")
                            .setPositiveButton("ok",(dialog,which)->{
                                if (which == DialogInterface.BUTTON_POSITIVE){
                                    ActivityCompat.requestPermissions(this,new String[]{Manifest.permission.RECORD_AUDIO},1);
                                }
                            })
                            .setNegativeButton("cancle",null)
                            .setTitle(R.string.NeedRecord)
                            .create()
                            .show();
                }
            }
        }
    }

}
