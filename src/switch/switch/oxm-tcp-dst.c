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


static uint32_t tcp_dst_field( const bool attr, const enum oxm_ofb_match_fields oxm_type );
static uint16_t tcp_dst_length( const match *match );
static void pack_tcp_dst( struct ofp_match *ofp_match, const match *match );


static struct oxm oxm_tcp_dst = {
  OFPXMT_OFB_TCP_DST,
  ( uint16_t ) sizeof( uint16_t ),
  tcp_dst_field,
  tcp_dst_length,
  pack_tcp_dst
};


void 
init_oxm_tcp_dst( void ) {
  register_oxm( &oxm_tcp_dst );
}


static uint32_t
tcp_dst_field( const bool attr, enum oxm_ofb_match_fields oxm_type ) {
  uint32_t field = 0;

  if ( attr && oxm_type == oxm_tcp_dst.type ) {
    field = OXM_OF_TCP_DST;
  }
  return field;
}


static uint16_t
tcp_dst_length( const match *match ) {
  uint16_t length = 0;
  
  if ( match->tcp_dst.valid ) {
    length = oxm_tcp_dst.length;
  }
  return length;
}


static void
pack_tcp_dst( struct ofp_match *ofp_match, const match *match ) {
  if ( match->tcp_dst.valid ) {
    ofp_match->type = oxm_tcp_dst.type;
    ofp_match->length = oxm_tcp_dst.length;
    memcpy( &ofp_match->oxm_fields, &match->tcp_dst.value, oxm_tcp_dst.length );
  }
}


/*
 * Local variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
