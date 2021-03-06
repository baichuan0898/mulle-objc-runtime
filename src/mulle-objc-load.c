//
//  mulle_objc_load.c
//  mulle-objc-runtime
//
//  Created by Nat! on 19.11.14.
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
#pragma clang diagnostic ignored "-Wparentheses"

#include "mulle-objc-load.h"

#include "mulle-objc-builtin.h"
#include "mulle-objc-callqueue.h"
#include "mulle-objc-class.h"
#include "mulle-objc-classpair.h"
#include "mulle-objc-universe-class.h"
#include "mulle-objc-dotdump.h"
#include "mulle-objc-infraclass.h"
#include "mulle-objc-metaclass.h"
#include "mulle-objc-methodlist.h"
#include "mulle-objc-propertylist.h"
#include "mulle-objc-protocollist.h"
#include "mulle-objc-universe.h"


#include "include-private.h"
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


static struct _mulle_objc_dependency  no_dependency =
{
   MULLE_OBJC_NO_CLASSID,
   MULLE_OBJC_NO_CATEGORYID
};


static void
   mulle_objc_loadcategory_enqueue_nofail( struct _mulle_objc_loadcategory *info,
                                           struct _mulle_objc_callqueue *loads,
                                           struct _mulle_objc_universe *universe);

static void
   mulle_objc_loadclass_enqueue_nofail( struct _mulle_objc_loadclass *info,
                                        struct _mulle_objc_callqueue *loads,
                                        struct _mulle_objc_universe *universe);


// this is destructive
static void    map_f( struct mulle_concurrent_hashmap *table,
                      mulle_objc_uniqueid_t uniqueid,
                      void (*f)( void *,
                                 struct _mulle_objc_callqueue *,
                                 struct _mulle_objc_universe *),
                      struct _mulle_objc_callqueue *loads,
                      struct _mulle_objc_universe *universe)
{
   struct mulle_concurrent_pointerarray            *list;
   struct mulle_concurrent_pointerarrayenumerator  rover;
   struct mulle_allocator                          *allocator;
   void                                            *value;

   if( ! table)
      return;

   list = _mulle_concurrent_hashmap_lookup( table, uniqueid);
   if( ! list)
      return;

   // because we are really single-threaded everything is much easier
   // just tear out the table (no-one can write into it concurrently)
   _mulle_concurrent_hashmap_remove( table, uniqueid, list);

   rover = mulle_concurrent_pointerarray_enumerate( list);
   while( value = mulle_concurrent_pointerarrayenumerator_next( &rover))
      (*f)( value, loads, universe);
   mulle_concurrent_pointerarrayenumerator_done( &rover);

   _mulle_concurrent_pointerarray_done( list);

   allocator = _mulle_objc_universe_get_allocator( universe);
   _mulle_allocator_free( allocator, list);
}


static mulle_objc_implementation_t
   _mulle_objc_methodlist_bsearch_dependencies_imp( struct _mulle_objc_methodlist *methods)
{
   struct _mulle_objc_method            *method;
   mulle_objc_implementation_t    imp;

   method = mulle_objc_method_bsearch( methods->methods,
                                      methods->n_methods,
                                      MULLE_OBJC_DEPENDENCIES_METHODID);
   if( ! method)
      return( 0);

   imp     = _mulle_objc_method_get_implementation( method);
   return( imp);
}


static struct _mulle_objc_dependency
   _mulle_objc_universe_fulfill_dependencies( struct _mulle_objc_universe *universe,
                                              struct _mulle_objc_infraclass *infra,
                                              struct _mulle_objc_dependency *dependencies)
{
   struct _mulle_objc_classpair    *pair;

   while( dependencies->classid)
   {
       if( universe->debug.trace.dependency)
            mulle_objc_universe_trace( universe, "+dependencies check class %08x \"%s\" ...",
                             dependencies->classid,
                             _mulle_objc_universe_describe_classid( universe, dependencies->classid));

      if( ! infra || (_mulle_objc_infraclass_get_classid( infra) != dependencies->classid))
      {
         infra = _mulle_objc_universe_lookup_infraclass( universe, dependencies->classid);
         if( ! infra)
         {
            if( universe->debug.trace.dependency)
            {
               mulle_objc_universe_trace( universe,
                                          "+dependencies class %08x \"%s\" is not present yet",
                                            dependencies->classid,
                                            _mulle_objc_universe_describe_classid( universe, dependencies->classid));
            }
            return( *dependencies);
         }
      }

      if( dependencies->categoryid)
      {
       if( universe->debug.trace.dependency)
            mulle_objc_universe_trace( universe, "+dependencies check category %08x,%08x \"%s( %s)\" ....",
                                          dependencies->classid,
                                          dependencies->categoryid,
                                          _mulle_objc_universe_describe_classid( universe, dependencies->classid),
                                          _mulle_objc_universe_describe_categoryid( universe, dependencies->categoryid));

         pair = _mulle_objc_infraclass_get_classpair( infra);
         if( ! _mulle_objc_classpair_has_categoryid( pair, dependencies->categoryid))
         {
            if( universe->debug.trace.dependency)
            {
               mulle_objc_universe_trace( universe,
                                          "+dependencies category %08x,%08x \"%s( %s)\" is not present yet",
                                          dependencies->classid,
                                          dependencies->categoryid,
                                          _mulle_objc_universe_describe_classid( universe, dependencies->classid),
                                          _mulle_objc_universe_describe_categoryid( universe, dependencies->categoryid));
            }
            return( *dependencies);
         }
      }
      ++dependencies;
   }

   return( no_dependency);
}


#pragma mark - classes

static void  loadclass_fprintf( FILE *fp,
                               struct _mulle_objc_loadclass *info)
{
   fprintf( fp, "class %08x \"%s\" (%p)",
           info->classid, info->classname, info);
}


static void  loadclass_trace( struct _mulle_objc_loadclass *info,
                              struct _mulle_objc_universe *universe,
                              char *format, ...)
{
   va_list   args;

   mulle_objc_universe_trace_nolf( universe, "");
   loadclass_fprintf( stderr, info);
   fputc( ' ', stderr);

   va_start( args, format);
   vfprintf( stderr, format, args);
   va_end( args);

   if( info->origin && universe->debug.print.print_origin)
      fprintf( stderr, " (%s)", info->origin);
   fputc( '\n', stderr);
}


static struct mulle_concurrent_pointerarray   *
   _mulle_objc_map_append_info( struct mulle_concurrent_hashmap *map,
                                mulle_objc_classid_t missingclassid,
                                void *info,
                                struct mulle_allocator *allocator)
{
   struct mulle_concurrent_pointerarray   *list;
   int                                    rval;

   for(;;)
   {
      list = _mulle_concurrent_hashmap_lookup( map, missingclassid);
      if( list)
         break;
      list = _mulle_allocator_calloc( allocator, 1, sizeof( *list));
      _mulle_concurrent_pointerarray_init( list, 1, allocator);

      rval = _mulle_concurrent_hashmap_insert( map, missingclassid, list);
      if( ! rval)
         break;

      // this can't really happen anymore can it ?
      assert( 0);

      _mulle_concurrent_pointerarray_done( list);
      _mulle_allocator_free( allocator, list);  // assume it was never
      if( rval != EEXIST)
         return( NULL);
   }

   _mulle_concurrent_pointerarray_add( list, info);
   return( list);
}


static int   mulle_objc_loadclass_delayedadd( struct _mulle_objc_loadclass *info,
                                              mulle_objc_classid_t missingclassid,
                                              struct _mulle_objc_universe *universe)
{
   struct mulle_concurrent_pointerarray   *list;

   if( ! info)
      mulle_objc_universe_fail_code( universe, EINVAL);

   assert( info->classid != missingclassid);

   list = _mulle_objc_map_append_info( &universe->waitqueues.classestoload,
                                       missingclassid,
                                       info,
                                       &universe->memory.allocator);
   if( ! list)
      mulle_objc_universe_fail_errno( universe);

