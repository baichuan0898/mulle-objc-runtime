//
//  mulle_objc_class.c
//  mulle-objc-runtime
//
//  Created by Nat! on 16/11/14.
//  Copyright (c) 2014 Nat! - Mulle kybernetiK.
//  Copyright (c) 2014 Codeon GmbH.
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
#include "mulle-objc-class.h"

#include "mulle-objc-classpair.h"
#include "mulle-objc-infraclass.h"
#include "mulle-objc-ivar.h"
#include "mulle-objc-ivarlist.h"
#include "mulle-objc-call.h"
#include "mulle-objc-callqueue.h"
#include "mulle-objc-kvccache.h"
#include "mulle-objc-metaclass.h"
#include "mulle-objc-method.h"
#include "mulle-objc-methodlist.h"
#include "mulle-objc-universe.h"
#include "mulle-objc-taggedpointer.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include "include-private.h"


// public but not publizied


# pragma mark - accessor

static char   *lookup_bitname( unsigned int bit)
{
   // some "known" values
   switch( bit)
   {
   case MULLE_OBJC_CLASS_CACHE_READY              : return( "CACHE_READY");
   case MULLE_OBJC_CLASS_ALWAYS_EMPTY_CACHE       : return( "ALWAYS_EMPTY_CACHE");
   case MULLE_OBJC_CLASS_FIXED_SIZE_CACHE         : return( "FIXED_SIZE_CACHE");
   case _MULLE_OBJC_CLASS_WARN_PROTOCOL           : return( "WARN_PROTOCOL");
   case _MULLE_OBJC_CLASS_IS_PROTOCOLCLASS        : return( "IS_PROTOCOLCLASS");
   case _MULLE_OBJC_CLASS_LOAD_SCHEDULED          : return( "LOAD_SCHEDULED");
   case _MULLE_OBJC_CLASS_HAS_CLEARABLE_PROPERTY  : return( "HAS_CLEARABLE_PROPERTY");
   case MULLE_OBJC_CLASS_INITIALIZING             : return( "INITIALIZING");
   case MULLE_OBJC_CLASS_INITIALIZE_DONE          : return( "INITIALIZE_DONE");
   case MULLE_OBJC_CLASS_FOUNDATION_BIT0          : return( "FOUNDATION #0");
   case MULLE_OBJC_CLASS_FOUNDATION_BIT1          : return( "FOUNDATION #1");
   }
   return( 0);
}


int   _mulle_objc_class_set_state_bit( struct _mulle_objc_class *cls,
                                       unsigned int bit)
{
   void   *state;
   void   *old;
   struct _mulle_objc_universe   *universe;
   char   *bitname;

   assert( bit);

   do
   {
      old   = _mulle_atomic_pointer_read( &cls->state);
      state = (void *) ((uintptr_t) old | bit);
      if( state == old)
         return( 0);
   }
   while( ! _mulle_atomic_pointer_weakcas( &cls->state, state, old));

   universe = _mulle_objc_class_get_universe( cls);
   if( universe->debug.trace.state_bit)
   {
      bitname = lookup_bitname( bit);
      mulle_objc_universe_trace( universe,
                                 "%s %08x \"%s\" (%p) "
                                 "gained the 0x%x bit (%s)",
                                 _mulle_objc_class_get_classtypename( cls),
                                 cls->classid, cls->name,
                                 cls,
                                 bit, bitname ? bitname : "???");
   }
   return( 1);
}


# pragma mark - initialization / deallocation

void   *_mulle_objc_object_call_class_needcache( void *obj,
                                                   mulle_objc_methodid_t methodid,
                                                   void *parameter,
                                                   struct _mulle_objc_class *cls);


