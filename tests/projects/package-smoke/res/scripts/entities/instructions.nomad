this.text.alignment = alignment.topLeft
this.text.color = rgb 240 240 240
this.text.font = global.font
this.keyCount = 0
this.clickCount = 0
this.setSprite "checker"

this.onPress "keyboard" keyPressed
this.onPress "click" mouseClicked
this.onPress "fullscreen" toggleFullscreen
this.onPress "quit" quit

refreshInstructions

if global.automated
    keyPressed
    mouseClicked
    assert this.keyCount == 1
    assert this.clickCount == 1
    this.onTimer 10 false quit
end

fun refreshInstructions
    this.text = $"NOMAD PACKAGING SMOKE TEST\n\nMove mouse: checkerboard follows pointer.\nSpace: keyboard counter. Left click: click counter.\nF11: fullscreen. Escape or window close: quit.\n\nKeyboard presses: {this.keyCount}\nMouse clicks: {this.clickCount}\n\nPass: readable text and a cyan/orange checkerboard.\nAudio playback is not tested."
end