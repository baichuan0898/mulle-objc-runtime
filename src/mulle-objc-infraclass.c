//
//  mulle_objc_infraclass.h
//  mulle-objc-runtime
//
//  Created by Nat! on 17/04/07
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
#include "mulle-objc-infraclass.h"

#include "mulle-objc-class.h"
#include "mulle-objc-class-search.h"
#include "mulle-objc-classpair.h"
#include "mulle-objc-infraclass.h"
#include "mulle-objc-ivar.h"
#include "mulle-objc-ivarlist.h"
#include "mulle-objc-object-convenience.h"
#include "mulle-objc-property.h"
#include "mulle-objc-propertylist.h"
#include "mulle-objc-signature.h"
#include "mulle-objc-universe.h"


void    _mulle_objc_infraclass_plusinit( struct _mulle_objc_infraclass *infra,
                                         struct mulle_allocator *allocator)
{
   struct _mulle_objc_universe   *universe;
   struct mulle_allocator        *objectallocator;

   _mulle_concurrent_pointerarray_init( &infra->ivarlists, 0, allocator);
   _mulle_concurrent_pointerarray_init( &infra->propertylists, 0, allocator);

   _mulle_concurrent_hashmap_init( &infra->cvars, 0, allocator);

   universe          = _mulle_objc_infraclass_get_universe( infra);
   objectallocator   = _mulle_objc_universe_get_foundationallocator( universe);
   infra->allocator  = objectallocator->calloc
                          ? objectallocator
                          : _mulle_objc_universe_get_allocator( universe);
}


void    _mulle_objc_infraclass_plusdone( struct _mulle_objc_infraclass *infra)
{
   // this is done earlier now
   _mulle_concurrent_hashmap_done( &infra->cvars);

   _mulle_concurrent_pointerarray_done( &infra->ivarlists);

   // initially room for 2 categories with properties
   _mulle_concurrent_pointerarray_done( &infra->propertylists);
}


# pragma mark - sanitycheck

int   mulle_objc_infraclass_is_sane( struct _mulle_objc_infraclass *infra)
{
   void   *storage;

   if( ! infra)
   {
      errno = EINVAL;
      return( 0);
   }

   if( ! _mulle_objc_class_is_sane( &infra->base))
      return( 0);

   // need at least one possibly empty ivar list
   storage = _mulle_atomic_pointer_nonatomic_read( &infra->ivarlists.storage.pointer);
   if( ! storage || ! mulle_concurrent_pointerarray_get_count( &infra->ivarlists))
   {
      errno = ECHILD;
      return( 0);
   }

   // need at least one possibly empty property list
   storage = _mulle_atomic_pointer_nonatomic_read( &infra->propertylists.storage.pointer);
   if( ! storage || ! mulle_concurrent_pointerarray_get_count( &infra->propertylists))
   {
      errno = ECHILD;
      return( 0);
   }

   return( 1);
}


# pragma mark - properties
//
// doesn't check for duplicates
//
struct _mulle_objc_property   *_mulle_objc_infraclass_search_property( struct _mulle_objc_infraclass *infra,
                                                                       mulle_objc_propertyid_t propertyid)
{
   struct _mulle_objc_property                             *property;
   struct _mulle_objc_propertylist                         *list;
   struct mulle_concurrent_pointerarrayreverseenumerator   rover;
   unsigned int                                            n;

   n     = mulle_concurrent_pointerarray_get_count( &infra->propertylists);
   rover = mulle_concurrent_pointerarray_reverseenumerate( &infra->propertylists, n);

   while( list = _mulle_concurrent_pointerarrayreverseenumerator_next( &rover))
   {
      property = _mulle_objc_propertylist_search( list, propertyid);
      if( property)
         return( property);
   }
   mulle_concurrent_pointerarrayreverseenumerator_done( &rover);

   if( ! infra->base.superclass)
      return( NULL);
   return( _mulle_objc_infraclass_search_property( (struct _mulle_objc_infraclass *) infra->base.superclass, propertyid));
}


