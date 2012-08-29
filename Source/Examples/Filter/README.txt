Sample Filter Plugin
=====================

This directory contains the code for a simple BlueTune media node plugin example.
This media node implementation shows an example of a node that receives BlueTune media packets containing PCM audio, and produces media packets with PCM samples that have been filtered, in-place.
The filter is a very simple FIR filters, where the FIR coefficients are supplied by setting a stream property.
(NOTE: the FIR filter implementation is a naive, non-optimized implementation, designed to be used as a simple example, not for production use).

Building the plugin:
-------------------
The media node implementation is mostly in the file SampleFilter.cpp
To build the media node plugin, another file is also compiled: Source/Plugins/DynamicLoading/BltDynamicPluginTemplate.cpp, which provides the reusable boiler plate code necessary for a dynamically-loadable plugin. This file is compiled with the C pre-processor symbol BLT_PLUGIN_TEMPLATE_MODULE_FACTORY_FUNCTION set to the name of the plugin's factory method. In this example, the factory method is called:
BLT_PLUGIN_TEMPLATE_MODULE_FACTORY_FUNCTION=BLT_SampleFilterModule_GetModuleObject
and is implemented in SampleFilter.cpp

The build files for the plugin are at the same place as the build files for the general BlueTune project (see the main documentation for details). When building the plugin, the build output is a dynamically loadable plugin. It can also be built as a statically linkable module (in this case it is not necessary to build and link with the file BltDynamicPluginTemplate.cpp, as the factory method BLT_SampleFilterModule_GetModuleObject can be called directly)

When building the plugin, the C pre-processor symbol ATX_CONFIG_ENABLE_LOGGING is also defined, so as to enable logging.

This example uses some of the functions and data types from BlueTune, as well as the Neptune and Atomix runtime libraries (used internally by the BlueTune runtime as well), so the final binary must be linked with the BlueTune, Neptune and Atomix libraries (those libraries are built by the BlueTune build system and available at the same place where the main BlueTune runtime library is).

Plugin functionality:
---------------------

Most of the code in SampleFilter.cpp takes care of the general boilerplate code that a BlueTune media node has to take care off. 
At the end of the file is the implementation of the SampleFilterModule class, which is the factory object responsible for responding to probes (used by the framework to find appropriate classes for different purposed when needed):
The SampleFilterModule_Probe method will respond positively to a request for a node with the name 'com.example.filter.sample' and a packet-in packet-out interface.
At the beginning of the file is the implementation of the SampleFilter object itself. This object is composed of an input part, an output part, and some internal data fields. Only the data fields used for the FIR filter implementation are specific to this example, the rest is generic and common to most media node implementations.
The data flow through the filter is relatively simple:
when a media packet is ready for processing, the SampleFilterInput_PutPacket method is called, with a BLT_MediaPacket instance. This method first checks that the packet provided is one with the expected type (PCM audio with 16-bit samples in native-endian signed integer format). Non-PCM packets will be rejected (this way, the media framework can automatically build a chain of media nodes that will convert other formats, such as compressed formats, into PCM). PCM packets that cannot be processed (either because the filter coefficients have not been set, or because the PCM data is not in 16-bit native-endian format) are simply left untouched.
Media packets that can be processed are processed in-place. A reference to the media packet is kept, and the function returns. 
The function SampleFilterOutput_GetPacket simply checks if a processed packet is available. If a packet is available, it is returned to the caller. If not, the method return BLT_ERROR_PORT_HAS_NO_DATA indicating that no data is available. This will cause the media framework to feed the next packet to the SampleFilterInput_PutPacket method.
This alternation between SampleFilterOutput_GetPacket and SampleFilterInput_PutPacket continues until there are not more packets to process.

This example plugin implements a simple FIR (Finite Impulse Response) filter as an example of a typical PCM audio filter. The PCM samples are convolved with an array of filter coefficients. The coefficients are passed to the filter through a stream property named 'sample.filter.coefficients' (this name is arbitrary, other modules use different property names for their own purposes). The implementation expects this property to be of type String and contain the hexadecimal encoding of an array of coefficients, each 4 bytes each, formatted as a 32-bit signed fixed-point value (16.16) in big-endian order. For example, a coefficient with value 1/3 would be represented as 00005555. 

Testing the plugin:
-------------------

The simplest way to test the plugin is to use the 'btplay' command-line.
This command-line application allows you to load one or more plugins, as well as adding media nodes (in our case the SampleFilter media node) to the decoding pipeline. In addition, properties can be set from the command line as well, allowing us to pass the filter coefficients easily.
Here is an example command line:

./btplay --verbose=all --load-plugin=SampleFilter.plugin --add-node=com.example.filter.sample --property=C:S:sample.filter.coefficients:fffff48800000844000004a40000023e0000017400000238000003ca000004ca000003de000000b4fffffc9afffffa0cfffffb320000002a0000066a000009ea000007ac00000012fffff728fffff28efffff5e00000000200000b4a000010b000000c4400000000fffff2fcffffed26fffff272fffffffc00000dd4000013a000000dd4fffffffcfffff272ffffed26fffff2fc0000000000000c44000010b000000b4a00000002fffff5e0fffff28efffff72800000012000007ac000009ea0000066a0000002afffffb32fffffa0cfffffc9a000000b4000003de000004ca000003ca00000238000001740000023e000004a400000844fffff488 somefile.mp3 


Using the plugin in a final application:
----------------------------------------

