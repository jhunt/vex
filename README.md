vex - A vim-like Hex Editor
===========================

`vex` is the hex editor I always wanted.  It doesn't so much let
you _edit_ binary files as _explore_ them.  That is, it definitely
doesn't let you change the files, but who needs that featureset?

Usage
-----

Pretty simple, really.  Tell `vex` what file you want to explore:

```
$ vex /bin/ls
```

Movement follows what you're accustomed to as a Vim user:

```
  h,j,k,l    Move one octet in any direction. Arrow keys also work.
  <N>(+-)    Move forward (+) or backward (-) N octets.
  Ctrl-U     Page up, with context where appropriate.
  Ctrl-D     Page down, with context where appropriate.

  <N><m>     Move N octets in any direction, m.  Try 4+ or 2j.
             Doesn't work with arrow keys.
```

Searching (unsurprisingly) also works like vim:

```
   /   Start a forward search.  Type your (currently only ASCII)
       search query, followed by <ENTER>, and vex will look for
       that substring starting from the current position, with
       wrap-around.

       An empty search query repeats the last attempted search.

   ?   Just like '/', except the search proceeds backwards through
       the file, starting at the current position.  Also supports
       wrap-around.

   n   Repeat the last search, forward-ly.  This is faster than
       having to type '/'+<ENTER>.

   N   Repeat the last search, backward-ly.  This is faster than
       having to type '?'+<ENTER>.
```

Other commands:

```
  q     Quit vex.
  r     Reload configuration
```

Configuration
-------------

You can configure vex by supplying a configuration file at one of
the following locations:

  - `$VEXRC`
  - `~/.vexrc`
  - `/etc/vexrc`

The first file found is used, and no other files are consulted.
Standard configuration conventions apply: blank lines and leading
whitespace are ignored, comments start at '#' and continue to the
end of line.

There are only two configuration directives:

**layout ..**

Controls what columns are printed, and how the binary data is
formatted in each column.  For example:

```
# emulate xxd
layout xa

# or, show hex, octal and ascii
layout xoa

# or, prettier hex / octal (author favorite)
layout XxOa
```

The following column types are defined:

```
  x   Simple hexadecimal, i.e. 'de ca fb ad 1d ea'
  X   "Prettier" hex, where '00' octets are replaced
      with '-', to make the non-zero octets stand out.

  o   Simple octal, i.e. ' 75  64 120   1'
  O   "Prettier" octal, where '000' octets are replaced
      with '-', to make the non-zero octets stand out.

  a   ASCII interpretation.  Printable ASCII values
      (code points 27 - 126) are printed as-is; others
      are represented as '.', per standard convention.
```

**status ...**

Controls the display of the status bar.  Each occurrence in the
configuration file appends another line to the status bar.  The
real power in configuring the status bar comes from the format
specifiers, which start with `%`, contain an optional field width,
and end with the format flag.

Currently defined format specificers:

```
  %ud  Interpret the next N bits as an unsigned integer,
       (where N is the field width), in the native machines
       endianness.  Valid field widths (N) are: 8, 16, 32, 64.

  %sd  Interpret the next N bits as a signed integer,
       (where N is the field width), in the native machines
       endianness.  Valid field widths (N) are: 8, 16, 32, 64.

  %x   Print the next N bits in hexadecimal, for cheap and easy
       lookahead.  If there aren't enough octets left in the
       buffer, spaces will be used to pad.

  %b   Print the next N bits as an unsigned integer, in binary
       format.  Valid fields widths (N) are: 8, 16, 32, 64.

  %f   Interpret the next N bits as an IEEE-754 float32 (N=32)
       or float64 (N=64) floating point number, and printed in
       decimal formatting (dd.dddd).

  %T   Interpret the next 32 bits as an unsigned integer, and
       treat that value as a UNIX epoch timestamp (seconds since
       Jan 1 1970 UTC), and print a human-readable date.  Handy
       for finding those embedded timestamps!

  %e   Interpret the next N bits as an IEEE-754 float32(N=32)
       or float64 (N=64) floating point number, and printed in
       scientific notation (d.dddddeD).

  %lz  Interpret the next N bits as an unsigned integer, and
       count the number of leading zero bits.  Valid values for
       N are 8, 16, 32, and 64.

  %tz  Interpret the next N bits as an unsigned integer, and
       count the number of trailing zero bits.  Valid values for
       N are 8, 16, 32, and 64.

  %p   Interpret the next N bits as an unsigned integer, and
       count the number of bits that are set.  Valid values for
       N are 8, 16, 32, and 64.

  %E   Print the host platform endianness.  Field width controls
       the exact value printed.  N=1 yields 'L' (little endian)
       or 'B' (big endian).  N=2 yields 'LE' or 'BE', N=3 gives
       'lil' or 'big', and no field width prints the string
       'little endian' or 'big endian'

  %o   Print the offset of the octet under the cursor, from the
       begining of the file, in decimal notation.

  %l   Print the length of the file, in decimal notation.

  %F   Print the file name, without any directory components.

  %P   Print the unmodified path to the file.  This depends
       specifically on what has been given to the vex binary
       as a file argument.
```

Each additional `status` directive adds a new line to the status
bar.  There is currently no way to affect alignment of the text in
the status bar, but pull requests are welcome.

Contributing
------------

I wrote this tool because I needed it, and no one else had written
it.  My hope is that you find it useful as well.  If it's close,
but not 100% there, why not fork it, fix what needs fixing, and
submit a pull request?

Happy Hacking!