void   _mulle_objc_class_init( struct _mulle_objc_class *cls,
                               char *name,
                               size_t  instancesize,
                               mulle_objc_classid_t classid,
                               struct _mulle_objc_class *superclass,
                               struct _mulle_objc_universe *universe)
{
   extern void   *_mulle_objc_object_call2_needcache( void *obj,
                                                        mulle_objc_methodid_t methodid,
                                                        void *parameter);
   extern mulle_objc_implementation_t
      _mulle_objc_class_superlookup2_needcache( struct _mulle_objc_class *cls,
                                                mulle_objc_superid_t superid);

   assert( universe);

   cls->name             = name;

   cls->superclass       = superclass;
   if( superclass)
   {
      cls->superclassid  = superclass->classid;
      cls->inheritance   = superclass->inheritance;
   }
   else
   {
      cls->superclassid  = MULLE_OBJC_NO_CLASSID;
      cls->inheritance   = universe->classdefaults.inheritance;

   }
   //   cls->nextclass        = superclass;
   cls->classid          = classid;
   cls->allocationsize   = sizeof( struct _mulle_objc_objectheader) + instancesize;
   cls->call             = _mulle_objc_object_call_class_needcache;
   cls->universe         = universe;

   cls->cachepivot.call2 = _mulle_objc_object_call2_needcache;
   cls->superlookup2     = _mulle_objc_class_superlookup2_needcache;
#ifdef HAVE_SUPERCACHE
   _mulle_atomic_pointer_nonatomic_write( &cls->supercachepivot.entries, universe->empty_cache.entries);
#endif
   _mulle_atomic_pointer_nonatomic_write( &cls->cachepivot.pivot.entries, universe->empty_cache.entries);
   _mulle_atomic_pointer_nonatomic_write( &cls->kvc.entries, universe->empty_cache.entries);

   _mulle_concurrent_pointerarray_init( &cls->methodlists, 0, &universe->memory.allocator);
#ifdef __MULLE_OBJC_FCS__
   _mulle_objc_fastmethodtable_init( &cls->vtab);
#endif
}


void   _mulle_objc_class_done( struct _mulle_objc_class *cls,
                               struct mulle_allocator *allocator)
{
   struct _mulle_objc_cache   *cache;

   assert( cls);
   assert( allocator);

#ifdef __MULLE_OBJC_FCS__
   _mulle_objc_fastmethodtable_done( &cls->vtab);
#endif
   _mulle_concurrent_pointerarray_done( &cls->methodlists);

   _mulle_objc_class_invalidate_kvccache( cls);

   cache = _mulle_objc_cachepivot_atomicget_cache( &cls->cachepivot.pivot);
   if( cache != &cls->universe->empty_cache)
      _mulle_objc_cache_free( cache, allocator);

#ifdef HAVE_SUPERCACHE
   {
      struct _mulle_objc_cache   *supercache;

      supercache = _mulle_objc_cachepivot_atomicget_cache( &cls->supercachepivot);
      if( supercache != &cls->universe->empty_cache)
         _mulle_objc_cache_free( supercache, allocator);
   }
#endif
}


# pragma mark - consistency check

static int  _mulle_objc_class_methodlists_are_sane( struct _mulle_objc_class *cls)
{
   void   *storage;

   // need at least one possibly empty message list
   storage = _mulle_atomic_pointer_nonatomic_read( &cls->methodlists.storage.pointer);
   if( ! storage || ! mulle_concurrent_pointerarray_get_count( &cls->methodlists))
   {
      errno = ECHILD;
      return( 0);
   }

   return( 1);
}

# pragma mark - consistency

int   __mulle_objc_class_is_sane( struct _mulle_objc_class *cls)
{
   //
   // just check for some glaring errors
   //
   if( ! cls)
   {
      errno = EINVAL;
      return( 0);
   }

   if( ! cls->name || ! strlen( cls->name))
   {
      errno = EINVAL;
      return( 0);
   }

   //
   // make sure the alignment is not off, so it might be treated as a
   // tagged pointer
   //
   if( mulle_objc_taggedpointer_get_index( cls))
   {
      errno = EACCES;
      return( 0);
   }

   if( ! mulle_objc_uniqueid_is_sane_string( cls->classid, cls->name))
      return( 0);

   assert( cls->call);
   assert( cls->cachepivot.call2);

   return( 1);
}


