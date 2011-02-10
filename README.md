This is a simple, incomplete Android profiler. It's especially good for testing
Fennec [1], but it can be used for any Android app.

Building
--------

You'll need the following to build from source. There are too many
source dependencies, unfortunately, but there's not a whole lot we can do here;
Android's build system is gnarly. The OCaml dependencies 

* The Android SDK, version 8 (version 5 will not work) with the `adb` utility.
* The Android NDK, release 5b or later.
* Objective Caml [2].
* `libcurl`. You probably already have this installed, if you're on Mac or
  Linux.

To build:

1. `$ cd android/core`
2. `$ cp Makefile.config.sample Makefile.config`
3. Edit `Makefile.config` and set the paths in it appropriately.
4. `$ make`
5. `$ cd ../../symbolicate`
6. `$ make`

Alternately, you can skip steps 1-4 with the prebuilt binary on GitHub. Click
on the "Downloads" button in the top right corner of the project page and
download the prebuilt binary [3].

Synopsis
--------

Profile your app:

1. Start your app.
2. `adb shell`
3. `$ run-as org.mozilla.fennec sh` (replace `org.mozilla.fennec` with your app ID as applicable).
4. `$ cd /tmp`
5. `$ piranha PID` (use `ps` to find your app's PID).
6. Perform the action you'd like to profile on your mobile device.
7. Press Ctrl+C. Wait for Piranha to catch up (this is a bug).
8. `$ exit`
9. `$ exit`

Add symbols to your profile:

1. On your machine, navigate to the `symbolicate` directory, and download your
   profile: `adb pull /tmp/profile.ebml`.
2. `$ ./piranha-symbolicate profile.ebml profile-syms.ebml`.

Use the web app to examine the results:

1. Open `piranha/analyzer/index.html` in your web browser (only tested in
   Firefox 4 at the moment).
2. Click the file chooser, and open your `profile-syms.ebml` file.

[1]: http://www.mozilla.com/en-US/mobile/
[2]: http://caml.inria.fr/
[3]: https://github.com/pcwalton/piranha

