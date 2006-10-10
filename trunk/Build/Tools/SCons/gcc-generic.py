import os

def generate(env, gcc_cross_prefix=None, gcc_extra_options='', gcc_relaxed_warnings=False):
    ### general compiler flags
    if gcc_relaxed_warnings:
	c_compiler_compliance_flags = ''
        cxx_compiler_warnings       = '-Wall'
    else:
	c_compiler_compliance_flags = '-pedantic'
        cxx_compiler_warnings       = '-Werror -Wall -W -Wundef -Wno-long-long'

    c_compiler_warnings         = cxx_compiler_warnings + ' -Wmissing-prototypes -Wmissing-declarations'
    c_compiler_defines          = '-D_REENTRANT'

    ### tomsfastmath flags
    tfm_c_compiler_warnings     = '-Wall -W -Wshadow'
    tfm_c_compiler_flags        = '-O3 -funroll-all-loops -fomit-frame-pointer'
    
    ### libtomcrypt flags
    ltc_c_compiler_warnings     = '-Wall -Wsign-compare -W -Wshadow -Wno-unused-parameter'
    ltc_c_compiler_flags        = '-O3 -funroll-all-loops -fomit-frame-pointer'
    
    ### sqlite flags
    sqlite_c_compiler_warnings  = '-Wall -Wsign-compare -W -Wshadow -Wno-unused-parameter'
    sqlite_c_compiler_flags     = ' '
    sqlite_c_compiler_defines   = '-DHAVE_FDATASYNC=1 -DSQLITE_OMIT_CURSOR -DSQLITE_THREAD_OVERRIDE_LOCK=-1'
    
    if env['build_config'] == 'Debug':
        c_compiler_flags = '-g'    
    else:
        c_compiler_flags = '-O3'
    
    if env['enable_profiling']:
        c_compiler_profile_flags = '-pg'
    else:
        c_compiler_profile_flags = ''
                
    if gcc_cross_prefix:
        env['ENV']['PATH'] += os.environ['PATH']
        env['AR']     = gcc_cross_prefix+'-ar'
        env['RANLIB'] = gcc_cross_prefix+'-ranlib'
        env['CC']     = gcc_cross_prefix+'-gcc ' + gcc_extra_options
        env['CXX']    = gcc_cross_prefix+'-g++ ' + gcc_extra_options
        env['LINK']   = gcc_cross_prefix+'-g++ ' + gcc_extra_options

    ### general environment
    #env['SHLIBSUFFIX'] = '.so'
    #env['SHLINKFLAGS'] = '-Wl,-Bsymbolic'
    env['CPPFLAGS']    = ' '.join([c_compiler_defines])
    env['CCFLAGS']     = ' '.join([c_compiler_profile_flags, c_compiler_compliance_flags, c_compiler_flags, c_compiler_warnings])
    env['CXXFLAGS']    = ' '.join([c_compiler_profile_flags, c_compiler_compliance_flags, c_compiler_flags, cxx_compiler_warnings])
    env['LINKFLAGS']   = c_compiler_profile_flags
    
    ### tomsfastmath environment
    tfm_env = env.Copy()
    tfm_env['CCFLAGS'] = ' '.join([tfm_c_compiler_flags, tfm_c_compiler_warnings])
    env['TFM_ENV']=tfm_env 
                
    ### libtomcrypt environment
    ltc_env = env.Copy()
    ltc_env['CCFLAGS']  = ' '.join([ltc_c_compiler_flags, ltc_c_compiler_warnings])
    env['LTC_ENV']=ltc_env
   
    ### SQLite environment
    sqlite_env = env.Copy()
    sqlite_env['CPPFLAGS'] = ' '.join([sqlite_c_compiler_defines])
    
    if env['build_config'] == 'Debug':
        sqlite_env['CCFLAGS']  = '-g -O2 '.join([sqlite_c_compiler_flags, sqlite_c_compiler_warnings])
    else:
        sqlite_env['CCFLAGS']  = '-O6 '.join([sqlite_c_compiler_flags, sqlite_c_compiler_warnings])
    
    env['SQLITE_ENV']=sqlite_env 
    
    ### ShiStorage environment
    shistorage_env = env.Copy()
    env['SHI_STORAGE_ENV']=shistorage_env
    