struct _mulle_objc_property  *mulle_objc_infraclass_search_property( struct _mulle_objc_infraclass *infra,
                                                                     mulle_objc_propertyid_t propertyid)
{
   assert( mulle_objc_uniqueid_is_sane( propertyid));

   if( ! infra)
   {
      errno = EINVAL;
      return( NULL);
   }

   return( _mulle_objc_infraclass_search_property( infra, propertyid));
}


static int   _mulle_objc_infraclass_add_propertylist( struct _mulle_objc_infraclass *infra,
                                                      struct _mulle_objc_propertylist *list)
{
   mulle_objc_propertyid_t                     last;
   struct _mulle_objc_property                 *property;
   struct _mulle_objc_propertylistenumerator   rover;
   struct _mulle_objc_universe                 *universe;

   /* register instance methods */
   last  = MULLE_OBJC_MIN_UNIQUEID - 1;
   rover = _mulle_objc_propertylist_enumerate( list);
   while( property = _mulle_objc_propertylistenumerator_next( &rover))
   {
      assert( mulle_objc_uniqueid_is_sane( property->propertyid));

      //
      // properties must be sorted by propertyid, so we can binary search them
      //
      if( last > property->propertyid)
      {
         errno = EDOM;
         return( -1);
      }
      last = property->propertyid;

      // it seems clearing readonly is incompatible, though it might be
      // backed by an ivar so don't do it
      if( ! (property->bits & _mulle_objc_property_readonly) && (property->bits & (_mulle_objc_property_setterclear|_mulle_objc_property_autoreleaseclear)))
      {
         _mulle_objc_infraclass_set_state_bit( infra, MULLE_OBJC_INFRACLASS_HAS_CLEARABLE_PROPERTY);
         break;
      }
   }
   _mulle_objc_propertylistenumerator_done( &rover);

   _mulle_concurrent_pointerarray_add( &infra->propertylists, list);

   return( 0);
}


int   mulle_objc_infraclass_add_propertylist( struct _mulle_objc_infraclass *infra,
                                              struct _mulle_objc_propertylist *list)
{
   struct _mulle_objc_universe   *universe;

   if( ! infra)
   {
      errno = EINVAL;
      return( -1);
   }

   if( ! list)
   {
      if( _mulle_concurrent_pointerarray_get_count( &infra->propertylists) != 0)
         return( 0);

      universe = _mulle_objc_infraclass_get_universe( infra);
      list     = &universe->empty_propertylist;
      _mulle_concurrent_pointerarray_add( &infra->propertylists, list);
      return( 0);
   }

   return( _mulle_objc_infraclass_add_propertylist( infra, list));
}


void   mulle_objc_infraclass_add_propertylist_nofail( struct _mulle_objc_infraclass *infra,
                                                      struct _mulle_objc_propertylist *list)
{
   struct _mulle_objc_universe   *universe;

   if( mulle_objc_infraclass_add_propertylist( infra, list))
   {
      universe = _mulle_objc_infraclass_get_universe( infra);
      mulle_objc_universe_fail_errno( universe);
   }
}


# pragma mark - ivar lists

int   mulle_objc_infraclass_add_ivarlist( struct _mulle_objc_infraclass *infra,
                                          struct _mulle_objc_ivarlist *list)
{
   struct _mulle_objc_universe   *universe;

   if( ! infra)
   {
      errno = EINVAL;
      return( -1);
   }

   // only add empty list, if there is nothing there yet
   if( ! list)
   {

      if( _mulle_concurrent_pointerarray_get_count( &infra->ivarlists) != 0)
         return( 0);

      universe = _mulle_objc_infraclass_get_universe( infra);
      list     = &universe->empty_ivarlist;
   }

   _mulle_concurrent_pointerarray_add( &infra->ivarlists, list);
   return( 0);
}


void   mulle_objc_infraclass_add_ivarlist_nofail( struct _mulle_objc_infraclass *infra,
                                                     struct _mulle_objc_ivarlist *list)
{
   struct _mulle_objc_universe   *universe;

