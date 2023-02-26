# Research

## Overview
Here we keep a file named research.gyp that permits to easily build an app to test code snippets during development.

## Usage
You need to have gyp installed:
```
sudo apt install gyp
```

Then a typical usage would be like this:
```
gyp research.gyp --depth=. # this will build the Makefiles (need to be done only once).

cp tests/test_pjmedia_sdp_parse.c test.c # copy some test file from tests to test.c in the root folder

vim test.c # adjust as necessary for the test you need.

make 
```
The resulting executable will be built as:
```
out/Default/research 
```

## Sample execution:
```
$ out/Default/research 
11:21:16.517         os_core_unix.c !pjlib 2.12-dev for POSIX initialized
sdp:
v=0
o= 0 0   
s=
t=0 0
m=audio 5004 RTP/AVP 0 8
b=AS:128
a=rtpmap:0 PCMU/8000
a=rtpmap:8 PCMA/8000
a=encrypt:1 AES_CM_128_HMAC_SHA1_80 inline:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

## Saving tests
After you are satisfied with the test, commit the test file at tests so that it can guide us in future development.
