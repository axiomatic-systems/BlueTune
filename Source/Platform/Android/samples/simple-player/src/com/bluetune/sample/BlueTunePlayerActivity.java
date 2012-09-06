package com.bluetune.sample;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.SeekBar;
import android.widget.SeekBar.OnSeekBarChangeListener;
import android.widget.TabHost;
import android.widget.TextView;

import com.bluetune.player.Player;
import com.bluetune.player.PlayerListener;
import com.bluetune.player.StreamInfo;

public class BlueTunePlayerActivity extends Activity implements PlayerListener, OnSeekBarChangeListener {
    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);
        
        seekBar =(SeekBar)findViewById(R.id.seekBar);
        seekBar.setOnSeekBarChangeListener(this);
        seekBar.setEnabled(false);
        
        volumeBar =(SeekBar)findViewById(R.id.volumeBar);
        volumeBar.setOnSeekBarChangeListener(this);

        playButton = (Button)findViewById(R.id.playButton);
		playButton.setEnabled(false);
		
		stopButton = (Button)findViewById(R.id.stopButton);
		stopButton.setEnabled(false);

		timecodeText = (TextView)findViewById(R.id.timecodeText);

		stateText = (TextView)findViewById(R.id.stateText);

    	inputEdit = (EditText)findViewById(R.id.inputNameText);

    	streamInfoItems = new String[] {"", "", "", "", "", "", "", "", ""};
    	streamInfoView = (ListView)findViewById(R.id.streamInfoView);
		streamInfoAdapter = new ArrayAdapter<String>(this,
				R.layout.stream_info_list_item, R.id.streamInfoListItem, streamInfoItems);
		streamInfoView.setAdapter(streamInfoAdapter);
	
    	streamPropertiesItems = new String[] {"", "", "", "", ""}; // max 5 properties
    	streamPropertiesView = (ListView)findViewById(R.id.streamPropertiesView);
		streamPropertiesAdapter = new ArrayAdapter<String>(this,
				R.layout.stream_info_list_item, R.id.streamInfoListItem, streamPropertiesItems);
		streamPropertiesView.setAdapter(streamPropertiesAdapter);

		TabHost tabs = (TabHost)findViewById(R.id.tabhost);
        tabs.setup();
        TabHost.TabSpec spec1 = tabs.newTabSpec("tag1");
        spec1.setContent(R.id.tab1);
        spec1.setIndicator("Stream Info");
        tabs.addTab(spec1);
        TabHost.TabSpec spec2 = tabs.newTabSpec("tag2");
        spec2.setContent(R.id.tab2);
        spec2.setIndicator("Properties");
        tabs.addTab(spec2);
        
    	player = new Player(this, new Handler());
    }
    
    public void onOpenButtonClicked(View button) {
    	String inputName = inputEdit.getText().toString();
    	player.setInput(inputName);

//    	// this is how you would go through a Java input instead of a native input
//    	RandomAccessFile rfile;
//		try {
//			rfile = new RandomAccessFile(inputName, "r");
//		} catch (FileNotFoundException e) {
//			e.printStackTrace();
//			return;
//		}
//    	Input input = new FileInputAdapter(rfile);
//    	player.setInput(input, "audio/mp3"); // hardcode the mime type here for convenience
    }
    
    public void onPlayButtonClicked(View button) {
    	if (isPlaying) {
    		player.pause();
    	} else {
    		player.play();
    	}
    }

    public void onStopButtonClicked(View button) {
    	player.stop();
    }

	public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
		if (seekBar == volumeBar) {
			player.setVolume((float)volumeBar.getProgress()/(float)volumeBar.getMax());
		}
	}

	public void onStartTrackingTouch(SeekBar seekBar) {
		if (seekBar == this.seekBar) {
			isSeeking = true;
		}
	}

	public void onStopTrackingTouch(SeekBar seekBar) {
		isSeeking = false;
		if (seekBar == this.seekBar) {
			player.seekToPosition(seekBar.getProgress(), seekBar.getMax());
			waitForSeekAck = true;
		}
	}

	//
	// BlueTune Player events
	//
    public void onAck(int commandId) {
		if (commandId == Player.COMMAND_ID_SET_INPUT) {
			playButton.setEnabled(true);
			stopButton.setEnabled(true);
			seekBar.setEnabled(true);
			player.play();
		} else if (commandId == Player.COMMAND_ID_SEEK_TO_POSITION) {
			waitForSeekAck = false;
		}
	}

	public void onNack(int commandId, int errorCode) {
		if (commandId == Player.COMMAND_ID_SET_INPUT) {
			playButton.setEnabled(false);
			stopButton.setEnabled(false);
			seekBar.setEnabled(false);
		} else if (commandId == Player.COMMAND_ID_SEEK_TO_POSITION) {
			waitForSeekAck = false;
		}
	}

	public void onDecoderEvent() {
	}

	public void onDecoderStateChanged(final int decoderState) {
		final String state;
		switch (decoderState) {
		case Player.DECODER_STATE_STOPPED:    state = "STOPPED"; break;
		case Player.DECODER_STATE_PLAYING:    state = "PLAYING"; break;
		case Player.DECODER_STATE_PAUSED:     state = "PAUSED";  break;
		case Player.DECODER_STATE_EOS:        state = "EOS";     break;
		default:                              state = "UNKNOWN"; break;
		}
		
		stateText.setText(state); 
		
		if (decoderState == Player.DECODER_STATE_PLAYING) {
			playButton.setText("Pause");
			isPlaying = true;
		} else {
			playButton.setText("Play");
			isPlaying = false;
		}
	}

	public void onPong(int cookie) {
	}

	public void onPropertyChanged(int scope, String source, String name, int type, Object value) {
		if (name.isEmpty()) {
			// all properties cleared
			for (int i=0; i<streamPropertiesItems.length; i++) {
				streamPropertiesItems[i] = "";
			}
			streamPropertiesAdapter.notifyDataSetChanged();
			return;
		}
		
		int first_empty = -1;
		boolean replaced = false;
		for (int i=0; i<streamPropertiesItems.length; i++) {
			if (streamPropertiesItems[i].isEmpty() && first_empty == -1) {
				first_empty = i;
			} else if (streamPropertiesItems[i].startsWith(name)) {
				streamPropertiesItems[i] = name+": "+value.toString();
				replaced = true;
				break;
			}
		}
		if (!replaced && first_empty >= 0) {
			streamPropertiesItems[first_empty] = name+": "+value.toString();
		}
		streamPropertiesAdapter.notifyDataSetChanged();
	}

	public void onStreamInfoChanged(int changeMask, StreamInfo streamInfo) {
		if ((changeMask & StreamInfo.STREAM_INFO_MASK_DATA_TYPE) != 0) {
			streamInfoItems[0] = "Data Type: " + streamInfo.dataType;
		}
		if ((changeMask & StreamInfo.STREAM_INFO_MASK_NOMINAL_BITRATE) != 0) {
			streamInfoItems[1] = "Nominal Bitrate: " + streamInfo.nominalBitrate;
		}
		if ((changeMask & StreamInfo.STREAM_INFO_MASK_AVERAGE_BITRATE) != 0) {
			streamInfoItems[2] = "Average Bitrate: " + streamInfo.averageBitrate;
		}
		if ((changeMask & StreamInfo.STREAM_INFO_MASK_INSTANT_BITRATE) != 0) {
			streamInfoItems[3] = "Instant Bitrate: " + streamInfo.instantBitrate;			
		}
		if ((changeMask & StreamInfo.STREAM_INFO_MASK_SIZE) != 0) {
			streamInfoItems[4] = "Size: " + streamInfo.size;
		}
		if ((changeMask & StreamInfo.STREAM_INFO_MASK_DURATION) != 0) {
			streamInfoItems[5] = "Duration: " + streamInfo.duration + " ms";
		}
		if ((changeMask & StreamInfo.STREAM_INFO_MASK_SAMPLE_RATE) != 0) {
			streamInfoItems[6] = "Sample Rate: " + streamInfo.sampleRate;
		}
		if ((changeMask & StreamInfo.STREAM_INFO_MASK_CHANNEL_COUNT) != 0) {
			streamInfoItems[7] = "Channels: " + streamInfo.channelCount;
		}
		if ((changeMask & StreamInfo.STREAM_INFO_MASK_FLAGS) != 0) {
			streamInfoItems[8] = "Flags: " + streamInfo.flags;
		}
		streamInfoAdapter.notifyDataSetChanged();
	}

	public void onStreamPosition(float position) {
		if (isSeeking || waitForSeekAck) return; // don't update the seek bar if the user is using it
		seekBar.setProgress((int) (position*seekBar.getMax()));
	}

	public void onStreamTimeCode(int h, int m, int s, int f) {
		String tc = String.format("%02d:%02d:%02d", h, m, s);
		timecodeText.setText(tc);
	}

	public void onVolumeChanged(float volume) {
		volumeBar.setProgress((int) (volume*volumeBar.getMax()));
	}
	
	// members
    private Player  player;
    private boolean isPlaying;
    private boolean isSeeking;
    private boolean waitForSeekAck;
    
    private SeekBar  seekBar;
    private SeekBar  volumeBar;
    private Button   playButton;
    private Button   stopButton;
    private TextView timecodeText;
    private TextView stateText;
    private EditText inputEdit;
    private ArrayAdapter<String>  streamInfoAdapter;
    private ListView streamInfoView;
    private String[] streamInfoItems;
    private ArrayAdapter<String>  streamPropertiesAdapter;
    private ListView streamPropertiesView;
    private String[] streamPropertiesItems;
}