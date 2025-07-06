
# sip-lab Documentation

## Overview

`sip-lab` is a Node.js module for creating and managing SIP (Session Initiation Protocol) functional tests. It provides a comprehensive set of tools for handling various aspects of SIP, including call control, media operations, and interaction with different media types.

## Features

- **SIP Call Control:**
  - Create and manage SIP calls using UDP, TCP, and TLS transports.
  - Send and receive various SIP requests and responses.
  - Handle call events such as incoming calls, call termination, and session progress.
- **Media Operations:**
  - Send and receive DTMF tones (in-band, RFC2833, INFO).
  - Send and receive BFSK (Binary Frequency-Shift Keying) bits.
  - Play and record audio from/to WAV files.
  - Send and receive faxes (T.30).
  - Send and receive MRCPv2 messages (TCP only).
  - Use SRTP for secure audio communication.
- **Speech Processing:**
  - Perform speech synthesis using Flite.
  - Perform speech recognition using PocketSphinx.
  - Integrate with external speech services like Google, Amazon, and Azure through `ws_speech_server`.

## Installation

To use `sip-lab`, you need to have Node.js and several system dependencies installed.

### Prerequisites

- Node.js (version 15.0.0 or higher)
- Build essentials and various development libraries.

### Installation Steps

1. **Install Dependencies:**
   ```bash
   apt install build-essential automake autoconf libtool libspeex-dev libopus-dev libsdl2-dev libavdevice-dev libswscale-dev libv4l-dev libopencore-amrnb-dev libopencore-amrwb-dev libvo-amrwbenc-dev libboost-dev libtiff-dev libpcap-dev libssl-dev uuid-dev flite-dev cmake git wget
   ```

2. **Install `sip-lab`:**
   ```bash
   npm install sip-lab
   ```

## Basic Usage

The following example demonstrates how to create two SIP endpoints, make a call between them, and then terminate the call.

```javascript
const sip = require('sip-lab');
const Zeq = require('@mayama/zeq');
const m = require('data-matching');
const sip_msg = require('sip-matching');

const z = new Zeq();

async function run() {
  // Trap events from sip-lab
  z.trap_events(sip.event_source, 'event', (evt) => evt.args[0]);

  // Start sip-lab
  sip.start();

  // Create two SIP transports
  const t1 = sip.transport.create({ address: "127.0.0.1" });
  const t2 = sip.transport.create({ address: "127.0.0.1" });

  // Create a call from t1 to t2
  const oc = sip.call.create(t1.id, {
    from_uri: 'sip:alice@test.com',
    to_uri: `sip:bob@${t2.address}:${t2.port}`,
  });

  // Wait for the incoming call on t2
  await z.wait([{
    event: "incoming_call",
    call_id: m.collect("ic_id"),
    transport_id: t2.id,
  }], 1000);

  const ic_id = z.store.ic_id;

  // Answer the call
  sip.call.respond(ic_id, { code: 200, reason: 'OK' });

  // Wait for the 200 OK response and media update
  await z.wait([
    {
      event: 'response',
      call_id: oc.id,
      method: 'INVITE',
      msg: sip_msg({ $rs: '200', $rr: 'OK' }),
    },
    { event: 'media_update', call_id: oc.id, status: 'ok' },
    { event: 'media_update', call_id: ic_id, status: 'ok' },
  ], 1000);

  // Terminate the call
  sip.call.terminate(oc.id);

  // Wait for call termination events
  await z.wait([
    {
      event: 'response',
      call_id: oc.id,
      method: 'BYE',
      msg: sip_msg({ $rs: '200', $rr: 'OK' }),
    },
    { event: 'call_ended', call_id: oc.id },
    { event: 'call_ended', call_id: ic_id },
  ], 1000);

  console.log("Success");

  // Stop sip-lab
  sip.stop();
  process.exit(0);
}

run().catch(e => {
  console.error(e);
  sip.stop();
  process.exit(1);
});
```

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

Enables DTMF aggregation with a specified timeout.

- `timeout`: The timeout in milliseconds.

#### `disable_telephone_event()`

Disables the telephone-event codec.

### `sip.transport`

#### `create(options)`

Creates a new SIP transport.

- `options`: An object with the following properties:
  - `address`: The IP address to bind to.
  - `port` (optional): The port to bind to. If not specified, an available port will be used.
  - `type` (optional): The transport type ('udp', 'tcp', or 'tls'). Defaults to 'udp'.
  - `cert_file` (optional): The path to the TLS certificate file.
  - `key_file` (optional): The path to the TLS key file.

### `sip.call`

#### `create(transport_id, options)`

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

#### `create(transport_id, options)`

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

#### `create(transport_id, options)`

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

#### `create(transport_id, options)`

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
