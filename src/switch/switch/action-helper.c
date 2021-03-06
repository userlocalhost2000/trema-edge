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


#include "trema.h"
#include "ofdp.h"
#include "action-tlv-interface.h"
#include "oxm-helper.h"


action_list *
_assign_actions( action_list *action_list, const struct ofp_action_header *action_hdr, uint16_t action_length ) {
  size_t offset = 0;
  while( action_length - offset >= sizeof( struct ofp_action_header ) ) {
    if ( action_hdr->type == OFPAT_OUTPUT ) {
      const struct ofp_action_output *action_output = ( const struct ofp_action_output * )( ( const char * )action_hdr + offset );
      action *ac = create_action_output( action_output->port, action_output->max_len );
      if ( ac != NULL ) {
        append_action( action_list, ac );
      }
    }
    else if ( action_hdr->type == OFPAT_COPY_TTL_OUT ) {
      append_action( action_list, create_action_copy_ttl_out() );
    }
    else if ( action_hdr->type == OFPAT_COPY_TTL_IN ) {
      append_action( action_list, create_action_copy_ttl_in() );
    }
    else if ( action_hdr->type == OFPAT_SET_MPLS_TTL ) {
      const struct ofp_action_mpls_ttl *action_mpls_ttl = ( const struct ofp_action_mpls_ttl * )( ( const char * )action_hdr + offset );
      append_action( action_list, create_action_set_mpls_ttl( action_mpls_ttl->mpls_ttl ) );
    }
    else if ( action_hdr->type == OFPAT_DEC_MPLS_TTL ) {
      append_action( action_list, create_action_dec_mpls_ttl() );
    }
    else if ( action_hdr->type == OFPAT_SET_NW_TTL ) {
      const struct ofp_action_nw_ttl *action_nw_ttl = ( const struct ofp_action_nw_ttl *)( ( const char * )action_hdr + offset );
      append_action( action_list, create_action_set_ipv4_ttl( action_nw_ttl->nw_ttl ) );
    }
    else if ( action_hdr->type == OFPAT_DEC_NW_TTL ) {
      append_action( action_list, create_action_dec_ipv4_ttl() );
    }
    else if ( action_hdr->type == OFPAT_PUSH_VLAN ) {
      const struct ofp_action_push *action_push = ( const struct ofp_action_push * )( ( const char * )action_hdr + offset );
      append_action( action_list, create_action_push_vlan( action_push->ethertype ) );
    }
    else if ( action_hdr->type == OFPAT_PUSH_MPLS ) {
      const struct ofp_action_push *action_push = ( const struct ofp_action_push * )( ( const char * )action_hdr + offset );
      append_action( action_list, create_action_push_mpls( action_push->ethertype ) );
    }
    else if ( action_hdr->type == OFPAT_PUSH_PBB ) {
      const struct ofp_action_push *action_push = ( const struct ofp_action_push * )( ( const char * )action_hdr + offset );
      append_action( action_list, create_action_push_pbb( action_push->ethertype ) );
    }
    else if ( action_hdr->type == OFPAT_POP_VLAN ) {
      append_action( action_list, create_action_pop_vlan() );
    }
    else if ( action_hdr->type == OFPAT_POP_MPLS ) {
      const struct ofp_action_pop_mpls *action_pop_mpls = ( const struct ofp_action_pop_mpls * )( ( const char * )action_hdr + offset );
      append_action( action_list, create_action_pop_mpls( action_pop_mpls->ethertype ) );
    }
    else if ( action_hdr->type == OFPAT_POP_PBB ) {
      append_action( action_list, create_action_pop_pbb() );
    }
    else if ( action_hdr->type == OFPAT_SET_QUEUE ) {
      const struct ofp_action_set_queue *set_queue = ( const struct ofp_action_set_queue * )( ( const char * )action_hdr + offset );
      append_action( action_list, create_action_set_queue( set_queue->queue_id ) );
    }
    else if ( action_hdr->type == OFPAT_SET_FIELD ) {
      const struct ofp_action_set_field *set_field = ( const struct ofp_action_set_field * )( ( const char * )action_hdr + offset );
      match *match = create_match();
      assign_match( match, ( oxm_match_header * ) &set_field->field );
      append_action( action_list, create_action_set_field( match ) );
    }
    else {
      warn( "Invalid action type %u", action_hdr->type );
      break;
    }
    offset += action_hdr->len;
  }
  return action_list;
}
action_list * ( *assign_actions )( action_list *action_list, const struct ofp_action_header *action, uint16_t action_len ) = _assign_actions;


void
_action_pack( void *dest, action_list **list  ) {
  if ( *list == NULL ) {
    return;
  }
  dlist_element *item = get_first_element( *list );
  action *action;
  struct ofp_action_header *ac_hdr = dest;
  while ( item != NULL ) {
    action = item->data;
    if ( action != NULL ) {
      action_tlv_pack( ac_hdr, action );
      ac_hdr = ( struct ofp_action_header * ) ( ( char * ) ac_hdr + ac_hdr->len );
    }
    item = item->next;
  }
}
void ( *action_pack )( void *dest, action_list **list ) = _action_pack;


/*
 * Local variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
