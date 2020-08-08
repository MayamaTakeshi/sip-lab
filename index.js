const addon = require('./build/Release/addon.node');

var events = require('events');
var eventEmitter = new events.EventEmitter();


process.on('SIGINT', function() {
	console.log("SIGINT");
	var res = addon.shutdown()
	process.exit(0)
});

addon.event_source = eventEmitter

var timerId = setInterval(() => {
	res = addon.do_poll();
	if(res) {
		//console.log("poll res", res)
		var evt
		var pos = res.indexOf("\n")
		if(pos > 0) {
			var json = res.substring(0, pos)
			evt = JSON.parse(json)
			var msg = res.substring(pos+1)
			if(msg != "") {
				evt.msg = msg
			}
		} else {
			evt = JSON.parse(res)
		}
		eventEmitter.emit("event", evt)
	}
}, 50)

addon.stop = () => {
	clearInterval(timerId)
	var res = addon.shutdown()
	process.exit(0)
}

addon.transport = {
  create: addon.transport_create,
}

addon.account = {
  create: addon.account_create,
  register: addon.account_register,
  unregister: addon.account_unregister,
}

addon.call = {
  create: addon.call_create,
  respond: addon.call_respond,
  terminate: addon.call_terminate,
  send_dtmf: addon.call_send_dtmf,
  reinvite: addon.call_reinvite,
  send_request: addon.call_send_request,
  start_recording: addon.call_start_record_wav,
  start_playing: addon.call_start_play_wav,
  stop_recording: addon.call_stop_record_wav,
  stop_playing: addon.call_stop_play_wav,
  get_stream_stat: addon.call_get_stream_stat,
  refer: addon.call_refer,
  get_info: addon.call_get_info,
  gen_string_replaces: addon.call_gen_string_replaces,
}

module.exports = addon;
