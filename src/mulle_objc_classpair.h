//
//  mulle_objc_classpair.h
//  mulle-objc
//
//  Created by Nat! on 17/04/08.
//  Copyright (c) 2017 Nat! - Mulle kybernetiK.
//  Copyright (c) 2017 Codeon GmbH.
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

#ifndef mulle_objc_classpair_h__
#define mulle_objc_classpair_h__

#include "mulle_objc_objectheader.h"
#include "mulle_objc_infraclass.h"
#include "mulle_objc_metaclass.h"
#include "mulle_objc_walktypes.h"
#include <mulle_concurrent/mulle_concurrent.h>


#define _MULLE_OBJC_CLASSPAIR_PADDING     (0x10 - ((sizeof( struct _mulle_objc_infraclass) + sizeof( struct _mulle_objc_objectheader)) & 0xF))

//
// Put all the common information like
// protocolids, categoryids and the like into the classpair
// structure. I should also have a separate space for infraclasspair,
// that just pertains to the infraclass.
//
struct _mulle_objc_classpair
{
   struct _mulle_objc_objectheader         infraclassheader;
   struct _mulle_objc_infraclass           infraclass;
   unsigned char                           _padding[ _MULLE_OBJC_CLASSPAIR_PADDING];
   struct _mulle_objc_objectheader         metaclassheader;
   struct _mulle_objc_metaclass            metaclass;

   // common stuff
   struct mulle_concurrent_pointerarray    protocolids;
   struct mulle_concurrent_pointerarray    categoryids;

   char                                    *origin;      // a start of shared info
};


# pragma mark - init and done

void    _mulle_objc_classpair_plusinit( struct _mulle_objc_classpair *pair,
                                              struct mulle_allocator *allocator);

void    _mulle_objc_classpair_plusdone( struct _mulle_objc_classpair *pair);

void    _mulle_objc_classpair_free( struct _mulle_objc_classpair *pair,
                                    struct mulle_allocator *allocator);



void   _mulle_objc_classpair_call_class_finalize( struct _mulle_objc_classpair *pair);
void   mulle_objc_classpair_free( struct _mulle_objc_classpair *pair,
                                     struct mulle_allocator *allocator);



# pragma mark - petty accessors


static inline struct _mulle_objc_infraclass   *_mulle_objc_classpair_get_infraclass( struct _mulle_objc_classpair *pair)
{
   return( &pair->infraclass);
}


static inline struct _mulle_objc_metaclass   *_mulle_objc_classpair_get_metaclass( struct _mulle_objc_classpair *pair)
{
   return( &pair->metaclass);
}


static inline struct _mulle_objc_infraclass   *mulle_objc_classpair_get_infraclass( struct _mulle_objc_classpair *pair)
{
   return( pair ? _mulle_objc_classpair_get_infraclass( pair) : NULL);
}



static inline struct _mulle_objc_metaclass   *mulle_objc_classpair_get_metaclass( struct _mulle_objc_classpair *pair)
{
   return( pair ? _mulle_objc_classpair_get_metaclass( pair) : NULL);
}


static inline void   _mulle_objc_classpair_set_origin( struct _mulle_objc_classpair *pair,
                                                      char *name)
{
   pair->origin = name;  // not copied gotta be a constant string
}


static inline char   *_mulle_objc_classpair_get_origin( struct _mulle_objc_classpair *pair)
{
   return( pair->origin);
}


# pragma mark - conveniences

static inline struct _mulle_objc_runtime   *_mulle_objc_classpair_get_runtime( struct _mulle_objc_classpair *pair)
{
   return( _mulle_objc_infraclass_get_runtime( _mulle_objc_classpair_get_infraclass( pair)));
}


static inline char   *_mulle_objc_classpair_get_name( struct _mulle_objc_classpair *pair)
{
   return( _mulle_objc_infraclass_get_name( _mulle_objc_classpair_get_infraclass( pair)));
}


static inline mulle_objc_classid_t   _mulle_objc_classpair_get_classid( struct _mulle_objc_classpair *pair)
{
   return( _mulle_objc_infraclass_get_classid( _mulle_objc_classpair_get_infraclass( pair)));
}


# pragma mark - infra/metaclass reverse calculations

static inline struct _mulle_objc_classpair   *_mulle_objc_infraclass_get_classpair( struct _mulle_objc_infraclass *infra)
{
   assert( infra->base.infraclass == NULL);
   return( (struct _mulle_objc_classpair *) _mulle_objc_object_get_objectheader( infra));
}


//
// this is faster than doing _mulle_objc_class_get_classpair_from_infraclass( cls->infraclass)
// because the result is just an address offset, not a dereference
//
static inline struct _mulle_objc_classpair   *_mulle_objc_metaclass_get_classpair( struct _mulle_objc_metaclass *meta)
{
   assert( meta->base.infraclass != NULL);

   return( (struct _mulle_objc_classpair *) &((char *) meta)[  - offsetof( struct _mulle_objc_classpair, metaclass)]);
}

# pragma mark - infra/metaclass conveniences


static inline struct _mulle_objc_metaclass   *_mulle_objc_infraclass_get_metaclass( struct _mulle_objc_infraclass *infra)
{
   struct _mulle_objc_classpair   *pair;

   pair = _mulle_objc_infraclass_get_classpair( infra);
   return( _mulle_objc_classpair_get_metaclass( pair));
}


static inline struct _mulle_objc_infraclass   *_mulle_objc_metaclass_get_infraclass( struct _mulle_objc_metaclass *cls)
{
   struct _mulle_objc_classpair   *pair;

   pair = _mulle_objc_metaclass_get_classpair( cls);
   return( _mulle_objc_classpair_get_infraclass( pair));
}


