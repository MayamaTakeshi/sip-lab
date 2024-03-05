{
  'targets': [
    {
      'target_name': 'all-settings',
      'type': 'none',
      'all_dependent_settings': {
          'include_dirs': [
            "3rdParty/pjproject/pjsip/include",
            "3rdParty/pjproject/pjlib/include",
            "3rdParty/pjproject/pjlib-util/include",
            "3rdParty/pjproject/pjnath/include",
            "3rdParty/pjproject/pjmedia/include",
            "include",
            "src",
            "src/pjmedia/include",
            "src/pjmedia/include/pjmedia",
            "3rdParty/rapidjson/include",
            "3rdParty/boost_1_51_0",
            "3rdParty/spandsp/src",
            "<!@(node -p \"require('node-addon-api').include\")",
          ],
          'conditions': [
            [ 'OS!="win"', {
              'cflags_cc': [
                '-g',
                '-fexceptions',
                '-Wno-maybe-uninitialized',
                '-fPIC',
              ],
              'ldflags_cc': [
                '-all-static',
              ]
              }
            ]
          ],
          'link_settings': {
              'libraries': [
                '-L ../3rdParty/pjproject/pjnath/lib',
                '-L ../3rdParty/pjproject/pjlib/lib',
                '-L ../3rdParty/pjproject/pjlib-util/lib',
                '-L ../3rdParty/pjproject/third_party/lib',
                '-L ../3rdParty/pjproject/pjmedia/lib',
                '-L ../3rdParty/pjproject/pjsip/lib',
                '-L ../3rdParty/pjproject/third_party/lib',
                '-l pjnath-x86_64-unknown-linux-gnu',
                '-l ilbccodec-x86_64-unknown-linux-gnu',
                '-l webrtc-x86_64-unknown-linux-gnu',
                '-l yuv-x86_64-unknown-linux-gnu',
                '-l speex-x86_64-unknown-linux-gnu',
                '-l gsmcodec-x86_64-unknown-linux-gnu',
                '-l g7221codec-x86_64-unknown-linux-gnu',
                '-l pjmedia-audiodev-x86_64-unknown-linux-gnu',
                '-l pjmedia-x86_64-unknown-linux-gnu',
                '-l resample-x86_64-unknown-linux-gnu',
                '-l pjmedia-codec-x86_64-unknown-linux-gnu',
                '-l pjmedia-videodev-x86_64-unknown-linux-gnu',
                '-l pjsdp-x86_64-unknown-linux-gnu',
                '-l pjsip-x86_64-unknown-linux-gnu',
                '-l pjsua2-x86_64-unknown-linux-gnu',
                '-l pjsip-ua-x86_64-unknown-linux-gnu',
                '-l pjsip-simple-x86_64-unknown-linux-gnu',
                '-l pjsua-x86_64-unknown-linux-gnu',
                '-l pj-x86_64-unknown-linux-gnu',
                '-l pjlib-util-x86_64-unknown-linux-gnu',
                '../3rdParty/spandsp/src/.libs/libspandsp.a',
                '../3rdParty/bcg729/src/libbcg729.a',
                '-lstdc++',
                '-lopus',
                '-lssl',
                '-lcrypto',
                '-luuid',
                '-lm',
                '-ldl',
                '-ltiff',
                '-lrt',
                '-lpthread',
                '-lasound',
                '-lavformat',
                '-lavcodec',
                '-lswscale',
                '-lavutil',
                '-lspeex',
                '-l srtp-x86_64-unknown-linux-gnu',
              ],
          },
       },
    },

    {
      'target_name': 'addon',
      'type': 'loadable_module', # this is default for node-gyp but gyp will complain if absent.
      'dependencies': ['all-settings'],
    
      'actions': [
        {
          'action_name': 'build_deps',
          'message': 'executing build_deps.sh',
          'inputs': [],
          'outputs': ['./3rdParty'],
          'action': ['bash', './build_deps.sh'],
        },
      ],

      'sources': [
        'src/log.cpp',
        'src/event_templates.cpp',
        'src/idmanager.cpp',
        'src/sip.cpp',
        'src/addon.cpp',
        'src/pjmedia/src/pjmedia/dtmfdet.c',
      ],
    },
  ],
}
