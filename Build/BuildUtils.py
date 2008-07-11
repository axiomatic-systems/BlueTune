from SCons.Script import *
import imp
from glob import glob

#######################################################
# reusable functions and data structures
#######################################################
DefaultEnv = Environment()
SourceDirMap = {}

def SetDefaultEnv(env):
    global DefaultEnv
    DefaultEnv = env

def MapSourceDir(key, val):
    SourceDirMap[key] = val

def LoadTool(name, env, **kw):
    config_path = GetBuildPath('#/Build/Tools/SCons')
    file, path, desc = imp.find_module(name, [config_path])
    module = imp.load_module(name, file, path, desc)
    module.generate(env, **kw)

def MergeListUnique(item_list, items):
    for item in items: 
        if not item in item_list: item_list.append(item)

def MergeItemUnique(item_list, item):
    if not item in item_list: item_list.append(item)
            
def GlobSources(directory, patterns, excluded_files=[]):
    root = GetBuildPath(GetDirPath(directory))
    files = []
    for pattern in Split(patterns):
        files += glob(root+'/'+pattern)
    return [directory+'/'+os.path.basename(x) for x in  files if os.path.basename(x) not in excluded_files]

def GetDirPath(directory):
    for key in SourceDirMap.keys():
        if directory.startswith(key):
            return SourceDirMap[key]+directory[len(key):]
    return '#/'+directory
    
def DeclareBuildDir(env, directory):
    env.BuildDir(directory, GetDirPath(directory), duplicate=0)
    
def GetIncludeDirs(modules, exclude=None):
    dirs = []
    for module in Split(modules):
        if Modules.has_key(module) and not module == exclude:
            MergeListUnique(dirs, Modules[module].GetIncludeDirs())
        else:
            MergeItemUnique(dirs, GetDirPath(module))
    return dirs
    
def GetObjects(modules):
    objects = []
    for module in Split(modules):
        if Modules.has_key(module):
            dep_objects = Modules[module].GetObjects()
            objects = [obj for obj in objects if not obj in dep_objects]+dep_objects
    return objects

def GetLibraries(modules, modules_only=False):
    libs = []
    for module in Split(modules):
        if module in libs: continue
        if Modules.has_key(module):
            dep_libs = Modules[module].GetLibraries()
            libs = [lib for lib in libs if not lib in dep_libs]+dep_libs
        elif not modules_only:
            libs.append(module)
    return libs

def GetLibraryDirs(modules):
    lib_dirs = []
    for module in modules:
        if Modules.has_key(module):
            MergeListUnique(lib_dirs, Modules[module].GetLibraryDirs())
    return lib_dirs

Modules = {}
class Module:
    def __init__(self, name, 
                 included_modules = [], 
                 linked_modules   = [],
                 lib_dirs         = []):
        self.name             = name
        self.included_modules = included_modules
        self.linked_modules   = linked_modules
        self.lib_dirs         = [GetDirPath(dir) for dir in lib_dirs]
        self.objects          = []
        self.library          = []
        Modules[name]         = self        
        
    def GetObjects(self):
        return self.objects+GetObjects(self.linked_modules)

    def GetLibraries(self):
        return self.library+GetLibraries(self.linked_modules)
        
    def GetLibraryDirs(self):
        return self.lib_dirs+GetLibraryDirs(self.linked_modules)

    def GetIncludeDirs(self):
        return GetIncludeDirs(self.included_modules, self.name)

class CompiledModule(Module):
    def __init__(self, name, 
                 module_type           = 'Objects',
                 source_root           = '',
                 build_source_dirs     = ['.'], 
                 build_source_files    = {},
                 build_source_pattern  = ['*.c', '*.cpp'], 
                 build_include_dirs    = [], 
                 included_modules      = [], 
                 included_only_modules = [],
                 linked_modules        = [],
                 environment           = None,
                 excluded_files        = [],
                 extra_cpp_defines     = [],
                 extra_lib_dirs        = []) :

        # store this new object in the module dictionary
        if build_source_dirs:
            build_source_dirs = [source_root+'/'+directory for directory in build_source_dirs]
        Module.__init__(self, 
                        name, 
                        Split(included_modules)+Split(included_only_modules)+Split(build_source_dirs), 
                        Split(linked_modules)+Split(included_modules),
                        extra_lib_dirs)

        # setup the environment        
        self.env = environment and environment.Clone() or DefaultEnv.Clone()
        if extra_cpp_defines:
            self.env.Append(CPPDEFINES=extra_cpp_defines)
        
        # for each source dir to build, create a BuildDir
        # to say where we want the object files to be built,
        # and compute the list of source files to build
        sources = []
        for directory in Split(build_source_dirs):
            DeclareBuildDir(self.env, directory)
            sources += GlobSources(directory, build_source_pattern, excluded_files)
            
        # add cherry-picked files
        for directory in build_source_files.keys():
            pattern = build_source_files[directory]
            if directory.startswith('/'):
                directory_path = directory[1:]
            else:
                directory_path = source_root+'/'+directory
            DeclareBuildDir(self.env, directory_path)
            sources += GlobSources(directory_path, pattern)

        # calculate our build include path
        cpp_path = GetIncludeDirs(Split(build_include_dirs) + 
                                  Split(build_source_dirs)  + 
                                  Split(included_modules)   + 
                                  Split(linked_modules))

        # check that the source list is not empty
        if len(sources) == 0 and build_source_dirs:
            raise 'Module '+name+' has no sources, build_source_dirs='+str(build_source_dirs)
        
        # create the module's product
        self.env.AppendUnique(CPPPATH=cpp_path)
        self.objects = [self.env.SharedObject(source=x) for x in sources]
        self.product = self.objects
        if module_type == 'StaticLibrary':
            dep_objects = GetObjects(self.linked_modules)
            self.library = self.env.StaticLibrary(target=name, source=self.objects+dep_objects)
            self.product = self.library
        elif module_type == 'Executable':
            libs = self.env.has_key('LIBS') and self.env['LIBS'] or []
            libs += GetLibraries(self.linked_modules)
            lib_path = self.env.has_key('LIBPATH') and self.env['LIBPATH'] or []
            lib_path += GetLibraryDirs(self.linked_modules)
            self.product = self.env.Program(target=name.lower(), source=self.objects, LIBS=libs, LIBPATH=lib_path)
            
        self.env.Alias(name, self.product)
     
class LinkedModule(CompiledModule):
    def __init__(self, name, 
                 module_type           = 'StaticLibrary',
                 included_modules      = [], 
                 included_only_modules = [],
                 linked_modules        = [],
                 environment           = None) :
        CompiledModule.__init__(self, name, 
                                module_type = module_type,
                                build_source_dirs = [],
                                included_modules = included_modules,
                                included_only_modules = included_only_modules,
                                linked_modules = linked_modules,
                                environment = environment)


#####################################################################
# Exports
#####################################################################
__all__ = ['SetDefaultEnv', 'LoadTool', 'CompiledModule', 'LinkedModule', 'MapSourceDir']
