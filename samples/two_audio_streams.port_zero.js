var sip = require ('../index.js')
var Zeq = require('@mayama/zeq')
var z = new Zeq()
var m = require('data-matching')
var sip_msg = require('sip-matching')
var sdp = require('sdp-matching')

async function test() {
    sip.set_log_level(9)

    //sip.set_log_level(6)
    sip.dtmf_aggregation_on(500)

    // Let's ignore '100 Trying'
    z.add_event_filter({
        event: 'response',
        msg: sip_msg({
            $rs: '100',
        }),
    })

    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    console.log(sip.start((data) => { console.log(data)} ))

    t1 = sip.transport.create({address: "127.0.0.1", type: 'udp'})
    t2 = sip.transport.create({address: "127.0.0.1", type: 'udp'})

    console.log("t1", t1)
    console.log("t2", t2)

    oc = sip.call.create(t1.id, {from_uri: 'sip:alice@test.com', to_uri: `sip:bob@${t2.address}:${t2.port}`, media: "audio,audio"})

    await z.wait([
        {
            event: "incoming_call",
            call_id: m.collect("call_id"),
        },
    ], 1000)

    ic = {
        id: z.store.call_id,
        sip_call_id: z.store.sip_call_id,
    }

    sip.call.respond(ic.id, {code: 200, reason: 'OK', media: "audio,audio"})

    await z.wait([
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
                $rb: sdp.jsonpath_matcher({
                    '$.media.length': [2],
                    '$.media[*].desc.type': ['audio','audio'],
                    '$.media[*].desc.port': [m.nonzero, m.nonzero],
                }),
            }),
        },
        {
            event: 'media_update',
            call_id: oc.id,
            status: 'ok',
            media: m.fm([
              m.pm({
                type: 'audio',
                local: {},
              }),
              m.pm({
                type: 'audio',
                local: {},
              }),
            ]),
        },
        {
            event: 'media_update',
            call_id: ic.id,
            status: 'ok',
            media: m.fm([
              m.pm({
                type: 'audio',
                local: {},
              }),
              m.pm({
                type: 'audio',
                local: {},
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
        {
            event: 'dtmf',
            call_id: ic.id,
            digits: '1234',
            mode: 0,
            media_id: 1
        },
        {
            event: 'dtmf',
            call_id: oc.id,
            digits: '4321',
            mode: 1,
            media_id: 1
        },
    ], 2000)

    sip.call.reinvite(oc.id, {media: "audio,audio"})

    await z.wait([
        {
            event: 'reinvite',
            call_id: ic.id,
        },
    ], 1000)

    sip.call.respond(ic.id, {code: 200, reason: 'OK', media: "audio,audio"})

    await z.wait([
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '200',
                $rb: sdp.jsonpath_matcher({
                    '$.media.length': [2],
                    '$.media[*].desc.type': ['audio','audio'],
                    '$.media[*].desc.port': [m.nonzero, m.nonzero],
                }),
            }),
        },
        {
            event: 'media_update',
            call_id: ic.id,
            status: 'ok',
            media: m.fm([
              m.pm({
                type: 'audio',
                local: {
                  mode: 'sendrecv'
                },
                remote: {
                  mode: 'sendrecv'
                },
                fmt: [
                  '0 PCMU/8000',
                  '120 telephone-event/8000'
                ]
              }),
              m.pm({
                type: 'audio',
                local: {
                  mode: 'sendrecv'
                },
                remote: {
                  mode: 'sendrecv'
                },
                fmt: [
                  '0 PCMU/8000',
                  '120 telephone-event/8000'
                ]
              }),
            ]),
        },
        {
            event: 'media_update',
            call_id: oc.id,
            status: 'ok',
            media: m.fm([
              m.pm({
                type: 'audio',
                local: {
                  mode: 'sendrecv'
                },
                remote: {
                  mode: 'sendrecv'
                },
                fmt: [
                  '0 PCMU/8000',
                  '120 telephone-event/8000'
                ]
              }),
              m.pm({
                type: 'audio',
                local: {
                  mode: 'sendrecv'
                },
                remote: {
                  mode: 'sendrecv'
                },
                fmt: [
                  '0 PCMU/8000',
                  '120 telephone-event/8000'
                ]
              }),
            ]),
        },
    ], 1000)

    await z.sleep(100)

    // Now change to single media
    sip.call.reinvite(oc.id, {media: [
        "audio",
        {
            type: "audio",
            port: 0,
        },
    ]})

    await z.wait([
        {
            event: 'reinvite',
            call_id: ic.id,
        },
    ], 1000)

    sip.call.respond(ic.id, {code: 200, reason: 'OK', media: [
        "audio",
        {
            type: "audio",
            port: 0,
        },
    ]})

    await z.wait([
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '200',
                $rb: sdp.jsonpath_matcher({
                    '$.media.length': [2],
                    '$.media[*].desc.type': ['audio','audio'],
                    '$.media[*].desc.port': [m.nonzero, '0'],
                }),
            }),
        },
        {
            event: 'media_update',
            call_id: ic.id,
            status: 'ok',
            media: m.fm([
              m.pm({
                type: 'audio',
                local: {
                  mode: 'sendrecv'
                },
                remote: {
                  mode: 'sendrecv'
                },
                fmt: [
                  '0 PCMU/8000',
                  '120 telephone-event/8000'
                ]
              }),
              m.pm({
                type: 'audio',
                port: 0,
              }),
            ]),
        },
        {
            event: 'media_update',
            call_id: oc.id,
            status: 'ok',
            media: m.fm([
              m.pm({
                type: 'audio',
                local: {
                  mode: 'sendrecv'
                },
                remote: {
                  mode: 'sendrecv'
                },
                fmt: [
                  '0 PCMU/8000',
                  '120 telephone-event/8000'
                ]
              }),
              m.pm({
                type: 'audio',
                port: 0,
              }),
            ]),
        },
    ], 1000)

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

    // now switch back to two media
    sip.call.reinvite(oc.id, {media: "audio,audio"})

    await z.wait([
        {
            event: 'reinvite',
            call_id: ic.id,
        },
    ], 1000)

    sip.call.respond(ic.id, {code: 200, reason: 'OK', media: "audio,audio"})

    await z.wait([
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '200',
                $rb: sdp.jsonpath_matcher({
                    '$.media.length': [2],
                    '$.media[*].desc.type': ['audio','audio'],
                    '$.media[*].desc.port': [m.nonzero, m.nonzero],
                }),
            }),
        },
        {
            event: 'media_update',
            call_id: ic.id,
            status: 'ok',
            media: m.fm([
              m.pm({
                type: 'audio',
                local: {
                  mode: 'sendrecv'
                },
                remote: {
                  mode: 'sendrecv'
                },
                fmt: [
                  '0 PCMU/8000',
                  '120 telephone-event/8000'
                ]
              }),
              m.pm({
                type: 'audio',
                local: {
                  mode: 'sendrecv'
                },
                remote: {
                  mode: 'sendrecv'
                },
                fmt: [
                  '0 PCMU/8000',
                  '120 telephone-event/8000'
                ]
              }),
            ]),
        },
        {
            event: 'media_update',
            call_id: oc.id,
            status: 'ok',
            media: m.fm([
              m.pm({
                type: 'audio',
                local: {
                  mode: 'sendrecv'
                },
                remote: {
                  mode: 'sendrecv'
                },
                fmt: [
                  '0 PCMU/8000',
                  '120 telephone-event/8000'
                ]
              }),
              m.pm({
                type: 'audio',
                local: {
                  mode: 'sendrecv'
                },
                remote: {
                  mode: 'sendrecv'
                },
                fmt: [
                  '0 PCMU/8000',
                  '120 telephone-event/8000'
                ]
              })
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
        {
            event: 'dtmf',
            call_id: ic.id,
            digits: '1234',
            mode: 0,
            media_id: 1
        },
        {
            event: 'dtmf',
            call_id: oc.id,
            digits: '4321',
            mode: 1,
            media_id: 1
        },
    ], 2000)

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

