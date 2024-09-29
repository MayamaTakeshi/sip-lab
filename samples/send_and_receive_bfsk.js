var sip = require ('../index.js')
var Zeq = require('@mayama/zeq')
var z = new Zeq()
var m = require('data-matching')
var sip_msg = require('sip-matching')
var sdp = require('sdp-matching')

async function test() {
    //sip.set_log_level(9)

    //sip.set_log_level(6)
    sip.dtmf_aggregation_on(500)

    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    //sip.set_codecs("pcmu/8000/1:128,pcma/8000/1:128,gsm/8000/1:128")
    sip.set_codecs("pcma/8000/1:128")

    console.log(sip.start((data) => { console.log(data)} ))

    t1 = sip.transport.create({address: "127.0.0.1", type: 'udp'})
    t2 = sip.transport.create({address: "127.0.0.1", type: 'udp'})

    console.log("t1", t1)
    console.log("t2", t2)

    oc = sip.call.create(t1.id, {from_uri: 'sip:alice@test.com', to_uri: `sip:bob@${t2.address}:${t2.port}`})

    await z.wait([
        {
            event: "incoming_call",
            call_id: m.collect("call_id"),
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

    ic = {
        id: z.store.call_id,
        sip_call_id: z.store.sip_call_id,
    }

    sip.call.respond(ic.id, {code: 200, reason: 'OK'})

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
            media: [
              {
                type: 'audio',
                local: {
                  mode: 'sendrecv'
                },
                remote: {
                  mode: 'sendrecv'
                },
              },
            ],
        },
        {
            event: 'media_update',
            call_id: ic.id,
            status: 'ok',
            media: [
              {
                type: 'audio',
                local: {
                  mode: 'sendrecv'
                },
                remote: {
                  mode: 'sendrecv'
                },
              }
            ],
        },
    ], 1000)

    sip.call.start_record_wav(oc.id, {file: 'oc.wav'})
    sip.call.start_record_wav(ic.id, {file: 'ic.wav'})

    sip.call.start_bfsk_detection(oc.id, {freq_zero: 500, freq_one: 2000})
    sip.call.start_bfsk_detection(ic.id, {freq_zero: 500, freq_one: 2000})

    oc_bits = '1010'
    ic_bits = '1100'

    // wait a little for voice path to open
    await z.sleep(50)

    for(var i=0 ; i<5 ; i++) {
        sip.call.send_bfsk(ic.id, {bits: ic_bits, freq_zero: 500, freq_one: 2000})
        sip.call.send_bfsk(oc.id, {bits: oc_bits, freq_zero: 500, freq_one: 2000})

        await z.wait([
           {
                event: 'bfsk',
                call_id: ic.id,
                bits: oc_bits,
                media_id: 0
           },
           {
                event: 'bfsk',
                call_id: oc.id,
                bits: ic_bits,
                media_id: 0
           },
        ], 10000)
    }

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

    console.log("Success")

    sip.stop()
    process.exit(0)
}


test()
.catch(e => {
    console.error(e)
    process.exit(1)
})

