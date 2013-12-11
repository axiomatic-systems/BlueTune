/*****************************************************************
|
|   BlueTune - PCM Decoder Example
|
|   (c) 2002-2013 Gilles Boccon-Gibod
|   Author: Gilles Boccon-Gibod (bok@bok.net)
|
 ****************************************************************/
/** @file 
 * Main code for PcmDecoder
 */

/*----------------------------------------------------------------------
|    includes
+---------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>

#include "BlueTune.h"

/*----------------------------------------------------------------------
|    main
+---------------------------------------------------------------------*/
int
main(int argc, char** argv)
{
    BLT_Result result;
    
    if (argc != 2) {
        fprintf(stderr, "usage: pcmdecoder <input>\n");
        return 1;
    }
    const char* input = argv[1];
    
    /* create a decoder */
    BLT_Decoder* decoder = NULL;
    BLT_Decoder_Create(&decoder);
    
    /* register builtin modules */
    BLT_Decoder_RegisterBuiltins(decoder);
    
    /* open the input */
    result = BLT_Decoder_SetInput(decoder, input, NULL);
    if (BLT_FAILED(result)) {
        fprintf(stderr, "ERROR: SetInput failed (%d)\n", result);
        return 1;
    }
    
    /* set the output to be a memory node */
    result = BLT_Decoder_SetOutput(decoder, "memory", "audio/pcm");
    if (BLT_FAILED(result)) {
        fprintf(stderr, "ERROR: SetOutput failed (%d)\n", result);
        return 1;
    }

    /* get a refrence to the output node */
    BLT_MediaNode* output_node = NULL;
    BLT_Decoder_GetOutputNode(decoder, &output_node);

    /* get the BLT_PacketProducer interface of the output node */
    BLT_PacketProducer* packet_source = ATX_CAST(output_node, BLT_PacketProducer);

    /* pump packets until there are no more */
    do {
        result = BLT_Decoder_PumpPacket(decoder);
        if (BLT_SUCCEEDED(result)) {
            BLT_MediaPacket* packet = NULL;
            result = BLT_PacketProducer_GetPacket(packet_source, &packet);
            if (BLT_SUCCEEDED(result) && packet) {
                const BLT_PcmMediaType* media_type;
                BLT_MediaPacket_GetMediaType(packet, (const BLT_MediaType**)&media_type);
                printf("PACKET: sample_rate=%d, channels=%d, bits_per_sample=%d, size=%d\n",
                       media_type->sample_rate,
                       media_type->channel_count,
                       media_type->bits_per_sample,
                       BLT_MediaPacket_GetPayloadSize(packet));
                
                /* don't forget to release the packet */
                BLT_MediaPacket_Release(packet);
            }
        }
    } while (BLT_SUCCEEDED(result));
    
    /* cleanup */
    BLT_Decoder_Destroy(decoder);
    
    return 0;
}
