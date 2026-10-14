# Text and localization

Nomad loads game text from CSV catalogs before compiling scripts. Each text
key becomes a read-only string constant under `t`, allowing scripts and scene
entities to use text without embedding it directly in source files.

## Default catalog

New projects include `res/text/text.csv`:

```csv
key,en
hello,Hello from Nomad!
```

The first column must be named `key`. Each remaining column header is a
language code, and each row supplies the value for one key in those languages:

```csv
key,en,fr
game.title,Example Game,Jeu d'exemple
menu.start,Start,Commencer
menu.quit,Quit,Quitter
```

Keep every row aligned with the header. Quote values that contain commas:

```csv
key,en,fr
welcome,"Welcome, traveler!","Bienvenue, voyageur!"
```

Nomad starts games using the `en` language. The runtime registers constants
for every key in that column.

## Use text in scripts

Prefix a catalog key with `t.` to read its value:

```nomad
this.text.value = t.game.title
```

Keys may contain dots to organize related text:

```csv
key,en,fr
menu.start,Start,Commencer
menu.quit,Quit,Quitter
```

```nomad
this.text.value = t.menu.start
```

Text constants are registered before scripts are compiled and cannot be
assigned new values. A script that references a key missing from the active
language does not compile because its `t.<key>` constant was not registered.

## Configure catalogs

Catalog paths are relative to `resources.directory`. The default is:

```toml
[text]
directories = ["text/text.csv"]
```

Despite the property name, each entry identifies a CSV file rather than a
directory. Load multiple catalogs to organize text by feature:

```toml
[text]
directories = [
    "text/common.csv",
    "text/dialogue.csv",
    "mods/example/text.csv",
]
```

Files are loaded in the listed order. If a later file contains the same
language and key as an earlier file, the later value replaces the earlier
value. This allows expansion or mod catalogs to override base-game text.

Set the list to an empty array to disable text loading:

```toml
[text]
directories = []
```

When the `[text]` table or `directories` property is omitted, Nomad loads
`text/text.csv`.

Configured paths must:

- be non-empty and relative to the resource directory;
- remain inside the resource directory;
- identify `.csv` files; and
- be unique, including case-insensitive duplicates.

See the [project configuration reference](configuration.md#text-directories)
for the complete configuration rules.

## Long-form text files

Use `#` as a CSV value to load that entry from a separate UTF-8 text file.
Nomad resolves the file as `res/text/<key>-<language>.txt`:

```csv
key,en,fr
credits,#,#
```

```text
res/
└── text/
    ├── text.csv
    ├── credits-en.txt
    └── credits-fr.txt
```

This is useful for credits, instructions, dialogue, and other multiline text
that is inconvenient to maintain inside CSV cells. The external file contents
replace the `#` marker when the file exists.

External text files always use the fixed `text` directory, even when the CSV
catalog that references them is configured at another path.

## Language selection

CSV catalogs may contain several language columns, but the standalone Nomad
runtime currently selects `en` during startup. Support for changing languages
at runtime from Nomad scripts is forthcoming. Until then, every catalog used
by a standalone game should provide an `en` value for each key referenced by
its scripts.