int   _mulle_objc_class_is_sane( struct _mulle_objc_class *cls)
{
   struct _mulle_objc_metaclass   *meta;

   if( ! __mulle_objc_class_is_sane( cls))
      return( 0);

   if( ! _mulle_objc_class_methodlists_are_sane( cls))
      return( 0);

   meta = _mulle_objc_class_get_metaclass( cls);
   if( ! meta)
   {
      errno = ECHILD;
      return( 0);
   }
   return( 1);
}


# pragma mark - caches

//
// pass methodid = 0, to invalidate all
//
int   _mulle_objc_class_invalidate_methodcacheentry( struct _mulle_objc_class *cls,
                                                     mulle_objc_methodid_t methodid)
{
   struct _mulle_objc_cacheentry   *entry;
   mulle_objc_uniqueid_t           offset;
   struct _mulle_objc_cache        *cache;

   if( _mulle_objc_class_get_state_bit( cls, MULLE_OBJC_CLASS_ALWAYS_EMPTY_CACHE))
      return( 0);

   cache = _mulle_objc_class_get_methodcache( cls);
   if( ! _mulle_atomic_pointer_read( &cache->n))
      return( 0);

   if( methodid != MULLE_OBJC_NO_METHODID)
   {
      assert( mulle_objc_uniqueid_is_sane( methodid));

      offset = _mulle_objc_cache_find_entryoffset( cache, methodid);
      entry  = (void *) &((char *) cache->entries)[ offset];

      // no entry is matching, fine
      if( ! entry->key.uniqueid)
         return( 0);
   }

   //
   // if we get NULL, from _mulle_objc_class_add_cacheentry_by_swapping_caches
   // someone else recreated the cache, fine by us!
   //
   for(;;)
   {
      // always break regardless of return value
      _mulle_objc_class_add_cacheentry_swappmethodcache( cls,
                                                                 cache,
                                                                 NULL,
                                                                 MULLE_OBJC_NO_METHODID,
                                                                 MULLE_OBJC_CACHESIZE_STAGNATE);
      break;
   }

   return( 0x1);
}


#ifdef HAVE_SUPERCACHE
int   _mulle_objc_class_invalidate_supercache( struct _mulle_objc_class *cls)
{
   struct _mulle_objc_cache   *supercache;

   if( _mulle_objc_class_get_state_bit( cls, MULLE_OBJC_CLASS_ALWAYS_EMPTY_CACHE))
      return( 0);

   supercache = _mulle_objc_class_get_supercache( cls);
   if( ! _mulle_atomic_pointer_read( &supercache->n))
      return( 0);

   //
   // if we get NULL, from _mulle_objc_class_add_entry_by_swapping_caches
   // someone else recreated the cache, fine by us!
   //
   for(;;)
   {
      // always break regardless of return value
      _mulle_objc_class_add_cacheentry_swapsupercache( cls,
                                                                supercache,
                                                                NULL,
                                                                MULLE_OBJC_NO_METHODID,
                                                                MULLE_OBJC_CACHESIZE_STAGNATE);
      break;
   }

   return( 0x1);
}
#endif


static int  invalidate_methodcacheentries( struct _mulle_objc_universe *universe,
                                           struct _mulle_objc_class *cls,
                                           enum mulle_objc_walkpointertype_t type,
                                           char *key,
                                           void *parent,
                                           struct _mulle_objc_methodlist *list)
{
   struct _mulle_objc_methodlistenumerator   rover;
   struct _mulle_objc_method                 *method;

   // preferably nothing there yet
   if( ! _mulle_objc_class_get_state_bit( cls, MULLE_OBJC_CLASS_CACHE_READY))
      return( mulle_objc_walk_ok);

