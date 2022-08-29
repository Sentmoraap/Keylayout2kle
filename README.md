# Keylayout2kle
Keylayout2kle is a command line tool that generates a json file for [Keyboard Layout Editor](http://www.keyboard-layout-editor.com) from a MacOS keyboard layout. You can use it to generate an image for a layout you want to learn mapped to your keyboard, even a niche ergonomic one or even a custom one.
It’s not aimed specically at MacOS users, but it’s XML format is convenient.

# Setup
I am too lazy to release a binary package so you have to download and compile it.

# Usage
Create a keyboard with Keyboard Layout Editor and dowload the JSON file. An example is provided in the examples folder. The keys you want to be replaced needs to have only a top label with a name starting with #.

Download a MacOS keyboard layout. The bundle folder should have keylayout files, pick one.
You must create a `settings.json` file.

Then execute `keylayout2kle layout.xml kle.json settings.json > out.json` and import the generated `out.json` into Keyboard Layout Editor.

If your layout has dead keys, the output file will have several keyboards, one for each state.

## Keyboard Layout Editor
An example json file is provided.

Keylayout2kle doesn’t check when a color change should be added. Make your keys in another color (in the example, dark grey) to be sure changing their colors does not change the colors of the next keys. The keys you want to be replaced should have only a top-left label.

You can create decals with labels containing the variables `$PAGE`, `$PATH`, `$LEGEND` and `$STATE`. Those will be replaced with information about the displayed state.

Keylayout2kle makes use of KLE’s “custom styles” feature. It’s output contains spans of classes `nongraphic`, `emoji`, `deadkey` (the next state when you press the key once), `deadkey2` (when you press it twice). The index contains classes `legend`, `stateName`, `path` and `pageNumber`. It contains paragraphs with the classes `indexLeft` and `indexRight`.

## Settings json
An example settings json is provided.

`keyMapSet`: which keylayout’s `keyMapSet` to use

`index`: the states list displayed at the top. You can specify `numColumns` and the `width` of one column.

`legends`: which legends to display, at which place, in which color. `place` is the legend position in kle. `index` is the keyMapIndex, 0 and 1 are probably respectively no modifiers and shift. Use `merge[x, y]` to replace legends at places x and y with a x at a new place when they are identical (no `mergeRule`), or the same  (`mergeRule`=`uppercase` or `lowercase`).

`modifers`: used to display a state “path”, i.e. one or several key chords to do to be in that state.

`stateDy`: vertical spacing between states

`states`: which states to display (`show`=`true`(default) or `false`), their names (`display`) and their `legend`.

`substitutions`: replace strings with other strings, for example for displaying the no-breaking space as ⍽.

