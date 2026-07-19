const sip = require('../index.js')
const Zeq = require('@mayama/zeq')
const m = require('data-matching')
const sip_msg = require('sip-matching')
const assert = require('assert')

var z = new Zeq()

async function test() {
    sip.dtmf_aggregation_on(500)

    z.trap_events(sip.event_source, 'event', (evt) => {
        return evt.args[0]
    })

    sip.set_codecs("opus/48000/2:128,pcmu/8000/1:128")

    console.log(await sip.start((data) => { console.log(data) }))

    // Create a WSS server transport (secure WebSocket listener)
    const t2 = await sip.transport.create({
        address: "127.0.0.1",
        port: 6062,
        type: "wss",
    })

    // Create a WSS client transport connecting to our server
    const t1 = await sip.transport.create({
        address: "127.0.0.1",
        type: "wss",
        ws_url: "wss://127.0.0.1:6062/sip",
    })

    console.log("t1", t1)
    console.log("t2", t2)

    // Make the call from t1 to t2 over Secure WebSocket
    // Use OPUS codec with SRTP and ICE
    const oc = await sip.call.create(t1.id, {
        from_uri: 'sip:alice@test.com',
        to_uri: 'sip:bob@127.0.0.1:6062',
        media: [{type: "audio", secure: true, ice: true}],
    })

    // Wait for the call to arrive at t2 and 100 Trying response at t1
    await z.wait([
        {
            event: "incoming_call",
            call_id: m.collect("call_id"),
            transport_id: t2.id,
            msg: sip_msg({
                $rU: 'bob',
                $fU: 'alice',
                $tU: 'bob',
                $fd: 'test.com',
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
    ], 2000)

    const ic = {
        id: z.$call_id,
        sip_call_id: z.$sip_call_id,
    }

    // Answer the call at t2 side with matching media config
    await sip.call.respond(ic.id, {
        code: 200,
        reason: 'OK',
        media: [{type: "audio", secure: true, ice: true}],
    })

    // Wait for 200 OK at t1 side and media setups
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
    ], 5000)

    await sip.call.start_inband_dtmf_detection(oc.id)
    await sip.call.start_inband_dtmf_detection(ic.id)

    // using 1234 fails frequently as we get things like '12334'
    await sip.call.send_dtmf(oc.id, {digits: '12', mode: 1})
    await sip.call.send_dtmf(ic.id, {digits: '12', mode: 1})

    await z.wait([
        {
            event: 'dtmf',
            call_id: ic.id,
            digits: '12',
            mode: 1,
            media_id: 0,
        },
        {
            event: 'dtmf',
            call_id: oc.id,
            digits: '12',
            mode: 1,
            media_id: 0,
        },
    ], 2000)

    stat1 = JSON.parse(await sip.call.get_stream_stat(oc.id, {media_id: 0}))
    stat2 = JSON.parse(await sip.call.get_stream_stat(ic.id, {media_id: 0}))

    console.log("stat1", stat1)
    console.log("stat2", stat2)

    assert(stat1.CodecInfo == "opus/8000/1")
    assert(stat2.CodecInfo == "opus/8000/1")

    // Terminate the call from t1 side
    await sip.call.terminate(oc.id)

    // Wait for call termination
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
    ], 2000)

    console.log("Secure WebSocket + OPUS + SRTP + ICE test successful")

    await sip.stop()
    process.exit(0)
}

test().catch(e => {
    console.error(e)
    process.exit(1)
})
