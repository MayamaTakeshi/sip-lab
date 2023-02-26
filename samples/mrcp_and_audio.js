var sip = require ('../index.js')
var Zeq = require('@mayama/zeq')
var z = new Zeq()
var m = require('data-matching')
var sip_msg = require('sip-matching')

async function test() {
    sip.set_log_level(9)

    //sip.set_log_level(6)
    sip.dtmf_aggregation_on(500)

    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    console.log(sip.start((data) => { console.log(data)} ))

    var t1 = sip.transport.create({address: "127.0.0.1", port: 5090, type: 'udp'})
    var t2 = sip.transport.create({address: "127.0.0.1", port: 5092, type: 'udp'})

    console.log("t1", t1)
    console.log("t2", t2)

    var client_media = [
        {
            type: 'mrcp', 
            fields: [
                'a=setup:active',
                'a=connection:new',
                'a=resource:speechsynth',
                'a=cmid:1',
            ],
        },
        {
            type: 'audio',
            fields: [
                'a=recvonly',
                'a=mid:1',
            ],
        },
    ]

    var server_media = [
        {
            type: 'mrcp', 
            fields: [
                'a=setup:passive',
                'a=connection:new',
                'a=channel:32AECB234338@speechsynth',
                'a=cmid:1',
            ],
        },        
        {
            type: 'audio',
            fields: [
                'a=sendonly',
            ],
        },
    ]

    oc = sip.call.create(t1.id, {
        from_uri: 'sip:alice@test.com',
        to_uri: `sip:bob@${t2.address}:${t2.port}`,
        media: client_media,
    })

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
                '$(hdrcnt(via))': 1,
                '$hdr(call-id)': m.collect('sip_call_id'),
                $fU: 'alice',
                $fd: 'test.com',
                $tU: 'bob',
                '$hdr(l)': '0',
            }),
        },
    ], 1000)

    ic = {
        id: z.store.call_id,
        sip_call_id: z.store.sip_call_id,
    }

    sip.call.respond(ic.id, {
        code: 200,
        reason: 'OK',
        media: server_media,
    })

    await z.wait([
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
                '$(hdrcnt(VIA))': 1,
                $fU: 'alice',
                $fd: 'test.com',
                $tU: 'bob',
                '$hdr(content-type)': 'application/sdp',
                $rb: '!{_}a=sendonly',
            }),
        },
        {
            event: 'media_update',
            call_id: oc.id,
            status: 'ok',
            media: [
              {
                type: 'mrcp',
                local: {
                  port: 9
                },
                remote: {
                  port: 1000
                },
              },
              {
                type: 'audio',
                local: {
                  port: 10000,
                  mode: 'recvonly'
                },
                remote: {
                  port: 10002,
                  mode: 'sendonly'
                }
              }
            ],
        },
        {
            event: 'media_update',
            call_id: ic.id,
            status: 'ok',
            media: [
              {
                type: 'mrcp',
                local: {
                  port: 1000
                },
                remote: {
                  port: 9
                }
              },
              {
                type: 'audio',
                local: {
                  port: 10002,
                  mode: 'sendonly'
                },
                remote: {
                  port: 10000,
                  mode: 'recvonly'
                }
              }
            ],
        },
    ], 1000)

    sip.call.reinvite(oc.id, {media: client_media})

    await z.wait([
        {
            event: 'reinvite',
            call_id: ic.id,
        },
    ], 500)

    sip.call.respond(ic.id, {code: 200, reason: 'OK', media: server_media})

    await z.wait([
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '100',
            }),
        },
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
                $rb: '!{_}a=sendonly',
            }),
        },
        {
            event: 'media_update',
            call_id: oc.id,
            status: 'ok',
            media: [
              {
                type: 'mrcp',
                local: {
                  port: 9
                },
                remote: {
                  port: 1000
                }
              },
              {
                type: 'audio',
                local: {
                  port: 10000,
                  mode: 'recvonly'
                },
                remote: {
                  port: 10002,
                  mode: 'sendonly'
                }
              }
            ],
        },
        {
            event: 'media_update',
            call_id: ic.id,
            status: 'ok',
            media: [
              {
                type: 'mrcp',
                local: {
                  port: 1000
                },
                remote: {
                  port: 9
                }
              },
              {
                type: 'audio',
                local: {
                  port: 10002,
                  mode: 'sendonly'
                },
                remote: {
                  port: 10000,
                  mode: 'recvonly'
                }
              }
            ],
        },
    ], 500)

    sip.call.reinvite(ic.id, {media: server_media})

    await z.wait([
        {
            event: 'reinvite',
            call_id: oc.id,
        },
    ], 500)

    sip.call.respond(oc.id, {code: 200, reason: 'OK', media: client_media})

    await z.wait([
        {
            event: 'response',
            call_id: ic.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '100',
            }),
        },
        {
            event: 'response',
            call_id: ic.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
                $rb: '!{_}a=recvonly',
            }),
        },
        {
            event: 'media_update',
            call_id: oc.id,
            status: 'ok',
            media: [
              {
                type: 'mrcp',
                local: {
                  port: 9
                },
                remote: {
                  port: 1000
                }
              },
              {
                type: 'audio',
                local: {
                  port: 10000,
                  mode: 'recvonly'
                },
                remote: {
                  port: 10002,
                  mode: 'sendonly'
                }
              }
            ],
        },
        {
            event: 'media_update',
            call_id: ic.id,
            status: 'ok',
            media: [
              {
                type: 'mrcp',
                local: {
                  port: 1000
                },
                remote: {
                  port: 9
                }
              },
              {
                type: 'audio',
                local: {
                  port: 10002,
                  mode: 'sendonly'
                },
                remote: {
                  port: 10000,
                  mode: 'recvonly'
                }
              }
            ],
        },
    ], 500)

    await z.sleep(1000) // we need this delay otherwise, frequently the app will crash after this point.

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

