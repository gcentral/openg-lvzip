/*
   refnum.h -- module to handle refnums for our library objects

   Version 1.1, Oct 22th, 2019

   Copyright (C) 2017-2019 Rolf Kalbermatter

   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
   following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the
	   following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
       following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of SciWare, James Kring, Inc., nor the names of its contributors may be used to endorse
	   or promote products derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#ifndef _refnum_H
#define _refnum_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvutil.h"

MgErr LibToMgErr(int err);

#define ZipMagic RTToL('Z','I','P','!')
#define UnzMagic RTToL('U','N','Z','!')
#define FileMagic RTToL('F','I','L','E')

MgErr lvzlibCreateRefnum(void *node, LVRefNum *refnum, uInt32 magic, LVBoolean autoreset);
MgErr lvzlibGetRefnum(LVRefNum *refnum, void **node, uInt32 magic);
MgErr lvzlibDisposeRefnum(LVRefNum *refnum, void **node, uInt32 magic);

#ifdef __cplusplus
}
#endif
#endif

