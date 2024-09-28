const sip = require ('../index.js')
const Zeq = require('@mayama/zeq')
const m = require('data-matching')
const sip_msg = require('sip-matching')
const sdp = require('sdp-matching')

// here we create our Zeq instance
var z = new Zeq()

async function test() {
    sip.dtmf_aggregation_on(500)

    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    // Let's ignore '100 Trying'
    z.add_event_filter({
        event: 'response',
        msg: sip_msg({
            $rs: '100',
        }),
    })

    console.log(sip.start((data) => { console.log(data)} ))

    const t1 = sip.transport.create({address: "127.0.0.1"})
    const t2 = sip.transport.create({address: "127.0.0.1"})

    console.log("t1", t1)
    console.log("t2", t2)

    const oc = sip.call.create(t1.id, {from_uri: 'sip:alice@test.com', to_uri: `sip:bob@${t2.address}:${t2.port}`,
        media: [
            {
                type: 'audio',
                secure: false,
            },
            {
                type: 'audio',
                secure: true,
            },
        ]})

    await z.wait([
        {
            event: "incoming_call",
            call_id: m.collect("call_id"),
            transport_id: t2.id,
        },
    ], 1000)

    const ic = {
        id: z.store.call_id,
        sip_call_id: z.store.sip_call_id,
    }

    sip.call.respond(ic.id, {code: 200, reason: 'OK', media: [
        {
            type: 'audio',
            port: 0, // media refused
        },
        {
            type: 'audio',
            secure: true,
        },
    ]})

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
            media: m.fm([
              m.pm({
                type: 'audio',
                protocol: 'RTP/AVP',
                port: 0,
              }),
              m.pm({
                type: 'audio',
                protocol: 'RTP/SAVP',
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
            call_id: ic.id,
            status: 'ok',
            media: m.fm([
              m.pm({
                type: 'audio',
                protocol: 'RTP/AVP',
                port: 0,
              }),
              m.pm({
                type: 'audio',
                protocol: 'RTP/SAVP',
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

    sip.call.start_inband_dtmf_detection(oc.id, {media_id: 1})
    sip.call.start_inband_dtmf_detection(ic.id, {media_id: 1})

    sip.call.send_dtmf(oc.id, {digits: '1234', mode: 1})
    sip.call.send_dtmf(ic.id, {digits: '1234', mode: 1})

    await z.wait([
        {
            event: 'dtmf',
            call_id: ic.id,
            digits: '1234',
            mode: 1,
            media_id: 1,
        },
        {
            event: 'dtmf',
            call_id: oc.id,
            digits: '1234',
            mode: 1,
            media_id: 1,
        },
    ], 2000)

    sip.call.reinvite(oc.id, {media: [
        {
            type: 'audio',
            secure: false,
        },
        {
            type: 'audio',
            secure: true,
        },
    ]})

    await z.wait([
        {
            event: 'reinvite',
            call_id: ic.id,
        },
    ], 500)

    sip.call.respond(ic.id, {code: 200, reason: 'OK', media: [
        {
            type: 'audio',
            port: 0, // media refused
        },
        {
            type: 'audio',
            secure: true,
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
                    '$.media[*].desc.type': ['audio', 'audio'],
                    '$.media[*].desc.port': ['0', m.nonzero],
                    '$.media[*].desc.protocol': ['RTP/AVP', 'RTP/SAVP'],
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
                protocol: 'RTP/AVP',
                port: 0,
              }),
              m.pm({
                type: 'audio',
                protocol: 'RTP/SAVP',
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
                protocol: 'RTP/AVP',
                port: 0,
              }),
              m.pm({
                type: 'audio',
                protocol: 'RTP/SAVP',
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

    sip.call.send_dtmf(oc.id, {digits: '1234', mode: 1})
    sip.call.send_dtmf(ic.id, {digits: '1234', mode: 1})

    await z.wait([
        {
            event: 'dtmf',
            call_id: ic.id,
            digits: '1234',
            mode: 1,
            media_id: 1,
        },
        {
            event: 'dtmf',
            call_id: oc.id,
            digits: '1234',
            mode: 1,
            media_id: 1,
        },
    ], 2000)


    sip.call.reinvite(ic.id, {media: [
        {
            type: 'audio',
            secure: false,
        },
        {
            type: 'audio',
            secure: true,
        },
    ]})

    await z.wait([
        {
            event: 'reinvite',
            call_id: oc.id,
        },
    ], 500)

    sip.call.respond(oc.id, {code: 200, reason: 'OK', media: [
        {
            type: 'audio',
            port: 0, // media refused
        },
        {
            type: 'audio',
            secure: true,
        },
    ]})

    await z.wait([
        {
            event: 'response',
            call_id: ic.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '200',
                $rb: sdp.jsonpath_matcher({
                    '$.media.length': [2],
                    '$.media[*].desc.type': ['audio', 'audio'],
                    '$.media[*].desc.port': ['0', m.nonzero],
                    '$.media[*].desc.protocol': ['RTP/AVP', 'RTP/SAVP'],
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
                protocol: 'RTP/AVP',
                port: 0,
              }),
              m.pm({
                type: 'audio',
                protocol: 'RTP/SAVP',
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
            call_id: ic.id,
            status: 'ok',
            media: m.fm([
              m.pm({
                type: 'audio',
                protocol: 'RTP/AVP',
                port: 0,
              }),
              m.pm({
                type: 'audio',
                protocol: 'RTP/SAVP',
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

    sip.call.send_dtmf(oc.id, {digits: '1234', mode: 1})
    sip.call.send_dtmf(ic.id, {digits: '1234', mode: 1})

    await z.wait([
        {
            event: 'dtmf',
            call_id: ic.id,
            digits: '1234',
            mode: 1,
            media_id: 1,
        },
        {
            event: 'dtmf',
            call_id: oc.id,
            digits: '1234',
            mode: 1,
            media_id: 1,
        },
    ], 2000)

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

    sip.stop()
    process.exit(0)
}


test()
.catch(e => {
    console.error(e)
    process.exit(1)
})