   _mulle_objc_class_invalidate_kvccache( cls);

// not having a supercache is so much better (timing...)
#ifdef HAVE_SUPERCACHE
   // supercaches need to be invalidate regardless
   _mulle_objc_class_invalidate_supercache( cls);
#endif

   // if caches have been cleaned for class, it's done
   rover = _mulle_objc_methodlist_enumerate( list);
   while( method = _mulle_objc_methodlistenumerator_next( &rover))
      if( _mulle_objc_class_invalidate_methodcacheentry( cls, method->descriptor.methodid))
         break;
   _mulle_objc_methodlistenumerator_done( &rover);

   return( mulle_objc_walk_ok);
}


#pragma mark - methods

//
// The assumption on the runtime is, that you can only add but don't
// interpose. In this regard caches might be outdated but not wrong.
// TODO: What happens when a class gets added a methodlist and
// the a method expects the superclass to also have a methodlist added
// already, which it hasn't. Solved by +dependencies!
// A problem remains: the superclass gets an incompatible method added,
// overriding the old, but the class isn't updated yet and a call
// happens which then supercalls.
// Solution: late categories may only add methods, not overwrite
//
void   mulle_objc_class_didadd_methodlist( struct _mulle_objc_class *cls,
                                            struct _mulle_objc_methodlist *list)
{
   //
   // now walk through the method list again
   // and update all caches, that need it
   // this is SLOW
   //
   if( list && list->n_methods)
   {
      //
      // this optimization works as long as you are installing plain classes.
      //
      if( _mulle_atomic_pointer_read( &cls->universe->cachecount_1))
         mulle_objc_universe_walk_classes( cls->universe, (mulle_objc_walkcallback_t) invalidate_methodcacheentries, list);
   }
}


int   _mulle_objc_class_add_methodlist( struct _mulle_objc_class *cls,
                                        struct _mulle_objc_methodlist *list)
{
   struct _mulle_objc_methodlistenumerator   rover;
   struct _mulle_objc_method                 *method;
   struct _mulle_objc_universe               *universe;
   mulle_objc_uniqueid_t                     last;
   unsigned int                              n;

   if( ! list)
   {
      if( _mulle_concurrent_pointerarray_get_count( &cls->methodlists) != 0)
         return( 0);

      universe = _mulle_objc_class_get_universe( cls);
      list     = &universe->empty_methodlist;
   }

   /* register instance methods */
   n     = 0;
   last  = MULLE_OBJC_MIN_UNIQUEID - 1;
   rover = _mulle_objc_methodlist_enumerate( list);
   while( method = _mulle_objc_methodlistenumerator_next( &rover))
   {
      assert( mulle_objc_uniqueid_is_sane( method->descriptor.methodid));
      //
      // methods must be sorted by signed methodid, so we can binary search them
      // (in the future)
      //
      if( last > method->descriptor.methodid)
      {
         errno = EDOM;
         return( -1);
      }

      last = method->descriptor.methodid;
      mulle_objc_universe_register_descriptor_nofail( cls->universe,
                                                        &method->descriptor);

      if( _mulle_objc_descriptor_is_preload_method( &method->descriptor))
      {
         cls->preloads++;
      }
      ++n;
   }
   _mulle_objc_methodlistenumerator_done( &rover);

   _mulle_concurrent_pointerarray_add( &cls->methodlists, list);
   return( 0);
}


int   mulle_objc_class_add_methodlist( struct _mulle_objc_class *cls,
                                       struct _mulle_objc_methodlist *list)
{
   int  rval;

   if( ! cls)
   {
      errno = EINVAL;
      return( -1);
   }

   rval = _mulle_objc_class_add_methodlist( cls, list);
   if( ! rval)
      mulle_objc_class_didadd_methodlist( cls, list);
   return( rval);
}


