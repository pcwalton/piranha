This is a simple, incomplete Android profiler. It's especially good for testing
Fennec [1], but it can be used for any Android app.

Building
--------

You'll need the following to build from source. There are too many
source dependencies, unfortunately, but there's not a whole lot we can do here;
Android's build system is gnarly. The OCaml dependencies 

* The Android SDK, with the `adb` utility.
* The Android Open Source Project, built in a place where `agcc` can find it.
* The `agcc` script [1].
* Objective Caml [2].
* `libcurl`. You probably already have this installed, if you're on Mac or
  Linux.

To build:

1. `$ cd android`
2. Edit the `Makefile` to point to your `agcc` location.
3. `$ make`
4. `$ cd ../symbolicate`
5. `$ make`

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

