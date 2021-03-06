#################################################################
# Important: this build file has been tested with Android NDK r6, r7 and r8
# It may or may not work with other releases of the NDK. Please notify
# us if you find a newer NDK for which this does not work.
#################################################################

import os
import re
import sys
import platform

# we need to know when the NDK is
ANDROID_NDK_ROOT=os.getenv('ANDROID_NDK_ROOT')
if not ANDROID_NDK_ROOT:
    raise Exception('ANDROID_NDK_ROOT environment variable not set')

# detect the host system on which we're running
if env.has_key('android_host_system') and env['android_host_system']:
	ANDROID_HOST_SYSTEM = env['android_host_system']
else:
	PLATFORM_TO_TARGET_MAP = { 
    	'linux-i386' : 'linux-x86',
    	'linux2'     : 'linux-x86',
    	'win32'      : 'windows',
    	'cygwin'     : 'windows',
    	'darwin'     : 'darwin-x86'
	}
	if sys.platform in PLATFORM_TO_TARGET_MAP:
		ANDROID_HOST_SYSTEM = PLATFORM_TO_TARGET_MAP[sys.platform]
	else:
		raise Exception('Android Host Platform cannot be determined')

# check if we're on darwin-x86_64 (not detected by sys.platform)
if ANDROID_HOST_SYSTEM == 'darwin-x86':
    if not os.path.exists(os.path.join(ANDROID_NDK_ROOT, 'prebuilt', 'darwin-x86')) and os.path.exists(os.path.join(ANDROID_NDK_ROOT, 'prebuilt', 'darwin-x86_64')):
        ANDROID_HOST_SYSTEM = 'darwin-x86_64'

if ANDROID_TOOLCHAIN == '' or not os.path.exists(os.path.join(ANDROID_NDK_ROOT, 'toolchains', ANDROID_TOOLCHAIN)):
    toolchain_dirs = os.listdir(ANDROID_NDK_ROOT+'/toolchains')
    for toolchain_dir in toolchain_dirs:
        if os.path.exists(os.path.join(ANDROID_NDK_ROOT, 'toolchains', toolchain_dir, 'prebuilt', ANDROID_HOST_SYSTEM)) and (toolchain_dir.startswith(ANDROID_CROSS_PREFIX+'-') or toolchain_dir.startswith(ANDROID_ARCH+'-')):
            ANDROID_TOOLCHAIN=toolchain_dir
            if ANDROID_CROSS_PREFIX == '':
                suffix_pos = toolchain_dir.rfind('-')
                if (suffix_pos >= 0):
                    ANDROID_CROSS_PREFIX = ANDROID_TOOLCHAIN[:suffix_pos]
            print "Auto-selecting toolchain:", ANDROID_TOOLCHAIN, "Cross-prefix:", ANDROID_CROSS_PREFIX
            break
        
if ANDROID_TOOLCHAIN == '':
    raise Exception('Android toolchain not found')

# override defaults from command line args
if ARGUMENTS.get('android_toolchain'):
    ANDROID_TOOLCHAIN=ARGUMENTS.get('android_toolchain')

if ARGUMENTS.get('android_cross_prefix'):
    ANDROID_CROSS_PREFIX=ARGUMENTS.get('android_cross_prefix')

if ARGUMENTS.get('android_platform'):
    ANDROID_PLATFORM=ARGUMENTS.get('android_platform')

if ARGUMENTS.get('android_arch'):
    ANDROID_ARCH=ARGUMENTS.get('android_arch')
		
print 'Building for Android: '
print 'ANDROID_HOST_SYSTEM =', ANDROID_HOST_SYSTEM
print 'ANDROID_TOOLCHAIN   =', ANDROID_TOOLCHAIN
print 'ANDROID_PLATFORM    =', ANDROID_PLATFORM
print 'ANDROID_ARCH        =', ANDROID_ARCH

ANDROID_TOOLCHAIN_BIN = ANDROID_NDK_ROOT+'/toolchains/'+ANDROID_TOOLCHAIN+'/prebuilt/'+ANDROID_HOST_SYSTEM+'/bin'
ANDROID_SYSROOT = ANDROID_NDK_ROOT+'/platforms/'+ANDROID_PLATFORM+'/arch-'+ANDROID_ARCH

### add the tools to the path
env.PrependENVPath('PATH', ANDROID_TOOLCHAIN_BIN)

### special C Runtime startup for executables
#env['BLT_EXTRA_EXECUTABLE_OBJECTS'] = [ANDROID_SYSROOT+'/usr/lib/crtbegin_static.o',
#                                       ANDROID_SYSROOT+'/usr/lib/crtend_android.o']
env['BLT_EXTRA_EXECUTABLE_OBJECTS'] = []
env['BLT_EXTRA_LIBS'] = ['gcc']

### Load the tools
LoadTool('gcc-generic', env, gcc_cross_prefix=ANDROID_CROSS_PREFIX, gcc_strict=False)
env.AppendUnique(CCFLAGS = ['-I'+ANDROID_NDK_ROOT+'/sources/cxx-stl/system/include' , 
                            '--sysroot', ANDROID_SYSROOT,
                            '-fpic',
                            '-fpie',
                            '-ffunction-sections',
                            '-funwind-tables',
                            '-fstack-protector',
                            '-fno-short-enums'] + ANDROID_EXTRA_CCFLAGS)
env.AppendUnique(CXXFLAGS = ['-fno-exceptions', '-fno-rtti'])
env.AppendUnique(CPPDEFINES = ['ANDROID', 'ATX_CONFIG_HAVE_SYSTEM_LOG_CONFIG', 'NPT_CONFIG_HAVE_SYSTEM_LOG_CONFIG'])
env.AppendUnique(LINKFLAGS = ['--sysroot', ANDROID_SYSROOT,
                              '-pie',
                			  '-Wl,--no-undefined', 
                              '-Wl,-z,noexecstack',
                			  '-L'+ANDROID_SYSROOT+'/usr/lib', 
                			  '-lc', 
                			  '-lstdc++', 
                			  '-lm', 
                			  '-llog', 
                			  '-ldl',
                			  '-lOpenSLES'])