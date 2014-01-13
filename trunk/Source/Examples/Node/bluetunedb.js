var bluetune = require('../../Bindings/Node/bluetune/build/Debug/bluetune');

function callback() {
    console.log(arguments);    
}

var player = new bluetune.Player(callback);
player.setInput("/Users/gilles/Documents/Audio/go.m4a").play();

setTimeout(function () {
    player.pause();
}, 5000);
