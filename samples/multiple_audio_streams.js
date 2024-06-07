const sip = require ('../index.js')
const Zeq = require('@mayama/zeq')
const m = require('data-matching')
const sip_msg = require('sip-matching')
const _ = require('lodash')

// here we create our Zeq instance
var z = new Zeq()

// here we are restricting codec to PCMU to avoid having the SDP to become too large
sip.set_codecs('pcmu/8000/1:128')

// here we asking for accumulated DTMF to be reported in case of no more digits after 500 ms
sip.dtmf_aggregation_on(500)

async function test() {
    // here we set our Zeq instance to trap events generated by sip-lab event_source
    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    // here we start sip-lab
    console.log(sip.start((data) => { console.log(data)} ))

    // Here we create the SIP endpoints (transports).
    // Since we don't specify the port, an available port will be allocated.
    // Since we don't specify the type ('udp' or 'tcp' or 'tls'), 'udp' will be used by default.
    const t1 = sip.transport.create({address: "127.0.0.1", type: 'tcp'})
    const t2 = sip.transport.create({address: "127.0.0.1", type: 'tcp'})

    // here we just print the transports
    console.log("t1", t1)
    console.log("t2", t2)

    const NUM_AUDIO_STREAMS = 8
    const media = new Array(NUM_AUDIO_STREAMS).fill('audio').join(',')

    const oc = sip.call.create(t1.id, {from_uri: 'sip:alice@test.com', to_uri: `sip:bob@${t2.address}:${t2.port}`, media: media})

    // Here we will wait for the call to arrive at t2
    // We will also get a '100 Trying' that is sent by sip-lab automatically
    // We will wait for at most 1000ms. If all events don't arrive within 1000ms, an exception will be thrown and the test will fail due to timeout.
    await z.wait([
        {
            event: "incoming_call",
            call_id: m.collect("call_id"),
            transport_id: t2.id,
        },
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '100',
                $rr: 'Trying',
                '$hdr(call-id)': m.collect('sip_call_id'),
            }),
        },
    ], 1000)

    // Here we store data for the incoming call
    // just to organize our code (not really needed)
    const ic = {
        id: z.store.call_id,
        sip_call_id: z.store.sip_call_id,
    }

    // Now we answer the call at t2 side and accept all 2 streams
    sip.call.respond(ic.id, {code: 200, reason: 'OK', media: media})

    // Then we wait for the '200 OK' at the t1 side
    // We will also get event 'media_update' for both sides indicating media streams (RTP) were set up successfully
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

    sip.call.send_dtmf(oc.id, {digits: '1234', mode: 0})

    await z.wait(_.chain(_.range(NUM_AUDIO_STREAMS)).map(n => ({
        event: 'dtmf',
        call_id: ic.id,
        digits: '1234',
        mode: 0,
        media_id: n,
    })).value(), 3000)

    sip.call.send_dtmf(ic.id, {digits: '4321', mode: 1})

    await z.wait(_.chain(_.range(NUM_AUDIO_STREAMS)).map(n => ({
        event: 'dtmf',
        call_id: oc.id,
        digits: '4321',
        mode: 1,
        media_id: n,
    })).value(), 3000)

    // now we terminate the call from t1 side
    sip.call.terminate(oc.id)

    // and wait for termination events
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

    await z.sleep(100) // wait for any unexpected events

    console.log("Success")

    sip.stop()
}


test()
.catch(e => {
    console.error(e)
    process.exit(1)
})