   if( universe->debug.trace.dependency)
      loadclass_trace( info, universe,
                      "waits for class %08x \"%s\" to load "
                      "or gain more categories on list %p",
                      missingclassid,
                      _mulle_objc_universe_describe_classid( universe, missingclassid),
                      list);

   return( 0);
}


static struct _mulle_objc_dependency
    _mulle_objc_loadclass_fulfill_user_dependencies( struct _mulle_objc_loadclass *info,
                                                     struct _mulle_objc_universe *universe)
{
   struct _mulle_objc_dependency   *dependencies;
   mulle_objc_implementation_t     imp;

   if( ! info->classmethods)
      return( no_dependency);

   imp = _mulle_objc_methodlist_bsearch_dependencies_imp( info->classmethods);
   if( ! imp)
      return( no_dependency);

   if( universe->debug.trace.load_call)
      loadclass_trace( info, universe, "call +[%s dependencies]", info->classname);

    dependencies = (*imp)( NULL, MULLE_OBJC_DEPENDENCIES_METHODID, NULL);
    if( ! dependencies)
       mulle_objc_universe_fail_generic( universe, "error in mulle_objc_universe %p: %s "
                                                   "returned NULL for +dependencies\n",
                                                   universe,
                                                   info->classname);

   return( _mulle_objc_universe_fulfill_dependencies( universe, NULL, dependencies));
}


static struct _mulle_objc_dependency
    _mulle_objc_loadclass_fulfill_dependencies( struct _mulle_objc_loadclass *info,
                                                struct _mulle_objc_universe *universe,
                                                struct _mulle_objc_infraclass  **p_superclass)
{
   struct _mulle_objc_infraclass    *protocolclass;
   struct _mulle_objc_infraclass    *superclass;
   mulle_objc_classid_t             *classid_p;
   struct _mulle_objc_dependency    dependency;

   assert( info);
   assert( universe);
   assert( p_superclass);

   dependency = no_dependency;

   superclass    = NULL;
   *p_superclass = NULL;

   if( universe->debug.trace.dependency)
      loadclass_trace( info, universe, "dependency check superclass %08x \"%s\" ...",
                       info->superclassid,
                       info->superclassname);

   if( info->superclassid)
   {
      superclass    = _mulle_objc_universe_lookup_infraclass( universe, info->superclassid);
      *p_superclass = superclass;

      if( ! superclass)
      {
         if( universe->debug.trace.dependency)
           loadclass_trace( info, universe, "superclass %08x \"%s\" is "
                                            "not present yet",
                                            info->superclassid,
                                            info->superclassname);
         dependency.classid    = info->superclassid;
         dependency.categoryid = MULLE_OBJC_NO_CATEGORYID;
         return( dependency);
      }

      if( strcmp( info->superclassname, superclass->base.name))
         mulle_objc_universe_fail_generic( universe,
             "error in mulle_objc_universe %p: hash collision %08x "
             "for classnames \"%s\" \"%s\"",
             universe,
             info->superclassid,
             info->superclassname,
             superclass->base.name);
   }

   if( superclass && superclass->ivarhash != info->superclassivarhash)
   {
      if( ! universe->config.ignore_ivarhash_mismatch)
         mulle_objc_universe_fail_generic( universe,
              "error in mulle_objc_universe %p: superclass \"%s\" of \"%s\" "
              "has changed. Recompile \"%s\" (%s).\n",
              universe,
              info->superclassname,
              info->classname,
              info->classname,
              info->origin ? info->origin : "<unknown file>");
   }

   // protocol classes present ?
   if( info->protocolclassids)
   {
      for( classid_p = info->protocolclassids; *classid_p; ++classid_p)
      {
         // avoid duplication and waiting for seld
         if( *classid_p == info->superclassid || *classid_p == info->classid)
            continue;

         if( universe->debug.trace.dependency)
            loadclass_trace( info, universe, "dependency check protocolclass %08x \"%s\" ...",
                             *classid_p,
                             _mulle_objc_universe_describe_classid( universe, *classid_p));

         protocolclass = _mulle_objc_universe_lookup_infraclass( universe, *classid_p);
         if( ! protocolclass)
         {
            if( universe->debug.trace.dependency)
            {
               loadclass_trace( info, universe, "protocolclass %08x \"%s\" is "
                                                "not present yet",
                               *classid_p,
                               _mulle_objc_universe_describe_classid( universe, *classid_p));
            }

            dependency.classid    = *classid_p;
            dependency.categoryid = MULLE_OBJC_NO_CATEGORYID;
            return( dependency);
         }
      }
   }

   return( _mulle_objc_loadclass_fulfill_user_dependencies( info, universe));
}


void
   mulle_objc_loadclass_print_unfulfilled_dependency( struct _mulle_objc_loadclass *info,
                                                      struct _mulle_objc_universe *universe)
{
   struct _mulle_objc_dependency   dependency;
   struct _mulle_objc_infraclass   *infra;
   char                            *s_class;

   if( ! info || ! universe)
      return;

   dependency = _mulle_objc_loadclass_fulfill_dependencies( info, universe, &infra);
   if( dependency.classid == MULLE_OBJC_NO_CLASSID)
      return;

   s_class = _mulle_objc_universe_describe_classid( universe, dependency.classid);
   if( universe->debug.print.stuck_class_coverage)
      printf( "%08x;%s;;\n", dependency.classid, s_class);

   fprintf( stderr, "\t%08x \"%s\" waiting for class %08x \"%s\"\n",
           info->classid, info->classname,
           dependency.classid, s_class);
}


// ensure the load class, minimally make sense
static int  mulle_objc_loadclass_is_sane( struct _mulle_objc_loadclass *info)
{
   if( ! info)
      return( 0);

   if( ! info->classname)
      return( 0);

   if( ! mulle_objc_uniqueid_is_sane_string( info->classid, info->classname))
      return( 0);

   // class method lists should have no owner
   if( info->classmethods && info->classmethods->owner)
      return( 0);
   if( info->instancemethods && info->instancemethods->owner)
      return( 0);

   return( 1);
}

//
// We call a method classExtraSize which should just return a size_t value
// it must not call any Objective-C code and in fact self is also passes as
// nil.
//
static size_t    call_classExtraSize( struct _mulle_objc_methodlist  *list)
{
   size_t                        extrasize;
   mulle_objc_implementation_t   imp;
   struct _mulle_objc_method     *method;

   extrasize = 0;
   if( list)
   {
      method = _mulle_objc_methodlist_search( list, 0x185d8c27); // classExtraSize
      if( method)
      {
         imp       = _mulle_objc_method_get_implementation( method);
         extrasize = ((size_t (*)( void *, mulle_objc_uniqueid_t)) imp)( NULL, 0x185d8c27);
      }
   }
   return( extrasize);
}


static mulle_objc_classid_t   _mulle_objc_loadclass_enqueue( struct _mulle_objc_loadclass *info,
                                                             struct _mulle_objc_callqueue *loads,
                                                             struct _mulle_objc_universe *universe)
{
   struct _mulle_objc_classpair    *pair;
   struct _mulle_objc_metaclass    *meta;
   struct _mulle_objc_infraclass   *infra;
   struct _mulle_objc_infraclass   *superclass;
   struct _mulle_objc_dependency   dependency;
   size_t                          extrasize;

   // root ?
   superclass = NULL;
   dependency = _mulle_objc_loadclass_fulfill_dependencies( info, universe, &superclass);
   if( dependency.classid != MULLE_OBJC_NO_CLASSID)
      return( dependency.classid);

   //
   // for those that reverse order their .o files in a shared library
   // categories of a class then the class
   // subclass first then superclass
   // this callqueue mechanism does the "right" thing
   //
   // ready to install

   extrasize = call_classExtraSize( info->classmethods);

