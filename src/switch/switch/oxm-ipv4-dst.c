/*
 * Copyright (C) 2008-2013 NEC Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#include <stdint.h>
#include "trema.h"
#include "ofdp.h"
#include "oxm.h"


static uint32_t ipv4_dst_field( const bool attr, const enum oxm_ofb_match_fields oxm_type );
static uint16_t ipv4_dst_length( const match *match );
static void pack_ipv4_dst( struct ofp_match *ofp_match, const match *match );


static struct oxm oxm_ipv4_dst = {
  OFPXMT_OFB_IPV4_DST,
  ( uint16_t ) sizeof( uint32_t ),
  ipv4_dst_field,
  ipv4_dst_length,
  pack_ipv4_dst
};


void 
init_oxm_ipv4_dst( void ) {
  register_oxm( &oxm_ipv4_dst );
}


static uint32_t
ipv4_dst_field( const bool attr, const enum oxm_ofb_match_fields oxm_type ) {
  uint32_t field = 0;

  if ( attr && oxm_type == oxm_ipv4_dst.type ) {
    field = OXM_OF_IPV4_DST;
  }
  return field;
}


static uint16_t
ipv4_dst_length( const match *match ) {
  uint16_t length = 0;
  
  if ( match->ipv4_dst.valid ) {
    length = oxm_ipv4_dst.length;
    if ( match->ipv4_dst.mask != UINT32_MAX ) {
      length = ( uint16_t ) ( length * 2 );
    }
  }
  return length;
}


static void
pack_ipv4_dst( struct ofp_match *ofp_match, const match *match ) {
  if ( match->ipv4_dst.valid ) {
    ofp_match->type = oxm_ipv4_dst.type;
    ofp_match->length = oxm_ipv4_dst.length;
    memcpy( &ofp_match->oxm_fields, &match->ipv4_dst.value, oxm_ipv4_dst.length );
  }
}


/*
 * Local variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
