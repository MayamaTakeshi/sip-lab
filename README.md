## sip-lab

Node module that helps to write functional tests for SIP systems (including media operations).
It uses pjproject for SIP and media processing.

This will require for you to have some libraries installed. So do:
```
apt install libsrtp0-dev libspeex-dev libopus-dev libsdl2-dev libavdevice-dev libswscale-dev libv4l-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libopus-dev libsdl2-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libboost-dev libspandsp-dev
```

Then install sip-lab by doing:
```
  npm install sip-lab --save-dev
```

However, since it takes a long time to fetch and build pjproject and the node addon for it, you could install sip-lab globally:
```
  npm install -g sip-lab
```

But if you do so, you will need to set NODE_PATH for node to find it by doing:
```
  export NODE_PATH=$(npm root --quiet -g)
```



