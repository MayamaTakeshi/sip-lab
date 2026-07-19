
# sip-lab Documentation

## Overview
A nodejs module that helps to write functional/integration tests for SIP systems (including media operations). It uses pjproject for SIP and media processing.

It permits to:

- make audio calls using UDP, TCP, TLS and WebSocket transports
- send/receive DTMF inband/RFC2833/INFO.
- send/receive BFSK bits.
- play/record audio on a call from/to a wav file
- send/receive fax (T.30 only)
- send/receive MRCPv2 messages (TCP only, no TLS)
- send/receive audio using SRTP
- do speech synth using flite
- do speech recog using pocketsphinx (but only works well with sampling rate of 16000)
- do speech synth/recog using ws_speech_server (this permits to use google/amazon/azure/etc speech services)
  
TODO:

- add support for video playing/recording from/to file
- add support for T.38 fax
- add support for MSRP

## Installation

Last tested distro and node.js version: Debian 11 and node v21.7.3

First install apt packages:

```
apt install libspeex-dev libopus-dev libavdevice-dev libtiff-dev libssl-dev uuid-dev flite-dev
```
Then:
```
npm i sip-lab
```

## Basic Usage

The following example demonstrates how to create two SIP endpoints, make a call between them, and then terminate the call.

```javascript
// This test creates 2 UDP SIP endpoints, makes a call between them and disconeects.

const sip = require ('./index.js')
const Zeq = require('@mayama/zeq')
const m = require('data-matching')
const sip_msg = require('sip-matching')

// here we create our Zeq instance
var z = new Zeq()


async function test() {
    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    console.log(sip.start((data) => { console.log(data)} ))

    const t1 = await sip.transport.create({address: "127.0.0.1"})
    const t2 = await sip.transport.create({address: "127.0.0.1"})

    // make the call from t1 to t2 with some custom heaaders
    const oc = await sip.call.create(t1.id, {
        from_uri: 'sip:alice@test.com',
        to_uri: `sip:bob@${t2.address}:${t2.port}`,
    })

    await z.wait([
        {
            event: "incoming_call",
            call_id: m.collect("call_id"),
            transport_id: t2.id,
            msg: sip_msg({
                $rU: 'bob',
                $fU: 'alice',
            })
        },
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '100',
                $rr: 'Trying',
            }),
        },
    ], 1000)

    const ic = {
        id: z.store.call_id,
        sip_call_id: z.store.sip_call_id,
    }

    sip.call.respond(ic.id, {
        code: 200,
        reason: 'OK',
    })

    await z.wait([
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
            }),
        },
        {
            event: 'media_update',
            call_id: oc.id,
            status: 'ok',
        },
        {
            event: 'media_update',
            call_id: ic.id,
            status: 'ok',
        },
    ], 1000)

    sip.call.terminate(oc.id)

    await z.wait([
        {
            event: 'response',
            call_id: oc.id,
            method: 'BYE',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
            }),
        },
        {
            event: 'call_ended',
            call_id: oc.id,
        },
        {
            event: 'call_ended',
            call_id: ic.id,
        },
    ], 1000)

    console.log("Success")

    await sip.stop()
    process.exit(0)
}


test()
.catch(e => {
    console.error(e)
    process.exit(1)
})

```