   if( mulle_objc_infraclass_add_ivarlist( infra, list))
   {
      universe = _mulle_objc_infraclass_get_universe( infra);
      mulle_objc_universe_fail_errno( universe);
   }
}

# pragma mark - ivars

//
// doesn't check for duplicates
//
struct _mulle_objc_ivar   *_mulle_objc_infraclass_search_ivar( struct _mulle_objc_infraclass *infra,
                                                               mulle_objc_ivarid_t ivarid)
{
   struct _mulle_objc_ivar                                 *ivar;
   struct _mulle_objc_ivarlist                             *list;
   struct mulle_concurrent_pointerarrayreverseenumerator   rover;
   unsigned int                                            n;

   n     = mulle_concurrent_pointerarray_get_count( &infra->ivarlists);
   rover = mulle_concurrent_pointerarray_reverseenumerate( &infra->ivarlists, n);

   while( list = _mulle_concurrent_pointerarrayreverseenumerator_next( &rover))
   {
      ivar = _mulle_objc_ivarlist_search( list, ivarid);
      if( ivar)
         return( ivar);
   }
   mulle_concurrent_pointerarrayreverseenumerator_done( &rover);

   if( ! infra->base.superclass)
      return( NULL);
   return( _mulle_objc_infraclass_search_ivar( (struct _mulle_objc_infraclass *) infra->base.superclass, ivarid));
}


struct _mulle_objc_ivar  *mulle_objc_infraclass_search_ivar( struct _mulle_objc_infraclass *infra,
                                                             mulle_objc_ivarid_t ivarid)
{
   assert( mulle_objc_uniqueid_is_sane( ivarid));

   if( ! infra)
   {
      errno = EINVAL;
      return( NULL);
   }

   return( _mulle_objc_infraclass_search_ivar( infra, ivarid));
}


# pragma mark - ivar walker

mulle_objc_walkcommand_t
	_mulle_objc_infraclass_walk_ivars( struct _mulle_objc_infraclass *infra,
                                      unsigned int inheritance,
                                      mulle_objc_walkivarscallback *f,
                                      void *userinfo)
{
   int                                                    rval;
   struct _mulle_objc_ivarlist                            *list;
   struct mulle_concurrent_pointerarrayreverseenumerator  rover;
   unsigned int                                           n;
   struct _mulle_objc_infraclass                          *superclass;
   // todo: need to lock class

   // only enable first (@implementation of class) on demand
   //  ->[0]    : implementation
   //  ->[1]    : category
   //  ->[n -1] : last category

   n = mulle_concurrent_pointerarray_get_count( &infra->ivarlists);
   if( inheritance & MULLE_OBJC_CLASS_DONT_INHERIT_CATEGORIES)
      n = 1;

   rover = mulle_concurrent_pointerarray_reverseenumerate( &infra->ivarlists, n);

   while( list = _mulle_concurrent_pointerarrayreverseenumerator_next( &rover))
   {
      if( rval = _mulle_objc_ivarlist_walk( list, f, infra, userinfo))
      {
         if( rval < mulle_objc_walk_ok)
            errno = ENOENT;
         return( rval);
      }
   }

   if( ! (inheritance & MULLE_OBJC_CLASS_DONT_INHERIT_SUPERCLASS))
   {
      superclass = _mulle_objc_infraclass_get_superclass( infra);
      if( superclass && superclass != infra)
         return( _mulle_objc_infraclass_walk_ivars( superclass, inheritance, f, userinfo));
   }

   return( mulle_objc_walk_ok);
}

# pragma mark - property walker