   pair = mulle_objc_universe_new_classpair( universe, info->classid,
                                                       info->classname,
                                                       info->instancesize,
                                                       extrasize,
                                                       superclass);
   if( ! pair)
      mulle_objc_universe_fail_errno( universe);  // unfailing vectors through there

   _mulle_objc_classpair_set_loadclass( pair, info);
   mulle_objc_classpair_add_protocollist_nofail( pair, info->protocols);
   mulle_objc_classpair_add_protocolclassids_nofail( pair, info->protocolclassids);

   meta = _mulle_objc_classpair_get_metaclass( pair);

   mulle_objc_metaclass_add_methodlist_nofail( meta, info->classmethods);
   mulle_objc_methodlist_add_load_to_callqueue_nofail( info->classmethods, meta, loads);

   infra = _mulle_objc_classpair_get_infraclass( pair);
   assert( meta == _mulle_objc_class_get_metaclass( &infra->base));

   _mulle_objc_infraclass_set_ivarhash( infra, info->classivarhash);

   mulle_objc_infraclass_add_ivarlist_nofail( infra, info->instancevariables);
   mulle_objc_infraclass_add_propertylist_nofail( infra, info->properties);
   mulle_objc_infraclass_add_methodlist_nofail( infra, info->instancemethods);

   if( info->fastclassindex >= 0)
      _mulle_objc_universe_set_fastclass( universe, infra, info->fastclassindex);

   if( mulle_objc_universe_add_infraclass( universe, infra))
   {
      switch( errno)
      {
      default  :
         mulle_objc_universe_fail_generic( universe,
               "error addding class %08x \"%s\" to mulle_objc_universe %p "
               "errno=%d\n",
               infra->base.classid, infra->base.name, universe, errno);

      case EFAULT : // how can this happen if we check for superclass up there ?
         mulle_objc_universe_fail_generic( universe,
               "error in mulle_objc_universe %p: "
               "superclass %08x \"%s\" of class %08x \"%s\" does not exist.\n",
                universe, 
                superclass->base.classid, superclass->base.name,
                infra->base.classid, infra->base.name);

      case EEXIST :
         mulle_objc_universe_fail_generic( universe,
               "error in mulle_objc_universe %p: "
               "duplicate class %08x \"%s\".\n",
                universe, infra->base.classid, infra->base.name);
      }
   }

   //
   // check if categories or classes are waiting for us ?
   //
   map_f( &universe->waitqueues.categoriestoload,
         info->classid,
         (void (*)()) mulle_objc_loadcategory_enqueue_nofail,
         loads,
         universe);
   map_f( &universe->waitqueues.classestoload,
         info->classid,
         (void (*)()) mulle_objc_loadclass_enqueue_nofail,
         loads,
         universe);

   return( MULLE_OBJC_NO_CLASSID);
}


static void
   mulle_objc_loadclass_enqueue_nofail( struct _mulle_objc_loadclass *info,
                                        struct _mulle_objc_callqueue *loads,
                                        struct _mulle_objc_universe *universe)
{
   mulle_objc_classid_t   missingclassid;

   // possibly get or create universe..

   if( ! mulle_objc_loadclass_is_sane( info))
      mulle_objc_universe_fail_code( universe, EINVAL);

   missingclassid = _mulle_objc_loadclass_enqueue( info, loads, universe);
   if( missingclassid != MULLE_OBJC_NO_CLASSID)
      if( mulle_objc_loadclass_delayedadd( info, missingclassid, universe))
         mulle_objc_universe_fail_errno( universe);
#ifdef MULLE_OBJC_DEBUG_SUPPORT
   if( universe->debug.trace.dump_universe)
      mulle_objc_universe_dotdump_frame_to_directory( universe, ".");
#endif
}


static void   _mulle_objc_loadclass_sort_lists( struct _mulle_objc_loadclass *lcls)
{
   qsort( lcls->protocolclassids,
          _mulle_objc_uniqueid_arraycount( lcls->protocolclassids),
          sizeof( mulle_objc_protocolid_t),
          (int (*)()) _mulle_objc_uniqueid_qsortcompare);
   mulle_objc_ivarlist_sort( lcls->instancevariables);
   mulle_objc_methodlist_sort( lcls->instancemethods);
   mulle_objc_methodlist_sort( lcls->classmethods);
   mulle_objc_propertylist_sort( lcls->properties);
   mulle_objc_protocollist_sort( lcls->protocols);
}


static void   loadprotocolclasses_dump( mulle_objc_protocolid_t *protocolclassids,
                                        char *prefix,
                                        struct _mulle_objc_protocollist *protocols)

{
   mulle_objc_protocolid_t      protoid;
   struct _mulle_objc_protocol  *protocol;

   for(; *protocolclassids; ++protocolclassids)
   {
      protoid = *protocolclassids;

      protocol = NULL;
      if( protocols)
         protocol = _mulle_objc_protocollist_search( protocols, protoid);
      if( protocol)
         fprintf( stderr, "%s@class %s;\n%s@protocol %s;\n", prefix, protocol->name, prefix, protocol->name);
      else
         fprintf( stderr, "%s@class %08x;\n%s@protocol #%08x;\n", prefix, protoid, prefix, protoid);
   }
}


static void   loadprotocols_dump( struct _mulle_objc_protocollist *protocols)

{
   struct _mulle_objc_protocol   *p;
   struct _mulle_objc_protocol   *sentinel;
   char                          *sep;

   fprintf( stderr, " <");
   sep = " ";
   p        = protocols->protocols;
   sentinel = &p[ protocols->n_protocols];
   for(; p < sentinel; ++p)
   {
      fprintf( stderr, "%s%s", sep, p->name);
      sep = ", ";
   }
   fprintf( stderr, ">");
}


static void   loadmethod_dump( struct _mulle_objc_method *method, char *prefix, char type)
{
   fprintf( stderr, "%s %c%s; // id=%08x signature=\"%s\" bits=0x%x\n",
            prefix,
            type,
            method->descriptor.name,
            method->descriptor.methodid,
            method->descriptor.signature,
            method->descriptor.bits);

}

static void   loadivar_dump( struct _mulle_objc_ivar *ivar, char *prefix)
{
   fprintf( stderr, "%s    %s; // @%ld id=%08x signature=\"%s\"\n",
           prefix,
           ivar->descriptor.name,
           (long) ivar->offset,
           ivar->descriptor.ivarid,
           ivar->descriptor.signature);
}

static void   loadproperty_dump( struct _mulle_objc_property *property, char *prefix)
{
   fprintf( stderr, "%s @property %s; // id=%08x ivarid=%08x signature=\"%s\" get=%08x set=%08x bits=0x%x\n",
           prefix,
           property->name,
           property->propertyid,
           property->ivarid,
           property->signature,
           property->getter,
           property->setter,
           property->bits);
}


static void   loadclass_dump( struct _mulle_objc_loadclass *p,
                              char *prefix)

{
   if( p->protocolclassids)
      loadprotocolclasses_dump( p->protocolclassids, prefix, p->protocols);

   fprintf( stderr, "%s@implementation %s", prefix, p->classname);
   if( p->superclassname)
      fprintf( stderr, " : %s", p->superclassname);

   if( p->protocols)
      loadprotocols_dump( p->protocols);

   fprintf( stderr, " // %08x", p->classid);
   if( p->origin)
      fprintf( stderr, ", %s", p->origin);

   fprintf( stderr, "\n");

   if( p->instancevariables)
   {
      fprintf( stderr, "%s{\n", prefix);
      struct _mulle_objc_ivar   *ivar;
      struct _mulle_objc_ivar   *sentinel;

      ivar     = p->instancevariables->ivars;
      sentinel = &ivar[ p->instancevariables->n_ivars];
      while( ivar < sentinel)
      {
         loadivar_dump( ivar, prefix);
         ++ivar;
      }
      fprintf( stderr, "%s}\n", prefix);
   }

   if( p->properties)
   {
      struct _mulle_objc_property   *property;
      struct _mulle_objc_property   *sentinel;

      property = p->properties->properties;
      sentinel = &property[ p->properties->n_properties];
      while( property < sentinel)
      {
         loadproperty_dump( property, prefix);
         ++property;
      }
   }

   if( p->classmethods)
   {
      struct _mulle_objc_method   *method;
      struct _mulle_objc_method   *sentinel;

      method   = p->classmethods->methods;
      sentinel = &method[ p->classmethods->n_methods];
      while( method < sentinel)
      {
         loadmethod_dump( method, prefix, '+');
         ++method;
      }
   }

   if( p->instancemethods)
   {
      struct _mulle_objc_method   *method;
      struct _mulle_objc_method   *sentinel;

      method = p->instancemethods->methods;
      sentinel = &method[ p->instancemethods->n_methods];
      while( method < sentinel)
      {
         loadmethod_dump( method, prefix, '-');
         ++method;
      }
   }

   fprintf( stderr, "%s@end\n", prefix);
}


