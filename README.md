# Why another editor?

Screen editors like vi, vim, nano, micro, and others take over the
entire terminal. Line editors like ed allow you to see terminal history
by scrolling but are difficult to use compared to screen editors. ted is
a screen editor that does not take over your terminal.

# Usage

Compile

    gcc -o ted ted.c

Run

    ./ted <file>

ted supports many of the emacs bindings. If you are unfamiliar with
these, edit text as normal, use `C-x C-s` to save, and use `C-x C-c` to
quit the editor.
