package com.bluetune.player;

import java.io.IOException;
import java.io.RandomAccessFile;

public class FileInputAdapter implements Input {

	public FileInputAdapter(RandomAccessFile file) {
		this.file = file;
		try {
			this.size = file.length();
		} catch (IOException e) {
			this.size = 0;
		}
	}
	
	public int read(byte[] buffer, int byteCount) {
		try {
			int result = file.read(buffer, 0, byteCount);
			if (result > 0) {
				position += result;
			}
			return result;
		} catch (IOException e) {
			return -1;
		}
	}

	public int seek(long offset) {
		try {
			file.seek(offset);
			position = offset;
		} catch (IOException e) {
			return -1;
		}
		return 0;
	}

	public long tell() {
		return position;
	}

	public long getSize() {
		return size;
	}

	public long getAvailable() {
		long remaining = size-position;
		return remaining >= 0? remaining : 0;
	}

	// members
	RandomAccessFile file;
	long             size;
	long             position;
}
