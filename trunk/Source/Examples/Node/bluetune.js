var bluetune = require('../../Bindings/Node/bluetune/build/Release/bluetune');

function callback() {
    console.log(arguments);    
}

var player = new bluetune.Player(callback);
player.setInput("/Users/gilles/Temp/audio.foo", "audio/mp4").play();

setTimeout(function () {
    player.setVolume(0.5);
}, 1000);

setTimeout(function () {
    player.pause();
}, 5000);
