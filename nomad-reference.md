# Nomad Engine Reference
* [Constants](#constants)
* [Enums](#enums)
* [Variables](#variables)
* [Intrinsic Members](#intrinsic-members)
* [NativeFunctions](#nativefunctions)
---
## Constants
* `false:bool = false`
* `pi:float = 3.1415927`
* `true:bool = true`

## Enums

### alignment

* `alignment.topLeft`
* `alignment.topMiddle`
* `alignment.topRight`
* `alignment.centerLeft`
* `alignment.centerMiddle`
* `alignment.centerRight`
* `alignment.bottomLeft`
* `alignment.bottomMiddle`
* `alignment.bottomRight`

### body

* `body.static`
* `body.dynamic`
* `body.kinematic`

### cardinal

* `cardinal.north`
* `cardinal.east`
* `cardinal.south`
* `cardinal.west`

### horizontalAlignment

* `horizontalAlignment.left`
* `horizontalAlignment.middle`
* `horizontalAlignment.right`

### verticalAlignment

* `verticalAlignment.top`
* `verticalAlignment.center`
* `verticalAlignment.bottom`

## Variables
- `game.clearColor:int` (dynamic)
- `game.debug.console.scale:float` (dynamic)
- `game.debug.console.visible:bool` (dynamic)
- `game.name:string` (dynamic)
- `game.organization:string` (dynamic)
- `input.mouse.deltaX:float` (dynamic)
- `input.mouse.deltaY:float` (dynamic)
- `input.mouse.x:float` (dynamic)
- `input.mouse.y:float` (dynamic)
- `other:int` (dynamic)
- `other.animation.direction:string` (dynamic)
- `other.animation.name:string` (dynamic)
- `other.animation.repeat:bool` (dynamic)
- `other.animation.reverse:bool` (dynamic)
- `other.animation.speed:int` (dynamic)
- `other.animation.variant:string` (dynamic)
- `other.body.height:float` (dynamic)
- `other.body.shape:int` (dynamic)
- `other.body.width:float` (dynamic)
- `other.collisionMask:int` (dynamic)
- `other.distanceMoved:float` (dynamic)
- `other.hasMoved:bool` (dynamic)
- `other.height:float` (dynamic)
- `other.layer:int` (dynamic)
- `other.mask:int` (dynamic)
- `other.name:string` (dynamic)
- `other.opacity:float` (dynamic)
- `other.previousX:float` (dynamic)
- `other.previousY:float` (dynamic)
- `other.radius:float` (dynamic)
- `other.sensorMask:int` (dynamic)
- `other.sprite.height:int` (dynamic)
- `other.sprite.name:string` (dynamic)
- `other.sprite.width:int` (dynamic)
- `other.sprite.x:float` (dynamic)
- `other.sprite.y:float` (dynamic)
- `other.text.alignment:alignment` (dynamic)
- `other.text.color:int` (dynamic)
- `other.text.font:int` (dynamic)
- `other.text.height:float` (dynamic)
- `other.text.lineSpacing:float` (dynamic)
- `other.text.value:string` (dynamic)
- `other.text.width:float` (dynamic)
- `other.text.x:float` (dynamic)
- `other.text.y:float` (dynamic)
- `other.velocity.x:float` (dynamic)
- `other.velocity.y:float` (dynamic)
- `other.visible:bool` (dynamic)
- `other.width:float` (dynamic)
- `other.x:float` (dynamic)
- `other.y:float` (dynamic)
- `other.z:float` (dynamic)
- `others.count:int` (dynamic)
- `scene.camera.x:float` (dynamic)
- `scene.camera.y:float` (dynamic)
- `scene.name:string` (dynamic)
- `scene.tilemap.height:int` (dynamic)
- `scene.tilemap.height.pixel:int` (dynamic)
- `scene.tilemap.width:int` (dynamic)
- `scene.tilemap.width.pixel:int` (dynamic)
- `scene.tint:int` (dynamic)
- `scene.z:int` (dynamic)
- `select.count:int` (dynamic)
- `this:int` (dynamic)
- `this.animation.direction:string` (dynamic)
- `this.animation.name:string` (dynamic)
- `this.animation.repeat:bool` (dynamic)
- `this.animation.reverse:bool` (dynamic)
- `this.animation.speed:int` (dynamic)
- `this.animation.variant:string` (dynamic)
- `this.body.height:float` (dynamic)
- `this.body.shape:int` (dynamic)
- `this.body.width:float` (dynamic)
- `this.collisionMask:int` (dynamic)
- `this.distanceMoved:float` (dynamic)
- `this.hasMoved:bool` (dynamic)
- `this.height:float` (dynamic)
- `this.layer:int` (dynamic)
- `this.mask:int` (dynamic)
- `this.name:string` (dynamic)
- `this.opacity:float` (dynamic)
- `this.previousX:float` (dynamic)
- `this.previousY:float` (dynamic)
- `this.radius:float` (dynamic)
- `this.sensorMask:int` (dynamic)
- `this.sprite.height:int` (dynamic)
- `this.sprite.name:string` (dynamic)
- `this.sprite.width:int` (dynamic)
- `this.sprite.x:float` (dynamic)
- `this.sprite.y:float` (dynamic)
- `this.text.alignment:alignment` (dynamic)
- `this.text.color:int` (dynamic)
- `this.text.font:int` (dynamic)
- `this.text.height:float` (dynamic)
- `this.text.lineSpacing:float` (dynamic)
- `this.text.value:string` (dynamic)
- `this.text.width:float` (dynamic)
- `this.text.x:float` (dynamic)
- `this.text.y:float` (dynamic)
- `this.velocity.x:float` (dynamic)
- `this.velocity.y:float` (dynamic)
- `this.visible:bool` (dynamic)
- `this.width:float` (dynamic)
- `this.x:float` (dynamic)
- `this.y:float` (dynamic)
- `this.z:float` (dynamic)
- `window.fps:int` (dynamic)
- `window.hasFocus:bool` (dynamic)
- `window.height:int` (dynamic)
- `window.isFullScreen:bool` (dynamic)
- `window.isMaximized:bool` (dynamic)
- `window.isMinimized:bool` (dynamic)
- `window.resolution.height:int` (dynamic)
- `window.resolution.width:int` (dynamic)
- `window.title:string` (dynamic)
- `window.width:int` (dynamic)

## Intrinsic Members

Intrinsic properties and methods are available on values of the receiver type.

### `string`

#### `byteLength` (*read-only property*)

`byteLength:int`

The number of bytes in the UTF-8 encoding of the string.

---
#### `contains` (*method*)

parameters:
- `text:string` - The text to find

`returns bool`

Return true when the string contains the given text.

---
#### `endsWith` (*method*)

parameters:
- `text:string` - The text to find

`returns bool`

Return true when the string ends with the given text.

---
#### `isEmpty` (*read-only property*)

`isEmpty:bool`

True when the string is empty.

---
#### `length` (*read-only property*)

`length:int`

The number of characters (Unicode code points) in the string.

---
#### `startsWith` (*method*)

parameters:
- `text:string` - The text to find

`returns bool`

Return true when the string starts with the given text.

---
#### `toLower` (*method*)

No parameters.

`returns string`

Return a lowercase copy of the string.

---
#### `toUpper` (*method*)

No parameters.

`returns string`

Return an uppercase copy of the string.

---
#### `trim` (*method*)

No parameters.

`returns string`

Return a copy with whitespace removed from both ends.

---
#### `trimEnd` (*method*)

No parameters.

`returns string`

Return a copy with whitespace removed from the end.

---
#### `trimStart` (*method*)

No parameters.

`returns string`

Return a copy with whitespace removed from the beginning.

---

## NativeFunctions
### `breakpoint` (*NativeFunction*)

No parameters.


`returns void`

Set a breakpoint

---
### `game.createScene` (*NativeFunction*)

parameters:
- `function:callback` - The function to execute to initialize the scene. Return the scene id.

`returns int`

Creates a new scene.

---
### `game.createSceneByName` (*NativeFunction*)

parameters:
- `functionName:string` - Name of the function to execute to initialize the scene. Return the scene id.

`returns int`

Creates a new scene.

---
### `game.createSceneByNameThen` (*NativeFunction*)

parameters:
- `functionName:string` - Name of the function to execute to initialize the scene.
- `postCreateFunction:callback` - Function to execute once the scene is created.

`returns int`

Creates a new scene and execute a function once the scene is created. Returns the scene id.

---
### `game.createSceneThen` (*NativeFunction*)

parameters:
- `function:callback` - The function to execute to initialize the scene.
- `postCreateFunction:callback` - Function to execute once the scene is created.

`returns int`

Creates a new scene and execute a function once the scene is created. Returns the scene id.

---
### `game.inventory.load` (*NativeFunction*)

parameters:
- `saveName:string` - Name of the save to load.

`returns void`

Loads `inventory.*` variables from `<pref>/save/<saveName>.json`. Variables absent from the save keep their current value.

---
### `game.inventory.save` (*NativeFunction*)

parameters:
- `saveName:string` - Name of the save.

`returns void`

Saves all `inventory.*` variables to `<pref>/save/<saveName>.json`.

---
### `game.inventory.saveExists` (*NativeFunction*)

parameters:
- `saveName:string` - Name of the save.

`returns bool`

Returns true if the save `<pref>/save/<saveName>.json` exists.

---
### `game.isInScene` (*NativeFunction*)

No parameters.


`returns bool`

Returns true if this function is executing in a scene.

---
### `game.loadFont` (*NativeFunction*)

parameters:
- `fontName:string` - Name of the font to load.
- `fontSize:int` - Size of the font in point to load.

`returns int`

Loads a font from a file. Returns the ID of the font.

---
### `game.loadImage` (*NativeFunction*)

parameters:
- `imageName:string` - Name of the image to load.

`returns int`

Loads a font from a file.

---
### `game.loadSpriteAtlas` (*NativeFunction*)

parameters:
- `atlasName:string` - Name of the sprite atlas to load.

`returns void`

Loads a sprite atlas from a file.

---
### `game.settings.load` (*NativeFunction*)

No parameters.


`returns void`

Loads `settings.*` variables from `<pref>/settings.json`. Variables absent from the file keep their current value.

---
### `game.settings.save` (*NativeFunction*)

No parameters.


`returns void`

Saves all `settings.*` variables to `<pref>/settings.json`.

---
### `game.trigger` (*NativeFunction*)

parameters:
- `event:eventDispatch` - The event to dispatch (trigger).

`returns void`

Trigger a global event.

---
### `log.info` (*NativeFunction*)

parameters:
- `$file:$file` - (hidden) The file name from which the log is issued
- `$function:$function` - (hidden) The function name from which the log is issued
- `$line:$line` - (hidden) The line number from which the log is issued
- `message:string` - The text to log

`returns void`

Write a 'info' level string to the log.

---
### `math.ceiling` (*NativeFunction*)

parameters:
- `value:float` - The float to ceiling

`returns float`

Ceiling a float value.

---
### `math.floor` (*NativeFunction*)

parameters:
- `value:float` - The float to floor

`returns float`

Floor a float value.

---
### `other.addSystem` (*NativeFunction*)

parameters:
- `function:callback` - Function to execute as a system.

`returns void`

Add a system function to the entity. The function will be executed every frame (update).

---
### `other.body.circle` (*NativeFunction*)

parameters:
- `bodyType:body` - Body type.
- `radius:float` - Radius of the circle body.

`returns void`

Set the circle body of the entity.

---
### `other.body.none` (*NativeFunction*)

No parameters.


`returns void`

Set no body on the entity.

---
### `other.body.rectangle` (*NativeFunction*)

parameters:
- `bodyType:body` - Body type.
- `width:float` - Width of the rectangle body.
- `height:float` - Height of the rectangle body.

`returns void`

Set the rectangle body of the entity.

---
### `other.getCollidingMask` (*NativeFunction*)

No parameters.


`returns int`

Get the collision mask at the entity's position.

---
### `other.getMaskAt` (*NativeFunction*)

parameters:
- `x:float` - X coordinate relative to entity
- `y:float` - Y coordinate relative to entity

`returns int`

Get the tile mask for the entity at the specified relative position.

---
### `other.moveTo` (*NativeFunction*)

parameters:
- `x:float` - X coordinate of the destination.
- `y:float` - Y coordinate of the destination.
- `speed:float` - Speed in pixels per seconds to move to the destination.

`returns void`

Move the entity to a specified location.

---
### `other.moveToThen` (*NativeFunction*)

parameters:
- `x:float` - X coordinate of the destination.
- `y:float` - Y coordinate of the destination.
- `speed:float` - Speed in pixels per seconds to move to the destination.
- `callback:callback` - Callback function or function.

`returns void`

Move the entity to a specified location then execute a function when destination is reached.

---
### `other.oldon` (*NativeFunction*)

parameters:
- `eventName:string` - Name of the event to listen to.
- `callback:callback` - Callback function or function.

`returns void`

Assign a function or function to an event for this entity.

---
### `other.oldtrigger` (*NativeFunction*)

parameters:
- `eventName:string` - Name of the event to trigger.

`returns void`

Trigger an event for this entity.

---
### `other.on` (*NativeFunction*)

parameters:
- `event:eventCallback` - The event to register for.

`returns void`

Assign a function or function to an event for this entity.

---
### `other.onBeginCollision` (*NativeFunction*)

parameters:
- `callback:callback` - Callback function or function.

`returns void`

Assign a function to the begin collision event for this entity.

---
### `other.onBeginCollisionWith` (*NativeFunction*)

parameters:
- `mask:int` - Collision mask.
- `callback:callback` - Callback function or function.

`returns void`

Assign a function to the begin collision event with a mask.

---
### `other.onEndCollision` (*NativeFunction*)

parameters:
- `callback:callback` - Callback function or function.

`returns void`

Assign a function or function to the collision end event.

---
### `other.onEndCollisionWith` (*NativeFunction*)

parameters:
- `mask:int` - Collision mask.
- `callback:callback` - Callback function or function.

`returns void`

Assign a function or function to the end collision with event.

---
### `other.onPress` (*NativeFunction*)

parameters:
- `actionName:string` - Name of the action to listen to.
- `callback:callback` - Callback function

`returns void`

Assign a function or function to the pressed event for this entity.

---
### `other.onRelease` (*NativeFunction*)

parameters:
- `actionName:string` - Name of the action to listen to.
- `callback:callback` - Callback function or function.

`returns void`

Assign a function or function to the release event for this entity.

---
### `other.onTimer` (*NativeFunction*)

parameters:
- `frameCount:int` - Number of frames to wait before triggering the timer.
- `repeat:bool` - Whether the timer should repeat.
- `callback:callback` - Callback function or function.

`returns void`

Assign a function or function to be called after a timer expires.

---
### `other.onUpdate` (*NativeFunction*)

parameters:
- `callback:callback` - Callback function or function.

`returns void`

Assign a function to the update event for this entity. Called every frame (update).

---
### `other.pauseOthers` (*NativeFunction*)

No parameters.


`returns void`

Pauses all other entities in the scene

---
### `other.playAnimation` (*NativeFunction*)

parameters:
- `name:string` - Name of the animation to set.
- `duration:int` - Number of frames to display an animation frame.
- `repeat:bool` - Whether to repeat the animation.
- `reverse:bool` - Whether to reverse the animation.

`returns void`

Play an animation for the entity.

---
### `other.playAnimationAndThen` (*NativeFunction*)

parameters:
- `variant:string` - Variant name of the animation to set.
- `duration:int` - Number of frames to display an animation frame.
- `repeat:bool` - Whether to repeat the animation. Callback executed every time the animation ends.
- `reverse:bool` - Whether to reverse the animation.
- `callback:callback` - Callback function or function.

`returns void`

Play an animation for the entity, then execute a function or function once the animation completes.

---
### `other.removeAllSystems` (*NativeFunction*)

No parameters.


`returns void`

Remove all system functions from the entity.

---
### `other.removeSelf` (*NativeFunction*)

No parameters.


`returns void`

Remove this entity from the scene.

---
### `other.removeSystem` (*NativeFunction*)

parameters:
- `registrationId:string` - Id of the system to remove from the entity.

`returns void`

Remove a system function from the entity.

---
### `other.repositionOnAnchor` (*NativeFunction*)

parameters:
- `x:float` - X coordinate
- `y:float` - Y coordinate

`returns void`

Reposition the entity based on the new sprite anchor.

---
### `other.setLocation` (*NativeFunction*)

parameters:
- `x:float` - X coordinate
- `y:float` - Y coordinate

`returns void`

Set the x,y location of the entity.

---
### `other.setSprite` (*NativeFunction*)

parameters:
- `name:string` - Name of the sprite to set.

`returns void`

Set the sprite of the entity.

---
### `other.setSpriteAnchor` (*NativeFunction*)

parameters:
- `x:float` - X coordinate
- `y:float` - Y coordinate

`returns void`

Set the anchor point of the sprite of the entity.

---
### `other.setText` (*NativeFunction*)

parameters:
- `fontId:int` - ID of the font to use
- `text:string` - Text to set

`returns void`

Set the font and text to display on an entity.

---
### `other.systems.afterUpdate` (*NativeFunction*)

parameters:
- `$function:$function` - ID of the system.
- `callback:callback` - Callback function or function.

`returns void`

Assign a function to be executed after updating this entity.

---
### `other.systems.beforeUpdate` (*NativeFunction*)

parameters:
- `$function:$function` - ID of the system.
- `callback:callback` - Callback function or function.

`returns void`

Assign a function to be executed before updating this entity.

---
### `other.trigger` (*NativeFunction*)

parameters:
- `event:eventDispatch` - The event to dispatch (trigger)

`returns void`

Trigger an event for this entity.

---
### `other.velocity.clear` (*NativeFunction*)

No parameters.


`returns void`

Clear the velocity of the entity.

---
### `other.velocity.set` (*NativeFunction*)

parameters:
- `x:float` - X velocity
- `y:float` - Y velocity

`returns void`

Set the velocity of the entity.

---
### `rgb` (*NativeFunction*)

parameters:
- `r:int` - Red component (0-255).
- `g:int` - Green component (0-255).
- `b:int` - Blue component (0-255).

`returns int`

Creates an RGB color from red, green and blue components (0-255).

---
### `rgba` (*NativeFunction*)

parameters:
- `r:int` - Red component (0-255).
- `g:int` - Green component (0-255).
- `b:int` - Blue component (0-255).
- `a:int` - Alpha component (0-255).

`returns int`

Creates an RGBA color from red, green, blue and alpha components (0-255).

---
### `scene.addAnimatedTile` (*NativeFunction*)

parameters:
- `tileName:string` - Name of the first tile in the animation.
- `count:int` - Number of consecutive tiles (frames) to animate.
- `speed:int` - Scene updates per displayed tile frame.

`returns void`

Adds a looping animation for a consecutive range of tiles.

---
### `scene.camera.follow` (*NativeFunction*)

parameters:
- `entityId:int` - ID of the entity to follow.

`returns void`

Makes the camera follow the specified entity.

---
### `scene.camera.setPosition` (*NativeFunction*)

parameters:
- `x:float` - X position of the camera.
- `y:float` - Y position of the camera.

`returns void`

Sets the camera position.

---
### `scene.createEntity` (*NativeFunction*)

parameters:
- `function:callback` - Function to initialize the entity.
- `x:float` - X position of the entity.
- `y:float` - Y position of the entity.
- `layer:int` - Layer of the entity.

`returns void`

Creates a new entity for this scene.

---
### `scene.createEntityByName` (*NativeFunction*)

parameters:
- `functionName:string` - Name of the function to execute to initialize the entity.
- `x:float` - X position of the entity.
- `y:float` - Y position of the entity.
- `layer:int` - Layer of the entity.

`returns void`

Creates a new entity for this scene.

---
### `scene.createEntityByNameThen` (*NativeFunction*)

parameters:
- `functionName:string` - Name of the function to execute to initialize the entity.
- `x:float` - X position of the entity.
- `y:float` - Y position of the entity.
- `layer:int` - Layer of the entity.
- `function:callback` - ID of the function to execute after creating the entity.

`returns void`

Creates a new entity for this scene.

---
### `scene.createEntityThen` (*NativeFunction*)

parameters:
- `function:callback` - Name of the function to execute to initialize the entity.
- `x:float` - X position of the entity.
- `y:float` - Y position of the entity.
- `layer:int` - Layer of the entity.
- `function:callback` - ID of the function to execute after creating the entity.

`returns void`

Creates a new entity for this scene.

---
### `scene.createTextByNameEntity` (*NativeFunction*)

parameters:
- `functionName:string` - Name of the function to execute to initialize the entity.
- `x:float` - X position of the entity.
- `y:float` - Y position of the entity.
- `layer:int` - Layer of the entity.
- `text:string` - Text to display for the entity.

`returns void`

Creates a new entity for this scene.

---
### `scene.createTextEntity` (*NativeFunction*)

parameters:
- `function:callback` - Name of the function to execute to initialize the entity.
- `x:float` - X position of the entity.
- `y:float` - Y position of the entity.
- `layer:int` - Layer of the entity.
- `text:string` - Text to display for the entity.

`returns void`

Creates a new entity for this scene.

---
### `scene.createTextEntityAndThen` (*NativeFunction*)

parameters:
- `functionName:string` - Name of the function to execute to initialize the entity.
- `x:float` - X position of the entity.
- `y:float` - Y position of the entity.
- `layer:int` - Layer of the entity.
- `text:string` - Text to display for the entity.
- `function:callback` - ID of the function to execute after creating the entity.

`returns void`

Creates a new entity for this scene.

---
### `scene.loadInputMapping` (*NativeFunction*)

parameters:
- `mappingName:string` - Name of the input mapping to load.

`returns void`

Load an input mapping for this scene.

---
### `scene.loadMap` (*NativeFunction*)

parameters:
- `mapName:string` - Name of the map to load.
- `tileSetTexture:string` - Name of the tile set texture to load.

`returns void`

Loads a map for this scene.

---
### `scene.on` (*NativeFunction*)

parameters:
- `event:eventCallback` - The event to set the callback for

`returns void`

Sets the onEvent callback for this scene. The callback function is called when the specified event is triggered.

---
### `scene.onPostUpdate` (*NativeFunction*)

parameters:
- `function:callback` - Function to execute after each update of the scene

`returns void`

Sets the onPostUpdate callback for this scene. The callback function is called after updating the entities in the scene.

---
### `scene.onUpdate` (*NativeFunction*)

parameters:
- `function:callback` - Function to execute on each update of the scene

`returns void`

Sets the onUpdate callback for this scene. The callback function is called before updating the entities in the scene.

---
### `scene.pauseAll` (*NativeFunction*)

No parameters.


`returns void`

Pauses all entities in this scene.

---
### `scene.queue` (*NativeFunction*)

parameters:
- `frameCount:int` - Number of frames to wait before triggering the event.
- `eventName:string` - Name of the event to trigger.

`returns void`

Trigger an event after a specified number of frames.

---
### `scene.removeSelf` (*NativeFunction*)

No parameters.


`returns void`

Unloads this scene.

---
### `scene.trigger` (*NativeFunction*)

parameters:
- `event:eventDispatch` - The event to dispatch (trigger).

`returns void`

Trigger an event for all entities on this scene.

---
### `scene.trigger.layer` (*NativeFunction*)

parameters:
- `event:eventDispatch` - The event to dispatch (trigger).
- `layerId:int` - Layer ID to trigger the event on.

`returns void`

Trigger an event for all entities on the specified layer.

---
### `scene.unpauseAll` (*NativeFunction*)

No parameters.


`returns void`

Unpauses all entities in this scene.

---
### `select` (*NativeFunction*)

parameters:
- `predicate:callback` - The predicate used to select other entities

`returns int`

Select entities in the same layer as the `this` entity that match the predicate.

---
### `select.all` (*NativeFunction*)

parameters:
- `predicate:callback` - The predicate used to select other entities

`returns int`

Select entities in all layers that match the predicate.

---
### `select.byName` (*NativeFunction*)

parameters:
- `name:string` - The name of the entity to select

`returns int`

Select entities across all layers that have the given name.

---
### `select.this` (*NativeFunction*)

No parameters.


`returns void`

Select the `this` as `other` entity. Useful to pass the `this` entity to a function that takes `other` entities.

---
### `system.exit` (*NativeFunction*)

No parameters.


`returns void`

Exits the game.

---
### `this.addSystem` (*NativeFunction*)

parameters:
- `function:callback` - Function to execute as a system.

`returns void`

Add a system function to the entity. The function will be executed every frame (update).

---
### `this.body.circle` (*NativeFunction*)

parameters:
- `bodyType:body` - Body type.
- `radius:float` - Radius of the circle body.

`returns void`

Set the circle body of the entity.

---
### `this.body.none` (*NativeFunction*)

No parameters.


`returns void`

Set no body on the entity.

---
### `this.body.rectangle` (*NativeFunction*)

parameters:
- `bodyType:body` - Body type.
- `width:float` - Width of the rectangle body.
- `height:float` - Height of the rectangle body.

`returns void`

Set the rectangle body of the entity.

---
### `this.getCollidingMask` (*NativeFunction*)

No parameters.


`returns int`

Get the collision mask at the entity's position.

---
### `this.getMaskAt` (*NativeFunction*)

parameters:
- `x:float` - X coordinate relative to entity
- `y:float` - Y coordinate relative to entity

`returns int`

Get the tile mask for the entity at the specified relative position.

---
### `this.moveTo` (*NativeFunction*)

parameters:
- `x:float` - X coordinate of the destination.
- `y:float` - Y coordinate of the destination.
- `speed:float` - Speed in pixels per seconds to move to the destination.

`returns void`

Move the entity to a specified location.

---
### `this.moveToThen` (*NativeFunction*)

parameters:
- `x:float` - X coordinate of the destination.
- `y:float` - Y coordinate of the destination.
- `speed:float` - Speed in pixels per seconds to move to the destination.
- `callback:callback` - Callback function or function.

`returns void`

Move the entity to a specified location then execute a function when destination is reached.

---
### `this.oldon` (*NativeFunction*)

parameters:
- `eventName:string` - Name of the event to listen to.
- `callback:callback` - Callback function or function.

`returns void`

Assign a function or function to an event for this entity.

---
### `this.oldtrigger` (*NativeFunction*)

parameters:
- `eventName:string` - Name of the event to trigger.

`returns void`

Trigger an event for this entity.

---
### `this.on` (*NativeFunction*)

parameters:
- `event:eventCallback` - The event to register for.

`returns void`

Assign a function or function to an event for this entity.

---
### `this.onBeginCollision` (*NativeFunction*)

parameters:
- `callback:callback` - Callback function or function.

`returns void`

Assign a function to the begin collision event for this entity.

---
### `this.onBeginCollisionWith` (*NativeFunction*)

parameters:
- `mask:int` - Collision mask.
- `callback:callback` - Callback function or function.

`returns void`

Assign a function to the begin collision event with a mask.

---
### `this.onEndCollision` (*NativeFunction*)

parameters:
- `callback:callback` - Callback function or function.

`returns void`

Assign a function or function to the collision end event.

---
### `this.onEndCollisionWith` (*NativeFunction*)

parameters:
- `mask:int` - Collision mask.
- `callback:callback` - Callback function or function.

`returns void`

Assign a function or function to the end collision with event.

---
### `this.onPress` (*NativeFunction*)

parameters:
- `actionName:string` - Name of the action to listen to.
- `callback:callback` - Callback function

`returns void`

Assign a function or function to the pressed event for this entity.

---
### `this.onRelease` (*NativeFunction*)

parameters:
- `actionName:string` - Name of the action to listen to.
- `callback:callback` - Callback function or function.

`returns void`

Assign a function or function to the release event for this entity.

---
### `this.onTimer` (*NativeFunction*)

parameters:
- `frameCount:int` - Number of frames to wait before triggering the timer.
- `repeat:bool` - Whether the timer should repeat.
- `callback:callback` - Callback function or function.

`returns void`

Assign a function or function to be called after a timer expires.

---
### `this.onUpdate` (*NativeFunction*)

parameters:
- `callback:callback` - Callback function or function.

`returns void`

Assign a function to the update event for this entity. Called every frame (update).

---
### `this.pauseOthers` (*NativeFunction*)

No parameters.


`returns void`

Pauses all other entities in the scene except `this`.

---
### `this.playAnimation` (*NativeFunction*)

parameters:
- `name:string` - Name of the animation to set.
- `duration:int` - Number of frames to display an animation frame.
- `repeat:bool` - Whether to repeat the animation.
- `reverse:bool` - Whether to reverse the animation.

`returns void`

Play an animation for the entity.

---
### `this.playAnimationAndThen` (*NativeFunction*)

parameters:
- `variant:string` - Variant name of the animation to set.
- `duration:int` - Number of frames to display an animation frame.
- `repeat:bool` - Whether to repeat the animation. Callback executed every time the animation ends.
- `reverse:bool` - Whether to reverse the animation.
- `callback:callback` - Callback function or function.

`returns void`

Play an animation for the entity, then execute a function or function once the animation completes.

---
### `this.removeAllSystems` (*NativeFunction*)

No parameters.


`returns void`

Remove all system functions from the entity.

---
### `this.removeSelf` (*NativeFunction*)

No parameters.


`returns void`

Remove this entity from the scene.

---
### `this.removeSystem` (*NativeFunction*)

parameters:
- `registrationId:string` - Id of the system to remove from the entity.

`returns void`

Remove a system function from the entity.

---
### `this.repositionOnAnchor` (*NativeFunction*)

parameters:
- `x:float` - X coordinate
- `y:float` - Y coordinate

`returns void`

Reposition the entity based on the new sprite anchor.

---
### `this.setLocation` (*NativeFunction*)

parameters:
- `x:float` - X coordinate
- `y:float` - Y coordinate

`returns void`

Set the x,y location of the entity.

---
### `this.setSprite` (*NativeFunction*)

parameters:
- `name:string` - Name of the sprite to set.

`returns void`

Set the sprite of the entity.

---
### `this.setSpriteAnchor` (*NativeFunction*)

parameters:
- `x:float` - X coordinate
- `y:float` - Y coordinate

`returns void`

Set the anchor point of the sprite of the entity.

---
### `this.setText` (*NativeFunction*)

parameters:
- `fontId:int` - ID of the font to use
- `text:string` - Text to set

`returns void`

Set the font and text to display on an entity.

---
### `this.systems.afterUpdate` (*NativeFunction*)

parameters:
- `$function:$function` - ID of the system.
- `callback:callback` - Callback function or function.

`returns void`

Assign a function to be executed after updating this entity.

---
### `this.systems.beforeUpdate` (*NativeFunction*)

parameters:
- `$function:$function` - ID of the system.
- `callback:callback` - Callback function or function.

`returns void`

Assign a function to be executed before updating this entity.

---
### `this.trigger` (*NativeFunction*)

parameters:
- `event:eventDispatch` - The event to dispatch (trigger)

`returns void`

Trigger an event for this entity.

---
### `this.velocity.clear` (*NativeFunction*)

No parameters.


`returns void`

Clear the velocity of the entity.

---
### `this.velocity.set` (*NativeFunction*)

parameters:
- `x:float` - X velocity
- `y:float` - Y velocity

`returns void`

Set the velocity of the entity.

---
### `toFloat` (*NativeFunction*)

parameters:
- `value:int` - The integer to convert to a float

`returns float`

Convert an integer to a float.

---
### `toFloat` (*NativeFunction*)

parameters:
- `value:string` - The string to convert to a float

`returns float`

Convert a string to a float. Returns 0 if the string is not a number.

---
### `toInt` (*NativeFunction*)

parameters:
- `value:string` - The string to convert to an integer

`returns int`

Convert a string to an integer. Returns 0 if the string is not a number.

---
### `toInt` (*NativeFunction*)

parameters:
- `value:float` - The float to convert to an integer

`returns int`

Convert a float to an integer.

---
### `window.clearOnClose` (*NativeFunction*)

No parameters.


`returns void`

Clears the callback registered by window.onClose.

---
### `window.clearOnGainFocus` (*NativeFunction*)

No parameters.


`returns void`

Clears the callback registered by window.onGainFocus.

---
### `window.clearOnLoseFocus` (*NativeFunction*)

No parameters.


`returns void`

Clears the callback registered by window.onLoseFocus.

---
### `window.clearOnMaximize` (*NativeFunction*)

No parameters.


`returns void`

Clears the callback registered by window.onMaximize.

---
### `window.clearOnMinimize` (*NativeFunction*)

No parameters.


`returns void`

Clears the callback registered by window.onMinimize.

---
### `window.clearOnMove` (*NativeFunction*)

No parameters.


`returns void`

Clears the callback registered by window.onMove.

---
### `window.clearOnResize` (*NativeFunction*)

No parameters.


`returns void`

Clears the callback registered by window.onResize.

---
### `window.clearOnRestore` (*NativeFunction*)

No parameters.


`returns void`

Clears the callback registered by window.onRestore.

---
### `window.maximize` (*NativeFunction*)

No parameters.


`returns void`

Maximizes the window.

---
### `window.minimize` (*NativeFunction*)

No parameters.


`returns void`

Minimizes the window.

---
### `window.onClose` (*NativeFunction*)

parameters:
- `callback:callback` - Callback to invoke when the window event occurs.

`returns void`

Sets the callback invoked when the user requests that the window close. The window is not closed automatically.

---
### `window.onGainFocus` (*NativeFunction*)

parameters:
- `callback:callback` - Callback to invoke when the window event occurs.

`returns void`

Sets the callback invoked when the window gains focus.

---
### `window.onLoseFocus` (*NativeFunction*)

parameters:
- `callback:callback` - Callback to invoke when the window event occurs.

`returns void`

Sets the callback invoked when the window loses focus.

---
### `window.onMaximize` (*NativeFunction*)

parameters:
- `callback:callback` - Callback to invoke when the window event occurs.

`returns void`

Sets the callback invoked when the window is maximized.

---
### `window.onMinimize` (*NativeFunction*)

parameters:
- `callback:callback` - Callback to invoke when the window event occurs.

`returns void`

Sets the callback invoked when the window is minimized.

---
### `window.onMove` (*NativeFunction*)

parameters:
- `callback:callback` - Callback to invoke when the window event occurs.

`returns void`

Sets the callback invoked when the window is moved. Receives the x and y position.

---
### `window.onResize` (*NativeFunction*)

parameters:
- `callback:callback` - Callback to invoke when the window event occurs.

`returns void`

Sets the callback invoked when the window is resized. Receives the width and height.

---
### `window.onRestore` (*NativeFunction*)

parameters:
- `callback:callback` - Callback to invoke when the window event occurs.

`returns void`

Sets the callback invoked when the window is restored.

---
### `window.setFps` (*NativeFunction*)

parameters:
- `framesPerSecond:int` - Frames per second.

`returns void`

Sets the frames per seconds (FPS) of the game.

---
### `window.setResolution` (*NativeFunction*)

parameters:
- `resolutionWidth:int` - Resolution width.
- `resolutionHeight:int` - Resolution height.

`returns void`

Sets the resolution of the game window.

---
### `window.setSize` (*NativeFunction*)

parameters:
- `windowWidth:int` - Window width.
- `windowHeight:int` - Window height.

`returns void`

Sets the size of the game window.

---
### `window.setSizeAndCenter` (*NativeFunction*)

parameters:
- `windowWidth:int` - Window width.
- `windowHeight:int` - Window height.

`returns void`

Sets the size of the game window.

---
### `window.setTitle` (*NativeFunction*)

parameters:
- `windowTitle:string` - Title of the game window.

`returns void`

Set the title of the game window.

---
### `window.toggleFullScreen` (*NativeFunction*)

No parameters.


`returns void`

Toggles the window between windowed and full-screen modes.

---