#pragma mark - classlists

static void   mulle_objc_loadclasslist_enqueue_nofail( struct _mulle_objc_loadclasslist *list,
                                                       int need_sort,
                                                       struct _mulle_objc_callqueue *loads,
                                                       struct _mulle_objc_universe *universe)
{
   struct _mulle_objc_loadclass   **p_class;
   struct _mulle_objc_loadclass   **sentinel;

   if( ! list)
      return;

   p_class = list->loadclasses;
   sentinel = &p_class[ list->n_loadclasses];
   while( p_class < sentinel)
   {
      if( need_sort)
         _mulle_objc_loadclass_sort_lists( *p_class);

      mulle_objc_loadclass_enqueue_nofail( *p_class, loads, universe);
      p_class++;
   }
}

static void   loadclasslist_dump( struct _mulle_objc_loadclasslist *list,
                                  char *prefix)
{
   struct _mulle_objc_loadclass   **p;
   struct _mulle_objc_loadclass   **sentinel;

   if( ! list)
      return;

   p        = list->loadclasses;
   sentinel = &p[ list->n_loadclasses];
   while( p < sentinel)
      loadclass_dump( *p++, prefix);
}


#pragma mark - categories


static void  loadcategory_fprintf( FILE *fp,
                            struct _mulle_objc_loadcategory *info)
{
   fprintf( fp, "category %08x,%08x \"%s( %s)\" (%p)",
           info->classid, info->categoryid,
           info->classname, info->categoryname,
           info);
}


static void  loadcategory_trace( struct _mulle_objc_loadcategory *info,
                                 struct _mulle_objc_universe *universe,
                                 char *format, ...)
{
   va_list   args;

   mulle_objc_universe_trace_nolf( universe, "");
   loadcategory_fprintf( stderr, info);
   fputc( ' ', stderr);

   va_start( args, format);
   vfprintf( stderr, format, args);
   va_end( args);

   if( info->origin && universe->debug.print.print_origin)
      fprintf( stderr, " (%s)", info->origin);
   fputc( '\n', stderr);
}



static int  mulle_objc_loadcategory_delayedadd( struct _mulle_objc_loadcategory *info,
                                                mulle_objc_classid_t missingclassid,
                                                struct _mulle_objc_universe *universe)
{
   struct mulle_concurrent_pointerarray   *list;

   if( ! info)
   {
      errno = EINVAL;
      return( -1);
   }

   list = _mulle_objc_map_append_info( &universe->waitqueues.categoriestoload,
                                       missingclassid,
                                       info,
                                       &universe->memory.allocator);
   if( ! list)
      mulle_objc_universe_fail_errno( universe);

   if( universe->debug.trace.dependency)
      loadcategory_trace( info, universe, "waits for class %08x \"%s\" to load "
                         "or gain more categories on list %p",
                         missingclassid,
                         _mulle_objc_universe_describe_classid( universe, missingclassid),
                         list);

   return( 0);
}


static struct _mulle_objc_dependency
   _mulle_objc_loadcategory_fulfill_user_dependencies( struct _mulle_objc_loadcategory *info,
                                                       struct _mulle_objc_infraclass *infra)
{
   struct _mulle_objc_universe     *universe;
   struct _mulle_objc_dependency   *dependencies;
   mulle_objc_implementation_t     imp;

   assert( info);
   assert( infra);

   if( ! info->classmethods)
      return( no_dependency);

   imp = _mulle_objc_methodlist_bsearch_dependencies_imp( info->classmethods);
   if( ! imp)
      return( no_dependency);

   universe = _mulle_objc_infraclass_get_universe( infra);
   if( universe->debug.trace.load_call)
      loadcategory_trace( info, universe, "call +[%s(%s) dependencies]",
              info->classname,
              info->categoryname);

   dependencies = (*imp)( infra, MULLE_OBJC_DEPENDENCIES_METHODID, infra);
   if( ! dependencies)
      mulle_objc_universe_fail_generic( universe,
                                               "error in mulle_objc_universe %p: "
                                               "%s(%s) returned NULL for "
                                               "+dependencies\n",
                                               universe,
                                               info->classname,
                                               info->categoryname);

   return( _mulle_objc_universe_fulfill_dependencies( universe, infra, dependencies));
}


static struct _mulle_objc_dependency
   _mulle_objc_loadcategory_fulfill_dependencies( struct _mulle_objc_loadcategory *info,
                                                  struct _mulle_objc_universe *universe,
                                                  struct _mulle_objc_infraclass **p_class)
{
   struct _mulle_objc_infraclass   *infra;
   struct _mulle_objc_infraclass   *protocolclass;
   mulle_objc_classid_t            *classid_p;
   struct _mulle_objc_dependency   dependency;

   assert( info);
   assert( universe);
   assert( p_class);

   if( universe->debug.trace.dependency)
      loadcategory_trace( info, universe, "dependency check ...");

   // check class
   infra    = _mulle_objc_universe_lookup_infraclass( universe, info->classid);
   *p_class = infra;


   if( ! infra)
   {
      if( universe->debug.trace.dependency)
      {
         loadcategory_trace( info, universe,
                             "its class %08x \"%s\" is not present yet",
                             info->classid,
                             info->classname);
      }
      dependency.classid    = info->classid;
      dependency.categoryid = MULLE_OBJC_NO_CATEGORYID;
      return( dependency);
   }

   // protocol classes present ?
   if( info->protocolclassids)
   {
      for( classid_p = info->protocolclassids; *classid_p; ++classid_p)
      {
         // avoid duplication
         if( *classid_p == info->classid)
            continue;

         protocolclass = _mulle_objc_universe_lookup_infraclass( universe, *classid_p);
         if( ! protocolclass)
         {
            if( universe->debug.trace.dependency)
            {
               loadcategory_trace( info, universe,
                                   "protocolclass %08x \"%s\" is not present yet",
                                   *classid_p,
                                  _mulle_objc_universe_describe_classid( universe, *classid_p));
            }
            dependency.classid    = *classid_p;
            dependency.categoryid = MULLE_OBJC_NO_CATEGORYID;
            return( dependency);
         }
      }
   }

   return( _mulle_objc_loadcategory_fulfill_user_dependencies( info, infra));
}


void
   mulle_objc_loadcategory_print_unfulfilled_dependency( struct _mulle_objc_loadcategory *info,
                                                         struct _mulle_objc_universe *universe)
{
   struct _mulle_objc_dependency   dependency;
   struct _mulle_objc_infraclass   *infra;
   int                             old;
   char                            *s_class;
   char                            *s_category;

   if( ! info || ! universe)
      return;

   infra = NULL;

   // turn this off (it annoys) -- we are assumed to be single-threaded here
   // anyway

   old = universe->debug.trace.dependency;
   universe->debug.trace.dependency = 0;
   {
      dependency = _mulle_objc_loadcategory_fulfill_dependencies( info, universe, &infra);
   }
   universe->debug.trace.dependency = old;

