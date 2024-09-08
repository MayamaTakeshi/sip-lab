var sip = require ('../index.js')
var Zeq = require('@mayama/zeq')
var z = new Zeq()
var m = require('data-matching')
var sip_msg = require('sip-matching')
var sdp = require('sdp-matching')

async function test() {
    //sip.set_log_level(6)
    sip.dtmf_aggregation_on(500)

    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    console.log(sip.start((data) => { console.log(data)} ))

    t1 = sip.transport.create({address: "127.0.0.1", type: 'udp'})
    t2 = sip.transport.create({address: "127.0.0.1", type: 'udp'})

    console.log("t1", t1)
    console.log("t2", t2)

    oc = sip.call.create(t1.id, {from_uri: 'sip:alice@test.com', to_uri: `sip:bob@${t2.address}:${t2.port}`, delayed_media: true})

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
                $fU: 'alice',
                $fd: 'test.com',
                $tU: 'bob',
                '$hdr(content-type)': 'application/sdp',
                $rb: sdp.matcher({
                    media: m.full_match([
                        m.partial_match({
                            desc: {
                                type: 'audio',
                                port: m.nonzero,
                                protocol: "RTP/AVP",
                            },
                        }),
                    ]),
                })
            }),
        },
        {
            event: 'media_update',
            call_id: oc.id,
            status: 'ok',
            media: m.full_match([
              m.partial_match({
                type: 'audio',
                local: {
                  mode: 'sendrecv'
                },
                remote: {
                  mode: 'sendrecv'
                },
              }),
            ]),
        },
        {
            event: 'media_update',
            call_id: ic.id,
            status: 'ok',
            media: m.full_match([
              m.partial_match({
                type: 'audio',
                local: {
                  mode: 'sendrecv'
                },
                remote: {
                  mode: 'sendrecv'
                },
              }),
            ]),
        },

    ], 1000)

    sip.call.start_inband_dtmf_detection(oc.id)

    sip.call.send_dtmf(oc.id, {digits: '1234', mode: 0})
    sip.call.send_dtmf(ic.id, {digits: '4321', mode: 1})

    await z.wait([
        {
            event: 'dtmf',
            call_id: ic.id,
            digits: '1234',
            mode: 0,
            media_id: 0
        },
        {
            event: 'dtmf',
            call_id: oc.id,
            digits: '4321',
            mode: 1,
            media_id: 0
        },
    ], 1500)

    //await z.sleep(100)
    sip.call.reinvite(oc.id, {delayed_media: true})

    await z.wait([
        {
            event: 'reinvite',
            call_id: ic.id,
            msg: sip_msg({
                $rb: m.absent,
            }),
        },
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '100',
            }),
        },
    ], 500)

    //await z.sleep(100)
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
                /*
                local: {
                  mode: 'recvonly'
                },
                remote: {
                  mode: 'sendrecv'
                },
                */
              }
            ],
        },
        {
            event: 'media_update',
            call_id: ic.id,
            status: 'ok',
            media: [
              {
                type: 'audio',
                /*
                local: {
                  mode: 'sendrecv'
                },
                remote: {
                  mode: 'recvonly'
                },
                */
              }
            ],
        },

    ], 500)

    sip.call.send_dtmf(oc.id, {digits: '1234', mode: 0})
    sip.call.send_dtmf(ic.id, {digits: '4321', mode: 1})

    await z.wait([
        {
            event: 'dtmf',
            call_id: ic.id,
            digits: '1234',
            mode: 0,
            media_id: 0
        },
        {
            event: 'dtmf',
            call_id: oc.id,
            digits: '4321',
            mode: 1,
            media_id: 0
        },
    ], 1500)

    //await z.sleep(100)
    sip.call.reinvite(ic.id, {delayed_media: true})

    await z.wait([
        {
            event: 'reinvite',
            call_id: oc.id,
            msg: sip_msg({
                $rb: m.absent,
            }),
        },
        {
            event: 'response',
            call_id: ic.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '100',
            }),
        },
    ], 500)

    //await z.sleep(100)
    sip.call.respond(oc.id, {code: 200, reason: 'OK'})

    await z.wait([
        {
            event: 'response',
            call_id: ic.id,
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

    ], 500)

    sip.call.send_dtmf(oc.id, {digits: '1234', mode: 0})
    sip.call.send_dtmf(ic.id, {digits: '4321', mode: 1})

    await z.wait([
        {
            event: 'dtmf',
            call_id: ic.id,
            digits: '1234',
            mode: 0,
            media_id: 0
        },
        {
            event: 'dtmf',
            call_id: oc.id,
            digits: '4321',
            mode: 1,
            media_id: 0
        },
    ], 1500)

    //await z.sleep(100)
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
}

test()
.catch(e => {
    console.error(e)
    process.exit(1)
})

