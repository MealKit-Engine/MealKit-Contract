# The MealKit tool contract

How the MealKit engine, its editor, MealKit Manager and MealKit tools (separate repositories, separate release cycles) find and talk to each other.
They share **files and a local HTTP connection only**: no tool links the engine, and one tool build works with every engine that speaks the same contract version.

This page is the spec. `mkcontract` (this repository) is its implementation in C++; anything else (a script, a tool in another language) follows this page.
Changing the contract means changing this page, the code and the tests in one commit, and bumping a version where the section says so.

| Section | Version that changes | Code |
|---|---|---|
| [Text files](#text-files) | each format's own `@format <kind> <N>` | `mkcontract/Text.h`, `Files.h` |
| [User folder](#user-folder) | none (new files only) | `mkcontract/User.h` |
| [Tool manifest](#tool-manifest) | `@format tool <N>` | `mkcontract/Tool.h` |
| [Live connection](#live-connection) | `live::kProtocol` | `mkcontract/Live.h`, `Client.h`, `Json.h` |
| [Module list](#module-list) | none yet (new columns are appended) | `mkcontract/Modules.h` |
| [Object kinds](#object-kinds) | `@format object-kinds <N>` | none (the engine writes it, MkRender reads it) |

## Text files

Every MealKit text format (engine, editor and tool formats alike):

- UTF-8, `\n` line ends (readers also accept `\r\n` and a leading BOM). Lines are trimmed; empty lines are skipped.
- `#` starts a comment line. Files usually start with a comment saying what the file is and what edits it.
- The **format line** `@format <kind> <N>` is the first line after the comments. `<kind>` names the format (`project`, `tuning`, `settings`, `table`,
  `scene`, `effect`, `ui`, `strings`, `tool`, `desync`, and tool-defined kinds such as `audio-events`), `<N>` is a whole number from 1.
  - No format line means version 1.
  - A different kind, or a version newer than the reader knows: warn, then read the keys it knows.
  - A line starting with `@` that isn't a valid format line: warn and ignore it.
  - Writers always write the latest version. When a format changes, `N` is bumped and readers migrate older versions.
- Settings-like files are `key value` lines: the key ends at the first space, tab or `=`, the value is the rest of the line (trimmed).
  Keys may repeat where a format says so (`toolPath`).
- **List files** hold one item per line (no comments, no format line).

Key names never start with `@`, so the format line can't collide with a key. Line-based files with a fixed key order merge in git.

## User folder

One folder per user, shared by the editor, MealKit Manager and tools:

| OS | Folder |
|---|---|
| Windows | `%LOCALAPPDATA%\MealKit` |
| macOS | `~/Library/Application Support/MealKit` |
| Linux and others | `$XDG_DATA_HOME/MealKit`, else `~/.local/share/MealKit` |

The environment variable `MEALKIT_USER_DIR` replaces the whole folder (tests).

| File | Kind | What |
|---|---|---|
| `recent_projects.txt` | list | project folders, most recently opened first, at most 100 |
| `favorite_projects.txt` | list | project folders |
| `engines.txt` | list | engine folders added by hand in MealKit Manager |
| `editor_settings.txt` | `key value` | editor settings. Others read `language` (`ko`, `en`) and `toolPath <folder>` (repeatable) |
| `tools/<tool>/` | folder | installed tools, each with a [manifest](#tool-manifest) |
| `manager/MealKitManager.exe` | file | installed MealKit Manager (the editor starts it when opened without a project) |

Folders in lists are absolute, with the OS's separators and no trailing separator. Two entries are the same folder if they are equal after
resolving `.`/`..` and symbolic links, ignoring case on Windows. Writers remove the old entry of the same folder before adding one.

## Tool manifest

A MealKit tool is a folder with `mealkit-tool.txt`:

```text
# MealKit tool manifest
@format tool 1
name Audio
exe MealKitAudio.exe
version 0.2.0
protocol 1
opens .events .wav
```

| Key | Required | What |
|---|---|---|
| `name` | yes | shown in menus; also the identity (the first tool found with a name wins) |
| `exe` | yes | the executable, relative to the manifest folder or absolute |
| `protocol` | yes | the [live connection](#live-connection) version the tool speaks. Hosts warn and don't open a tool whose version differs from theirs |
| `version` | no | the tool's own version, shown only |
| `opens` | no | file extensions the tool opens, space-separated |

Unknown keys are reported and ignored.

**Finding tools**, in this order: folders given on the command line (`--tool-path`), `<user folder>/tools`, then each `toolPath` of the editor settings.
A folder that has a manifest is one tool; otherwise each folder directly under it that has a manifest is one (in name order).
Tools are listed by name.

**Opening a tool**: `"<exe>" --project "<project folder>"`, working folder = the manifest folder.
To open one file in a tool whose `opens` lists its extension, hosts add `--file "<asset path>"` (relative to `assets/`, `/` separators, such as `ui/menu.ui`). A tool that takes no file ignores it.

## Live connection

Dev builds of the editor and games (not shipping builds) run a small HTTP/1.1 server on `127.0.0.1`.

**Finding it**: each running server writes `<project>/build/live/<kind>-<pid>.json` and deletes it on exit (`kind` is `editor` or `game`):

```json
{"protocol":1,"kind":"editor","pid":1234,"port":27220,"token":"…32 hex…","project":"sandbox","engine":"0.7.6","url":"http://127.0.0.1:27220/…/"}
```

A file whose process is gone may remain (a crash); a failed connection means the instance is gone. Servers delete stale files of processes that no longer exist when they start.
Several instances of one project (an editor plus game windows) each have their own file.

**Requests**: `http://127.0.0.1:<port>/<token>/<path>`. The random per-run token is only in the info file, so other programs on the machine can't call the server.

| Path | What |
|---|---|
| `GET api/hello` | `{"protocol","kind","project","engine","pid","services":[…],"topics":[…]}` |
| `GET api/events?topics=a,b` | Server-Sent Events: `event: <topic>` + `data: <JSON>` blocks for the listed topics, kept open |
| `* <service>/…` | a feature the host added (`audio/` in games and the editor, `penpot/` in the editor) |

- Request bodies are forms (`application/x-www-form-urlencoded`) unless a service says otherwise; responses are JSON (`{"error":"…"}` with a 4xx/5xx status).
- Requests with an `Origin` header (browsers) are refused unless the service allows that origin, so web pages can't call the local server.
- The host handles requests on its main thread between frames: keep them short.

`live::kProtocol` (currently **1**) is bumped when a path or response shape changes. Tools say which version they speak in their manifest.

### Scene service

The editor adds `scene/` so the MkRender scene editor can use the editor's play view (the editor plays; MkRender only edits files). Requests act on the **saved** scene file: the tool saves first.

| Path | Body | What |
|---|---|---|
| `GET scene/state` | | `{"mode":"edit"\|"play","scene":"scenes/x.scene","dirty":bool,"game":bool,"players":n}` (`scene` relative to `assets/`, `dirty` = the editor's own unsaved edits, `game` = game code loaded) |
| `POST scene/play` | `file`, optional `from=1`, `x`, `y`, `z`, `yaw` (radians, the editor camera's), `spawn` (object id) | opens or reloads `file` (relative to `assets/`) and plays it; `from=1` plays from that camera (solo only): a `spawn` that is a spawn point is used as is, otherwise spawn points move to the floor under the camera |
| `POST scene/stop` | | stops play, or ends a look-through/cutscene preview |
| `POST scene/look` | `file`, `camera` (virtual camera id) or `cutscene` (name) | in edit mode: the editor's view looks through that virtual camera, or previews the cutscene |

Errors are `{"ok":false,"error":"…"}` with 409 (playing, game code not loaded, the editor has unsaved edits of its open scene) or 404 (no such file, camera or cutscene).

## Object kinds

`<project>/build/editor/kinds.txt` describes the scene object kinds of a project, so tools that edit `.scene` files without the engine
(MkRender) show the same kinds, fields and defaults as the editor. The engine writes it (`scene::writeObjectKinds`): the editor each time it
loads the game code, and `<game>_headless --write-kinds` without the editor. It is rewritten only when its text changes. Not in git (under `build/`).

```text
# MealKit object kinds (...)
@format object-kinds 1

kind point_light
label Point light
flags marker cosmetic
field range
type float
default 6
label Range (m)|gizmo=radius

kind JumpPad
label Jump pad
game
field power
type float
default 14
```

`key value` lines in blocks. `kind <name>` starts a kind (its scene file `kind` value); the keys after it belong to the kind until the first
`field`, and the keys after `field <path>` belong to that field until the next `field` or `kind`.

| Key | Of | What |
|---|---|---|
| `kind` | | kind name; built-in kinds come first, then the game's |
| `label` | kind, field | display name. A field label may end with `\|hint` parts (`gizmo=radius`) |
| `game` | kind | declared by the game (`MEALKIT_OBJECT`), not built in |
| `flags` | kind | space-separated: `marker` (position only), `area` (size only, drawn as a box), `cosmetic` (no effect on results) |
| `field` | | field path as written in scene `field` lines (`a.b`, `a[2]`) |
| `type` | field | `bool int uint float enum vec3 quat string seconds color row id text` |
| `default` | field | default value text as in scene files (missing = empty) |
| `enum` | field | value names of an `enum`, space-separated, in order from 0 |
| `flags` | field | `view` (outside the content hash), `readonly` (written by tools, shown not edited) |

Readers ignore unknown keys and types (show the field as text). Without the file a tool shows field lines as text.

## Module list

The engine's `tools\module.ps1 search -Tsv -Project <folder>` prints the project's source modules and the registry list for the editor and MealKit Manager:
one item per line, fields separated by tabs (tabs and line breaks inside fields become spaces), UTF-8.

```text
installed <name> <version> <modified file count> <.new file count> <source> <type> <title> <summary> <tags> <icon>
module    <name> <version for this engine, or -> <newest version> <engine MAJOR.MINOR of that> <title> <summary> <tags (space-separated)> <author> <repo> <icon>
error     <why the registry list couldn't be fetched>
```

`type` is `engine` or `source`; `title`, `summary`, `tags` and `icon` come from the module's `module.txt` (`icon` is one keyword such as `gameplay` or `audio`, blank when the module gives none;
each program picks the picture for a keyword and falls back to its tags). The columns after `<source>` on `installed` lines and `<icon>` on `module` lines came in contract 0.2;
an older engine leaves them out and readers take them as blank.

### Module icons

`icon` keywords, in `kModuleIconKeywords`: `gameplay game timer combat health ai map save ui audio render rhi camera physics net online input data asset scene debug tool editor settings loc feedback live session sim`.
`moduleIconKeyword(icon, tags, name)` picks the one to show: `icon` if it is a keyword, else the first tag that is one (not `engine`), else the name or the part before its `-`, else blank.
Each program maps keywords to its own pictures (the editor and MealKit Manager use Segoe Fluent Icons glyphs, the registry site emoji) and draws a default for blank. New keywords are added at the end.

Readers skip unknown lines and ignore extra fields, so new line kinds and columns can be added at the end.