   if( dependency.classid == MULLE_OBJC_NO_CLASSID)
      return;

   s_class = _mulle_objc_universe_describe_classid( universe, dependency.classid);
   if( dependency.categoryid == MULLE_OBJC_NO_CATEGORYID)
   {
      if( universe->debug.print.stuck_class_coverage)
         printf( "%08x;%s;;\n", dependency.classid, s_class);

      fprintf( stderr, "\tCategory %08x,%08x \"%s( %s)\" is waiting for "
                       "class %08x \"%s\"\n",
              info->classid, info->categoryid,
              info->classname, info->categoryname,
              dependency.classid,
              s_class);
      return;
   }

   s_category = _mulle_objc_universe_describe_categoryid( universe, dependency.categoryid);
   if( universe->debug.print.stuck_category_coverage)
      printf( "%08x;%s;%08x;%s\n", dependency.classid, s_class, dependency.categoryid, s_category);

   fprintf( stderr, "\tCategory %08x,%08x \"%s( %s)\" is waiting for "
                    "category %08x,%08x \"%s( %s)\"\n",
           info->classid, info->categoryid,
           info->classname, info->categoryname,
           dependency.classid,
           dependency.categoryid,
           s_class,
           s_category);
}


// ensure the load category, minimally makes sense
static int  mulle_objc_loadcategory_is_sane( struct _mulle_objc_loadcategory *info)
{
   if( ! info || ! info->categoryname || ! info->classname)
   {
      errno = EINVAL;
      return( 0);
   }

   if( ! mulle_objc_uniqueid_is_sane_string( info->classid, info->classname))
      return( 0);
   if( ! mulle_objc_uniqueid_is_sane_string( info->categoryid, info->categoryname))
      return( 0);

   return( 1);
}


static void  fail_duplicate_category( struct _mulle_objc_classpair *pair,
                                                 struct _mulle_objc_loadcategory *info)
{
   struct _mulle_objc_universe    *universe;
   char                           *info_origin;
   char                           *pair_origin;

   universe     = _mulle_objc_classpair_get_universe( pair);
   pair_origin = _mulle_objc_classpair_get_origin( pair);
   if( ! pair_origin)
      pair_origin = "<unknown origin>";

   info_origin = info->origin;
   if( ! info_origin)
      info_origin = "<unknown origin>";

   mulle_objc_universe_fail_generic( universe,
      "error in mulle_objc_universe %p: category %08x \"%s( %s)\" (%s) "
      "is already present in class %08x \"%s\" (%s).\n",
          universe,
          info->categoryid,
          info->classname,
          info->categoryname ? info->categoryname : "???",
          info_origin,
          _mulle_objc_classpair_get_classid( pair),
          _mulle_objc_classpair_get_name( pair),
          pair_origin);
}



static mulle_objc_classid_t
   _mulle_objc_loadcategory_enqueue( struct _mulle_objc_loadcategory *info,
                                     struct _mulle_objc_callqueue *loads,
                                     struct _mulle_objc_universe *universe)
{
   struct _mulle_objc_infraclass   *infra;
   struct _mulle_objc_metaclass    *meta;
   struct _mulle_objc_classpair    *pair;
   struct _mulle_objc_dependency   dependency;

   infra      = NULL;
   dependency = _mulle_objc_loadcategory_fulfill_dependencies( info, universe, &infra);
   if( dependency.classid != MULLE_OBJC_NO_CLASSID)
      return( dependency.classid);

   if( strcmp( info->classname, infra->base.name))
      mulle_objc_universe_fail_generic( universe,
         "error in mulle_objc_universe %p: hashcollision %08x "
         "for classnames \"%s\" and \"%s\"\n",
            universe, info->classid, info->classname, infra->base.name);
   if( info->classivarhash != infra->ivarhash)
      mulle_objc_universe_fail_generic( universe,
         "error in mulle_objc_universe %p: class %08x \"%s\" of "
         "category %08x \"%s( %s)\" has changed. Recompile %s\n",
            universe,
            info->classid,
            info->classname,
            info->categoryid,
            info->classname,
            info->categoryname ? info->categoryname : "???",
            info->origin ? info->origin : "<unknown origin>");

   pair = _mulle_objc_infraclass_get_classpair( infra);
   meta = _mulle_objc_classpair_get_metaclass( pair);

   if( info->categoryid && _mulle_objc_classpair_has_categoryid( pair, info->categoryid))
      fail_duplicate_category( pair, info);

   // checks for hash collisions
   mulle_objc_universe_add_category_nofail( universe, info->categoryid, info->categoryname);

   // the loader sets the categoryid as owner
   if( info->instancemethods && info->instancemethods->n_methods)
   {
      info->instancemethods->owner = (void *) (uintptr_t) info->categoryid;
      if( mulle_objc_class_add_methodlist( &infra->base, info->instancemethods))
         mulle_objc_universe_fail_errno( universe);
   }
   if( info->classmethods && info->classmethods->n_methods)
   {
      info->classmethods->owner = (void *) (uintptr_t) info->categoryid;
      if( mulle_objc_class_add_methodlist( &meta->base, info->classmethods))
         mulle_objc_universe_fail_errno( universe);
   }

   if( info->properties && info->properties->n_properties)
      if( mulle_objc_infraclass_add_propertylist( infra, info->properties))
         mulle_objc_universe_fail_errno( universe);

   //
   // TODO need to check that protocolids name are actually correct
   // emit protocolnames together with ids. Keep central directory in
   // universe
   //
   mulle_objc_classpair_add_protocollist_nofail( pair, info->protocols);
   mulle_objc_classpair_add_protocolclassids_nofail( pair, info->protocolclassids);
   if( info->categoryid)
      mulle_objc_classpair_add_categoryid_nofail( pair, info->categoryid);

   // this queues things up
   mulle_objc_methodlist_add_load_to_callqueue_nofail( info->classmethods, meta, loads);

   //
   // retrigger those who are waiting for their dependencies
   //
   map_f( &universe->waitqueues.categoriestoload,
          info->classid,
          (void (*)()) mulle_objc_loadcategory_enqueue_nofail,
          loads,
          universe);
   map_f( &universe->waitqueues.classestoload,
          info->classid,
          (void (*)()) mulle_objc_loadclass_enqueue_nofail,
          loads,
          universe);

   return( MULLE_OBJC_NO_CLASSID);
}



static void
   mulle_objc_loadcategory_enqueue_nofail( struct _mulle_objc_loadcategory *info,
                                           struct _mulle_objc_callqueue *loads,
                                           struct _mulle_objc_universe *universe)
{
   mulle_objc_classid_t   missingclassid;

   if( ! mulle_objc_loadcategory_is_sane( info))
      mulle_objc_universe_fail_code( universe, EINVAL);

   missingclassid = _mulle_objc_loadcategory_enqueue( info, loads, universe);
   if( missingclassid != MULLE_OBJC_NO_CLASSID)
      if( mulle_objc_loadcategory_delayedadd( info, missingclassid, universe))
         mulle_objc_universe_fail_errno( universe);

#ifdef MULLE_OBJC_DEBUG_SUPPORT
   if( universe->debug.trace.dump_universe)
      mulle_objc_universe_dotdump_frame_to_directory( universe, ".");
#endif
}


