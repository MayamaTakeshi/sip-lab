// This test creates 100 caller UDP SIP endpoints, 100 callee UDP SIP endpoines and makes one call between them, test dtmf and disconnects.

const sip = require ('../index.js')
const Zeq = require('@mayama/zeq')
const m = require('data-matching')
const sip_msg = require('sip-matching')
const _ = require('lodash')

const NUMBER_OF_CALLS = 100

var z = new Zeq()

sip.dtmf_aggregation_on(500)

z.add_event_filter({
    event: 'response',
    msg: sip_msg({
        $rs: '100',
    }),
})

async function test() {
    z.trap_events(sip.event_source, 'event', (evt) => {
        var e = evt.args[0]
        return e
    })

    console.log(sip.start((data) => { console.log(data)} ))

    const caller_ts = []
    const callee_ts = []

    const ocs = []

    for(var i=0 ; i<NUMBER_OF_CALLS ; i++) {   
        const caller_t = sip.transport.create({address: "127.0.0.1"})
        caller_ts.push(caller_t)
        const callee_t = sip.transport.create({address: "127.0.0.1"})
        callee_ts.push(callee_t)

        // make the call 
        const oc = sip.call.create(caller_t.id, {from_uri: 'sip:alice@test.com', to_uri: `sip:bob@${callee_t.address}:${callee_t.port}`})
        ocs.push(oc)
    }

    // Here we will wait for the calls to arrive
    await z.wait(_.chain(callee_ts).map(t => ({
        event: "incoming_call",
        call_id: m.push("ic_ids"),
        transport_id: t.id,
    })).value(), 50000)

    // Now we answer the calls
    z.store.ic_ids.forEach(ic_id => { 
        sip.call.respond(ic_id, {code: 200, reason: 'OK'})
    })

    // Then we wait for the '200 OK' at the caller side
    var events = _.chain(ocs).map(oc => ({
        event: 'response',
        call_id: oc.id,
        method: 'INVITE',
        msg: sip_msg({
            $rs: '200',
            $rr: 'OK',
        }),
    })).value()

    events = events.concat(_.chain(ocs).map(oc => ({
        event: 'media_update',
        call_id: oc.id,
        status: 'ok',
    })).value())
    
    events = events.concat(_.chain(z.store.ic_ids).map(ic_id => ({
        event: 'media_update',
        call_id: ic_id,
        status: 'ok',
    })).value())

    await z.wait(events, 50000)

    ocs.forEach(oc => {
        sip.call.start_inband_dtmf_detection(oc.id)
    })

    // first send RFC2833 DTMF digits
    ocs.forEach(oc => {
        sip.call.send_dtmf(oc.id, {digits: '1234', mode: 0})
    })

    await z.wait(_.chain(z.store.ic_ids).map(ic_id => ({
        event: 'dtmf',
        call_id: ic_id,
        digits: '1234',
        mode: 0,
        media_id: 0,
    })).value(), 50000)

    // now send inband DTMF digits
    z.store.ic_ids.forEach(ic_id => {
        sip.call.send_dtmf(ic_id, {digits: '4321', mode: 1})
    })

    await z.wait(_.chain(ocs).map(oc => ({
        event: 'dtmf',
        call_id: oc.id,
        digits: '4321',
        mode: 1,
        media_id: 0,
    })).value(), 50000)

    // now we terminate the calls
    ocs.forEach(oc => {
        sip.call.terminate(oc.id)
    })

    // and wait for termination events
    events = _.chain(ocs).map(oc => ({
        event: 'response',
        call_id: oc.id,
        method: 'BYE',
        msg: sip_msg({
            $rs: '200',
            $rr: 'OK',
        }),
    })).value()

    events = events.concat(_.chain(ocs).map(oc => ({
        event: 'call_ended',
        call_id: oc.id,
    })).value())

    events = events.concat(_.chain(z.store.ic_ids).map(ic_id => ({
        event: 'call_ended',
        call_id: ic_id,
    })).value())

    await z.wait(events, 50000)

    console.log("Success")

    sip.stop()
    process.exit(0)
}


test()
.catch(e => {
    console.error(e)
    process.exit(1)
})