void   mulle_objc_class_add_methodlist_nofail( struct _mulle_objc_class *cls,
                                               struct _mulle_objc_methodlist *list)
{
   int   error;

   error = mulle_objc_class_add_methodlist( cls, list);
   if( error)
      mulle_objc_universe_fail_code( cls->universe, error);
}



static mulle_objc_walkcommand_t
   _mulle_objc_class_protocol_walk_methods( struct _mulle_objc_class *cls,
                                            unsigned int inheritance,
                                            mulle_objc_method_walkcallback_t f,
                                            void *userinfo)
{
   mulle_objc_walkcommand_t                     rval;
   struct _mulle_objc_class                     *walk_cls;
   struct _mulle_objc_infraclass                *infra;
   struct _mulle_objc_metaclass                 *meta_proto_cls;
   struct _mulle_objc_infraclass                *proto_cls;
   struct _mulle_objc_classpair                 *pair;
   struct _mulle_objc_protocolclassenumerator   rover;
   int                                          is_meta;

   rval    = 0;
   pair    = _mulle_objc_class_get_classpair( cls);
   infra   = _mulle_objc_classpair_get_infraclass( pair);
   is_meta = _mulle_objc_class_is_metaclass( cls);

   rover = _mulle_objc_classpair_enumerate_protocolclasses( pair);
   while( proto_cls = _mulle_objc_protocolclassenumerator_next( &rover))
   {
      if( proto_cls == infra)
         continue;

      walk_cls = _mulle_objc_infraclass_as_class( proto_cls);
      if( is_meta)
      {
         meta_proto_cls = _mulle_objc_infraclass_get_metaclass( proto_cls);
         walk_cls       = _mulle_objc_metaclass_as_class( meta_proto_cls);
      }

      if( rval = _mulle_objc_class_walk_methods( walk_cls,
                                                 inheritance | walk_cls->inheritance,
                                                 f,
                                                 userinfo))
         break;
   }
   _mulle_objc_protocolclassenumerator_done( &rover);

   return( rval);
}


static unsigned int
   _mulle_objc_class_count_protocolspreloadinstancemethods( struct _mulle_objc_class *cls)
{
   struct _mulle_objc_class                     *walk_cls;
   struct _mulle_objc_classpair                 *pair;
   struct _mulle_objc_infraclass                *proto_cls;
   struct _mulle_objc_infraclass                *infra;
   struct _mulle_objc_metaclass                 *meta_proto_cls;
   struct _mulle_objc_protocolclassenumerator   rover;
   unsigned int                                 preloads;
   int                                          is_meta;

   preloads = 0;

   pair    = _mulle_objc_class_get_classpair( cls);
   infra   = _mulle_objc_classpair_get_infraclass( pair);
   rover   = _mulle_objc_classpair_enumerate_protocolclasses( pair);
   is_meta = _mulle_objc_class_is_metaclass( cls);

   while( proto_cls = _mulle_objc_protocolclassenumerator_next( &rover))
   {
      if( proto_cls == infra)
         continue;

      walk_cls = _mulle_objc_infraclass_as_class( proto_cls);
      if( is_meta)
      {
         meta_proto_cls = _mulle_objc_infraclass_get_metaclass( proto_cls);
         walk_cls       = _mulle_objc_metaclass_as_class( meta_proto_cls);
      }

      preloads += _mulle_objc_class_count_preloadmethods( walk_cls);
   }
   _mulle_objc_protocolclassenumerator_done( &rover);

   return( preloads);
}


# pragma mark - methods

unsigned int   _mulle_objc_class_count_preloadmethods( struct _mulle_objc_class *cls)
{
   struct _mulle_objc_class   *dad;
   unsigned int               preloads;

   preloads = cls->preloads;
   if( ! (cls->inheritance & MULLE_OBJC_CLASS_DONT_INHERIT_SUPERCLASS))
   {
      dad = cls;
      while( dad = dad->superclass)
         preloads += _mulle_objc_class_count_preloadmethods( dad);
   }

   if( ! (cls->inheritance & MULLE_OBJC_CLASS_DONT_INHERIT_PROTOCOLS))
      preloads += _mulle_objc_class_count_protocolspreloadinstancemethods( cls);

   return( preloads);
}


