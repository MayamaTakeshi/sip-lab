var sip = require ('../index.js')
var Zeq = require('@mayama/zeq')
var z = new Zeq()
var m = require('data-matching')
var sip_msg = require('sip-matching')
var sdp = require('sdp-matching')

function stringToBinary(str) {
  return str.split('').map(char => {
    return char.charCodeAt(0).toString(2).padStart(8, '0');
  }).join('');
}

async function test() {
    //sip.set_log_level(6)
    sip.dtmf_aggregation_on(500)

    sip.set_codecs("PCMU/8000/1:128")

    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    console.log(sip.start((data) => { console.log(data)} ))

    t1 = sip.transport.create({address: "127.0.0.1", type: 'tcp'})
    t2 = sip.transport.create({address: "127.0.0.1", type: 'tcp'})

    console.log("t1", t1)
    console.log("t2", t2)

    oc = sip.call.create(t1.id, {
        from_uri: '"abc"<sip:alice@test.com>',
        to_uri: `sip:bob@${t2.address}:${t2.port}`,
    })

    await z.wait([
        {
            event: "incoming_call",
            call_id: m.collect("call_id"),
            msg: sip_msg({
                $rm: 'INVITE',
                $fU: 'alice',
                $fd: 'test.com',
                $tU: 'bob',
            }),
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

    ic = {
        id: z.store.call_id,
        sip_call_id: z.store.sip_call_id,
    }

    sip.call.respond(ic.id, {
        code: 200,
        reason:'OK',
    })

    await z.wait([
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
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
            }),
        },
    ], 1000)

    sip.call.start_record_wav(oc.id, {file: './oc.wav'})
    sip.call.start_record_wav(ic.id, {file: './ic.wav'})

    sip.call.start_speech_recog(oc.id, {server_url: 'ws://0.0.0.0:8080', engine: 'bfsk-sr', language: '500:2000'})
    sip.call.start_speech_recog(ic.id, {server_url: 'ws://0.0.0.0:8080', engine: 'bfsk-sr', language: '500:2000'})

    await z.sleep(100)

    sip.call.send_bfsk(oc.id, {bits: stringToBinary('abcd'), freq_zero: 500, freq_one: 2000})
    sip.call.send_bfsk(ic.id, {bits: stringToBinary('dcba'), freq_zero: 500, freq_one: 2000})

    await z.wait([
        {
            event: 'speech',
            call_id: oc.id,
            transcript: 'dcba'
        },
        {
            event: 'speech',
            call_id: ic.id,
            transcript: 'abcd'
        },
    ], 2000)

    await z.sleep(1000)

    sip.call.stop_record_wav(oc.id)
    sip.call.stop_record_wav(ic.id)

    sip.call.terminate(oc.id)

    await z.wait([
        {
            event: 'call_ended',
            call_id: oc.id,
        },
        {
            event: 'call_ended',
            call_id: ic.id,
        },
        {
            event: 'response',
            call_id: oc.id,
            method: 'BYE',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
            }),
        },
    ], 1000)

    await z.sleep(100)

    console.log("Success")

    sip.stop()
}


test()
.catch(e => {
    console.error(e)
    process.exit(1)
})

