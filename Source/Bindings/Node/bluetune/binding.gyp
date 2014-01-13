{
    'targets': [
        {
            'target_name'  : 'bluetune',
            'sources'      : ['node-bluetune.cpp'],
            'include_dirs' : ['$(BLUETUNE_SDK_HOME)/include'],
            'defines'      : ['NPT_CONFIG_ENABLE_LOGGING'],
            'conditions'   : [
                ['OS=="mac"', {
		            'cflags_cc!'    : ['-fno-rtti'],
		            'xcode_settings': {
                        'GCC_ENABLE_CPP_RTTI': 'YES'
                    },
                    'link_settings' : {
                        'libraries': ['-lBlueTune', '-lAtomix', '-lNeptune', '-lBento4', '-lBento4Atomix'],
                        'configurations': {
                            'Debug': {
                                'xcode_settings': {
                                    'OTHER_LDFLAGS': ['-L$(BLUETUNE_SDK_HOME)/Targets/universal-apple-macosx/lib/Debug']
                                }
                            },
                            'Release': {
                                'xcode_settings': {
                                    'OTHER_LDFLAGS': ['-L$(BLUETUNE_SDK_HOME)/Targets/universal-apple-macosx/lib/Release']
                                }
                            }
                        }
                    }
                }]      
            ]
        }
    ]
}