// 0: continue
mulle_objc_walkcommand_t
   _mulle_objc_class_walk_methods( struct _mulle_objc_class *cls,
                                   unsigned int inheritance,
                                   mulle_objc_method_walkcallback_t f,
                                   void *userinfo)
{
   mulle_objc_walkcommand_t                                rval;
   struct _mulle_objc_methodlist                           *list;
   struct mulle_concurrent_pointerarrayreverseenumerator   rover;
   unsigned int                                            n;
   unsigned int                                            tmp;

   // todo: need to lock class

   // only enable first (@implementation of class) on demand
   //  ->[0]    : implementation
   //  ->[1]    : category
   //  ->[n -1] : last category

   n = mulle_concurrent_pointerarray_get_count( &cls->methodlists);
   if( inheritance & MULLE_OBJC_CLASS_DONT_INHERIT_CATEGORIES)
      n = 1;

   rover = mulle_concurrent_pointerarray_reverseenumerate( &cls->methodlists, n);
   while( list = _mulle_concurrent_pointerarrayreverseenumerator_next( &rover))
   {
      if( rval = _mulle_objc_methodlist_walk( list, f, cls, userinfo))
      {
         if( rval < mulle_objc_walk_ok)
            errno = ENOENT;
         return( rval);
      }
   }

   if( ! (inheritance & MULLE_OBJC_CLASS_DONT_INHERIT_PROTOCOLS))
   {
      tmp = MULLE_OBJC_CLASS_DONT_INHERIT_SUPERCLASS;
      if( inheritance & MULLE_OBJC_CLASS_DONT_INHERIT_PROTOCOL_CATEGORIES)
         tmp |= MULLE_OBJC_CLASS_DONT_INHERIT_CATEGORIES;

      if( rval = _mulle_objc_class_protocol_walk_methods( cls, tmp, f, userinfo))
      {
         if( rval < mulle_objc_walk_ok)
            errno = ENOENT;
         return( rval);
      }
   }

   if( ! (inheritance & MULLE_OBJC_CLASS_DONT_INHERIT_SUPERCLASS))
   {
      if( cls->superclass && cls->superclass != cls)
         return( _mulle_objc_class_walk_methods( cls->superclass, inheritance, f, userinfo));
   }

   return( 0);
}



//
// lookup a method in class and categories, do not look through protocol
// class methodlists, as they are shared by other classes
//
struct lookup_method_ctxt
{
   mulle_objc_methodid_t      methodid;
   struct _mulle_objc_method  *found;
};


static mulle_objc_walkcommand_t   find_method( struct _mulle_objc_method *method,
                                               struct _mulle_objc_methodlist *list,
                                               struct _mulle_objc_class *cls,
                                               void *userinfo)
{
   struct lookup_method_ctxt   *ctxt = userinfo;

   if( _mulle_objc_method_get_methodid( method) == ctxt->methodid)
   {
      ctxt->found = method;
      return( mulle_objc_walk_done);
   }
   return( mulle_objc_walk_ok);
}


struct _mulle_objc_method  *
   _mulle_objc_class_lookup_method( struct _mulle_objc_class *cls,
                                    mulle_objc_methodid_t methodid)
{
   unsigned int                inheritance;
   struct lookup_method_ctxt   ctxt;

   assert( cls);

   ctxt.found    = NULL;
   ctxt.methodid = methodid;
   inheritance   = MULLE_OBJC_CLASS_DONT_INHERIT_PROTOCOLS |
                   MULLE_OBJC_CLASS_DONT_INHERIT_SUPERCLASS;
   if( _mulle_objc_class_walk_methods( cls, inheritance, find_method, &ctxt) == \
       mulle_objc_walk_done)
   {
      return( ctxt.found);
   }
   return( NULL);
}