# pragma mark - class reverse


static inline struct _mulle_objc_classpair   *_mulle_objc_class_get_classpair( struct _mulle_objc_class *cls)
{
   if( ! cls->infraclass)
      return( _mulle_objc_infraclass_get_classpair( (struct _mulle_objc_infraclass *) cls));
   return( _mulle_objc_metaclass_get_classpair( (struct _mulle_objc_metaclass *) cls));
}



#pragma mark - categories

static inline int   _mulle_objc_classpair_has_category( struct _mulle_objc_classpair *pair, mulle_objc_categoryid_t categoryid)
{
   return( _mulle_concurrent_pointerarray_find( &pair->categoryids, (void *) (uintptr_t) categoryid));
}



static inline void   _mulle_objc_classpair_add_category( struct _mulle_objc_classpair *pair,
                                                        mulle_objc_categoryid_t categoryid)
{
   assert( pair);
   assert( categoryid != MULLE_OBJC_NO_CATEGORYID);
   assert( categoryid != MULLE_OBJC_INVALID_CATEGORYID);
   assert( ! _mulle_objc_classpair_has_category( pair, categoryid));

   _mulle_concurrent_pointerarray_add( &pair->categoryids, (void *) (uintptr_t) categoryid);
}


int   _mulle_objc_classpair_walk_categoryids( struct _mulle_objc_classpair *pair,
                                              int (*f)( mulle_objc_categoryid_t,
                                                       struct _mulle_objc_classpair *,
                                                       void *),
                                              void *userinfo);


void   mulle_objc_classpair_unfailing_add_category( struct _mulle_objc_classpair *cls,
                                                    mulle_objc_categoryid_t categoryid);





#pragma mark - protocols

static inline int   _mulle_objc_classpair_has_protocol( struct _mulle_objc_classpair *pair,
                                                       mulle_objc_protocolid_t protocolid)
{
   return( _mulle_concurrent_pointerarray_find( &pair->protocolids, (void *) (uintptr_t) protocolid));
}


int   _mulle_objc_classpair_walk_protocolids( struct _mulle_objc_classpair *pair,
                                              int (*f)( mulle_objc_protocolid_t,
                                                       struct _mulle_objc_classpair *,
                                                       void *),
                                              void *userinfo);

// searches hierarchy
int   _mulle_objc_classpair_conformsto_protocol( struct _mulle_objc_classpair *pair,
                                                 mulle_objc_protocolid_t protocolid);


static inline int   mulle_objc_classpair_conformsto_protocol( struct _mulle_objc_classpair *pair, mulle_objc_protocolid_t protocolid)
{
   if( ! pair)
      return( 0);
   return( _mulle_objc_classpair_conformsto_protocol( pair, protocolid));
}

void   _mulle_objc_classpair_add_protocol( struct _mulle_objc_classpair *pair,
                                           mulle_objc_protocolid_t protocolid);

void   mulle_objc_classpair_unfailing_add_protocols( struct _mulle_objc_classpair *pair,
                                                    mulle_objc_protocolid_t *protocolids);

# pragma mark - protocol class enumerator

struct _mulle_objc_protocolclassenumerator
{
   struct mulle_concurrent_pointerarrayenumerator   list_rover;
   struct _mulle_objc_classpair                     *pair;
};


static inline struct _mulle_objc_protocolclassenumerator
_mulle_objc_classpair_enumerate_protocolclasses( struct _mulle_objc_classpair *pair)
{
   struct _mulle_objc_protocolclassenumerator  rover;

   rover.list_rover = mulle_concurrent_pointerarray_enumerate( &pair->protocolids);
   rover.pair       = pair;
   return( rover);
}


static inline void  _mulle_objc_protocolclassenumerator_done( struct _mulle_objc_protocolclassenumerator *rover)
{
   mulle_concurrent_pointerarrayenumerator_done( &rover->list_rover);
}


static inline struct _mulle_objc_classpair  *_mulle_objc_protocolclassenumerator_get_classpair( struct _mulle_objc_protocolclassenumerator *rover)
{
   return( rover->pair);
}


struct _mulle_objc_infraclass  *_mulle_objc_protocolclassenumerator_next( struct _mulle_objc_protocolclassenumerator *rover);




#pragma mark - debug support

mulle_objc_walkcommand_t
   mulle_objc_classpair_walk( struct _mulle_objc_classpair *pair,
                              mulle_objc_walkcallback_t callback,
                              void *userinfo);



#pragma mark - conveniences for class


#if 0
static inline int   _mulle_objc_class_has_category( struct _mulle_objc_class *cls,
                                                   mulle_objc_categoryid_t categoryid)
{
   struct _mulle_objc_classpair   *pair;

   pair = _mulle_objc_class_get_classpair( cls);
   return( _mulle_objc_classpair_has_category( pair, categoryid));
}

static inline int   _mulle_objc_class_has_protocol( struct _mulle_objc_class *cls,
                                                   mulle_objc_protocolid_t protocolid)
{
   struct _mulle_objc_classpair   *pair;

   pair = _mulle_objc_class_get_classpair( cls);
   return( _mulle_objc_classpair_has_protocol( pair, protocolid));
}


static inline int   mulle_objc_class_conforms_to_protocol( struct _mulle_objc_class *cls, mulle_objc_protocolid_t protocolid)
{
   struct _mulle_objc_classpair   *pair;

   pair = _mulle_objc_class_get_classpair( cls);
   return( mulle_objc_classpair_conformsto_protocol( pair, protocolid));
}


#endif

#endif /* mulle_objc_classpair_h */