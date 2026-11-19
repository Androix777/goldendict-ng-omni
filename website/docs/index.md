GoldenDict-ng is an advanced dictionary lookup program.

## Features

* Supports many dictionary formats, such as MDX, DSL, StarDict, Zim...
* Presents lookup results of multiple dictionaries in the same page
* Supports special types of "dictionaries", such as external program, website, audio files...
* Lots of auxiliary features
    * Popup window
    * Full text search
    * Anki integration
    * Transliteration for some languages
    * Word stemming and spelling correction via Hunspell's morphology analysis 
    * Unicode case, diacritics, punctuation and whitespace folding (e.g. typing 'Grussen' would yield 'grüßen' in German dictionaries).
    * ...

## Local browser viewer

Choose **File > Open Local Browser Viewer** to read the current word in your browser. The viewer listens on `127.0.0.1:18742` while GoldenDict is running. Bookmark the page, or open multiple tabs with different words and groups. The text can be scanned by Yomitan using dictionaries installed in the extension.

The viewer searches enabled local dictionaries in the selected group and supports links between entries, dictionary styles, pictures and audio. Use **File > Stop Local Browser Viewer** to close the listener. Builds with the viewer need the Qt HTTP Server module (`-DWITH_WEB_VIEWER=ON`).

To serve your bookmarked viewer automatically, enable **Preferences > Network > Start the local browser server when GoldenDict starts**. On subsequent launches the server starts after dictionaries and groups are loaded. Your browser opens only when you choose **Open Local Browser Viewer**. Stopping the server from the File menu applies to the current session.

Select **Invert colors** to invert the reader and its illustrations. Your browser remembers this setting across lookups and sessions. The Yomitan popup uses its own colors.

## Local dictionary API

The browser viewer also serves a read-only JSON API at `http://127.0.0.1:18742/api/v1/`. Starting or stopping the viewer controls both interfaces.

| GET endpoint | Response |
| --- | --- |
| `groups` | `groups` array with numeric `id` and `name`, including the built-in All group. |
| `dictionaries` or `dictionaries?group=ID` | `dictionaries` array in group order. Each local dictionary has a string `id`, `name` and group-specific `enabled` flag. |
| `lookup?word=WORD` | The query in `word` and an `articles` array. Use `group=ID` to select a group or `dictionary=ID` to select one enabled dictionary from All. The default is all enabled local dictionaries in All. |

Each article contains `dictionaryId` and `dictionaryName`. A dictionary that reports a lookup failure also has an `error` field in its result.

- `format=html` (default) returns `html`, the UTF-8 fragment produced by the dictionary backend, including dictionary markup and GoldenDict resource links such as `bres://` and `gdau://`.
- `format=text` returns `text` and a `warnings` array for each article. The renderer uses dictionary styles to retain sense numbers, examples, readings and table divisions. Inline images without a label may become `[glyph]` and larger images `[image]`. The warning `primarily-visual` indicates that images dominate the available text. The renderer also reports `image-without-text`, `stylesheet-unavailable` and `empty-text` when applicable.

```powershell
$api = 'http://127.0.0.1:18742/api/v1'
$word = [uri]::EscapeDataString('回')
$result = Invoke-RestMethod "$api/lookup?word=$word&format=text"
$result.articles | Select-Object dictionaryName, text, warnings
```

A word with no matches returns HTTP 200 and an empty `articles` array. Missing or invalid parameters return 400, and an unknown group or unavailable dictionary returns 404. The server can return 503 while reloading its catalog and 504 when a lookup times out. Errors use a JSON object with an `error` message.

GoldenDict renders text in its built-in Qt WebEngine using an isolated, off-screen page. Unrecognized CSS numbering styles use a decimal fallback and report `unsupported-css-counter`. An unsupported generated label expression reports `unsupported-css-content`. Up to two text renders can run concurrently. A busy renderer returns HTTP 503. A render that exceeds two minutes returns HTTP 504.

## Screenshots

On Windows 11 with default theme:

![Windows Theme](img/windows_white.webp){ width="450" }

On macOS with **dark reader mode** and native system dark theme 

![macOS](img/mac_black.webp){ width="450" }

On Linux with custom Qt color theme

![Linux Custom Theme](img/linux_genshin.webp){ width="450" }

## Project history

This project is forked from the original GoldenDict which was developed at <http://goldendict.org/>

The `-ng` in name means "next generation".
