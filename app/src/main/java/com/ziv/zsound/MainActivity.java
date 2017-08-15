package com.ziv.zsound;

import android.os.AsyncTask;
import android.os.Bundle;
import android.os.Environment;
import android.support.v7.app.AppCompatActivity;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;

import java.io.File;
import java.io.IOError;
import java.io.IOException;

public class MainActivity extends AppCompatActivity implements View.OnClickListener {
    private EditText fileNameEdit;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        fileNameEdit = (EditText) findViewById(R.id.file_name_edit);
        Button playButton = (Button) findViewById(R.id.play_button);
        playButton.setOnClickListener(this);
    }

    @Override
    public void onClick(View v) {
        int id = v.getId();
        switch (id) {
            case R.id.play_button:
                onPlayButtonClick();
                break;
        }
    }

    private void onPlayButtonClick() {
        File file = new File(Environment.getExternalStorageDirectory(),
                fileNameEdit.getText().toString());
        PlayTask playTask = new PlayTask();
        playTask.execute(file.getAbsolutePath());
    }

    /**
     * 播放任务
     */
    private class PlayTask extends AsyncTask<String, Void, Exception>{
        @Override
        protected Exception doInBackground(String... file) {
            Exception result = null;
            try {
                play(file[0]);
            } catch (IOException e) {
                result = e;
                e.printStackTrace();
            }
            return result;
        }
    }

    /**
     * 使用原生AVI播放制定的WAVE文件
     *
     * @param fileName 文件名
     * @throws IOException
     */
    private native void play(String fileName) throws IOException;

    static {
        System.loadLibrary("WAVPlayer");
    }
}
