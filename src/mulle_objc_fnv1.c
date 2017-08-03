//
//  mulle_objc_fnv1.c
//  mulle-objc-runtime
//
//  Created by Nat! on 19.04.16.
//  Copyright (c) 2016 Nat! - Mulle kybernetiK.
//  Copyright (c) 2016 Codeon GmbH.
//  All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:
//
//  Redistributions of source code must retain the above copyright notice, this
//  list of conditions and the following disclaimer.
//
//  Redistributions in binary form must reproduce the above copyright notice,
//  this list of conditions and the following disclaimer in the documentation
//  and/or other materials provided with the distribution.
//
//  Neither the name of Mulle kybernetiK nor the names of its contributors
//  may be used to endorse or promote products derived from this software
//  without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
//  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
//  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
//  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
//  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
//  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//  POSSIBILITY OF SUCH DAMAGE.
//
#include "mulle_objc_fnv1.h"


#define FNV1_32_PRIME   0x01000193


uint32_t   _mulle_objc_chained_fnv1_32( void *buf, size_t len, uint32_t hash)
{
   unsigned char   *s;
   unsigned char   *sentinel;

   s        = buf;
   sentinel = &s[ len];

   /*
    * FNV-1 hash each octet in the buffer
    */
   while( s < sentinel)
   {
      hash *= FNV1_32_PRIME;
      hash ^= (uint32_t) *s++;
   }

   return( hash);
}


#define FNV1_64_PRIME   0x100000001b3ULL


uint64_t   _mulle_objc_chained_fnv1_64( void *buf, size_t len, uint64_t hash)
{
   unsigned char   *s;
   unsigned char   *sentinel;

   s        = buf;
   sentinel = &s[ len];

   /*
    * FNV-1 hash each octet in the buffer
    */
   while( s < sentinel)
   {
      hash *= FNV1_64_PRIME;
      hash ^= *s++;
   }

   return( hash);
}


// Build it with:
// cc -o mulle_objc_fnv1 -DMAIN mulle_objc_fnv1.c mulle_objc_uniqueid.c
//
#ifdef MAIN
#include <stdio.h>
extern uint32_t  mulle_objc_uniqueid_from_string( char *s);

int main(int argc, const char * argv[])
{
   if( argc != 2)
      return( -1);

   printf( "%08x\n", (uint32_t) mulle_objc_uniqueid_from_string( (char *) argv[ 1]));
   return 0;
}

#endif
