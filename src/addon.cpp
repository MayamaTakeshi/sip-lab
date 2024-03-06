#define NAPI_EXPERIMENTAL /* for napi_threadsafe functions */

#include "sip.hpp"
#include <napi.h>
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
  pj_status_t status = pj_thread_register("main_thread", poll_thread_descriptor,
&poll_thread); if(status != PJ_SUCCESS)
  {
    printf("pj_thread_register(poll_thread) failed\n");
    exit(1);
  }

  char buf[4096];
  while(true) {
    wait(50);
    int res = __pjw_poll(buf);
    if(res == 0) {
      if(napi_call_threadsafe_function(tsf, buf, napi_tsfn_blocking) != napi_ok)
{ printf("napi_call_threadsafe_function falied\n");
      }
      //printf("js land called\n");
    }
  }
}
*/

Napi::Value transport_create(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::TypeError::New(env, "Wrong number of arguments. Expected params")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsString()) {
    Napi::TypeError::New(env,
                         "Wrong argument type: params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  const string json = info[0].As<Napi::String>().Utf8Value();

  int out_t_id;
  char out_t_address[256];
  int out_t_port;
  int res =
      pjw_transport_create(json.c_str(), &out_t_id, out_t_address, &out_t_port);

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Object obj = Napi::Object::New(env);
  obj.Set(Napi::String::New(env, "id"), Napi::Number::New(env, out_t_id));
  obj.Set(Napi::String::New(env, "address"),
          Napi::String::New(env, out_t_address));
  obj.Set(Napi::String::New(env, "port"), Napi::Number::New(env, out_t_port));

  return obj;
}

Napi::Value account_create(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::TypeError::New(
        env, "Wrong number of arguments. Expected: transport_id, params.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env,
                         "Wrong argument type: transport_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int transport_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env,
                         "Wrong argument type: params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int out_a_id;
  int res = pjw_account_create(transport_id, json.c_str(), &out_a_id);

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::Number::New(env, out_a_id);
}

Napi::Value account_register(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::TypeError::New(
        env, "Wrong number of arguments. Expected: account_id, params.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "Wrong argument type: account_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int account_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_account_register(account_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value account_unregister(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::TypeError::New(env, "Wrong number of arguments. Expected: account_id")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "Wrong argument type: account_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int account_id = info[0].As<Napi::Number>().Int32Value();

  int res = pjw_account_unregister(account_id);

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value request_create(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(
        env, "Wrong number of arguments. Expected: transport_id, params.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "transport_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int transport_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  long int out_request_id;
  char out_sip_call_id[256];

  int res = pjw_request_create(transport_id, json.c_str(), &out_request_id,
                               out_sip_call_id);

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Object obj = Napi::Object::New(env);
  obj.Set(Napi::String::New(env, "id"), Napi::Number::New(env, out_request_id));
  obj.Set(Napi::String::New(env, "sip_call_id"), out_sip_call_id);

  return obj;
}

Napi::Value request_respond(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env,
                     "Wrong number of arguments. Expected: request_id, params")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "request_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int request_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_request_respond(request_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_create(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(
        env, "Wrong number of arguments. Expected: transport_id, params.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "transport_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int transport_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  long int out_call_id;
  char out_sip_call_id[256];

  int res = pjw_call_create(transport_id, json.c_str(), &out_call_id,
                            out_sip_call_id);

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Object obj = Napi::Object::New(env);
  obj.Set(Napi::String::New(env, "id"), Napi::Number::New(env, out_call_id));
  obj.Set(Napi::String::New(env, "sip_call_id"), out_sip_call_id);

  return obj;
}

Napi::Value call_respond(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env,
                     "Wrong number of arguments. Expected: call_id, params")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_call_respond(call_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_terminate(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env,
                     "Wrong number of arguments. Expected: call_id, params")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_call_terminate(call_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_send_dtmf(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env,
                     "Wrong number of arguments. Expected: call_id, params.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_call_send_dtmf(call_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_reinvite(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env,
                     "Wrong number of arguments. Expected: call_id, params.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_call_reinvite(call_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_send_request(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env,
                     "Wrong number of arguments. Expected: call_id, params.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_call_send_request(call_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_start_record_wav(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env,
                     "Wrong number of arguments. Expected: call_id, params.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_call_start_record_wav(call_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_start_play_wav(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env,
                     "Wrong number of arguments. Expected: call_id, params.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_call_start_play_wav(call_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_start_fax(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env,
                     "Wrong number of arguments. Expected: call_id, params.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_call_start_fax(call_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_start_flite(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env,
                     "Wrong number of arguments. Expected: call_id, params.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_call_start_flite(call_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_stop_record_wav(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env,
                     "Wrong number of arguments. Expected: call_id, params")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_call_stop_record_wav(call_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_stop_play_wav(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env,
                     "Wrong number of arguments. Expected: call_id, params")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_call_stop_play_wav(call_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_stop_fax(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_call_stop_fax(call_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value call_get_stream_stat(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env,
                     "Wrong number of arguments. Expected: call_id, params")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  char out_stats[4096];
  int res = pjw_call_get_stream_stat(call_id, json.c_str(), out_stats);

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::String::New(env, out_stats);
}

/*
Napi::Value call_refer(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id,
params.").ThrowAsJavaScriptException(); return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be
number.").ThrowAsJavaScriptException(); return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON
string.").ThrowAsJavaScriptException(); return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  long out_subscription_id;

  int res = pjw_call_refer(call_id, json.c_str(), &out_subscription_id);

  if(res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::Number::New(env, out_subscription_id);
}
*/

Napi::Value call_get_info(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(
        env, "Wrong number of arguments. Expected: call_id, required_info")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "required_info must be string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  string required_info = info[1].As<Napi::String>().Utf8Value();

  if (required_info != "Call-ID" && required_info != "RemoteMediaEndPoint") {
    Napi::TypeError::New(
        env, "required_info must be 'Call-ID' or 'RemoteMediaEndPoint'.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  char out_info[4096];

  int res = pjw_call_get_info(call_id, required_info.c_str(), out_info);

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::String::New(env, out_info);
}

Napi::Value call_gen_string_replaces(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  char out_replaces[4096];

  int res = pjw_call_gen_string_replaces(call_id, out_replaces);

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::String::New(env, out_replaces);
}

Napi::Value call_send_tcp_msg(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: call_id, params")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "call_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int call_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_call_send_tcp_msg(call_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}


Napi::Value dtmf_aggregation_on(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::Error::New(env,
                     "Wrong number of arguments. Expected: inter_digit_timer")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "inter_digit_timer must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int inter_digit_timer = info[0].As<Napi::Number>().Int32Value();

  int res = pjw_dtmf_aggregation_on(inter_digit_timer);

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value dtmf_aggregation_off(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  int res = pjw_dtmf_aggregation_off();

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value get_codecs(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  char out_codecs[4096];
  int res = pjw_get_codecs(out_codecs);

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::String::New(env, out_codecs);
}

Napi::Value set_codecs(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: codec_info")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsString()) {
    Napi::TypeError::New(env, "codec_info must be string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  string codec_info = info[0].As<Napi::String>().Utf8Value();

  int res = pjw_set_codecs(codec_info.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value notify(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(
        env, "Wrong number of arguments. Expected: subscriber_id, params.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "subscriber_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int subscriber_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env,
                         "Wrong argument type: params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_notify(subscriber_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value notify_xfer(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(
        env, "Wrong number of arguments. Expected: subscriber_id, params")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "subscriber_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int subscriber_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env,
                         "Wrong argument type: params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_notify_xfer(subscriber_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value register_pkg(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: event, accept")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsString()) {
    Napi::TypeError::New(env, "event must be string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  string event = info[0].As<Napi::String>().Utf8Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env, "accept must be string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  string accept = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_register_pkg(event.c_str(), accept.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value subscription_create(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(
        env, "Wrong number of arguments. Expected: transport_id, params.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "transport_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int transport_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env,
                         "Wrong argument type: params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  long out_subscription_id;

  int res =
      pjw_subscription_create(transport_id, json.c_str(), &out_subscription_id);

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::Number::New(env, out_subscription_id);
}

Napi::Value subscription_subscribe(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 2) {
    Napi::Error::New(
        env, "Wrong number of arguments. Expected: subscription_id, params")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "subscription_id must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int subscription_id = info[0].As<Napi::Number>().Int32Value();

  if (!info[1].IsString()) {
    Napi::TypeError::New(env,
                         "Wrong argument type: params must be a JSON string.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  const string json = info[1].As<Napi::String>().Utf8Value();

  int res = pjw_subscription_subscribe(subscription_id, json.c_str());

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Value set_log_level(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: log_level")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "log_level must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  int log_level = info[0].As<Napi::Number>().Int32Value();

  int res = pjw_log_level(log_level);

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::Number::New(env, 0);
}

Napi::Value set_flags(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 1) {
    Napi::Error::New(env, "Wrong number of arguments. Expected: flags")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  if (!info[0].IsNumber()) {
    Napi::TypeError::New(env, "flags must be number.")
        .ThrowAsJavaScriptException();
    return env.Null();
  }
  unsigned flags = info[0].As<Napi::Number>().Uint32Value();

  pjw_set_flags(flags);

  return Napi::Number::New(env, 0);
}

Napi::Value enable_telephone_event(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 0) {
    Napi::Error::New(env, "Wrong number of arguments. Expected zero arguments")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  pjw_enable_telephone_event();

  return Napi::Number::New(env, 0);
}

Napi::Value disable_telephone_event(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() != 0) {
    Napi::Error::New(env, "Wrong number of arguments. Expected zero arguments")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  pjw_disable_telephone_event();

  return Napi::Number::New(env, 0);
}


static void CallJs(napi_env napiEnv, napi_value napi_js_cb, void *context,
                   void *data) {
  Napi::Env env = Napi::Env(napiEnv);

  Napi::Function js_cb = Napi::Value(env, napi_js_cb).As<Napi::Function>();

  Napi::String str = Napi::String::New(env, (char *)data);
  js_cb.Call(env.Global(), {str});
}

Napi::Value start(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsFunction()) {
    Napi::TypeError::New(env, "Function expected as argument[0]")
        .ThrowAsJavaScriptException();
    return env.Undefined();
    ;
  }

  Napi::Function js_cb = info[0].As<Napi::Function>();

  Napi::String name = Napi::String::New(env, "bla");

  napi_status status = napi_create_threadsafe_function(
      env, js_cb, NULL, name, 0, 1, NULL, NULL, NULL, CallJs, &tsf);
  if (status != napi_ok) {
    Napi::TypeError::New(env, "napi_create_threadsafe_function failed")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }

  // boost::thread t{poll};

  return env.Undefined();
}

Napi::Value do_poll(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  char buf[4096];
  int res = __pjw_poll(buf);
  if (res == 0) {
    return Napi::String::New(env, buf);
  } else {
    return env.Null();
  }
}

Napi::Value shutdown_(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  int res = __pjw_shutdown();

  if (res != 0) {
    Napi::Error::New(env, pjw_get_error()).ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Null();
}

Napi::Object init(Napi::Env env, Napi::Object exports) {
  int i = __pjw_init();
  printf("__pjw_init res=%i\n", i);

  exports.Set("start", Napi::Function::New(env, start));

  exports.Set("transport_create", Napi::Function::New(env, transport_create));

  exports.Set("account_create", Napi::Function::New(env, account_create));
  exports.Set("account_register", Napi::Function::New(env, account_register));
  exports.Set("account_unregister",
              Napi::Function::New(env, account_unregister));

  exports.Set("request_create", Napi::Function::New(env, request_create));
  exports.Set("request_respond", Napi::Function::New(env, request_respond));

  exports.Set("call_create", Napi::Function::New(env, call_create));
  exports.Set("call_respond", Napi::Function::New(env, call_respond));
  exports.Set("call_terminate", Napi::Function::New(env, call_terminate));
  exports.Set("call_send_dtmf", Napi::Function::New(env, call_send_dtmf));
  exports.Set("call_reinvite", Napi::Function::New(env, call_reinvite));
  exports.Set("call_send_request", Napi::Function::New(env, call_send_request));
  exports.Set("call_start_record_wav",
              Napi::Function::New(env, call_start_record_wav));
  exports.Set("call_start_play_wav",
              Napi::Function::New(env, call_start_play_wav));
  exports.Set("call_start_fax", Napi::Function::New(env, call_start_fax));

  exports.Set("call_start_flite", Napi::Function::New(env, call_start_flite));

  exports.Set("call_stop_record_wav",
              Napi::Function::New(env, call_stop_record_wav));
  exports.Set("call_stop_play_wav",
              Napi::Function::New(env, call_stop_play_wav));
  exports.Set("call_stop_fax", Napi::Function::New(env, call_stop_fax));
  exports.Set("call_get_stream_stat",
              Napi::Function::New(env, call_get_stream_stat));
  // exports.Set("call_refer", Napi::Function::New(env, call_refer));
  exports.Set("call_get_info", Napi::Function::New(env, call_get_info));
  exports.Set("call_gen_string_replaces",
              Napi::Function::New(env, call_gen_string_replaces));

  exports.Set("call_send_tcp_msg",
              Napi::Function::New(env, call_send_tcp_msg));

  exports.Set("set_log_level", Napi::Function::New(env, set_log_level));

  exports.Set("do_poll", Napi::Function::New(env, do_poll));
  exports.Set("shutdown", Napi::Function::New(env, shutdown_));

  exports.Set("dtmf_aggregation_on",
              Napi::Function::New(env, dtmf_aggregation_on));
  exports.Set("dtmf_aggregation_off",
              Napi::Function::New(env, dtmf_aggregation_off));

  exports.Set("get_codecs", Napi::Function::New(env, get_codecs));
  exports.Set("set_codecs", Napi::Function::New(env, set_codecs));

  exports.Set("notify", Napi::Function::New(env, notify));
  exports.Set("notify_xfer", Napi::Function::New(env, notify_xfer));

  exports.Set("register_pkg", Napi::Function::New(env, register_pkg));

  exports.Set("subscription_create",
              Napi::Function::New(env, subscription_create));
  exports.Set("subscription_subscribe",
              Napi::Function::New(env, subscription_subscribe));

  exports.Set("set_flags", Napi::Function::New(env, set_flags));

  exports.Set("disable_telephone_event", Napi::Function::New(env, disable_telephone_event));
  exports.Set("enable_telephone_event", Napi::Function::New(env, enable_telephone_event));

  return exports;
}

NODE_API_MODULE(addon, init)
