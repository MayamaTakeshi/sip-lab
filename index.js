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

module.exports = addon;