mulle_objc_walkcommand_t
	_mulle_objc_infraclass_walk_properties( struct _mulle_objc_infraclass *infra,
                                           unsigned int inheritance,
                                           mulle_objc_walkpropertiescallback *f,
                                           void *userinfo)
{
   int                                                     rval;
   struct _mulle_objc_propertylist                         *list;
   struct mulle_concurrent_pointerarrayreverseenumerator   rover;
   unsigned int                                            n;
   struct _mulle_objc_infraclass                           *superclass;

   // todo: need to lock class

   // only enable first (@implementation of class) on demand
   //  ->[0]    : implementation
   //  ->[1]    : category
   //  ->[n -1] : last category

   n = mulle_concurrent_pointerarray_get_count( &infra->propertylists);
   if( inheritance & MULLE_OBJC_CLASS_DONT_INHERIT_CATEGORIES)
      n = 1;

   rover = mulle_concurrent_pointerarray_reverseenumerate( &infra->propertylists, n);
   while( list = _mulle_concurrent_pointerarrayreverseenumerator_next( &rover))
   {
      if( rval = _mulle_objc_propertylist_walk( list, f, infra, userinfo))
         return( rval);
   }

   if( ! (inheritance & MULLE_OBJC_CLASS_DONT_INHERIT_SUPERCLASS))
   {
      superclass = _mulle_objc_infraclass_get_superclass( infra);
      if( superclass && superclass != infra)
         return( _mulle_objc_infraclass_walk_properties( superclass, inheritance, f, userinfo));
   }

   return( mulle_objc_walk_ok);
}


# pragma mark - infraclass walking

static int  print_categoryid( mulle_objc_protocolid_t categoryid,
                              struct _mulle_objc_classpair *pair,
                              void *userinfo)
{
   struct _mulle_objc_universe  *universe;

   universe = _mulle_objc_classpair_get_universe( pair);
   fprintf( stderr, "\t%08x \"%s\"\n",
            categoryid,
           _mulle_objc_universe_describe_categoryid( universe, categoryid));
   return( 0);
}



static char  footer[] = "(so it is not used as protocol class)\n";


struct bouncy_info
{
   void                          *userinfo;
   struct _mulle_objc_universe   *universe;
   void                          *parent;
   mulle_objc_walkcallback_t     callback;
   mulle_objc_walkcommand_t      rval;
};


static int   bouncy_property( struct _mulle_objc_property *property,
                              struct _mulle_objc_infraclass *infra,
                              void *userinfo)
{
   struct bouncy_info   *info;

   info       = userinfo;
   info->rval = (info->callback)( info->universe,
                                  property,
                                  mulle_objc_walkpointer_is_property,
                                  NULL,
                                  infra,
                                  info->userinfo);
   return( mulle_objc_walkcommand_is_stopper( info->rval));
}


static int   bouncy_ivar( struct _mulle_objc_ivar *ivar,
                          struct _mulle_objc_infraclass *infra,
                          void *userinfo)
{
   struct bouncy_info   *info;

   info       = userinfo;
   info->rval = (info->callback)( info->universe,
                                  ivar,
                                  mulle_objc_walkpointer_is_ivar,
                                  NULL,
                                  infra,
                                  info->userinfo);
   return( mulle_objc_walkcommand_is_stopper( info->rval));
}


mulle_objc_walkcommand_t
   mulle_objc_infraclass_walk( struct _mulle_objc_infraclass   *infra,
                               enum mulle_objc_walkpointertype_t  type,
                               mulle_objc_walkcallback_t callback,
                               void *parent,
                               void *userinfo)
{
   mulle_objc_walkcommand_t     cmd;
   struct bouncy_info           info;
   unsigned int                 inheritance;

   cmd = mulle_objc_class_walk( _mulle_objc_infraclass_as_class( infra),
                                 type,
                                 callback,
                                 parent,
                                 userinfo);
   if( cmd != mulle_objc_walk_ok)
      return( cmd);

   info.callback = callback;
   info.parent   = parent;
   info.userinfo = userinfo;
   info.universe = _mulle_objc_infraclass_get_universe( infra);
   inheritance   = _mulle_objc_infraclass_get_inheritance( infra);
   cmd = _mulle_objc_infraclass_walk_properties( infra, inheritance, bouncy_property, &info);
   if( cmd != mulle_objc_walk_ok)
      return( cmd);

   cmd = _mulle_objc_infraclass_walk_ivars( infra, inheritance, bouncy_ivar, &info);
   return( cmd);
}


