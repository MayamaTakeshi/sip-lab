// This test creates 2 UDP SIP endpoints, makes a call between them and disconeects.

const sip = require ('../index.js')
const Zeq = require('@mayama/zeq')
const m = require('data-matching')
const sip_msg = require('sip-matching')

// here we create our Zeq instance
var z = new Zeq()


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
    const t1 = sip.transport.create({address: "127.0.0.1"})
    const t2 = sip.transport.create({address: "127.0.0.1"})

    // here we just print the transports
    console.log("t1", t1)
    console.log("t2", t2)

    // make the call from t1 to t2
    const oc = sip.call.create(t1.id, {from_uri: 'sip:alice@test.com', to_uri: `sip:bob@${t2.address}:${t2.port}`})

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
                '$(hdrcnt(via))': 1,
                '$hdr(call-id)': m.collect('sip_call_id'),
                $fU: 'alice',
                $fd: 'test.com',
                $tU: 'bob',
                '$hdr(l)': '0',
            }),
        },
    ], 1000)
    // Details about zeq wait(list_of_events_to_wait_for, timeout_in_ms):
    // The order of events in the list is irrelevant.
    // What matters is that all events arrive within the specified timeout.
    // When specifying events, you can be as detailed or succinct as you need.
    // For example, the above event 'response' is waiting for a SIP '100 Trying' to arrive,
    // but we are specifying things to match just to show that we can be very detailed when performing a match.
    // But it could have been just like this:
    //
    //  {
    //      event: 'response',
    //      call_id: oc.id,
    //      method: 'INVITE',
    //      msg: sip_msg({
    //          $rs: '100',
    //      }),
    //  }
    // Regarding the function sip_msg() this is a special matching function provided by https://github.com/MayamaTakeshi/sip-matching that makes it 
    // easy to match a SIP message using openser/kamailio/opensips pseudo-variables syntax.
 

    // Here we store data for the incoming call
    // just to organize our code (not really needed)
    const ic = {
        id: z.store.call_id,
        sip_call_id: z.store.sip_call_id,
    }

    // Now we answer the call at t2 side
    sip.call.respond(ic.id, {code: 200, reason: 'OK'})

    // Then we wait for the '200 OK' at the t1 side
    // We will also get event 'media_status' for both sides indicating media streams (RTP) were set up successfully
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
                $rb: '!{_}a=sendrecv',
            }),
        },
        {
            event: 'media_status',
            call_id: oc.id,
            status: 'setup_ok',
            local_mode: 'sendrecv',
            remote_mode: 'sendrecv',
        },
        {
            event: 'media_status',
            call_id: ic.id,
            status: 'setup_ok',
            local_mode: 'sendrecv',
            remote_mode: 'sendrecv',
        },
    ], 1000)

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

    console.log("Success")

    sip.stop()
}


test()
.catch(e => {
    console.error(e)
    process.exit(1)
})