#pragma mark - debug support

struct bouncy_info
{
   void                          *userinfo;
   struct _mulle_objc_universe   *universe;
   void                          *parent;
   mulle_objc_walkcallback_t     callback;
   mulle_objc_walkcommand_t      rval;
};



static int   bouncy_method( struct _mulle_objc_method *method,
                            struct _mulle_objc_methodlist *list,
                            struct _mulle_objc_class *cls,
                            void *userinfo)
{
   struct bouncy_info               *info;
   struct _mulle_objc_methodparent  parent;

   parent.cls  = cls;
   parent.list = list;
   info        = userinfo;
   info->rval  = (info->callback)( info->universe,
                                   method,
                                   mulle_objc_walkpointer_is_method,
                                   NULL,
                                   &parent,
                                   info->userinfo);
   return( mulle_objc_walkcommand_is_stopper( info->rval));
}


// don't expose, because it's bit too weird

mulle_objc_walkcommand_t
   mulle_objc_class_walk( struct _mulle_objc_class   *cls,
                          enum mulle_objc_walkpointertype_t  type,
                          mulle_objc_walkcallback_t callback,
                          void *parent,
                          void *userinfo)
{
   struct _mulle_objc_universe   *universe;
   mulle_objc_walkcommand_t      cmd;
   struct bouncy_info            info;
   unsigned int                  inheritance;

   universe = _mulle_objc_class_get_universe( cls);
   cmd      = (*callback)( universe, cls, type, NULL, parent, userinfo);
   if( cmd != mulle_objc_walk_ok)
      return( cmd);

   info.callback = callback;
   info.parent   = parent;
   info.userinfo = userinfo;
   info.universe = universe;

   // assume this is a universe walk, and protocols and superclass
   // will be visited anyway
   inheritance = MULLE_OBJC_CLASS_DONT_INHERIT_PROTOCOLS|MULLE_OBJC_CLASS_DONT_INHERIT_SUPERCLASS;

   cmd = _mulle_objc_class_walk_methods( cls, inheritance, bouncy_method, &info);
   return( cmd);
}



void   _mulle_objc_class_trace_alloc_instance( struct _mulle_objc_class *cls,
                                               void *obj,
                                               size_t extra)
{
   fprintf( stderr, "[==] %p instance %p allocated (\"%s\" (%08x)) ",
                     _mulle_objc_object_get_objectheader( obj),
                     obj,
                     _mulle_objc_class_get_name( cls),
                     _mulle_objc_class_get_classid( cls));
   if( extra)
      fprintf( stderr, " (+%ld)", extra);
   fputc( '\n', stderr);
}


// we don't have a mulle-objc-object.c so here
static void   _mulle_objc_object_trace_operation( void *obj, char *operation)
{
   struct _mulle_objc_class   *cls;

   cls = _mulle_objc_object_get_isa( obj);
   fprintf( stderr, "[==] %p instance %p %s (\"%s\" (%08x))\n",
                     _mulle_objc_object_get_objectheader( obj),
                     obj,
                     operation,
                     _mulle_objc_class_get_name( cls),
                     _mulle_objc_class_get_classid( cls));
}


void   _mulle_objc_object_trace_free( void *obj)
{
   _mulle_objc_object_trace_operation( obj, "freed");
}


void   _mulle_objc_object_trace_release( void *obj)
{
   _mulle_objc_object_trace_operation( obj, "released");
}


void   _mulle_objc_object_trace_retain( void *obj)
{
   _mulle_objc_object_trace_operation( obj, "retained");
}


void   _mulle_objc_object_trace_autorelease( void *obj)
{
   _mulle_objc_object_trace_operation( obj, "autoreleased");
}

