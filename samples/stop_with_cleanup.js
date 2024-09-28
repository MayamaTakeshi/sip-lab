const sip = require ('../index.js')
const Zeq = require('@mayama/zeq')
const m = require('data-matching')
const sip_msg = require('sip-matching')

// here we create our Zeq instance
var z = new Zeq()


async function test() {
    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    console.log(sip.start((data) => { console.log(data)} ))

    const t1 = sip.transport.create({address: "127.0.0.1"})
    const t2 = sip.transport.create({address: "127.0.0.1"})

    const oc = sip.call.create(t1.id, {
        from_uri: 'sip:alice@test.com',
        to_uri: `sip:bob@${t2.address}:${t2.port}`,
        headers: {
            'X-MyHeader1': 'abc',
            'X-MyHeader2': 'def',
        },
    })

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
    ], 1000)

    const ic = {
        id: z.store.call_id,
        sip_call_id: z.store.sip_call_id,
    }

    sip.call.respond(ic.id, {
        code: 200,
        reason: 'OK',
    })

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

    // Now force an error in the script.
    // This will be catched by exception handler at the end of the script where
    // sip.stop(true) will be called (true: terminate all remaining calls, registrations and subscriptions)

    throw "SOME ERROR"

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
    process.exit(0)
}


test()
.catch(e => {
    console.error(e)
    sip.stop(true)

    if(e == "SOME ERROR") {
        console.log("Expected error catched")
        process.exit(0)
    } else {
        console.log("Unexpected error catched")
        process.exit(1)
    }
})

