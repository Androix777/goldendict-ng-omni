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
