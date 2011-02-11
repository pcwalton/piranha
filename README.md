This is a simple, incomplete Android profiler. It's especially good for testing
Fennec [1], but it can be used for any Android app.

Building
--------

You'll need the following to build from source.

* The Android SDK, version 8 (version 5 will not work) with the `adb` utility.
* The Android NDK, release 5b or later.
* Objective Caml [2].
* `libcurl`. You probably already have this installed, if you're on Mac or
  Linux.

To build:

1. `$ cd android/core`
2. `$ cp Makefile.config.sample Makefile.config`
3. Edit `Makefile.config` and set the paths in it appropriately.
4. `$ cd ../..`
5. `$ make -C android/core`
6. `$ make -C android/driver`
7. `$ make -C symbolicate`

Alternately, you can skip steps 1-5 with the prebuilt binary on GitHub. Click
on the "Downloads" button in the top right corner of the project page and
download the prebuilt binary [3]. Be warned that the prebuilt binary might be
out of date; if you have trouble, try building from source.

Synopsis
--------

Profile your app:

1. Start your app.
2. `$ ./android/driver/piranha-driver android/core/piranha org.mozilla.fennec` (replace `org.mozilla.fennec` with your app ID as applicable).
3. Perform the action you'd like to profile on your mobile device.
4. Press Return.

Add symbols to your profile:

1. `$ ./symbolicate/piranha-symbolicate profile.ebml profile-syms.ebml`.

Use the web app to examine the results:

1. Open `analyzer/index.html` in your web browser (only tested in Firefox 4 at
   the moment).
2. Click the file chooser, and open your `profile-syms.ebml` file.

[1]: http://www.mozilla.com/en-US/mobile/
[2]: http://caml.inria.fr/
[3]: https://github.com/pcwalton/piranha