static void   loadcategory_dump( struct _mulle_objc_loadcategory *p,
                                 char *prefix)
{
   struct _mulle_objc_method   *method;
   struct _mulle_objc_method   *sentinel;

   if( p->protocolclassids)
      loadprotocolclasses_dump( p->protocolclassids, prefix, p->protocols);

   fprintf( stderr, "%s@implementation %s( %s)", prefix, p->classname, p->categoryname);

   if( p->protocols)
      loadprotocols_dump( p->protocols);

   fprintf( stderr, " // %08x,%08x", p->classid, p->categoryid);
   if( p->origin)
      fprintf( stderr, ", %s", p->origin);
   fprintf( stderr, "\n");

   if( p->classmethods)
   {
      method = p->classmethods->methods;
      sentinel = &method[ p->classmethods->n_methods];
      while( method < sentinel)
      {
         loadmethod_dump( method, prefix, '+');
         ++method;
      }
   }

   if( p->instancemethods)
   {
      method = p->instancemethods->methods;
      sentinel = &method[ p->instancemethods->n_methods];
      while( method < sentinel)
      {
         loadmethod_dump( method, prefix, '-');
         ++method;
      }
   }

   fprintf( stderr, "%s@end\n", prefix);
}


# pragma mark - categorylists

static void   _mulle_objc_loadcategory_sort_lists( struct _mulle_objc_loadcategory *lcat)
{
   qsort( lcat->protocolclassids,
          _mulle_objc_uniqueid_arraycount( lcat->protocolclassids),
          sizeof( mulle_objc_protocolid_t),
          (int (*)()) _mulle_objc_uniqueid_qsortcompare);

   mulle_objc_methodlist_sort( lcat->instancemethods);
   mulle_objc_methodlist_sort( lcat->classmethods);
   mulle_objc_propertylist_sort( lcat->properties);
   mulle_objc_protocollist_sort( lcat->protocols);
}


static void   mulle_objc_loadcategorylist_enqueue_nofail( struct _mulle_objc_loadcategorylist *list,
                                                          int need_sort,
                                                          struct _mulle_objc_callqueue *loads,
                                                          struct _mulle_objc_universe *universe)
{
   struct _mulle_objc_loadcategory   **p_category;
   struct _mulle_objc_loadcategory   **sentinel;

   if( ! list)
      return;

   p_category = list->loadcategories;
   sentinel   = &p_category[ list->n_loadcategories];
   while( p_category < sentinel)
   {
      if( need_sort)
         _mulle_objc_loadcategory_sort_lists( *p_category);

      mulle_objc_loadcategory_enqueue_nofail( *p_category, loads, universe);
      p_category++;
   }
}


static void   loadcategorylist_dump( struct _mulle_objc_loadcategorylist *list,
                                     char *prefix)
{
   struct _mulle_objc_loadcategory   **p;
   struct _mulle_objc_loadcategory   **sentinel;

   if( ! list)
      return;

   p        = list->loadcategories;
   sentinel = &p[ list->n_loadcategories];
   while( p < sentinel)
      loadcategory_dump( *p++, prefix);
}


# pragma mark - stringlists

static void   mulle_objc_loadstringlist_enqueue_nofail( struct _mulle_objc_loadstringlist *list,
                                                        struct _mulle_objc_universe *universe)
{
   struct _mulle_objc_object     **p_string;
   struct _mulle_objc_object     **sentinel;

   if( ! list)
      return;

   p_string = list->loadstrings;
   sentinel = &p_string[ list->n_loadstrings];

   // memo: the actual staticstringclass is likely not installed yet

   while( p_string < sentinel)
   {
      _mulle_objc_universe_add_staticstring( universe, *p_string);
      p_string++;
   }
}


# pragma mark - hashedstring

char   *_mulle_objc_loadhashedstring_bsearch( struct _mulle_objc_loadhashedstring *buf,
                                              unsigned int n,
                                              mulle_objc_uniqueid_t search)
{
   int   first;
   int   last;
   int   middle;
   struct _mulle_objc_loadhashedstring   *p;

   assert( mulle_objc_uniqueid_is_sane( search));

   first  = 0;
   last   = n - 1;  // unsigned not good (need extra if)
   middle = (first + last) / 2;

   while( first <= last)
   {
      p = &buf[ middle];
      if( p->uniqueid <= search)
      {
         if( p->uniqueid == search)
            return( p->string);

         first = middle + 1;
      }
      else
         last = middle - 1;

      middle = (first + last) / 2;
   }

   return( NULL);
}


char   *_mulle_objc_loadhashedstring_search( struct _mulle_objc_loadhashedstring *buf,
                                             unsigned int n,
                                             mulle_objc_uniqueid_t search)
{
   struct _mulle_objc_loadhashedstring   *p;
   struct _mulle_objc_loadhashedstring   *sentinel;

   p        = buf;
   sentinel = &p[ n];
   while( p < sentinel)
   {
      if( p->uniqueid == search)
         return( p->string);
      ++p;
   }
   return( NULL);
}

int  _mulle_objc_loadhashedstring_compare( struct _mulle_objc_loadhashedstring *a,
                                           struct _mulle_objc_loadhashedstring *b)
{
   mulle_objc_uniqueid_t   a_id;
   mulle_objc_uniqueid_t   b_id;

   a_id = a->uniqueid;
   b_id = b->uniqueid;
   if( a_id < b_id)
      return( -1);
   return( a_id != b_id);
}


void   mulle_objc_loadhashedstring_sort( struct _mulle_objc_loadhashedstring *methods,
                                         unsigned int n)
{
   if( ! methods)
      return;

   qsort( methods,
         n,
         sizeof( struct _mulle_objc_loadhashedstring),
         (int(*)())  _mulle_objc_loadhashedstring_compare);
}


int  mulle_objc_loadhashedstring_is_sane( struct _mulle_objc_loadhashedstring *p)
{
   if( ! p || ! p->string)
   {
      errno = EINVAL;
      return( 0);
   }

   if( ! mulle_objc_uniqueid_is_sane_string( p->uniqueid, p->string))
      return( 0);

   return( 1);
}


# pragma mark - loadsuperlist

static void   mulle_objc_loadsuperlist_enqueue_nofail( struct _mulle_objc_superlist *list,
                                                       struct _mulle_objc_universe *universe)
{
   ;
   struct _mulle_objc_super      *p;
   struct _mulle_objc_super      *sentinel;

   if( ! list)
      return;

   p        = &list->supers[ 0];
   sentinel = &p[ list->n_supers];
   while( p < sentinel)
   {
      mulle_objc_universe_add_super_nofail( universe, p);
      ++p;
   }
}


static void   loadsuper_dump( struct _mulle_objc_super *p,
                              char *prefix,
                              struct _mulle_objc_loadhashedstringlist *strings,
                              struct _mulle_objc_universe *universe)
{
   char   *classname;
   char   *methodname;

   // because we aren't sorted necessarily use slow search
   classname  = mulle_objc_loadhashedstringlist_search( strings, p->classid);
   if( ! classname)
      classname = _mulle_objc_universe_describe_classid( universe, p->superid);
   methodname = mulle_objc_loadhashedstringlist_search( strings, p->methodid);
   if( ! methodname && universe)
      methodname = _mulle_objc_universe_describe_methodid( universe, p->superid);

   fprintf( stderr, "%s // super %08x \"%s\" is class %08x \"%s\" "
                    "and method %08x \"%s\"\n",
           prefix,
           p->superid,
           p->name,
           p->classid, classname,
           p->methodid, methodname);
}


static void   loadsuperlist_dump( struct _mulle_objc_superlist *list,
                                  char *prefix,
                                  struct _mulle_objc_loadhashedstringlist *strings,
                                  struct _mulle_objc_universe *universe)
{
   struct _mulle_objc_super   *p;
   struct _mulle_objc_super   *sentinel;

   if( ! list)
      return;

   p        = list->supers;
   sentinel = &p[ list->n_supers];
   while( p < sentinel)
      loadsuper_dump( p++, prefix, strings, universe);
}

# pragma mark - hashedstringlists

