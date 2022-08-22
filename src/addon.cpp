#define NAPI_EXPERIMENTAL /* for napi_threadsafe functions */

#include <napi.h>
#include "sip.hpp"
#include <string>

//#include <boost/thread.hpp>
//#include <boost/chrono.hpp>
//#include <iostream>

using namespace std;

static napi_threadsafe_function tsf;

/*
void wait(int ms)
{
  boost::this_thread::sleep_for(boost::chrono::milliseconds{ms});
}

static pj_thread_desc poll_thread_descriptor;
static pj_thread_t *poll_thread = NULL;

void poll()
{
  pj_status_t status = pj_thread_register("main_thread", poll_thread_descriptor, &poll_thread);
  if(status != PJ_SUCCESS)
  {
    printf("pj_thread_register(poll_thread) failed\n");
    exit(1);
  }

  char buf[4096];
  while(true) {
    wait(50);
    int res = __pjw_poll(buf);
    if(res == 0) {
      if(napi_call_threadsafe_function(tsf, buf, napi_tsfn_blocking) != napi_ok) {
         printf("napi_call_threadsafe_function falied\n");
      }
      //printf("js land called\n");
    }
  } 
}
*/


Napi::Value transport_create(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Wrong number of arguments").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsString()) {
    Napi::TypeError::New(env, "Wrong argument type: sip_ipaddr must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }

  const string sip_ipaddr = info[0].As<Napi::String>().Utf8Value();

  int port = 0;

  if (info.Length() > 1) {
    if(!info[1].IsNumber()) {
      Napi::TypeError::New(env, "Wrong argument type: port must be number").ThrowAsJavaScriptException();
      return env.Null();
    }
    port = info[1].As<Napi::Number>().Int32Value();
  }

  int type = PJSIP_TRANSPORT_UDP;

  if (info.Length() > 2) {
    if(!info[2].IsNumber()) {
      Napi::TypeError::New(env, "Wrong argument type: transport type must be number").ThrowAsJavaScriptException();
      return env.Null();
    }
    type = info[2].As<Napi::Number>().Int32Value();
  }

  int out_t_id;
  int out_t_port;
  int res = pjw_transport_create(sip_ipaddr.c_str(), port, (pjsip_transport_type_e)type, &out_t_id, &out_t_port);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Object obj = Napi::Object::New(env);
  obj.Set(Napi::String::New(env, "id"), Napi::Number::New(env, out_t_id));
  obj.Set(Napi::String::New(env, "ip"), info[0].ToString());
  obj.Set(Napi::String::New(env, "port"), Napi::Number::New(env, out_t_port));

  return obj;
}


Napi::Value account_create(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 5) {
    Napi::TypeError::New(env, "Wrong number of arguments. Expected: transport_id, domain, server, user, pass [, additional_headers, c_to_url]").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "Wrong argument type: transport_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int transport_id = info[0].As<Napi::Number>().Int32Value();
 
  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "Wrong argument type: domain must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  const string domain = info[1].As<Napi::String>().Utf8Value();

  if (!info[2].IsString()) {
    Napi::TypeError::New(env, "Wrong argument type: server must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  const string server = info[2].As<Napi::String>().Utf8Value();

  if (!info[3].IsString()) {
    Napi::TypeError::New(env, "Wrong argument type: user must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  const string user = info[3].As<Napi::String>().Utf8Value();

  if (!info[4].IsString()) {
    Napi::TypeError::New(env, "Wrong argument type: password must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  const string pass = info[4].As<Napi::String>().Utf8Value();

  string additional_headers = string("");

  if (info.Length() > 5) {
	  if (!info[5].IsString()) {
	    Napi::TypeError::New(env, "Wrong argument type: additional_headers must be string.").ThrowAsJavaScriptException();
	    return env.Null();
	  }
	  additional_headers = info[5].As<Napi::String>().Utf8Value();
  }

  string c_to_url = string("");

  if (info.Length() > 6) {
	  if (!info[6].IsString()) {
	    Napi::TypeError::New(env, "Wrong argument type: c_to_url must be string.").ThrowAsJavaScriptException();
	    return env.Null();
	  }
	  c_to_url = info[6].As<Napi::String>().Utf8Value().c_str();
  }

  int expires = 60;

  if (info.Length() > 7) {
    if (!info[7].IsNumber()) {
      Napi::TypeError::New(env, "Wrong argument type: expires must be number.").ThrowAsJavaScriptException();
	  return env.Null();
    }
    expires = info[7].As<Napi::Number>().Int32Value();
  }

  int out_a_id;
  int res = pjw_account_create(transport_id, domain.c_str(), server.c_str(), user.c_str(), pass.c_str(), additional_headers[0] ? additional_headers.c_str() : NULL, c_to_url[0] ? c_to_url.c_str() : NULL, expires, &out_a_id);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::Number::New(env, out_a_id);
}


Napi::Value account_register(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Wrong number of arguments. Expected: account_id [, auto_register]").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "Wrong argument type: account_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int account_id = info[0].As<Napi::Number>().Int32Value();

  bool auto_register = false;

  if (info.Length() > 1) {
    if(!info[1].IsBoolean()) {
      Napi::TypeError::New(env, "Wrong argument type: auto_register must be boolean").ThrowAsJavaScriptException();
      return env.Null();
    }
    auto_register = info[1].As<Napi::Boolean>().Value();
  }

  int res = pjw_account_register(account_id, auto_register);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value account_unregister(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::TypeError::New(env, "Wrong number of arguments. Expected: account_id").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "Wrong argument type: account_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int account_id = info[0].As<Napi::Number>().Int32Value();

  int res = pjw_account_unregister(account_id);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}


Napi::Value call_create(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 4) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: transport_id, flags, from_uri, to_uri [, request_uri, proxy_uri, additional_headers, realm, user, pass]").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "transport_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int transport_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "flags must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  unsigned flags = info[1].As<Napi::Number>().Uint32Value();
 
  if (!info[2].IsString()) {
    Napi::TypeError::New(env, "from_uri must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  const string from_uri = info[2].As<Napi::String>().Utf8Value();

  if (!info[3].IsString()) {
    Napi::TypeError::New(env, "to_uri must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  const string to_uri = info[3].As<Napi::String>().Utf8Value();

  string request_uri = string("");

  if (info.Length() > 4) {
    if (!info[3].IsString()) {
      Napi::TypeError::New(env, "request_uri must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    request_uri = info[4].As<Napi::String>().Utf8Value();
  } else {
    request_uri = to_uri; // TODO: remove angle brackets if present
  }

  string proxy_uri = string("");

  if (info.Length() > 5) {
	  if (!info[5].IsString()) {
	    Napi::TypeError::New(env, "proxy_uri must be string.").ThrowAsJavaScriptException();
	    return env.Null();
	  }
	  proxy_uri = info[5].As<Napi::String>().Utf8Value();
  }

  string additional_headers = string("");

  if (info.Length() > 6) {
    if (!info[5].IsString()) {
      Napi::TypeError::New(env, "additional_headers must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    additional_headers = info[5].As<Napi::String>().Utf8Value();
  }

  string c_to_url = string("");

  string realm = string("");
  string user = string("");
  string pass = string("");

  if (info.Length() > 7) {
    if(info.Length() < 10) {
      Napi::Error::New(env, "incomplete credentials arguments: you must provide realm, user and pass").ThrowAsJavaScriptException();
      return env.Null();
    }

    if (!info[7].IsString()) {
      Napi::TypeError::New(env, "realm must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    realm = info[7].As<Napi::String>().Utf8Value().c_str();

    if (!info[8].IsString()) {
      Napi::TypeError::New(env, "user must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    user = info[8].As<Napi::String>().Utf8Value().c_str();

    if (!info[9].IsString()) {
      Napi::TypeError::New(env, "pass must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    pass = info[9].As<Napi::String>().Utf8Value().c_str();
  }

  long int out_call_id;
  char out_sip_call_id[256];

  int res = pjw_call_create(transport_id, flags, from_uri.c_str(), to_uri.c_str(), request_uri.c_str(), proxy_uri[0] ? proxy_uri.c_str() : NULL, additional_headers[0] ? additional_headers.c_str() : NULL, realm[0] ? realm.c_str() : NULL, user[0] ? user.c_str() : NULL, pass[0] ? pass.c_str() : NULL, &out_call_id, out_sip_call_id);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Object obj = Napi::Object::New(env);
  obj.Set(Napi::String::New(env, "id"), Napi::Number::New(env, out_call_id));
  obj.Set(Napi::String::New(env, "sip_call_id"), out_sip_call_id);

  return obj;
}

Napi::Value call_respond(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 3) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id, code, reason [, additional_headers]").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();
 
  if (!info[1].IsNumber()) {
    Napi::TypeError::New(env, "code must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int code = info[1].As<Napi::Number>().Int32Value();

  if (!info[2].IsString()) {
    Napi::TypeError::New(env, "reason must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  const string reason = info[2].As<Napi::String>().Utf8Value();

  string additional_headers = string("");

  if (info.Length() > 3) {
    if (!info[3].IsString()) {
      Napi::TypeError::New(env, "additional_headers must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    additional_headers = info[3].As<Napi::String>().Utf8Value();
  }

  int res = pjw_call_respond(call_id, code, reason.c_str(), additional_headers[0] ? additional_headers.c_str() : NULL);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_terminate(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id [, code, reason, additional_headers]").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();
 
  int code = 0;
  if (info.Length() > 1) {
    if (!info[1].IsNumber()) {
      Napi::TypeError::New(env, "code must be number.").ThrowAsJavaScriptException();
      return env.Null();
    }
    code = info[1].As<Napi::Number>().Int32Value();
  }

  string reason = string("");

  if (info.Length() > 2) {
    if (!info[2].IsString()) {
      Napi::TypeError::New(env, "reason must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    reason = info[2].As<Napi::String>().Utf8Value();
  }

  string additional_headers = string("");

  if (info.Length() > 3) {
    if (!info[3].IsString()) {
      Napi::TypeError::New(env, "additional_headers must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    additional_headers = info[3].As<Napi::String>().Utf8Value();
  }

  int res = pjw_call_terminate(call_id, code, reason[0] ? reason.c_str() : NULL, additional_headers[0] ? additional_headers.c_str() : NULL);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_send_dtmf(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 3) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id, digits, mode").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();
 
  string digits = string("");
  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "digits must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  digits = info[1].As<Napi::String>().Utf8Value();

  if(digits.length() == 0) {
    Napi::Error::New(env, "digits is empty string").ThrowAsJavaScriptException();
    return env.Null();
  }

  int mode = 0;
  if (!info[2].IsNumber()) {
    Napi::TypeError::New(env, "mode must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  mode = info[2].As<Napi::Number>().Int32Value();

  if(mode != 0 && mode != 1) {
    Napi::TypeError::New(env, "mode must be 0 (RFC2833) or 1 (in-band)").ThrowAsJavaScriptException();
    return env.Null();
  }

  int res = pjw_call_send_dtmf(call_id, digits.c_str(), mode);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_reinvite(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 3) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id, hold, flags").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();
 
  if(!info[1].IsBoolean()) {
    Napi::TypeError::New(env, "Wrong argument type: hold must be boolean").ThrowAsJavaScriptException();
    return env.Null();
  }
  bool hold = info[1].As<Napi::Boolean>().Value();

  if (!info[2].IsNumber()) {
    Napi::TypeError::New(env, "flags must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  unsigned flags = info[2].As<Napi::Number>().Uint32Value();

  int res = pjw_call_reinvite(call_id, hold, flags);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_send_request(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 2) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id, method [, additional_headers, body, ct_type, ct_subtype]").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  string method = string("");
  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "method must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  method = info[1].As<Napi::String>().Utf8Value();

  string additional_headers = string("");
  if (info.Length() > 2) {
    if (!info[2].IsString()) {
      Napi::TypeError::New(env, "additional_headers must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    additional_headers = info[2].As<Napi::String>().Utf8Value();
  }

  string body = string("");
  if (info.Length() > 3) {
    if (!info[3].IsString()) {
      Napi::TypeError::New(env, "body must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    body = info[3].As<Napi::String>().Utf8Value();
  }

  string ct_type = string("");
  if (info.Length() > 4) {
    if (!info[4].IsString()) {
      Napi::TypeError::New(env, "ct_type must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    ct_type = info[4].As<Napi::String>().Utf8Value();
  }

  string ct_subtype = string("");
  if (info.Length() > 5) {
    if (!info[5].IsString()) {
      Napi::TypeError::New(env, "ct_subtype must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    ct_subtype = info[5].As<Napi::String>().Utf8Value();
  }

  int res = pjw_call_send_request(call_id, method.c_str(), additional_headers[0] ? additional_headers.c_str() : NULL, body[0] ? body.c_str() : NULL, ct_type[0] ? ct_type.c_str() : NULL, ct_subtype[0] ? ct_subtype.c_str(): NULL);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_start_record_wav(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id, output_file]").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "output_file must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string output_file = info[1].As<Napi::String>().Utf8Value();

  if (output_file.length() == 0) {
    Napi::Error::New(env, "output_file is invalid (blank string)").ThrowAsJavaScriptException();
    return env.Null();
  }

  int res = pjw_call_start_record_wav(call_id, output_file.c_str());

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_start_play_wav(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id, input_file]").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "input_file must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string input_file = info[1].As<Napi::String>().Utf8Value();

  if (input_file.length() == 0) {
    Napi::Error::New(env, "input_file is invalid (blank string)").ThrowAsJavaScriptException();
    return env.Null();
  }

  int res = pjw_call_start_play_wav(call_id, input_file.c_str());

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_start_fax(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 3) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id, is_sender, file]").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsBoolean()) {
    Napi::TypeError::New(env, "is_sender must be boolean.").ThrowAsJavaScriptException();
    return env.Null();
  }
  bool is_sender = info[1].As<Napi::Boolean>().Value();

  if (!info[2].IsString()) {
    Napi::TypeError::New(env, "file must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string file = info[2].As<Napi::String>().Utf8Value();

  if (file.length() == 0) {
    Napi::Error::New(env, "input_file is invalid (blank string)").ThrowAsJavaScriptException();
    return env.Null();
  }

  int res = pjw_call_start_fax(call_id, is_sender, file.c_str());

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_stop_record_wav(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  int res = pjw_call_stop_record_wav(call_id);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_stop_play_wav(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  int res = pjw_call_stop_play_wav(call_id);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_stop_fax(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  int res = pjw_call_stop_fax(call_id);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_get_stream_stat(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  char out_stats[4096];
  int res = pjw_call_get_stream_stat(call_id, out_stats);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::String::New(env, out_stats);
}

Napi::Value call_refer(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 2) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id, dest_uri [, additional_headers]").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();
 
  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "reason must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  const string dest_uri = info[1].As<Napi::String>().Utf8Value();

  string additional_headers = string("");
  if (info.Length() > 2) {
    if (!info[2].IsString()) {
      Napi::TypeError::New(env, "additional_headers must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    additional_headers = info[2].As<Napi::String>().Utf8Value();
  }

  long out_subscription_id;

  int res = pjw_call_refer(call_id, dest_uri.c_str(), additional_headers[0] ? additional_headers.c_str() : NULL, &out_subscription_id);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::Number::New(env, out_subscription_id);
}

Napi::Value call_get_info(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id, required_info").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();
 
  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "required_info must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string required_info = info[1].As<Napi::String>().Utf8Value();

  if(required_info != "Call-ID" && required_info != "RemoteMediaEndPoint") {
    Napi::TypeError::New(env, "required_info must be 'Call-ID' or 'RemoteMediaEndPoint'.").ThrowAsJavaScriptException();
    return env.Null();
  }

  char out_info[4096];

  int res = pjw_call_get_info(call_id, required_info.c_str(), out_info);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::String::New(env, out_info);
}

Napi::Value call_gen_string_replaces(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();
 
  char out_replaces[4096];

  int res = pjw_call_gen_string_replaces(call_id, out_replaces);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::String::New(env, out_replaces);
}



Napi::Value dtmf_aggregation_on(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: inter_digit_timer").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "inter_digit_timer must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int inter_digit_timer = info[0].As<Napi::Number>().Int32Value();
 
  int res = pjw_dtmf_aggregation_on(inter_digit_timer);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}


Napi::Value dtmf_aggregation_off(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  int res = pjw_dtmf_aggregation_off();

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}


Napi::Value packetdump_start(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: dev, pcap_file").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsString()) {
    Napi::TypeError::New(env, "dev must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string dev = info[0].As<Napi::String>().Utf8Value();

  if (dev.length() == 0) {
    Napi::Error::New(env, "dev is invalid (blank string)").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "pcap_file must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string pcap_file = info[1].As<Napi::String>().Utf8Value();

  if (pcap_file.length() == 0) {
    Napi::Error::New(env, "dev is invalid (blank string)").ThrowAsJavaScriptException();
    return env.Null();
  }

  int res = pjw_packetdump_start(dev.c_str(), pcap_file.c_str());

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value packetdump_stop(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  int res = pjw_packetdump_stop();

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value get_codecs(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  char out_codecs[4096];
  int res = pjw_get_codecs(out_codecs);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::String::New(env, out_codecs);
}

Napi::Value set_codecs(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: codec_info").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsString()) {
    Napi::TypeError::New(env, "codec_info must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string codec_info = info[0].As<Napi::String>().Utf8Value();

  int res = pjw_set_codecs(codec_info.c_str());

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value notify(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 5) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: subscriber_id, content_type, body, subscription_state, reason [, additional_headers]").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "subscriber_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int subscriber_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "content_type must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string content_type = info[1].As<Napi::String>().Utf8Value();

  if (!info[2].IsString()) {
    Napi::TypeError::New(env, "body must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string body = info[2].As<Napi::String>().Utf8Value();

  if (!info[3].IsNumber()) {
    Napi::TypeError::New(env, "subscription_state must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int subscription_state = info[3].As<Napi::Number>().Int32Value();

  if (!info[4].IsString()) {
    Napi::TypeError::New(env, "reason must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string reason = info[4].As<Napi::String>().Utf8Value();

  string additional_headers = string("");
  if (info.Length() > 5) {
    if (!info[5].IsString()) {
      Napi::TypeError::New(env, "additional_headers must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    additional_headers = info[5].As<Napi::String>().Utf8Value();
  }

  int res = pjw_notify(subscriber_id, content_type.c_str(), body.c_str(), subscription_state, reason.c_str(), additional_headers[0] ? additional_headers.c_str() : NULL);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value notify_xfer(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 4) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: subscriber_id, subscription_state, xfer_status_code, xfer_status_text").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "subscriber_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int subscriber_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsNumber()) {
    Napi::TypeError::New(env, "subscription_state must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int subscription_state = info[1].As<Napi::Number>().Int32Value();

  if (!info[2].IsNumber()) {
    Napi::TypeError::New(env, "xfer_status_code must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int xfer_status_code = info[2].As<Napi::Number>().Int32Value();

  if (!info[3].IsString()) {
    Napi::TypeError::New(env, "xfer_status_text must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string xfer_status_text= info[3].As<Napi::String>().Utf8Value();

  int res = pjw_notify_xfer(subscriber_id, subscription_state, xfer_status_code, xfer_status_text.c_str());

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value register_pkg(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: event, accept").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsString()) {
    Napi::TypeError::New(env, "event must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string event = info[0].As<Napi::String>().Utf8Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "accept must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string accept = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_register_pkg(event.c_str(), accept.c_str());

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value subscription_create(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 6) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: transport_id, event, accept, from_uri, to_uri, request_uri [, proxy_uri, realm, user, pass]").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "transport_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int transport_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "event must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string event = info[1].As<Napi::String>().Utf8Value();

  if (!info[2].IsString()) {
    Napi::TypeError::New(env, "accept must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string accept = info[2].As<Napi::String>().Utf8Value();

  if (!info[3].IsString()) {
    Napi::TypeError::New(env, "from_uri must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string from_uri = info[3].As<Napi::String>().Utf8Value();

  if (!info[4].IsString()) {
    Napi::TypeError::New(env, "to_uri must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string to_uri = info[4].As<Napi::String>().Utf8Value();

  if (!info[5].IsString()) {
    Napi::TypeError::New(env, "request_uri must be string.").ThrowAsJavaScriptException();
    return env.Null();
  }
  string request_uri = info[5].As<Napi::String>().Utf8Value();

  string proxy_uri = string("");
  if (info.Length() > 6) {
    if (!info[6].IsString()) {
      Napi::TypeError::New(env, "proxy_uri must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    proxy_uri = info[6].As<Napi::String>().Utf8Value();
  }

  string realm = string("");
  string user = string("");
  string pass = string("");

  if (info.Length() > 7) {
    if(info.Length() < 10) {
      Napi::TypeError::New(env, "missing credentials. you must provide realm, user, pass.").ThrowAsJavaScriptException();
      return env.Null();
    }

    if (!info[7].IsString()) {
      Napi::TypeError::New(env, "realm must be string").ThrowAsJavaScriptException();
      return env.Null();
    }
    realm = info[7].As<Napi::String>().Utf8Value();

    if (!info[8].IsString()) {
      Napi::TypeError::New(env, "user must be string").ThrowAsJavaScriptException();
      return env.Null();
    }
    user = info[8].As<Napi::String>().Utf8Value();

    if (!info[9].IsString()) {
      Napi::TypeError::New(env, "pass must be string").ThrowAsJavaScriptException();
      return env.Null();
    }
    pass = info[9].As<Napi::String>().Utf8Value();
  }

  long out_subscription_id;

  int res = pjw_subscription_create(transport_id, event.c_str(), accept.c_str(), from_uri.c_str(), to_uri.c_str(), request_uri.c_str(), 
    proxy_uri[0] ? proxy_uri.c_str() : NULL,
    realm[0] ? realm.c_str() : NULL,
    user[0] ? user.c_str() : NULL,
    pass[0] ? pass.c_str() : NULL,
    &out_subscription_id);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::Number::New(env, out_subscription_id);
}

Napi::Value subscription_subscribe(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 2) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: subscription_id, expires [, additional_headers]").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "subscription_id must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int subscription_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsNumber()) {
    Napi::TypeError::New(env, "expires must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int expires = info[1].As<Napi::Number>().Int32Value();

  string additional_headers = string("");
  if (info.Length() > 2) {
    if (!info[2].IsString()) {
      Napi::TypeError::New(env, "additional_headers must be string.").ThrowAsJavaScriptException();
      return env.Null();
    }
    additional_headers = info[2].As<Napi::String>().Utf8Value();
  }

  int res = pjw_subscription_subscribe(subscription_id, expires, additional_headers[0] ? additional_headers.c_str() : NULL);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}



Napi::Value set_log_level(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: log_level").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "log_level must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  int log_level = info[0].As<Napi::Number>().Int32Value();
 
  int res = pjw_log_level(log_level);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::Number::New(env, 0);
}


Napi::Value set_flags(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: flags").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "flags must be number.").ThrowAsJavaScriptException();
    return env.Null();
  }
  unsigned flags = info[0].As<Napi::Number>().Uint32Value();
 
  pjw_set_flags(flags);

  return Napi::Number::New(env, 0);
}




static void CallJs(napi_env napiEnv, napi_value napi_js_cb, void* context, void* data) {
	Napi::Env env = Napi::Env(napiEnv);

	Napi::Function js_cb = Napi::Value(env, napi_js_cb).As<Napi::Function>();

	Napi::String str = Napi::String::New(env, (char*)data);
	js_cb.Call(env.Global(), { str });
}

Napi::Value start(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsFunction()) {
    Napi::TypeError::New(env, "Function expected as argument[0]").ThrowAsJavaScriptException();
      return env.Undefined();;
  }

  Napi::Function js_cb = info[0].As<Napi::Function>();

  Napi::String name = Napi::String::New(env, "bla");

  napi_status status = napi_create_threadsafe_function(env,
				 js_cb,
				 NULL,
				 name,
				 0,
				 1,
				 NULL,
				 NULL,
				 NULL,
				 CallJs,
				 &tsf);
  if(status != napi_ok) {
          Napi::TypeError::New(env, "napi_create_threadsafe_function failed").ThrowAsJavaScriptException();
          return env.Undefined();
  }

  //boost::thread t{poll};

  return env.Undefined();
}

Napi::Value do_poll(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  char buf[4096];
  int res = __pjw_poll(buf);
  if(res == 0) {
    return Napi::String::New(env, buf);
  } else {
    return env.Null();
  } 
}
 
Napi::Value shutdown_(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  int res = __pjw_shutdown();

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}
 
Napi::Object init(Napi::Env env, Napi::Object exports) {
  int i = __pjw_init();
  //printf("__pjw_init res=%i\n", i);

  exports.Set("start", Napi::Function::New(env, start));

  exports.Set("transport_create", Napi::Function::New(env, transport_create));

  exports.Set("account_create", Napi::Function::New(env, account_create));
  exports.Set("account_register", Napi::Function::New(env, account_register));
  exports.Set("account_unregister", Napi::Function::New(env, account_unregister));

  exports.Set("call_create", Napi::Function::New(env, call_create));
  exports.Set("call_respond", Napi::Function::New(env, call_respond));
  exports.Set("call_terminate", Napi::Function::New(env, call_terminate));
  exports.Set("call_send_dtmf", Napi::Function::New(env, call_send_dtmf));
  exports.Set("call_reinvite", Napi::Function::New(env, call_reinvite));
  exports.Set("call_send_request", Napi::Function::New(env, call_send_request));
  exports.Set("call_start_record_wav", Napi::Function::New(env, call_start_record_wav));
  exports.Set("call_start_play_wav", Napi::Function::New(env, call_start_play_wav));
  exports.Set("call_start_fax", Napi::Function::New(env, call_start_fax));
  exports.Set("call_stop_record_wav", Napi::Function::New(env, call_stop_record_wav));
  exports.Set("call_stop_play_wav", Napi::Function::New(env, call_stop_play_wav));
  exports.Set("call_stop_fax", Napi::Function::New(env, call_stop_fax));
  exports.Set("call_get_stream_stat", Napi::Function::New(env, call_get_stream_stat));
  exports.Set("call_refer", Napi::Function::New(env, call_refer));
  exports.Set("call_get_info", Napi::Function::New(env, call_get_info));
  exports.Set("call_gen_string_replaces", Napi::Function::New(env, call_gen_string_replaces));

  exports.Set("set_log_level", Napi::Function::New(env, set_log_level));

  exports.Set("do_poll", Napi::Function::New(env, do_poll));
  exports.Set("shutdown", Napi::Function::New(env, shutdown_));

  exports.Set("dtmf_aggregation_on", Napi::Function::New(env, dtmf_aggregation_on));
  exports.Set("dtmf_aggregation_off", Napi::Function::New(env, dtmf_aggregation_off));

  exports.Set("packetdump_start", Napi::Function::New(env, packetdump_start));
  exports.Set("packetdump_stop", Napi::Function::New(env, packetdump_stop));

  exports.Set("get_codecs", Napi::Function::New(env, get_codecs));
  exports.Set("set_codecs", Napi::Function::New(env, set_codecs));

  exports.Set("notify", Napi::Function::New(env, notify));
  exports.Set("notify_xfer", Napi::Function::New(env, notify_xfer));

  exports.Set("register_pkg", Napi::Function::New(env, register_pkg));

  exports.Set("subscription_create", Napi::Function::New(env, subscription_create));
  exports.Set("subscription_subscribe", Napi::Function::New(env, subscription_subscribe));

  exports.Set("set_flags", Napi::Function::New(env, set_flags));

  return exports;
}

NODE_API_MODULE(addon, init)