# pragma mark - protocolclass check

//
// must be root, must conform to own protocol, must not have ivars
// must not conform to other protocols (it's tempting to conform to NSObject)
// If you conform to NSObject, NSObject methods will override your superclass(!)
//
int    mulle_objc_infraclass_is_protocolclass( struct _mulle_objc_infraclass *infra)
{
   struct _mulle_objc_universe         *universe;
   struct _mulle_objc_classpair       *pair;
   struct _mulle_objc_uniqueidarray   *array;
   int                                is_NSObject;
   int                                has_categories;
   unsigned int                       inheritance;

   if( ! infra)
      return( 0);

   universe = _mulle_objc_infraclass_get_universe( infra);

   if( _mulle_objc_infraclass_get_superclass( infra))
   {
      if( universe->debug.warn.protocolclass)
      {
         if( _mulle_objc_infraclass_set_state_bit( infra, MULLE_OBJC_INFRACLASS_WARN_PROTOCOL))
            fprintf( stderr, "mulle_objc_universe %p warning: class \"%s\" "
                             "matches a protocol of same name, but it is "
                             "not a root class %s",
                    universe,
                    _mulle_objc_infraclass_get_name( infra),
                    footer);
      }
      return( 0);
   }

   if( infra->base.allocationsize > sizeof( struct _mulle_objc_objectheader))
   {
      if( universe->debug.warn.protocolclass)
      {
         if( _mulle_objc_infraclass_set_state_bit( infra, MULLE_OBJC_INFRACLASS_WARN_PROTOCOL))
            fprintf( stderr, "mulle_objc_universe %p warning: class \"%s\" matches a protocol of the same name"
                 ", but implements instance variables %s",
                   universe,
                    _mulle_objc_infraclass_get_name( infra),
                   footer);
      }
      return( 0);
   }

   pair = _mulle_objc_infraclass_get_classpair( infra);

   if( ! _mulle_objc_classpair_conformsto_protocolid( pair,
                                                     _mulle_objc_infraclass_get_classid( infra)))
   {
      if( universe->debug.warn.protocolclass)
      {
         if( _mulle_objc_infraclass_set_state_bit( infra, MULLE_OBJC_INFRACLASS_WARN_PROTOCOL))
            fprintf( stderr, "mulle_objc_universe %p warning: class \"%s\" matches a protocol but does not conform to it %s",
                    universe,
                    _mulle_objc_infraclass_get_name( infra),
                    footer);
      }
      return( 0);
   }

   if( _mulle_objc_classpair_get_protocolclasscount( pair))
   {
      if( universe->debug.warn.protocolclass)
      {
         if( _mulle_objc_infraclass_set_state_bit( infra, MULLE_OBJC_INFRACLASS_WARN_PROTOCOL))
            fprintf( stderr, "mulle_objc_universe %p warning: class \"%s\" matches a protocol but also inherits from other protocolclasses %s",
                    universe,
                    _mulle_objc_infraclass_get_name( infra),
                    footer);
      }
      return( 0);
   }

   //
   // check if someone bolted on categories to the protocol. In theory
   // it's OK, but them not being picked up might be a point of
   // confusion (On NSObject though its not worth a warning)
   //
   is_NSObject = _mulle_objc_infraclass_get_classid( infra) ==
                 _mulle_objc_universe_get_rootclassid( universe);
   if( is_NSObject)
      return( 1);

   inheritance = _mulle_objc_class_get_inheritance( _mulle_objc_infraclass_as_class( infra));
   if( inheritance & MULLE_OBJC_CLASS_DONT_INHERIT_PROTOCOL_CATEGORIES)
   {
      array = _mulle_atomic_pointer_read( &pair->p_categoryids.pointer);
      has_categories = array->n != 0;
      if( has_categories)
      {
         if( _mulle_objc_infraclass_set_state_bit( infra, MULLE_OBJC_INFRACLASS_WARN_PROTOCOL))
         {
            fprintf( stderr, "mulle_objc_universe %p warning: class \"%s\" conforms "
                    "to a protocol but has gained some categories, which "
                    "will be ignored.\n",
                    universe,
                    _mulle_objc_infraclass_get_name( infra));

            fprintf( stderr, "Categories:\n");
            _mulle_objc_classpair_walk_categoryids( pair,
                                                    MULLE_OBJC_CLASS_DONT_INHERIT_SUPERCLASS,
                                                    print_categoryid,
                                                    NULL);
         }
      }
   }

   return( 1);
}



