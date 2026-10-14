# Sound and music

Nomad supports preloaded sound effects and streamed music in WAV, Ogg Vorbis,
MP3, and FLAC formats.

## Resource names

Audio APIs use extensionless dotted resource names. Directory separators become
dots, so these files have the following names:

```text
res/audio/player/jump.ogg -> audio.player.jump
res/music/title/theme.mp3 -> music.title.theme
```

Names must be unique case-insensitively across supported formats. For example,
`music/theme.mp3` and `music/Theme.wav` cannot coexist.

## Sound effects

Sound-effect files are preloaded from the directories configured by
`audio.directories` in `nomad.toml`. The default directory is `audio`.

Load a controllable sound-effect instance with `game.audio.load`:

```nomad
hit = game.audio.load "audio.hit"
hit.volume = 0.5
hit.play
```

An audio instance provides:

- `play`: play once from the beginning.
- `playOnceThen callback`: play once and invoke `callback` after playback
  finishes naturally.
- `playLoop`: play repeatedly.
- `pause`, `resume`, and `stop`: control playback.
- `volume`: instance volume from `0.0` to `1.0`.
- `playing`, `paused`, and `looping`: read-only playback state.

`game.audio.pauseAll`, `game.audio.resumeAll`, and `game.audio.stopAll` affect
sound effects only. They do not affect music.

Starting an instance again or stopping it cancels its pending completion
callback.

## Music

Music files live under the fixed `music` directory and are opened on demand.
Nomad streams them from disk instead of preloading them with sound effects.

```nomad
game.music.volume = 0.6
game.music.play "music.title.theme"
```

`game.music.play` repeats the selected music indefinitely.
`game.music.playOnce` plays it once. Only one music track is active at a time;
starting another resource replaces the current stream.

Use `game.music.playThen name callback` to play a music resource once and
invoke `callback` when it finishes naturally. Stopping or replacing the music
cancels its pending callback.

Use `game.music.pause`, `game.music.resume`, and `game.music.stop` to control
the current track. The read-only `game.music.playing`,
`game.music.paused`, and `game.music.looping` values report its state.

## Volume

All volume values are clamped to the range `0.0` through `1.0`.

- `game.volume` controls the mixer master volume.
- `game.audio.volume` controls the sound-effect category.
- `audio.volume` controls an individual sound-effect instance.
- `game.music.volume` controls the music track.

The effective sound-effect gain is `game.volume * game.audio.volume *
audio.volume`. The effective music gain is `game.volume * game.music.volume`.

## Pausing with the game

Sound effects and music pause and resume with `game.pause` by default. Authors
can control the two categories independently:

```nomad
game.audio.pauseAutomatically = false
game.music.pauseAutomatically = true
```

Only playback paused automatically by the game is resumed by `game.resume`.
Audio that was already paused manually remains paused.