static void   mulle_objc_loadhashedstringlist_enqueue_nofail( struct _mulle_objc_loadhashedstringlist *map,
                                                              struct _mulle_objc_universe   *universe,
                                                              int need_sort)
{
   if( ! map)
      return;

   if( need_sort)
      mulle_objc_loadhashedstringlist_sort( map);

   if( universe->debug.trace.hashstrings)
   {
      struct _mulle_objc_loadhashedstring  *p;
      struct _mulle_objc_loadhashedstring  *sentinel;
      unsigned int                         i;

      p        = map->loadentries;
      sentinel = &p[ map->n_loadentries];
      i        = 0;
      while( p < sentinel)
      {
         mulle_objc_universe_trace( universe,
                                   "#%d: %x is \"%s\"",
                                   i++,
                                   p->uniqueid,
                                   p->string);
         ++p;
      }

   }

   _mulle_objc_universe_add_loadhashedstringlist( universe, map);
}


# pragma mark - info

static void  dump_bits( unsigned int bits)
{
   char   *delim;

   delim ="";
   if( bits & _mulle_objc_loadinfo_unsorted)
   {
      fprintf( stderr, "unsorted");
      delim=", ";
   }

   if( bits & _mulle_objc_loadinfo_aaomode)
   {
      fprintf( stderr, "%s.aam", delim);
      delim=", ";
   }

   if( bits & _mulle_objc_loadinfo_notaggedptrs)
   {
      fprintf( stderr, "%s-fobjc-no-tps", delim);
      delim=", ";
   }

   if( bits & _mulle_objc_loadinfo_nofastcalls)
   {
      fprintf( stderr, "%s-fobjc-no-fcs", delim);
      delim=", ";
   }

   fprintf( stderr, "%s-O%d", delim, (bits >> 8) & 0x7);
}


static void   print_version( char *prefix, uint32_t version)
{
   fprintf( stderr, "%s=%u.%u.%u", prefix,
            mulle_objc_version_get_major( version),
            mulle_objc_version_get_minor( version),
            mulle_objc_version_get_patch( version));
}


static void   loadinfo_dump( struct _mulle_objc_loadinfo *info,
                             char *prefix,
                             struct _mulle_objc_universe *universe)
{
   fprintf( stderr, "%s", prefix);
   print_version( "universe", info->version.runtime);
   print_version( ", foundation", info->version.foundation);
   print_version( ", user", info->version.user);
   fprintf( stderr, " (");
   dump_bits( info->version.bits);
   fprintf( stderr, ")\n");

   loadclasslist_dump( info->loadclasslist, prefix);
   loadcategorylist_dump( info->loadcategorylist, prefix);
   loadsuperlist_dump( info->loadsuperlist, prefix, info->loadhashedstringlist, universe);
}


static void   call_load( struct _mulle_objc_metaclass *meta,
                         mulle_objc_methodid_t sel,
                         mulle_objc_implementation_t imp,
                         struct _mulle_objc_universe *universe)
{
   struct _mulle_objc_infraclass   *infra;

   if( universe->debug.trace.load_call)
     mulle_objc_universe_trace( universe,
                                "%08x \"%s\" call +[%s load]",
                                _mulle_objc_metaclass_get_classid( meta),
                                _mulle_objc_metaclass_get_name( meta),
                                _mulle_objc_metaclass_get_name( meta));

   // the "meta" class is not the object passed
   infra = _mulle_objc_metaclass_get_infraclass( meta);
   (*imp)( (struct _mulle_objc_object *) _mulle_objc_infraclass_as_class( infra),
                                         sel,
                                         _mulle_objc_metaclass_as_class( meta));
}


void    mulle_objc_universe_assert_loadinfo( struct _mulle_objc_universe *universe,
                                             struct _mulle_objc_loadinfo *info)
{
   unsigned int   optlevel;
   int            load_tps;
   int            mismatch;
   uintptr_t      bits;

   if( info->version.load != MULLE_OBJC_RUNTIME_LOAD_VERSION)
   {
      loadinfo_dump( info, "loadinfo:   ", universe);
      //
      // if you reach this, and you go huh ? it may mean, that an older
      // shared library version of the Foundation was loaded.
      //
      mulle_objc_universe_fail_inconsistency( universe,
         "mulle_objc_universe %p: the loaded binary was produced for "
         "load version %d, but this universe %u.%u.%u (%s) supports "
         "load version %d only",
            universe, info->version.load,
            mulle_objc_version_get_major( _mulle_objc_universe_get_version( universe)),
            mulle_objc_version_get_minor( _mulle_objc_universe_get_version( universe)),
            mulle_objc_version_get_patch( _mulle_objc_universe_get_version( universe)),
            _mulle_objc_universe_get_path( universe) ? _mulle_objc_universe_get_path( universe) : "???",
            MULLE_OBJC_RUNTIME_LOAD_VERSION);
   }

   if( info->version.foundation)
   {
      if( ! universe->foundation.universefriend.versionassert)
      {
         loadinfo_dump( info, "loadinfo:   ", universe);
         mulle_objc_universe_fail_inconsistency( universe,
            "mulle_objc_universe %p: foundation version set (0x%x), but "
            "universe foundation provides no versionassert",
               universe, info->version.foundation);
      }
      (*universe->foundation.universefriend.versionassert)( universe, &universe->foundation, &info->version);
   }

   if( info->version.user)
   {
      if( ! universe->userinfo.versionassert)
      {
         loadinfo_dump( info, "loadinfo:   ", universe);
         mulle_objc_universe_fail_inconsistency( universe,
            "mulle_objc_universe %p: loadinfo user version set (0x%x), but "
            "universe userinfo provides no versionassert",
               universe, info->version.user);
      }
      (*universe->userinfo.versionassert)( universe, &universe->userinfo, &info->version);
   }


   //
   // check for tagged pointers. What can happen ?
   // Remember that static strings can be tps!
   //
   // universe | Code   | Description
   // ---------|--------|--------------
   // No-TPS   | No-TPS | Works
   // No-TPS   | TPS    | Crashes
   // TPS      | No-TPS | Works, but slower. Does not mix with "TPS Code"
   // TPS      | TPS    | Works
   //
   // Allow loading of "NO TPS"-code into a "TPS" aware universe as long
   // as no "TPS"-code is loaded also.
   //
   load_tps = ! (info->version.bits & _mulle_objc_loadinfo_notaggedptrs);

   bits     = _mulle_objc_universe_get_loadbits( universe);

   mismatch = (load_tps && (bits & MULLE_OBJC_UNIVERSE_HAVE_NO_TPS_LOADS)) ||
              (! load_tps && (bits & MULLE_OBJC_UNIVERSE_HAVE_TPS_LOADS));
   mismatch |= universe->config.no_tagged_pointer && load_tps;
   if( mismatch)
   {
      loadinfo_dump( info, "loadinfo:   ", universe);
      mulle_objc_universe_fail_inconsistency( universe,
         "mulle_objc_universe %p: the universe is %sconfigured for "
         "tagged pointers, but classes are compiled differently",
             universe,
             universe->config.no_tagged_pointer ? "not " : "");
   }

   _mulle_objc_universe_set_loadbit( universe,
                                     load_tps
                                        ? MULLE_OBJC_UNIVERSE_HAVE_TPS_LOADS
                                        : MULLE_OBJC_UNIVERSE_HAVE_NO_TPS_LOADS);

   //
   // check that if a universename is given, that tps is off
   //
   if( info->loaduniverse && ! universe->config.no_tagged_pointer)
   {
      loadinfo_dump( info, "loadinfo:   ", universe);
      mulle_objc_universe_fail_inconsistency( universe,
            "mulle_objc_universe %p: the universe is not the default universe "
            "but TPS is enabled", universe);
   }

   // make sure everything is compiled with say -O0 (or -O1 at least)
   // if u want to...
   optlevel = ((info->version.bits >> 16) & 0x7);
   if( optlevel < universe->config.min_optlevel || optlevel > universe->config.max_optlevel)
   {
      loadinfo_dump( info, "loadinfo:   ", universe);
      mulle_objc_universe_fail_inconsistency( universe,
          "mulle_objc_universe %p: loadinfo was compiled with optimization "
          "level %d but universe requires between (%d and %d)",
               universe,
               optlevel,
               universe->config.min_optlevel,
               universe->config.max_optlevel);
   }


