var sip = require ('../index.js')
var z = require('zester')
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

flags = 1 // late negotiation

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

sip.call.respond(ic.id, 200, 'OK')

z.wait([
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
	{
		event: 'response',
		call_id: oc.id,
		method: 'INVITE',
		msg: sip_msg({
			$rs: '200',
			$rr: 'OK',
			'$(hdrcnt(VIA))': 1,
			$fU: 'a',
			$fd: 't',
			$tU: 'b',
			'$hdr(content-type)': 'application/sdp',
			$rb: '!{_}a=sendrecv',
		}),
	},
], 1000)

sip.call.send_dtmf(oc.id, '1234', 0)
sip.call.send_dtmf(ic.id, '4321', 1)

z.wait([
	{
		event: 'dtmf',
		call_id: ic.id,
		digits: '1234',
		mode: 0,
	},
	{
		event: 'dtmf',
		call_id: oc.id,
		digits: '4321',
		mode: 1,
	},
], 2000)


sip.call.reinvite(oc.id, true, flags)

z.wait([
	{
		event: 'response',
		call_id: oc.id,
		method: 'INVITE',
		msg: sip_msg({
			$rs: '200',
			$rr: 'OK',
			$rb: '!{_}a=sendrecv',
		}),
	},
	{
		event: 'media_status',
		call_id: oc.id,
		status: 'setup_ok',
		local_mode: 'sendonly',
		remote_mode: 'sendrecv',
	},
	{
		event: 'media_status',
		call_id: ic.id,
		status: 'setup_ok',
		local_mode: 'recvonly',
		remote_mode: 'sendonly',
	},
], 500)

sip.call.send_dtmf(oc.id, '1234', 0)
sip.call.send_dtmf(ic.id, '4321', 1) // This will not generate event 'dtmf'

z.wait([
	{
		event: 'dtmf',
		call_id: ic.id,
		digits: '1234',
		mode: 0,
	},
], 2000)

sip.call.reinvite(ic.id, false, flags)

z.wait([
	{
		event: 'response',
		call_id: ic.id,
		method: 'INVITE',
		msg: sip_msg({
			$rs: '200',
			$rr: 'OK',
			$rb: '!{_}a=sendonly',
		}),
	},
	{
		event: 'media_status',
		call_id: oc.id,
		status: 'setup_ok',
		local_mode: 'sendonly',
		remote_mode: 'recvonly',
	},
	{
		event: 'media_status',
		call_id: ic.id,
		status: 'setup_ok',
		local_mode: 'recvonly',
		remote_mode: 'sendonly',
	},
], 500)

sip.call.send_dtmf(oc.id, '1234', 0)
sip.call.send_dtmf(ic.id, '4321', 1) // This will not generate event 'dtmf'

z.wait([
	{
		event: 'dtmf',
		call_id: ic.id,
		digits: '1234',
		mode: 0,
	},
], 2000)

sip.call.send_request(oc.id, 'INFO')

z.wait([
	{
		event: 'request',
		call_id: ic.id,
		msg: sip_msg({
			$rm: 'INFO',
		}),
	},
	{
		event: 'response',
		call_id: oc.id,
		method: 'INFO',
		msg: sip_msg({
			$rs: '200',
			$rr: 'OK',
		}),
	},
], 500)

z.sleep(100)

sip.call.reinvite(oc.id, false, flags)

z.wait([
	{
		event: 'response',
		call_id: oc.id,
		method: 'INVITE',
		msg: sip_msg({
			$rs: '200',
			$rr: 'OK',
			$rb: '!{_}a=recvonly',
		}),
	},
	{
		event: 'media_status',
		call_id: oc.id,
		status: 'setup_ok',
		local_mode: 'sendonly',
		remote_mode: 'recvonly',
	},
	{
		event: 'media_status',
		call_id: ic.id,
		status: 'setup_ok',
		local_mode: 'recvonly',
		remote_mode: 'sendonly',
	},
], 500)


sip.call.send_dtmf(oc.id, '1234', 0)
sip.call.send_dtmf(ic.id, '4321', 1) // This will not generate event 'dtmf'

z.wait([
	{
		event: 'dtmf',
		call_id: ic.id,
		digits: '1234',
		mode: 0,
	},
], 2000)


sip.call.terminate(oc.id)

z.wait([
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

z.sleep(1000)

sip.stop()
