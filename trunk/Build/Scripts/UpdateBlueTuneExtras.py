#! /usr/bin/env python

import sys
import os
import shutil

if len(sys.argv) != 3:
    print "UpdateBlueTuneExtras.py <target> <bluetune-extras-root-dir>"
    sys.exit(1)
TARGET = sys.argv[1]
BLUETUNE_EXTRAS_ROOT = sys.argv[2]

SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
TARGET_ROOT = SCRIPT_DIR+'/../../Build/Targets/'+TARGET

lib_prefix = 'lib'
lib_suffix = '.a'
if TARGET.startswith('x86-microsoft-win32'):
    lib_prefix = ''
    lib_suffix = '.lib'
    lib_name = 'BltWmaDecoder'
    TARGET_ROOT = SCRIPT_DIR+'/../../Build/Targets/x86-microsoft-win32-vs2005/BltWmaPlugin'
else:
	lib_name = 'BltWmaDecoderPluginStatic'
	
def CopyIfChanged(frm, to):
    if not os.path.exists(frm):
        print 'ERROR: file', frm, 'does not exist'
        return
    if not os.path.exists(os.path.dirname(to)):
        print 'ERROR: dir', os.path.dirname(to), 'does not exist'
        return
    print "COPY", frm, to
    shutil.copy(frm, to)
    
CopyIfChanged(SCRIPT_DIR+'/../../Source/Plugins/Decoders/WMA/BltWmaDecoder.h',
              BLUETUNE_EXTRAS_ROOT+'/WmaPlugin/Targets/'+ TARGET+ '/include/BltWmaDecoder.h')
for config in ['Debug', 'Release']:
    CopyIfChanged(TARGET_ROOT+'/' + config+'/'+lib_prefix+lib_name+lib_suffix,
                  BLUETUNE_EXTRAS_ROOT+'/WmaPlugin/Targets/'+ TARGET + '/lib/' + config+'/'+lib_prefix+'BltWmaDecoder'+lib_suffix)
    CopyIfChanged(TARGET_ROOT+'/'+ config + '/BltWmaDecoder.plugin',
                  BLUETUNE_EXTRAS_ROOT+'/WmaPlugin/Targets/'+ TARGET + '/bin/' + config + '/BltWmaDecoder.plugin')
