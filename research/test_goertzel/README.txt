# Code from
https://github.com/ericksc/goertzel/blob/master/main.cpp
adapted by ChatGPT.

# Build the app
$ g++ -lm test_goertzel.cpp -o test_goertzel

# generate the audio file with sampling_rate=16000 frequency=440 bit-depth=16 and duration=2
$ ./test_goertzel aa.raw 16000 440 16 2

# play the audio file
$ play -r 16000 -t raw -c 1 -e unsigned -b 16 aa.raw

# Compared with https://www.youtube.com/watch?v=xGXYFJmvIvk : OK

# test with target frequency:
$ ./test_goertzel aa.raw 16000 440 16 
Relative magnitude squared = 490555072.000000


# but it is not good:

$ ./test_goertzel aa.raw 16000 880 16 
Relative magnitude squared = 1014633536.000000

$ ./test_goertzel aa.raw 16000 44 16 
Relative magnitude squared = 1382325632.000000


