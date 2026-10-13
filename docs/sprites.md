# Sprites

Nomad automatically discovers PNG files in configured resource directories,
packs them into texture pages, and registers them as sprites before the entry
script runs. Games do not need to build atlases ahead of time or call
`game.loadSpriteAtlas`.

## Directory layout and names

By default, Nomad scans `res/sprites` recursively:

```text
res/
└── sprites/
    ├── player/
    │   ├── idle/
    │   │   ├── south.00.png
    │   │   └── south.01.png
    │   └── portrait.png
    └── ui/
        └── heart.png
```

Each sprite name is its resource-relative path with directory separators
replaced by dots and the `.png` extension removed:

| File | Sprite name |
| --- | --- |
| `res/sprites/player/portrait.png` | `sprites.player.portrait` |
| `res/sprites/player/idle/south.00.png` | `sprites.player.idle.south.00` |
| `res/sprites/ui/heart.png` | `sprites.ui.heart` |

Names preserve the case used by files and directories. Use the same case in
scripts. Nomad rejects names that differ only by case because those names
would be ambiguous on case-insensitive file systems.

Assign a registered sprite directly to an entity:

```nomad
this.sprite.name = "sprites.ui.heart"
```

## Animations

Animation frames use the naming convention:

```text
<animation name>.<variant>.<direction>.<frame>
```

For example:

```text
res/sprites/player/idle/south.00.png
res/sprites/player/idle/south.01.png
res/sprites/player/walk/south.00.png
res/sprites/player/walk/south.01.png
```

These files can be used as follows:

```nomad
this.animation.name = "sprites.player"
this.animation.variant = "idle"
this.animation.direction = "south"
this.playAnimation "idle" 8 true false
```

Frame names are sorted alphabetically. Use consistently padded frame numbers
such as `00`, `01`, and `02` so their alphabetical order is also their playback
order.

## Sprite groups and texture pages

The first directory beneath each configured sprite directory defines a logical
sprite group. In the default layout, `sprites/player` and `sprites/ui` are
separate groups. Deeper directories organize names but remain in their
first-level group.

Nomad packs each group independently. If a group does not fit within one
texture, it is split across as many physical pages as necessary. Physical
texture page names use the internal form `<group>#<page>`, such as
`sprites.player#0`; scripts continue to use sprite names and do not need to
refer to pages.

Packing is deterministic and does not rotate images. Nomad trims transparent
edges while preserving each sprite's original logical size and offset. Exact
duplicate images may share packed pixels without sharing sprite names.

## Configuration

Configure sprite discovery in `nomad.toml`:

```toml
[sprites]
directories = [
    "sprites",
    "mod/sprites",
]
max-texture-size = 2048
padding = 1
```

Directories are relative to `resources.directory`. They are scanned
recursively and cannot be absolute, escape the resource directory, overlap, or
duplicate one another. Set `directories = []` to disable automatic sprite
discovery.

`max-texture-size` limits both dimensions of each physical page. `padding` is
the exact transparent distance between adjacent packed images and does not add
padding around a page's outer edge.

See the [project configuration reference](configuration.md) for all validation
rules and defaults.

## Inspect packing

Run the project-aware check from the project directory:

```console
nomad check
```

The report lists discovered source images and each packed texture page,
including its dimensions, logical and unique sprite counts, and occupancy.
The command performs packing on the CPU without opening a window or uploading
textures to the GPU.

Use `nomad check` after adding or reorganizing images to catch malformed PNG
files, duplicate names, invalid directory configuration, and images that
exceed the configured maximum texture size.