Notice that you can specify matching of SIP headers and other elements using opensips/kamailio/openser pseudo-variables syntax (courtesy of [sip-matching](https://github.com/MayamaTakeshi/sip-matching) )

For more examples look into folder samples.

## API Reference

### `sip` Module

#### `start(callback)`

Starts the `sip-lab` module.

- `callback` (optional): A function to be called with log data.

#### `stop(cleanup)`

Stops the `sip-lab` module.

- `cleanup` (optional): A boolean indicating whether to terminate all active calls, registrations, and subscriptions.

#### `set_log_level(level)`

Sets the log level for the module.

- `level`: An integer representing the log level.

#### `set_codecs(codecs)`

Sets the enabled codecs for media streams.

- `codecs`: A string containing a comma-separated list of codec definitions.

#### `dtmf_aggregation_on(timeout)`

Enables DTMF aggregation with a specified timeout. This means instead of firing a 'digits' event every time they are received, we will aggregated them and fire a single event upon timeout.

- `timeout`: The timeout in milliseconds.

#### `disable_telephone_event()`

Disables the telephone-event codec.

### `sip.transport`

#### `async create(options)`

Creates a new SIP transport.

- `options`: An object with the following properties:
  - `address`: The IP address to bind to.
  - `port` (optional): The port to bind to. If not specified, an available port will be used.
  - `type` (optional): The transport type ('udp', 'tcp', or 'tls'). Defaults to 'udp'.
  - `cert_file` (optional): The path to the TLS certificate file.
  - `key_file` (optional): The path to the TLS key file.

### `sip.call`

#### `async create(transport_id, options)`

Creates a new outgoing call.

- `transport_id`: The ID of the transport to use for the call.
- `options`: An object with the following properties:
  - `from_uri`: The From URI for the call.
  - `to_uri`: The To URI for the call.
  - `headers` (optional): An object containing custom SIP headers.
  - `media` (optional): A string or array specifying the media streams to offer.
  - `delayed_media` (optional): A boolean indicating whether to use delayed media negotiation.

#### `respond(call_id, options)`

Responds to an incoming call.

- `call_id`: The ID of the call to respond to.
- `options`: An object with the following properties:
  - `code`: The SIP response code.
  - `reason`: The SIP reason phrase.
  - `headers` (optional): An object containing custom SIP headers.
  - `media` (optional): A string or array specifying the media streams to answer with.

#### `terminate(call_id)`

Terminates a call.

- `call_id`: The ID of the call to terminate.

#### `reinvite(call_id, options)`

Sends a re-INVITE request.

- `call_id`: The ID of the call to re-invite.
- `options` (optional): An object with the following properties:
  - `media` (optional): A string or array specifying the new media streams.

#### `update(call_id, options)`

Sends an UPDATE request.

- `call_id`: The ID of the call to update.
- `options`: An object with the following properties:
  - `headers` (optional): An object containing custom SIP headers.

#### `send_dtmf(call_id, options)`

Sends DTMF tones.

- `call_id`: The ID of the call to send DTMF to.
- `options`: An object with the following properties:
  - `digits`: The DTMF digits to send.
  - `mode`: The DTMF mode (0 for RFC2833, 1 for in-band).

#### `start_inband_dtmf_detection(call_id, options)`

Starts in-band DTMF detection.

- `call_id`: The ID of the call to start detection on.
- `options` (optional): An object with the following properties:
  - `media_id`: The ID of the media stream to start detection on.

#### `start_play_wav(call_id, options)`

Starts playing a WAV file.

- `call_id`: The ID of the call to play the WAV file on.
- `options`: An object with the following properties:
  - `file`: The path to the WAV file.
  - `end_of_file_event` (optional): A boolean indicating whether to emit an `end_of_file` event.
  - `no_loop` (optional): A boolean indicating whether to loop the WAV file.

#### `stop_play_wav(call_id)`

Stops playing a WAV file.

- `call_id`: The ID of the call to stop playing on.

#### `start_record_wav(call_id, options)`

Starts recording audio to a WAV file.

- `call_id`: The ID of the call to record.
- `options`: An object with the following properties:
  - `file`: The path to the WAV file to record to.

#### `stop_record_wav(call_id)`

Stops recording audio.

- `call_id`: The ID of the call to stop recording on.

#### `start_speech_recog(call_id)`

Starts speech recognition.

- `call_id`: The ID of the call to start recognition on.

#### `stop_speech_recog(call_id)`

Stops speech recognition.

- `call_id`: The ID of the call to stop recognition on.

#### `start_speech_synth(call_id, options)`

Starts speech synthesis.

- `call_id`: The ID of the call to start synthesis on.
- `options`: An object with the following properties:
  - `voice`: The voice to use for synthesis.
  - `text`: The text to synthesize.

#### `stop_speech_synth(call_id)`

Stops speech synthesis.

- `call_id`: The ID of the call to stop synthesis on.

#### `start_fax(call_id, options)`

Starts a fax session.

- `call_id`: The ID of the call to start the fax session on.
- `options`: An object with the following properties:
  - `is_sender`: A boolean indicating whether this endpoint is the sender.
  - `file`: The path to the TIFF file to send or receive.
  - `transmit_on_idle`: A boolean indicating whether to transmit on idle.

#### `send_bfsk(call_id, options)`

Sends BFSK bits.

- `call_id`: The ID of the call to send BFSK on.
- `options`: An object with the following properties:
  - `bits`: The bits to send.
  - `freq_zero`: The frequency for the '0' bit.
  - `freq_one`: The frequency for the '1' bit.

#### `start_bfsk_detection(call_id, options)`

Starts BFSK detection.

- `call_id`: The ID of the call to start detection on.
- `options`: An object with the following properties:
  - `freq_zero`: The frequency for the '0' bit.
  - `freq_one`: The frequency for the '1' bit.

#### `send_mrcp_msg(call_id, options)`

Sends an MRCP message.

- `call_id`: The ID of the call to send the message on.
- `options`: An object with the following properties:
  - `msg`: The MRCP message to send.

### `sip.account`

#### `async create(transport_id, options)`

Creates a new SIP account.

- `transport_id`: The ID of the transport to use for the account.
- `options`: An object with the following properties:
  - `domain`: The SIP domain.
  - `server`: The SIP server address.
  - `username`: The username for authentication.
  - `password`: The password for authentication.
  - `headers` (optional): An object containing custom SIP headers.
  - `expires` (optional): The registration expiration time in seconds.

#### `register(account, options)`

Registers a SIP account.

- `account`: The account object to register.
- `options` (optional): An object with the following properties:
  - `auto_refresh`: A boolean indicating whether to automatically refresh the registration.

#### `unregister(account)`

Unregisters a SIP account.

- `account`: The account object to unregister.

### `sip.subscription`

#### `async create(transport_id, options)`

Creates a new SIP subscription.

- `transport_id`: The ID of the transport to use for the subscription.
- `options`: An object with the following properties:
  - `event`: The event to subscribe to.
  - `accept`: The accepted content type.
  - `from_uri`: The From URI for the subscription.
  - `to_uri`: The To URI for the subscription.
  - `request_uri`: The request URI for the subscription.
  - `proxy_uri`: The proxy URI for the subscription.
  - `auth` (optional): An object containing authentication details.

#### `subscribe(subscription, options)`

Subscribes to an event.

- `subscription`: The subscription object.
- `options` (optional): An object with the following properties:
  - `expires`: The subscription expiration time in seconds.

### `sip.request`

#### `async create(transport_id, options)`

Creates a new non-dialog SIP request.

- `transport_id`: The ID of the transport to use for the request.
- `options`: An object with the following properties:
  - `method`: The SIP method for the request.
  - `from_uri`: The From URI for the request.
  - `to_uri`: The To URI for the request.
  - `request_uri`: The request URI for the request.
  - `headers` (optional): An object containing custom SIP headers.

#### `respond(request_id, options)`

Responds to a non-dialog SIP request.

- `request_id`: The ID of the request to respond to.
- `options`: An object with the following properties:
  - `code`: The SIP response code.
  - `reason`: The SIP reason phrase.
  - `headers` (optional): An object containing custom SIP headers.

#### `async sip.stop(cleanup)`

Stops the sip engine

- `cleanup`: true|false : if true performs cleanup by hanging up calls and unregistering sip accounts.

## Events

`sip-lab` emits various events that can be trapped and used for test automation.

- `incoming_call`: Fired when a new call is received.
- `response`: Fired when a SIP response is received.
- `call_ended`: Fired when a call is terminated.
- `media_update`: Fired when the media status of a call is updated.
- `dtmf`: Fired when DTMF tones are received.
- `bfsk`: Fired when BFSK bits are received.
- `fax_result`: Fired when a fax transmission is complete.
- `mrcp_msg`: Fired when an MRCP message is received.
- `speech`: Fired when speech is recognized.
- `speech_synth_complete`: Fired when speech synthesis is complete.
- `end_of_file`: Fired when a WAV file has finished playing.
- `registration_status`: Fired when the status of a registration changes.
- `non_dialog_request`: Fired when a non-dialog SIP request is received.
- `request`: Fired when a dialog-related SIP request is received.
- `reinvite`: Fired when a re-INVITE request is received.
