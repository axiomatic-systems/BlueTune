package com.bluetune.sample;

import com.bluetune.player.Player;

import android.app.Activity;
import android.os.Bundle;

public class BlueTunePlayerActivity extends Activity {
    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);
        
        Player player = new Player();
        player.setInput("http://www.bok.net/tmp/test.mp3");
        player.play();
    }
}