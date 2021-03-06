/* ------------------------------------------------------------------
 * Copyright (C) 1998-2009 PacketVideo
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 * -------------------------------------------------------------------
 */
/*

 Filename: mdct_tables_fxp.cpp
 Funtions:

------------------------------------------------------------------------------
 MODULE DESCRIPTION

    MDCT rotation tables fixpoint tables

    For a table with N complex points:

    cos_n + j*sin_n == exp(j(2pi/N)(n+1/8))

------------------------------------------------------------------------------
*/


/*----------------------------------------------------------------------------
; INCLUDES
----------------------------------------------------------------------------*/
#include "pv_audio_type_defs.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*----------------------------------------------------------------------------
    ; MACROS
    ; Define module specific macros here
    ----------------------------------------------------------------------------*/

    /*----------------------------------------------------------------------------
    ; DEFINES
    ; Include all pre-processor statements here. Include conditional
    ; compile variables also.
    ----------------------------------------------------------------------------*/

    /*----------------------------------------------------------------------------
    ; LOCAL FUNCTION DEFINITIONS
    ; Function Prototype declaration
    ----------------------------------------------------------------------------*/

    /*----------------------------------------------------------------------------
    ; LOCAL VARIABLE DEFINITIONS
    ; Variable declaration - defined here and used outside this module
    ----------------------------------------------------------------------------*/


    /*----------------------------------------------------------------------------
    ; EXTERNAL FUNCTION REFERENCES
    ; Declare functions defined elsewhere and referenced in this module
    ----------------------------------------------------------------------------*/

    /*----------------------------------------------------------------------------
    ; EXTERNAL VARIABLES REFERENCES
    ; Declare variables used in this module but defined elsewhere
    ----------------------------------------------------------------------------*/




    const Int32 exp_rotation_N_256[64] =
    {

        0x5A820047,  0x5A7A0280, 0x5A6304B8, 0x5A3E06EF,
        0x5A0C0926,  0x59CB0B5B, 0x597D0D8E, 0x59210FBF,
        0x58B711EE,  0x5840141A, 0x57BB1643, 0x57281868,
        0x56881A8A,  0x55DB1CA8, 0x55201EC1, 0x545820D5,
        0x538322E5,  0x52A224EF, 0x51B326F3, 0x50B828F1,
        0x4FB12AE9,  0x4E9D2CDA, 0x4D7D2EC5, 0x4C5230A8,
        0x4B1A3284,  0x49D73458, 0x48883624, 0x472F37E7,
        0x45CA39A2,  0x445A3B54, 0x42E03CFD, 0x415C3E9C,
        0x3FCE4032,  0x3E3541BE, 0x3C944340, 0x3AE844B7,
        0x39344624,  0x37774786, 0x35B148DD, 0x33E44A29,
        0x320E4B69,  0x30304C9E, 0x2E4B4DC6, 0x2C5F4EE3,
        0x2A6C4FF4,  0x287250F8, 0x267251F0, 0x246D52DB,
        0x226153BA,  0x2051548B, 0x1E3B5550, 0x1C215607,
        0x1A0256B1,  0x17DF574E, 0x15B957DD, 0x138F585F,
        0x116358D3,  0x0F335939, 0x0D015992, 0x0ACE59DD,
        0x08985A1A,  0x06625A49, 0x042A5A6A, 0x01F25A7D
    };






    const Int32 exp_rotation_N_2048[512] =
    {

        0x5A820009,  0x5A820050, 0x5A820097, 0x5A8100DE,
        0x5A810125,  0x5A80016C, 0x5A7E01B3, 0x5A7D01FA,
        0x5A7B0242,  0x5A790289, 0x5A7702D0, 0x5A750317,
        0x5A72035E,  0x5A7003A5, 0x5A6D03EC, 0x5A6A0433,
        0x5A66047A,  0x5A6304C1, 0x5A5F0508, 0x5A5B054F,
        0x5A560596,  0x5A5205DD, 0x5A4D0624, 0x5A48066A,
        0x5A4306B1,  0x5A3E06F8, 0x5A38073F, 0x5A320786,
        0x5A2C07CD,  0x5A260814, 0x5A20085A, 0x5A1908A1,
        0x5A1208E8,  0x5A0B092F, 0x5A040975, 0x59FC09BC,
        0x59F40A03,  0x59EC0A49, 0x59E40A90, 0x59DC0AD7,
        0x59D30B1D,  0x59CA0B64, 0x59C10BAA, 0x59B80BF1,
        0x59AE0C37,  0x59A50C7E, 0x599B0CC4, 0x59910D0A,
        0x59860D51,  0x597C0D97, 0x59710DDD, 0x59660E23,
        0x595B0E6A,  0x594F0EB0, 0x59440EF6, 0x59380F3C,
        0x592C0F82,  0x59200FC8, 0x5913100E, 0x59061054,
        0x58F9109A,  0x58EC10E0, 0x58DF1126, 0x58D1116B,
        0x58C411B1,  0x58B611F7, 0x58A7123C, 0x58991282,
        0x588A12C8,  0x587B130D, 0x586C1353, 0x585D1398,
        0x584E13DD,  0x583E1423, 0x582E1468, 0x581E14AD,
        0x580D14F2,  0x57FD1538, 0x57EC157D, 0x57DB15C2,
        0x57CA1607,  0x57B9164C, 0x57A71690, 0x579516D5,
        0x5783171A,  0x5771175F, 0x575E17A3, 0x574C17E8,
        0x5739182C,  0x57261871, 0x571218B5, 0x56FF18FA,
        0x56EB193E,  0x56D71982, 0x56C319C6, 0x56AF1A0A,
        0x569A1A4F,  0x56851A93, 0x56701AD6, 0x565B1B1A,
        0x56461B5E,  0x56301BA2, 0x561A1BE5, 0x56041C29,
        0x55EE1C6D,  0x55D81CB0, 0x55C11CF3, 0x55AA1D37,
        0x55931D7A,  0x557C1DBD, 0x55651E00, 0x554D1E43,
        0x55351E86,  0x551D1EC9, 0x55051F0C, 0x54EC1F4F,
        0x54D31F91,  0x54BB1FD4, 0x54A12016, 0x54882059,
        0x546F209B,  0x545520DE, 0x543B2120, 0x54212162,
        0x540721A4,  0x53EC21E6, 0x53D12228, 0x53B62269,
        0x539B22AB,  0x538022ED, 0x5364232E, 0x53492370,
        0x532D23B1,  0x531123F2, 0x52F42434, 0x52D82475,
        0x52BB24B6,  0x529E24F7, 0x52812538, 0x52642578,
        0x524625B9,  0x522825FA, 0x520B263A, 0x51EC267A,
        0x51CE26BB,  0x51B026FB, 0x5191273B, 0x5172277B,
        0x515327BB,  0x513427FB, 0x5114283A, 0x50F4287A,
        0x50D428BA,  0x50B428F9, 0x50942938, 0x50742978,
        0x505329B7,  0x503229F6, 0x50112A35, 0x4FF02A74,
        0x4FCE2AB2,  0x4FAD2AF1, 0x4F8B2B2F, 0x4F692B6E,
        0x4F472BAC,  0x4F242BEA, 0x4F022C29, 0x4EDF2C67,
        0x4EBC2CA4,  0x4E992CE2, 0x4E752D20, 0x4E522D5D,
        0x4E2E2D9B,  0x4E0A2DD8, 0x4DE62E15, 0x4DC22E53,
        0x4D9D2E90,  0x4D792ECD, 0x4D542F09, 0x4D2F2F46,
        0x4D0A2F83,  0x4CE42FBF, 0x4CBF2FFB, 0x4C993038,
        0x4C733074,  0x4C4D30B0, 0x4C2630EC, 0x4C003127,
        0x4BD93163,  0x4BB2319E, 0x4B8B31DA, 0x4B643215,
        0x4B3D3250,  0x4B15328B, 0x4AED32C6, 0x4AC53301,
        0x4A9D333C,  0x4A753376, 0x4A4C33B1, 0x4A2433EB,
        0x49FB3425,  0x49D2345F, 0x49A83499, 0x497F34D3,
        0x4955350C,  0x492C3546, 0x4902357F, 0x48D835B9,
        0x48AD35F2,  0x4883362B, 0x48583664, 0x482E369C,
        0x480336D5,  0x47D7370E, 0x47AC3746, 0x4781377E,
        0x475537B6,  0x472937EE, 0x46FD3826, 0x46D1385E,
        0x46A43895,  0x467838CD, 0x464B3904, 0x461E393B,
        0x45F13972,  0x45C439A9, 0x459739E0, 0x45693A16,
        0x453C3A4D,  0x450E3A83, 0x44E03AB9, 0x44B13AEF,
        0x44833B25,  0x44553B5B, 0x44263B90, 0x43F73BC6,
        0x43C83BFB,  0x43993C30, 0x43693C65, 0x433A3C9A,
        0x430A3CCF,  0x42DA3D04, 0x42AA3D38, 0x427A3D6C,
        0x424A3DA0,  0x42193DD4, 0x41E93E08, 0x41B83E3C,
        0x41873E6F,  0x41563EA3, 0x41253ED6, 0x40F33F09,
        0x40C23F3C,  0x40903F6F, 0x405E3FA1, 0x402C3FD4,
        0x3FFA4006,  0x3FC74038, 0x3F95406A, 0x3F62409C,
        0x3F2F40CE,  0x3EFC4100, 0x3EC94131, 0x3E964162,
        0x3E634193,  0x3E2F41C4, 0x3DFB41F5, 0x3DC74226,
        0x3D934256,  0x3D5F4286, 0x3D2B42B6, 0x3CF642E6,
        0x3CC24316,  0x3C8D4346, 0x3C584375, 0x3C2343A5,
        0x3BEE43D4,  0x3BB84403, 0x3B834432, 0x3B4D4460,
        0x3B18448F,  0x3AE244BD, 0x3AAC44EB, 0x3A754519,
        0x3A3F4547,  0x3A094575, 0x39D245A2, 0x399B45CF,
        0x396445FD,  0x392D462A, 0x38F64656, 0x38BF4683,
        0x388746B0,  0x385046DC, 0x38184708, 0x37E04734,
        0x37A84760,  0x3770478B, 0x373847B7, 0x36FF47E2,
        0x36C7480D,  0x368E4838, 0x36554863, 0x361D488E,
        0x35E348B8,  0x35AA48E2, 0x3571490C, 0x35384936,
        0x34FE4960,  0x34C44989, 0x348B49B3, 0x345149DC,
        0x34164A05,  0x33DC4A2E, 0x33A24A56, 0x33684A7F,
        0x332D4AA7,  0x32F24ACF, 0x32B74AF7, 0x327C4B1F,
        0x32414B46,  0x32064B6E, 0x31CB4B95, 0x31904BBC,
        0x31544BE3,  0x31184C0A, 0x30DD4C30, 0x30A14C56,
        0x30654C7C,  0x30294CA2, 0x2FEC4CC8, 0x2FB04CEE,
        0x2F734D13,  0x2F374D38, 0x2EFA4D5D, 0x2EBD4D82,
        0x2E804DA7,  0x2E434DCB, 0x2E064DEF, 0x2DC94E13,
        0x2D8C4E37,  0x2D4E4E5B, 0x2D104E7E, 0x2CD34EA2,
        0x2C954EC5,  0x2C574EE8, 0x2C194F0A, 0x2BDB4F2D,
        0x2B9D4F4F,  0x2B5E4F71, 0x2B204F93, 0x2AE14FB5,
        0x2AA34FD7,  0x2A644FF8, 0x2A255019, 0x29E6503A,
        0x29A7505B,  0x2968507C, 0x2929509C, 0x28E950BC,
        0x28AA50DC,  0x286A50FC, 0x282B511C, 0x27EB513B,
        0x27AB515B,  0x276B517A, 0x272B5199, 0x26EB51B7,
        0x26AB51D6,  0x266A51F4, 0x262A5212, 0x25E95230,
        0x25A9524E,  0x2568526B, 0x25275288, 0x24E652A5,
        0x24A652C2,  0x246452DF, 0x242352FB, 0x23E25318,
        0x23A15334,  0x235F5350, 0x231E536B, 0x22DC5387,
        0x229B53A2,  0x225953BD, 0x221753D8, 0x21D553F3,
        0x2193540D,  0x21515427, 0x210F5442, 0x20CD545B,
        0x208B5475,  0x2048548F, 0x200654A8, 0x1FC354C1,
        0x1F8154DA,  0x1F3E54F2, 0x1EFB550B, 0x1EB85523,
        0x1E76553B,  0x1E335553, 0x1DF0556A, 0x1DAC5582,
        0x1D695599,  0x1D2655B0, 0x1CE355C7, 0x1C9F55DD,
        0x1C5C55F4,  0x1C18560A, 0x1BD55620, 0x1B915636,
        0x1B4D564B,  0x1B095661, 0x1AC55676, 0x1A82568B,
        0x1A3E569F,  0x19F956B4, 0x19B556C8, 0x197156DC,
        0x192D56F0,  0x18E95704, 0x18A45717, 0x1860572A,
        0x181B573E,  0x17D75750, 0x17925763, 0x174D5775,
        0x17095788,  0x16C4579A, 0x167F57AB, 0x163A57BD,
        0x15F557CE,  0x15B057DF, 0x156B57F0, 0x15265801,
        0x14E15812,  0x149C5822, 0x14575832, 0x14115842,
        0x13CC5851,  0x13875861, 0x13415870, 0x12FC587F,
        0x12B6588E,  0x1271589D, 0x122B58AB, 0x11E558B9,
        0x11A058C7,  0x115A58D5, 0x111458E2, 0x10CE58F0,
        0x108858FD,  0x1042590A, 0x0FFD5916, 0x0FB75923,
        0x0F71592F,  0x0F2A593B, 0x0EE45947, 0x0E9E5952,
        0x0E58595E,  0x0E125969, 0x0DCC5974, 0x0D85597E,
        0x0D3F5989,  0x0CF95993, 0x0CB2599D, 0x0C6C59A7,
        0x0C2559B1,  0x0BDF59BA, 0x0B9959C4, 0x0B5259CD,
        0x0B0B59D5,  0x0AC559DE, 0x0A7E59E6, 0x0A3859EE,
        0x09F159F6,  0x09AA59FE, 0x09645A05, 0x091D5A0D,
        0x08D65A14,  0x08905A1B, 0x08495A21, 0x08025A28,
        0x07BB5A2E,  0x07745A34, 0x072D5A3A, 0x06E75A3F,
        0x06A05A44,  0x06595A49, 0x06125A4E, 0x05CB5A53,
        0x05845A57,  0x053D5A5C, 0x04F65A60, 0x04AF5A63,
        0x04685A67,  0x04215A6A, 0x03DA5A6D, 0x03935A70,
        0x034C5A73,  0x03055A76, 0x02BE5A78, 0x02775A7A,
        0x02305A7C,  0x01E95A7D, 0x01A25A7F, 0x015B5A80,
        0x01135A81,  0x00CC5A82, 0x00855A82, 0x003E5A82
    };


#ifdef __cplusplus
}
#endif
