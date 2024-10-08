const sip = require ('../index.js')
const Zeq = require('@mayama/zeq')
const m = require('data-matching')
const sip_msg = require('sip-matching')
const uuid = require('uuid')

const sipjs = require('sipjs-lab')
const {endpoint, dialog, sip_msg: sipjs_sip_msg} = require('sipjs-lab')

// here we create our Zeq instance

var z = new Zeq()

async function test() {
    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    z.trap_events(sipjs.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    sip.set_codecs("pcmu/8000/1:128,pcma/8000/1:128,gsm/8000/1:128")

    console.log(sip.start((data) => { console.log(data)} ))

    const address = "127.0.0.1"

    const e1_port = 7070
    const e1 = endpoint.create({
        address,
        port: e1_port,
        publicAddress: address
    })

    // hack to not lose first INVITE
    await z.sleep(0)

    const t1 = sip.transport.create({address})

    const sip_call_id = uuid.v4()

    var oc = sip.call.create(t1.id, {
        from_uri: 'sip:alice@test.com',
        to_uri: `sip:bob@${address}:${e1_port}`,
        headers: {
            'Call-ID': sip_call_id,
            'Supported': 'timer',
            'Min-SE': '180',
            'Session-Expires': '180;refresher=uac',
        },
    })

    await z.wait([
        {
            source: 'sip_endpoint',
            req: m.collect('req', sipjs_sip_msg({
                $rm: 'INVITE',
                hdr_supported: 'timer',
                hdr_min_se: '180',
                hdr_session_expires: '180;refresher=uac',
            })),
            event: 'dialog_offer',
            dialog_id: m.collect('dialog_id'),
        },
    ], 1000)

    dialog.send_reply(
        z.store.dialog_id,
        z.store.req,
        {
            status: 422,
            reason: 'Session Timer Too Small',
            headers: {
                'Min-SE': '300',
                'Server': 'TBSIP',
            },
        }
    )

    await z.wait([
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '422',
                $rr: 'Session Timer Too Small',
                hdr_min_se: '300',
            }),
        },
        {
            event: 'call_ended',
            call_id: oc.id,
        }
    ], 1000)

    dialog.destroy(z.store.dialog_id)

    delete z.store.dialog_id

    oc = sip.call.create(t1.id, {
        from_uri: 'sip:alice@test.com',
        to_uri: `sip:bob@${address}:${e1_port}`,
        headers: {
            'Call-ID': sip_call_id,
            'Supported': 'timer',
            'Min-SE': '300',
            'Session-Expires': '300;refresher=uac',
        },
    })

    delete z.store.req

    await z.wait([
        {
            source: 'sip_endpoint',
            req: m.collect('req', sipjs_sip_msg({
                $rm: 'INVITE',
                hdr_supported: 'timer',
                hdr_min_se: '300',
                hdr_session_expires: '300;refresher=uac'
            })),
            event: 'dialog_offer',
            dialog_id: m.collect('dialog_id'),
        },
    ], 1000)

     const sdp_answer =`v=0
o=- 3933986675 3933986676 IN IP4 0.0.0.0
s=-
c=IN IP4 ${address}
t=0 0
m=audio 20000 RTP/AVP 0 101
a=sendrecv
a=rtpmap:0 PCMU/8000
a=rtpmap:101 telephone-event/8000
a=fmtp:101 0-15
a=ptime:20`.replace(/\n/g, "\r\n")

    dialog.send_reply(
        z.store.dialog_id,
        z.store.req,
        {
            status: 183,
            reason: 'Session Progress',
            headers: {
                'content-type': 'application/sdp',
            },
            content: sdp_answer,
        }
    )

    await z.wait([
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '183',
                $rr: 'Session Progress',
            }),
        },
        {
            event: 'media_update',
            call_id: oc.id,
            status: 'ok',
        },
    ], 1000)

    dialog.send_reply(
        z.store.dialog_id,
        z.store.req,
        {
            status: 200,
            reason: 'OK',
            headers: {
                'Supported': 'timer',
                'Min-SE': '300',
                'Session-Expires': '300;refresher=uac',
                'content-type': 'application/sdp',
            },
            content: sdp_answer,
        }
    )

    await z.wait([
        {
            event: 'response',
            call_id: oc.id,
            method: 'INVITE',
            msg: sip_msg({
                $rs: '200',
                $rr: 'OK',
                hdr_supported: 'timer',
                hdr_min_se: '300',
                hdr_session_expires: '300;refresher=uac',
            }),
        },
        {
            event: 'media_update',
            call_id: oc.id,
            status: 'ok',
        },
        {
            source: 'sip_endpoint',
            endpoint_id: z.store.endpoint_id,
            req: {
              method: 'ACK',
            },
            event: 'in_dialog_request',
            dialog_id: z.store.dialog_id
        },
    ], 1000)

    for(var i=0 ; i<5 ; i++) {

        // sleep here as much as you like
        await z.sleep(0)

        sip.call.update(oc.id, {
            headers: {
                'Supported': 'timer',
                'Min-SE': '300',
                'Session-Expires': '300;refresher=uac',
            },
        })

        delete z.store.req

        await z.wait([
            {
                source: 'sip_endpoint',
                req: m.collect('req', sipjs_sip_msg({
                    $rm: 'UPDATE',
                    hdr_supported: 'timer',
                    hdr_min_se: '300',
                    hdr_session_expires: '300;refresher=uac'
                })),
                event: 'in_dialog_request',
                dialog_id: z.store.dialog_id
            },
        ], 1000)

        dialog.send_reply(
            z.store.dialog_id,
            z.store.req,
            {
                status: 200,
                reason: 'OK',
                headers: {
                    'Supported': 'timer',
                    'Min-SE': '300',
                    'Session-Expires': '300;refresher=uac',
                },
            }
        )

        await z.wait([
            {
                event: 'response',
                call_id: oc.id,
                method: 'UPDATE',
                msg: sip_msg({
                    $rs: '200',
                    $rr: 'OK',
                    hdr_supported: 'timer',
                    hdr_min_se: '300',
                    hdr_session_expires: '300;refresher=uac',
                }),
            },
        ], 1000)

    }

    sip.call.terminate(oc.id)

    delete z.store.req

    await z.wait([
        {                                                                                                 
            source: 'sip_endpoint',
            req: m.collect('req', sipjs_sip_msg({ 
              $rm: 'BYE',
            })),
        },
    ], 1000)

    dialog.send_reply(
        z.store.dialog_id,
        z.store.req,
        {
            status: 200,
            reason: 'OK',
        }
    )

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
    ], 1000)

    await z.sleep(100) // wait for any unexpected events

    console.log("Success")

    sip.stop()
    process.exit(0)
}

test()
.catch(e => {
    console.error(e)
    process.exit(1)
})

