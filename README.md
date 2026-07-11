# Changes

I have compiled the code with [Cosmopolitan Libc](https://justine.lol/cosmopolitan/index.html), thus the binary runs on Linux/Windows/macOS/RPi. The binary is _beagle.ape_, rename as you wish. Ported the canonical tinyBenchmarks from Squeak.

On macOS turn off AirPlay, because it uses port 5000. (General - AirDrop & Continuity - AirPlay).

**Note**: Good luck!


# Beagle Smalltalk
Version 1.1

Copyright 2025 Simberon Incorporated

Released under the MIT license
https://opensource.org/license/mit



Beagle Smalltalk is a new implementation of Smalltalk designed to allow people to explore the world of programming.



The "doc" folder contains some beginning information for people wanting to learn about Beagle Smalltalk.



To get this Smalltalk running, you have two choices.

 	1) Download it with executables from https://www.simberon.com/BeagleSmalltalk.html

 	2) Compile it on your own



Here's how you can compile it on your own.



Windows:

 	- Download and install cygwin along with the GCC compiler and make

 	- From the cygwin bash shell, create a directory for BeagleSmalltalk and cd to it

 	- Type make

 	- Run with:   ./beagle.exe beagle.im

 	- Open the file BeagleUI.html in a web browser

 	- To run outside of cygwin, copy cygwin1.dll to a folder called lib



Linux:

 	- Create a directory for BeagleSmalltalk and cd to it

	- Make sure you have gcc c++ and make installed

 	- Type make

 	- Run with:   ./beagle beagle.im

 	- Open the file BeagleUI.html in a web browser

