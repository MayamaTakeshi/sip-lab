var sip = require ('../index.js')
var Zester = require('zester')
var z = new Zester()
var m = require('data-matching')
var sip_msg = require('sip-matching')

//sip.set_log_level(6)
sip.dtmf_aggregation_on(500)

z.trap_events(sip.event_source, 'event', (evt) => {
	var e = evt.args[0]
	return e
})

console.log(sip.start((data) => { console.log(data)} ))

t1 = sip.transport.create("127.0.0.1", 5090, 1)
t2 = sip.transport.create("127.0.0.1", 5092, 1)

console.log("t1", t1)
console.log("t2", t2)

flags = 0

oc = sip.call.create(t1.id, flags, 'sip:a@t', 'sip:b@127.0.0.1:5092')

z.wait([
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
			$fU: 'a',
			$fd: 't',
			$tU: 'b',
			'$hdr(l)': '0',
		}),
	},
], 1000)

ic = {
	id: z.store.call_id,
	sip_call_id: z.store.sip_call_id,
}

sip.call.respond(ic.id, 180, 'Ringing')

z.wait([
	{
		event: 'response',
		call_id: oc.id,
		method: 'INVITE',
		msg: sip_msg({
			$rs: '180',
			$rr: 'Ringing',
			'$(hdrcnt(VIA))': 1,
			$fU: 'a',
			$fd: 't',
			$tU: 'b',
		}),
	},
], 1000)

sip.call.terminate(oc.id)

z.wait([
    {
        event: 'request',
        call_id: ic.id,
        msg: sip_msg({
            $rm: 'CANCEL',
        }),
    },
	{
		event: 'response',
		call_id: oc.id,
		method: 'CANCEL',
		msg: sip_msg({
			$rs: '200',
			$rr: 'OK',
		}),
	},
    {
        event: 'response',
        call_id: oc.id,
        method: 'INVITE',
        msg: sip_msg({
            $rs: '487',
            $rr: 'Request Terminated',
        })
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

z.sleep(1000)

sip.stop()