static struct _mulle_objc_method  *
   _mulle_objc_infraclass_search_method_noinherit( struct _mulle_objc_infraclass *infra,
                                                   mulle_objc_methodid_t methodid)
{
   struct _mulle_objc_searcharguments   search;
   struct _mulle_objc_method            *method;
   struct _mulle_objc_metaclass         *meta;

   _mulle_objc_searcharguments_defaultinit( &search, methodid);
   meta   = _mulle_objc_infraclass_get_metaclass( infra);
   method = mulle_objc_class_search_method( _mulle_objc_metaclass_as_class( meta),
                                            &search,
                                            -1,  // inherit nothing
                                            NULL);
   return( method);
}


static void   _mulle_objc_infraclass_call_unloadmethod( struct _mulle_objc_infraclass *infra,
                                                        struct _mulle_objc_method *method,
                                                        char *name)
{
   struct _mulle_objc_universe     *universe;
   mulle_objc_implementation_t     imp;

   universe = _mulle_objc_infraclass_get_universe( infra);
   if( universe->debug.trace.initialize)
      mulle_objc_universe_trace( universe,
                                 "call +%s on class #%ld %s",
                                 name,
                                 _mulle_objc_classpair_get_classindex( _mulle_objc_infraclass_get_classpair( infra)),
                                 _mulle_objc_infraclass_get_name( infra));

   imp = _mulle_objc_method_get_implementation( method);
   (*imp)( infra, _mulle_objc_method_get_methodid( method), infra);
}


void   _mulle_objc_infraclass_call_deinitialize( struct _mulle_objc_infraclass *infra)
{
   struct _mulle_objc_universe   *universe;
   struct _mulle_objc_method     *method;

   if( ! _mulle_objc_infraclass_get_state_bit( infra, MULLE_OBJC_INFRACLASS_INITIALIZE_DONE))
      return;

   method = _mulle_objc_infraclass_search_method_noinherit( infra,
                                                            MULLE_OBJC_DEINITIALIZE_METHODID);
   if( ! method)
      return;

   _mulle_objc_infraclass_call_unloadmethod( infra, method, "deinitialize");
}


// in reverse order
void   _mulle_objc_infraclass_call_unload( struct _mulle_objc_infraclass *infra)
{
   struct _mulle_objc_metaclass        *meta;
   struct _mulle_objc_class             *cls;
   struct _mulle_objc_method            *method;
   int                                  inheritance;
   struct _mulle_objc_searcharguments   search;
   mulle_objc_implementation_t          imp;

   meta = _mulle_objc_infraclass_get_metaclass( infra);
   cls  = _mulle_objc_metaclass_as_class( meta);

   inheritance = MULLE_OBJC_CLASS_DONT_INHERIT_SUPERCLASS|MULLE_OBJC_CLASS_DONT_INHERIT_PROTOCOLS;

   // search will find last recently added category first
   // but we need to call all
   _mulle_objc_searcharguments_defaultinit( &search, MULLE_OBJC_UNLOAD_METHODID);
   for(;;)
   {
      method = mulle_objc_class_search_method( cls, &search, inheritance, NULL);
      if( ! method)
         break;

      _mulle_objc_infraclass_call_unloadmethod( infra, method, "unload");
      _mulle_objc_searcharguments_previousmethodinit( &search, method);
   }
}

