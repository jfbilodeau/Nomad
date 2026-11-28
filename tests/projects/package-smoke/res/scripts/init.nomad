game.name = "Nomad Packaging Smoke Test"
game.organization = "Nomad"
game.clearColor = rgb 24 32 48

game.window.setTitle "Nomad Packaging Smoke Test"
game.window.setSizeAndCenter 800 480
game.window.setResolution 800 480
game.window.setFps 60

global.font = game.loadFont "fonts/ProggyClean.ttf" 26

game.createScene scenes.smokeScene

print "Nomad Packaging Smoke Test initialized."