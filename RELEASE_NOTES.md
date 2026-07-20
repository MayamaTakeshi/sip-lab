### Release Notes

## 1.47.0
  - New Feature: [Add bfsk_aggregation_on](https://github.com/MayamaTakeshi/sip-lab/issues/140)

## 1.45.1
  - Bug Correction: [Correction in bfsk detection by opencode](https://github.com/MayamaTakeshi/sip-lab/issues/138)

## 1.45.0
  - Improvement [Consider running sip-lab as an external process](https://github.com/MayamaTakeshi/sip-lab/issues/55)

    Now we run the the core as an external tcp server and and implies a breaking change as we need to wait for a reply from the server.

Before:

```
    t = sip.transport.create({address: "127.0.0.1", type: 'udp'})
    
    oc = sip.call.create(t1.id, { from_uri: 'sip:alice@test.com', to_uri: `sip:bob@${t2.address}:${t2.port}`}) 	   
    
    a = sip.account.create(t1.id, ...}
    
    s = sip.subscription.create(t1.id, ...}
    
    sip.stop()
```

Now:  

```

    t = await sip.transport.create({address: "127.0.0.1", type: 'udp'})
    
    oc = await sip.call.create(t1.id, { from_uri: 'sip:alice@test.com', to_uri: `sip:bob@${t2.address}:${t2.port}`}) 	   
    
    a = await sip.account.create(t1.id, ...}
    
    s = await sip.subscription.create(t1.id, ...}    
    
    await sip.stop()
```


## 1.41.2
  - Bug Correction: [Corrections in bfsk detection by kiro-cli](https://github.com/MayamaTakeshi/sip-lab/issues/135)

## 1.41.0
  - Moved to NAPI

## 1.40.1
  - New Feature: [Add SIP over WebSocket support](https://github.com/MayamaTakeshi/sip-lab/issues/51)
  - New Feature: [Add support for webrtc](https://github.com/MayamaTakeshi/sip-lab/issues/81)
  - Bug Correction: [codec gsm not working](https://github.com/MayamaTakeshi/sip-lab/issues/127)
  - Bug Correction: [Codec ilbc not working](https://github.com/MayamaTakeshi/sip-lab/issues/90)
  - New Feature: [Permit to set opus params](https://github.com/MayamaTakeshi/sip-lab/issues/126)

## 1.39.0
  - [failure to report mrcp message in case it is sent without delay from the previous one](https://github.com/MayamaTakeshi/sip-lab/issues/125)

#
## 1.38.0
  - Updated @mayama/zeq to v 4.23.0

## 1.34.4
  - Bug Correction: [Issue when trying to send header Max-Forwards](https://github.com/MayamaTakeshi/sip-lab/issues/122)

## 1.34.1
  - Reiussued after docker image rebuid (pjsip recompiled with opus support)

## 1.34.0
  - New Feature: [Enable codec opus](https://github.com/MayamaTakeshi/sip-lab/issues/121)

## 1.33.0
  - New Feature: [Add support for BFSK generation/detection](https://github.com/MayamaTakeshi/sip-lab/issues/108)

## 1.32.0
  - New Feature: [sip.stop should accept parameter clean_up (true|false)](https://github.com/MayamaTakeshi/sip-lab/issues/12)

## 1.31.0
  - New Feature: [Permit to set specific Call-ID when creating call](https://github.com/MayamaTakeshi/sip-lab/issues/111)
  - New Feature: [Implement new function call.update](https://github.com/MayamaTakeshi/sip-lab/issues/112)

