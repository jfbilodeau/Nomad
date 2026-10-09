game.name = "Nomad Packaging Smoke Test"
game.organization = "Nomad"
game.clearColor = rgb 24 32 48

window.setTitle "Nomad Packaging Smoke Test"
window.setSizeAndCenter 800 480
window.setResolution 800 480
window.setFps 60

global.font = game.loadFont "fonts/ProggyClean.ttf" 26

game.loadSpriteAtlas "images/checker.json"
game.createScene scenes.smokeScene

log.info "Nomad Packaging Smoke Test initialized."