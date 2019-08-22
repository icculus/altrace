Quick start:

- This work is funded by my Patreon! If you like it, throw in a buck:

    https://patreon.com/icculus/

- This is all brand new code, if something looks obviously broken it probably
  is. At this time, it definitely works on my computer, so feel free to
  come by and try it at my house if you have problems.

- This works on macOS and Linux (and other Unix systems, probably). Windows
  is currently unsupported (but can be in the future!).

- You'll need CMake ( https://cmake.org/ ) to build. If you want the GUI,
  you'll need wxWidgets ( https://wxwidgets.org/ ). The command line tools
  and trace recorder does not need wxWidgets. I built this with wxWidgets 3.0;
  if you're on macOS, you'll want to use at least 3.1 or Mojave's Dark Mode
  won't work.

- Build the thing with CMake:

   cd altrace
   mkdir cmake-build
   cd cmake-build
   cmake -DCMAKE_BUILD_TYPE=Release ..
   make

- You'll end up with a libaltrace_record.so (or .dylib) file. Take that and
  make your game use it. It's a drop-in replacement for your usual OpenAL
  library, so either link against it directly, or dlopen it, or force it
  to load over the usual OpenAL with LD_PRELOAD.

- When your game runs, alTrace will write out a tracefile (something like
  MyExecutableName.altrace, or *.1.altrace, *.2.altrace, etc). Any time your
  game talks to OpenAL, the details are logged to the tracefile.

- When you're done, quit your game.

- You can see the list of OpenAL calls made by your game and their results
  with the command line tool:

    altrace_cli MyGameName.altrace

- Want to see _everything_ we recorded, including state changes and OpenAL
  errors and such? Try --dump-all ...

    altrace_cli --dump-all MyGameName.altrace

- (there are other command lines to make this less of a firehose, if you only
   want some specific pieces of information. Run altrace_cli with no arguments
   to see the list.)

- Want to _replay_ the tracefile? This will run the same function calls back
  through OpenAL (possibly a different OpenAL implementation, if you're into
  that sort of thing). This is useful if you want to debug OpenAL itself and
  want to ditch all the game logic and get a reproduction case: just run
  altrace_cli under a debugger.  :)   Otherwise, someone can send you a
  tracefile when your game, unrelated to the OpenAL implementation, got into
  a weird state and you want to hear what happened.

    altrace_cli --run MyGameName.altrace

- If you built altrace_wx, you can run that for a GUI that lets you visualize
  the data:

    altrace_wx MyGameName.altrace

- Need more clarity into the data? alTrace exposes an OpenAL extension that
  lets you label objects, annotate the stream of function calls, and group
  sections of calls together, so your app can cooperate to make the
  information more user-friendly.

- Future plans: support for more OpenAL extensions (mostly this is just core
  OpenAL 1.1 right now), Windows support, more features in the GUI, more
  help on tracking down problems, etc.

- Questions? Bug reports? Hit me up: icculus@icculus.org

Thanks!

--ryan.