   //
   // Check for fast methods classes. What can happen ?
   // Compatibility of fast method funtions must be ascertained with version
   // numbering.
   //
   // universe | Code   | Description
   // ---------|--------|--------------
   // No-FCS   | No-FCS | Works
   // No-FCS   | FCS    | Crashes
   // FCS      | No-FCS | Crashes (wrong class size messes up classpair code)
   // FCS      | FCS    | Works
   //
#ifdef __MULLE_OBJC_FCS__
   if( info->version.bits & _mulle_objc_loadinfo_nofastcalls)
   {
      loadinfo_dump( info, "loadinfo:   ", universe);
      mulle_objc_universe_fail_inconsistency( universe,
         "mulle_objc_universe %p: the universe is compiled for fast methods, "
         "but classes and categories are not.", universe);
   }
#endif

#ifdef __MULLE_OBJC_NO_FCS__
   if( ! (info->version.bits & _mulle_objc_loadinfo_nofastcalls))
   {
      loadinfo_dump( info, "loadinfo:   ", universe);
      mulle_objc_universe_fail_inconsistency( universe,
         "mulle_objc_universe %p: the universe can't handle fast methods, "
         "but classes and categories use them.", universe);
   }
#endif
}


char   *mulle_objc_loadinfo_get_originator( struct _mulle_objc_loadinfo *info)
{
   char  *s;

   if( ! info)
      mulle_objc_universe_fail_code( NULL, EINVAL);

   s = NULL;
   if( info->loadclasslist && info->loadclasslist->n_loadclasses)
      s = info->loadclasslist->loadclasses[ 0]->origin;
   if( ! s)
      if( info->loadcategorylist && info->loadcategorylist->n_loadcategories)
         s = info->loadcategorylist->loadcategories[ 0]->origin;
   return( s);
}


//
// this is function called per .o file
//
void   mulle_objc_loadinfo_enqueue_nofail( struct _mulle_objc_loadinfo *info)
{
   struct _mulle_objc_universe             *universe;
   int                                     need_sort;
   static struct _mulle_objc_loaduniverse  empty;
   struct _mulle_objc_loaduniverse         *loaduniverse;

   // allow NULL input so mulle_objc_list can call this once, so the
   // linker can't optimize it away
   if( ! info)
      return;

   loaduniverse = info->loaduniverse;
   if( ! info->loaduniverse)
       loaduniverse = &empty;
   else
   {
      if( info->loaduniverse->universeid == MULLE_OBJC_DEFAULTUNIVERSEID)
         mulle_objc_universe_fail_inconsistency( universe,
            "mulle_objc_universe %p: loaduniverse uses default id 0 "
            "(don't emit loaduniverse for 0)", universe);
   }

   universe = mulle_objc_global_register_universe( loaduniverse->universeid,
                                                   loaduniverse->universename);
   assert( universe);

   if( _mulle_objc_universe_is_uninitialized( universe))
      mulle_objc_universe_fail_inconsistency( universe,
         "mulle_objc_universe %p: universe was not properly initialized "
         "by `__register_mulle_objc_universe`.", universe);

   if( ! universe->memory.allocator.calloc)
      mulle_objc_universe_fail_inconsistency( universe,
         "mulle_objc_universe %p: Has no allocator installed.", universe);

   if( ! _mulle_objc_thread_isregistered_universe_gc( universe))
   {
      mulle_objc_universe_fail_inconsistency( universe,
         "mulle_objc_universe %p: The function "
         "\"mulle_objc_loadinfo_enqueue_nofail\" is called from a "
         "non-registered thread.", universe, info->version.foundation);
   }

   _mulle_objc_universe_assert_runtimeversion( universe, &info->version);

   if( universe->callbacks.should_load_loadinfo)
   {
      if( ! (*universe->callbacks.should_load_loadinfo)( universe, info))
      {
         if( universe->debug.trace.loadinfo)
         {
            mulle_objc_universe_trace( universe,
                                       "loadinfo %p ignored on request\n",
                                       info);
            loadinfo_dump( info, "   ", universe);
         }
         return;
      }
   }

   mulle_objc_universe_assert_loadinfo( universe, info);

   if( universe->debug.trace.loadinfo)
   {
      mulle_objc_universe_trace( universe,
                                 "loads loadinfo %p in thread %p",
                                 info,
                                 (void *) mulle_thread_self());
      loadinfo_dump( info, "   ", universe);
   }

#ifdef MULLE_OBJC_DEBUG_SUPPORT
   if( universe->debug.trace.dump_universe)
      mulle_objc_universe_dotdump_frame_to_directory( universe, ".");
#endif

   if( universe->debug.trace.loadinfo)
   {
      mulle_objc_universe_trace( universe, "loading strings...");
   }

   // load strings in first, can be done unlocked
   mulle_objc_loadstringlist_enqueue_nofail( info->loadstringlist, universe);

   if( universe->debug.trace.loadinfo || universe->debug.trace.hashstrings)
   {
      char   *s;
      char   *sep;

      s = mulle_objc_loadinfo_get_originator( info);
      sep = ": ";
      if( ! s || !strlen( s))
         s = sep = "";
      mulle_objc_universe_trace( universe, "%s%sloading hashed strings...", s, sep);
   }

   // pass universe thru...
   need_sort = info->version.bits & _mulle_objc_loadinfo_unsorted;

   mulle_objc_loadhashedstringlist_enqueue_nofail( info->loadhashedstringlist,
                                                   universe,
                                                   need_sort);

   if( universe->debug.trace.loadinfo)
   {
      mulle_objc_universe_trace( universe, "loading super strings...");
   }

   // super strings are unproblematic also
   mulle_objc_loadsuperlist_enqueue_nofail( info->loadsuperlist, universe);

   if( universe->debug.trace.loadinfo)
   {
      mulle_objc_universe_trace( universe, "locking waitqueues...");
   }

   _mulle_objc_universe_lock_waitqueues( universe);
   {
      if( universe->debug.trace.loadinfo)
      {
         mulle_objc_universe_trace( universe,  "lock successful");
      }

      //
      // serialize the classes and categories for +load!
      // see dox/load/LOAD.md for more info
      //
      //
      // the load-queue is kinda superflous, since we are single-threaded
      // but the sequencing is nicer, since all classes and categories in
      // .o are now loaded (should document this (or nix it)))
      //
      // TODO: just callin +load in add_infraclass would do it find me thinks
      //
      struct _mulle_objc_callqueue   loads;

      if( mulle_objc_callqueue_init( &loads, &universe->memory.allocator))
         mulle_objc_universe_fail_errno( universe);

      //
      // the wait-queues are maintained in the universe
      // Because these are locked now anyway, the pointerarray is overkill
      // and not that useful, because you can't remove entries
      //

      if( universe->debug.trace.loadinfo)
         mulle_objc_universe_trace( universe,  "loading classes...");

      mulle_objc_loadclasslist_enqueue_nofail( info->loadclasslist,
                                               need_sort,
                                               &loads,
                                               universe);
      if( universe->debug.trace.loadinfo)
         mulle_objc_universe_trace( universe,  "loading categories...");

      mulle_objc_loadcategorylist_enqueue_nofail( info->loadcategorylist,
                                                  need_sort,
                                                  &loads,
                                                  universe);

      if( universe->debug.trace.loadinfo)
         mulle_objc_universe_trace( universe,  "performing +load calls...");

      mulle_objc_callqueue_walk( &loads, (void (*)()) call_load, universe);
      mulle_objc_callqueue_done( &loads);
   }

   if( universe->debug.trace.loadinfo)
      mulle_objc_universe_trace( universe, "unlocking waitqueues...");

   _mulle_objc_universe_unlock_waitqueues( universe);


   if( universe->debug.trace.loadinfo)
      mulle_objc_universe_trace( universe, "finished with loadinfo %p", info);
}

