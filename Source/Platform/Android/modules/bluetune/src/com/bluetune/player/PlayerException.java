package com.bluetune.player;

public class PlayerException extends Exception {
	private static final long serialVersionUID = 1L;

	PlayerException(int code) {
		this.code = code;
	}
	
	public String toString() {
		return ("Player Error Code =  " + code) ;
	}
	
	private int code;
}
