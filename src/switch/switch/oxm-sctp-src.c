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


static uint32_t sctp_src_field( const bool attr, const enum oxm_ofb_match_fields oxm_type );
static uint16_t sctp_src_length( const match *match );
static void pack_sctp_src( struct ofp_match *ofp_match, const match *match );


static struct oxm oxm_sctp_src = {
  OFPXMT_OFB_SCTP_SRC,
  ( uint16_t ) sizeof( uint16_t ),
  sctp_src_field,
  sctp_src_length,
  pack_sctp_src
};


void 
init_oxm_sctp_src( void ) {
  register_oxm( &oxm_sctp_src );
}


static uint32_t
sctp_src_field( const bool attr, const enum oxm_ofb_match_fields oxm_type ) {
  uint32_t field = 0;

  if ( attr && oxm_type == oxm_sctp_src.type ) {
    field = OXM_OF_SCTP_SRC;
  }
  return field;
}


static uint16_t
sctp_src_length( const match *match ) {
  uint16_t length = 0;
  
  if ( match->sctp_src.valid ) {
    length = oxm_sctp_src.length;
  }
  return length;
}


static void
pack_sctp_src( struct ofp_match *ofp_match, const match *match ) {
  if ( match->sctp_src.valid ) {
    ofp_match->type = oxm_sctp_src.type;
    ofp_match->length = oxm_sctp_src.length;
    memcpy( &ofp_match->oxm_fields, &match->sctp_src.value, oxm_sctp_src.length );
  }
}


/*
 * Local variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
