# Why another editor?

Screen editors like vi, vim, nano, micro, and others take over the
entire terminal. Line editors like ed allow you to see terminal history
by scrolling but are difficult to use compared to screen editors. ted is
a screen editor that does not take over your terminal.

# Demo

[ted_demo.webm](https://github.com/balu/ted/assets/101276/905a741e-967e-40e5-b02f-8b8fa4bfd467)

# Usage


Compile

    gcc -o ted ted.c

Run

    ./ted <file>

ted supports many of the emacs bindings. If you are unfamiliar with
these, edit text as normal, use `C-x C-s` to save, and use `C-x C-c` to
quit the editor.

# Keybindings

The following are the motion commands. A *row* is a visible line of
text on screen. A line is an actual line of text in the buffer.

- `C-f` Move the cursor forward by one character.
- `C-b` Move the cursor backward by one character.
- `M-f` Move the cursor forward by one word.
- `M-b` Move the cursor backward by one word.
- `C-M-f` Move the cursor forward by one paragraph.
- `C-M-b` Move the cursor backward by one paragraph.
- `M->` Move the cursor to the end of buffer.
- `M-<` Move the cursor to the beginning of buffer.
- `C-a` Move the cursor to the beginning of row.
- `C-e` Move the cursor to the end of row.
- `M-a` Move the cursor to the beginning of line.
- `M-e` Move the cursor to the end of line.

All motion commands accept a *prefix argument* when it makes sense. A
prefix argument can be provided by typing `C-u <num>`. The command is
then repeated `<num>` times. The following motion commands are mostly
used with a prefix argument.

- `M-g` Go to line.
- `M-%` Go to percentage.

A prefix argument can be given without a `<num>`. Such prefix
arguments act like a Boolean argument.

The following commands manipulate the selection in the buffer.

- `C-<space>` Set mark. The mark is a position in the buffer that is
  independent of point. The text between mark and point is the
  selected text. To deactivate the mark, type `C-g`.
- `C-w` Kill (cut) text.
- `M-w` Kill and save (copy) text.
- `C-y` Yank (paste) text.

You can exchange point and mark using the command `C-x C-x`. The marks
are kept in a mark ring and each `C-<space>` pushes the current
position into it. You can cycle the point through the saved marks
using the command `C-u C-<space>` repeatedly.

The following commands allow conveniently adding new lines of text.

- `C-o` Open a line. Insert a newline at point and put the cursor
  before the newline.
- `M-o` Open next line. Insert a newline at the end of the current
  line and move after it. This is the same as Vim's `o` command.
- `M-O` Open previous line. Insert a newline at the beginning of the
  current line and move before it. This is the same as Vim's `O`
  command.

The command `C-x C-s` saves the buffer.

The following commands quit ted.

- `C-x C-c` Quit with success status. Fail to quit if there are
  unsaved changes. With a prefix argument, save and quit.
- `C-x M-c` Quit with failure status. Used to quit without saving
  changes. Also, used to cancel commits when using git.

The command `C-x =` prints line and column position of the cursor.

The command `C-x C-n` can be used to set the current column as the
*goal column*. The cursor will gravitate towards this column on motion
commands. This is helpful for editing vertically aligned text. With a
prefix argument, it cancels the forced goal column.
