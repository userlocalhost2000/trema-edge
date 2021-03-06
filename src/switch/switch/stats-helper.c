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


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/utsname.h>
#include "trema.h"
#include "ofdp.h"
#include "action-helper.h"
#include "action-tlv-interface.h"
#include "action_executor.h"
#include "oxm-helper.h"
#include "oxm-interface.h"
#include "stats-helper.h"


#ifdef UNIT_TESTING

// Allow static functions to be called from unit tests.
#define static

#endif // UNIT_TESTING


static list_element *
new_list( void ) {
  list_element *list;

  if ( create_list( &list ) == false ) {
    assert( 0 );
  }
  return list;
}


/*
 * A field by field copy because the flow_stats not aligned to ofp_flow_stats.
 * First argument is destination second argument is source.
 * TODO if flow_stats is added a pad2[ 4 ] field all fields upto match can be
 * copied using memcpy.
 */
static void
assign_ofp_flow_stats( struct ofp_flow_stats *fs, const flow_stats *stats ) {
  fs->table_id = stats->table_id;
  fs->duration_sec = stats->duration_sec;
  fs->duration_nsec = stats->duration_nsec;
  fs->priority = stats->priority;
  fs->idle_timeout = stats->idle_timeout;
  fs->hard_timeout = stats->hard_timeout;
  fs->flags = stats->flags;
  fs->cookie = stats->cookie;
  fs->packet_count = stats->packet_count;
  fs->byte_count = stats->byte_count;
}


static void
sum_ofp_aggregate_stats( struct ofp_aggregate_stats_reply *as_reply, const flow_stats *stats ) {
  as_reply->packet_count += stats->packet_count;
  as_reply->byte_count += stats->byte_count;
}


static flow_stats *
retrieve_flow_stats( uint32_t *nr_stats, const uint8_t table_id, const uint32_t out_port, const uint32_t out_group,
                     const uint64_t cookie, const uint64_t cookie_mask, const struct ofp_match *ofp_match ) {
  match *flow_match = create_match();
  size_t match_len = 0;
  if ( ofp_match != NULL ) {
    match_len = ofp_match->length - offsetof( struct ofp_match, oxm_fields );
    // translate the ofp_match to datapath match.
    const oxm_match_header *hdr = ( const oxm_match_header * ) ofp_match->oxm_fields;
    while ( match_len > 0 ) {
      assign_match( flow_match, hdr );
      match_len -= ( sizeof( *hdr ) + OXM_LENGTH( *hdr ) );
      if ( match_len > 0 ) {
        hdr = ( const oxm_match_header * ) ( ( const char * ) hdr + sizeof( *hdr ) + OXM_LENGTH( *hdr ) );
      }
    }
  }

  flow_stats *stats = NULL;
  OFDPE ret = get_flow_stats( table_id, flow_match, cookie, cookie_mask, out_port, out_group, &stats, nr_stats );
  if ( ret != OFDPE_SUCCESS ) {
    error( "Failed to retrieve flow stats from datapath ( ret = %d ).", ret );
  }
  delete_match( flow_match );

  return stats;
}


static uint16_t
action_list_length( action_list **list ) {
  if ( *list == NULL ) {
    return 0;
  }
  uint16_t length = 0;
  dlist_element *item = get_first_element( *list );
  action *action;
  while ( item != NULL ) {
    action = item->data;
    if ( action != NULL ) {
      length = ( uint16_t ) ( length + action_tlv_length_by_type( action->type ) );
      if ( action->type == OFPAT_SET_FIELD && action->match ) {
        length = ( uint16_t ) ( length + match_length( action->match ) );
      }
    }
    item = item->next;
  }
  return length;
}


static uint16_t
bucket_list_length( bucket_list **list ) {
  if ( *list == NULL ) {
    return 0;
  }
  uint16_t length = 0;
  bucket_list *item = get_first_element( *list );
  bucket *bucket;
  while ( item != NULL ) {
    bucket = item->data;
    if ( bucket != NULL ) {
      length = ( uint16_t ) ( length + sizeof( struct ofp_bucket ) );
      length = ( uint16_t ) ( length + action_list_length( &bucket->actions ) );
    }
    item = item->next;
  }
  return length;
}


static void
pack_bucket( struct ofp_bucket *ofp_bucket, bucket_list **list ) {
  if ( *list == NULL ) {
    return;
  }
  bucket_list *element = get_first_element( *list );
  bucket *bucket;
  uint16_t bucket_length = 0;
  while ( element != NULL ) {
    bucket = element->data;
    if ( bucket != NULL ) {
      bucket_length = ( uint16_t ) ( sizeof( struct ofp_bucket ) + action_list_length( &bucket->actions ) );
      ofp_bucket->len = bucket_length;
      ofp_bucket->weight = bucket->weight;
      ofp_bucket->watch_port = bucket->watch_port;
      ofp_bucket->watch_group = bucket->watch_group;
      void *p = ofp_bucket->actions;
      action_pack( p, &bucket->actions );
      ofp_bucket = ( struct ofp_bucket * ) ( ( char * )( ofp_bucket + bucket_length ) );
    }
    element = element->next;
  }
}


static bool
is_any_table_feature_set( const void *feature, size_t feature_size ) {
  const uint8_t *byte = feature;

  for ( unsigned int i = 0; i < ( feature_size * 8 ); i++ ) {
    if ( ( *byte >> ( i % 8 ) ) & 1 ) {
      return true;
    }
    if ( ( i + 1 ) % 8 == 0 ) {
      byte++;
    }
  }

  return false;
}


static uint16_t
count_features( void *feature, size_t feature_size ) {
  uint8_t *byte = feature;
  uint16_t count = 0;

  for ( unsigned int i = 0; i < ( feature_size * 8 ); i++ ) {
    if ( ( *byte >> ( i % 8 ) ) & 1 ) {
      count++;
    }
    if ( ( i + 1 ) % 8 == 0 ) {
      byte++;
    }
  }

  return count;
}


static bool
is_any_table_feature_instruction_set( instruction_capabilities *ins_cap ) {
  return is_any_table_feature_set( ins_cap, sizeof( instruction_capabilities ) );
}


static bool
is_any_table_feature_action_set( action_capabilities *ac_cap ) {
  return is_any_table_feature_set( ac_cap, sizeof( action_capabilities ) );
}


static bool
is_any_table_feature_match_set( match_capabilities *match_cap ) {
  return is_any_table_feature_set( match_cap, sizeof( match_capabilities ) );
}


static uint16_t
instructions_capabilities_len( instruction_capabilities *ins_cap ) {
  return ( uint16_t ) ( count_features( ins_cap, sizeof( *ins_cap ) ) * sizeof( struct ofp_instruction ) );
}


static uint16_t
actions_capabilities_len( action_capabilities *ac_cap ) {
  return ( uint16_t ) ( count_features( ac_cap, sizeof( *ac_cap ) ) * sizeof( struct ofp_action_header ) );
}


static uint16_t
match_capabilities_len( match_capabilities *match_cap ) {
  return ( uint16_t ) ( count_features( match_cap, sizeof( *match_cap ) ) * sizeof( uint32_t ) );
}


static uint16_t
prop_next_table_ids_len( bool *tables ) {
  size_t len = 0;

  for ( unsigned int i = 0; i <= FLOW_TABLE_ID_MAX; i++ ) {
    if ( tables[ i ] ) {
      len += sizeof( uint8_t );
    }
  }

  return ( uint16_t ) len;
}


static size_t
get_table_features_len( flow_table_features *table_feature ) {
  size_t len;
  size_t total_len = 0;

  len = instructions_capabilities_len( &table_feature->instructions );
  if ( len ) {
    len += sizeof( struct ofp_table_feature_prop_instructions );
    total_len += len + PADLEN_TO_64( len );
  }

  len = instructions_capabilities_len( &table_feature->instructions_miss );
  if ( len ) {
    len += sizeof( struct ofp_table_feature_prop_instructions );
    total_len += len + PADLEN_TO_64( len );
  }

  len = actions_capabilities_len( &table_feature->write_actions );
  if ( len ) {
    len += sizeof( struct ofp_table_feature_prop_actions );
    total_len += len + PADLEN_TO_64( len );
  }
  
  len = actions_capabilities_len( &table_feature->write_actions_miss );
  if ( len ) {
    len += sizeof( struct ofp_table_feature_prop_actions );
    total_len += len + PADLEN_TO_64( len );
  }

  len = actions_capabilities_len( &table_feature->apply_actions );
  if ( len ) {
    len += sizeof( struct ofp_table_feature_prop_actions );
    total_len += len + PADLEN_TO_64( len );
  }

  len = actions_capabilities_len( &table_feature->apply_actions_miss );
  if ( len ) {
    len += sizeof( struct ofp_table_feature_prop_actions );
    total_len += len + PADLEN_TO_64( len );
  }

  len = match_capabilities_len( &table_feature->matches );
  if ( len ) {
    len += sizeof( struct ofp_table_feature_prop_oxm );
    total_len += len + PADLEN_TO_64( len );
  }
  
  len = match_capabilities_len( &table_feature->wildcards );
  if ( len ) {
    len += sizeof( struct ofp_table_feature_prop_oxm );
    total_len += len + PADLEN_TO_64( len );
  }

  len = match_capabilities_len( &table_feature->write_setfield );
  if ( len ) {
    len += sizeof( struct ofp_table_feature_prop_oxm );
    total_len += len + PADLEN_TO_64( len );
  }

  len = match_capabilities_len( &table_feature->write_setfield_miss );
  if ( len ) {
    len += sizeof( struct ofp_table_feature_prop_oxm );
    total_len += len + PADLEN_TO_64( len );
  }

  len = match_capabilities_len( &table_feature->apply_setfield );
  if ( len ) {
    len += sizeof( struct ofp_table_feature_prop_oxm );
    total_len += len + PADLEN_TO_64( len );
  }

  len = match_capabilities_len( &table_feature->apply_setfield_miss );
  if ( len ) {
    len += sizeof( struct ofp_table_feature_prop_oxm );
    total_len += len + PADLEN_TO_64( len );
  }

  len = prop_next_table_ids_len( table_feature->next_table_ids );
  if ( len ) {
    len = offsetof( struct ofp_table_feature_prop_next_tables, next_table_ids );
    total_len += len + PADLEN_TO_64( len );
  }

  len = prop_next_table_ids_len( table_feature->next_table_ids_miss );
  if ( len ) {
    len = offsetof( struct ofp_table_feature_prop_next_tables, next_table_ids );
    total_len += len + PADLEN_TO_64( len );
  }

  return total_len;
}


static uint16_t
assign_instruction_ids( struct ofp_instruction *ins, instruction_capabilities *instructions_cap ) {
  const instruction_capabilities c = *instructions_cap;
  uint16_t len = ( uint16_t ) sizeof( struct ofp_instruction );
  uint16_t total_len = 0;

  if ( c & INSTRUCTION_METER ) {
    ins->type = OFPIT_METER;
    ins->len = len;
    total_len = ( uint16_t )( total_len + len );
    ins = ( struct ofp_instruction * )( ( char * ) ins + len );
  }
  if ( c & INSTRUCTION_APPLY_ACTIONS ) {
    ins->type = OFPIT_APPLY_ACTIONS;
    ins->len = len;
    total_len = ( uint16_t )( total_len + len );
    ins = ( struct ofp_instruction * )( ( char * ) ins + len );
  }
  if ( c & INSTRUCTION_CLEAR_ACTIONS ) {
    ins->type = OFPIT_CLEAR_ACTIONS;
    ins->len = len;
    total_len = ( uint16_t )( total_len + len );
    ins = ( struct ofp_instruction * )( ( char * ) ins + len );
  }
  if ( c & INSTRUCTION_WRITE_ACTIONS ) {
    ins->type = OFPIT_WRITE_ACTIONS;
    ins->len = len;
    total_len = ( uint16_t )( total_len + len );
    ins = ( struct ofp_instruction * )( ( char * ) ins + len );
  }
  if ( c & INSTRUCTION_WRITE_METADATA ) {
    ins->type = OFPIT_WRITE_METADATA;
    ins->len = len;
    total_len = ( uint16_t )( total_len + len );
    ins = ( struct ofp_instruction * )( ( char * ) ins + len );
  }
  if ( c & INSTRUCTION_GOTO_TABLE ) {
    ins->type = OFPIT_GOTO_TABLE;
    ins->len = len;
    total_len = ( uint16_t )( total_len + len );
    ins = ( struct ofp_instruction * )( ( char * ) ins + len );
  }
  return total_len;
}


static uint16_t
assign_action_ids( struct ofp_action_header *ac_hdr, action_capabilities *action_cap ) {
  const action_capabilities c = *action_cap;
  uint16_t len = ( uint16_t ) sizeof( struct ofp_action_header );
  uint16_t total_len = 0;

  if ( c & ACTION_OUTPUT ) {
    ac_hdr->type = OFPAT_OUTPUT;
    ac_hdr->len = len;
    total_len = ( uint16_t )( total_len + len );
    ac_hdr = ( struct ofp_action_header * )( ( char * ) ac_hdr + len );
  }
  if ( c & ACTION_SET_QUEUE ) {
    ac_hdr->type = OFPAT_SET_QUEUE;
    ac_hdr->len = len;
    total_len = ( uint16_t )( total_len + len );
    ac_hdr = ( struct ofp_action_header * )( ( char * ) ac_hdr + len );
  }
  if ( c & ACTION_GROUP ) {
    ac_hdr->type = OFPAT_GROUP;
    ac_hdr->len = len;
    total_len = ( uint16_t )( total_len + len );
    ac_hdr = ( struct ofp_action_header * )( ( char * ) ac_hdr + len );
  }
  if ( c & ACTION_PUSH_VLAN ) {
    ac_hdr->type = OFPAT_PUSH_VLAN;
    ac_hdr->len = len;
    total_len = ( uint16_t )( total_len + len );
    ac_hdr = ( struct ofp_action_header * )( ( char * ) ac_hdr + len );
  }
  if ( c & ACTION_POP_VLAN ) {
    ac_hdr->type = OFPAT_POP_VLAN;
    ac_hdr->len = len;
    total_len = ( uint16_t )( total_len + len );
    ac_hdr = ( struct ofp_action_header * )( ( char * ) ac_hdr + len );
  }
  if ( c & ACTION_PUSH_MPLS ) {
    ac_hdr->type = OFPAT_PUSH_MPLS;
    ac_hdr->len = len;
    total_len = ( uint16_t )( total_len + len );
    ac_hdr = ( struct ofp_action_header * )( ( char * ) ac_hdr + len );
  }
  if( c & ACTION_POP_MPLS ) {
    ac_hdr->type = OFPAT_POP_MPLS;
    ac_hdr->len = len;
    total_len = ( uint16_t )( total_len + len );
    ac_hdr = ( struct ofp_action_header * )( ( char * ) ac_hdr + len );
  }
  if ( c & ACTION_PUSH_PBB ) {
    ac_hdr->type = OFPAT_PUSH_PBB;
    ac_hdr->len = len;
    total_len = ( uint16_t )( total_len + len );
    ac_hdr = ( struct ofp_action_header * )( ( char * ) ac_hdr + len );
  }
  if ( c & ACTION_POP_PBB ) {
    ac_hdr->type = OFPAT_POP_PBB;
    ac_hdr->len = len;
    total_len = ( uint16_t )( total_len + len );
    ac_hdr = ( struct ofp_action_header * )( ( char * ) ac_hdr + len );
  }
  return total_len;
}


static struct ofp_table_features *
assign_table_features( flow_table_features *table_feature ) {
  size_t total_len = sizeof( struct ofp_table_features );

  total_len += get_table_features_len( table_feature );
  size_t total_padded_len = total_len + PADLEN_TO_64( total_len );

  struct ofp_table_features *ofp_table_feature = ( struct ofp_table_features * ) xmalloc( total_padded_len );
  ofp_table_feature->table_id = table_feature->table_id;
  ofp_table_feature->length = ( uint16_t ) total_padded_len;
  strncpy( ofp_table_feature->name, table_feature->name, OFP_MAX_TABLE_NAME_LEN );
  ofp_table_feature->name[ OFP_MAX_TABLE_NAME_LEN - 1 ] = '\0';
  ofp_table_feature->config = table_feature->config;
  ofp_table_feature->max_entries = table_feature->max_entries;
  ofp_table_feature->metadata_match = table_feature->metadata_match;
  ofp_table_feature->metadata_write = table_feature->metadata_write;

  uint16_t prop_hdr_len = ( uint16_t ) sizeof( struct ofp_table_feature_prop_instructions );
  size_t properties_len = 0;

  if ( is_any_table_feature_instruction_set( &table_feature->instructions ) ) {
    struct ofp_table_feature_prop_instructions *tfpi = ( struct ofp_table_feature_prop_instructions * )( ( char * ) ofp_table_feature->properties + properties_len );
    tfpi->type = OFPTFPT_INSTRUCTIONS;
    struct ofp_instruction *ins = tfpi->instruction_ids;
    tfpi->length = ( uint16_t ) ( prop_hdr_len + assign_instruction_ids( ins, &table_feature->instructions ) );
    tfpi->length = ( uint16_t ) ( tfpi->length + PADLEN_TO_64( tfpi->length ) );
    properties_len += tfpi->length;
  }

  if ( is_any_table_feature_instruction_set( &table_feature->instructions_miss ) ) {
    struct ofp_table_feature_prop_instructions *tfpi = ( struct ofp_table_feature_prop_instructions * )( ( char * ) ofp_table_feature->properties + properties_len );
    tfpi->type = OFPTFPT_INSTRUCTIONS_MISS;
    struct ofp_instruction *ins = tfpi->instruction_ids;
    tfpi->length = ( uint16_t ) ( prop_hdr_len + assign_instruction_ids( ins, &table_feature->instructions ) );
    tfpi->length = ( uint16_t ) ( tfpi->length + PADLEN_TO_64( tfpi->length ) );
    properties_len += tfpi->length;
  }

  prop_hdr_len = ( uint16_t ) sizeof( struct ofp_table_feature_prop_actions );
  if ( is_any_table_feature_action_set( &table_feature->write_actions ) ) {
    struct ofp_table_feature_prop_actions *tfpa = ( struct ofp_table_feature_prop_actions * )( ( char * ) ofp_table_feature->properties + properties_len );
    tfpa->type = OFPTFPT_WRITE_ACTIONS;
    struct ofp_action_header *ac_hdr = tfpa->action_ids;
    tfpa->length = ( uint16_t ) ( prop_hdr_len + assign_action_ids( ac_hdr, &table_feature->write_actions ) );
    tfpa->length = ( uint16_t ) ( tfpa->length + PADLEN_TO_64( tfpa->length ) );
    properties_len += tfpa->length;
  }
  
  if ( is_any_table_feature_action_set( &table_feature->write_actions_miss ) ) {
    struct ofp_table_feature_prop_actions *tfpa = ( struct ofp_table_feature_prop_actions * )( ( char * ) ofp_table_feature->properties + properties_len );

    tfpa->type = OFPTFPT_WRITE_ACTIONS_MISS;
    struct ofp_action_header *ac_hdr = tfpa->action_ids;
    tfpa->length = ( uint16_t ) ( prop_hdr_len + assign_action_ids( ac_hdr, &table_feature->write_actions_miss ) );
    tfpa->length = ( uint16_t ) ( tfpa->length + PADLEN_TO_64( tfpa->length ) );
    properties_len += tfpa->length;
  }
  
  if ( is_any_table_feature_action_set( &table_feature->apply_actions ) ) {
    struct ofp_table_feature_prop_actions *tfpa = ( struct ofp_table_feature_prop_actions * )( ( char * ) ofp_table_feature->properties + properties_len );
    tfpa->type = OFPTFPT_APPLY_ACTIONS;
    struct ofp_action_header *ac_hdr = tfpa->action_ids;
    tfpa->length = ( uint16_t ) ( prop_hdr_len + assign_action_ids( ac_hdr, &table_feature->apply_actions ) );
    tfpa->length = ( uint16_t ) ( tfpa->length + PADLEN_TO_64( tfpa->length ) );
    properties_len += tfpa->length;
  }

  if ( is_any_table_feature_action_set( &table_feature->apply_actions_miss ) ) {
    struct ofp_table_feature_prop_actions *tfpa = ( struct ofp_table_feature_prop_actions * )( ( char * ) ofp_table_feature->properties + properties_len );
    tfpa->type = OFPTFPT_APPLY_ACTIONS_MISS;
    struct ofp_action_header *ac_hdr = tfpa->action_ids;
    tfpa->length = ( uint16_t ) ( prop_hdr_len + assign_action_ids( ac_hdr, &table_feature->apply_actions_miss ) );
    tfpa->length = ( uint16_t ) ( tfpa->length + PADLEN_TO_64( tfpa->length ) );
    properties_len += tfpa->length;
  }

  prop_hdr_len = ( uint16_t ) sizeof( struct ofp_table_feature_prop_oxm );
  if ( is_any_table_feature_match_set( &table_feature->matches ) ) {
    struct ofp_table_feature_prop_oxm *tfpo = ( struct ofp_table_feature_prop_oxm * )( ( char * ) ofp_table_feature->properties + properties_len );
    tfpo->type = OFPTFPT_MATCH;
    uint32_t *oxm_id =  ( uint32_t * ) &tfpo->oxm_ids;
    tfpo->length = ( uint16_t ) ( prop_hdr_len + assign_oxm_ids( oxm_id, &table_feature->matches ) );
    tfpo->length = ( uint16_t ) ( tfpo->length + PADLEN_TO_64( tfpo->length ) );
    properties_len += tfpo->length;
  }

  if ( is_any_table_feature_match_set( &table_feature->wildcards ) ) {
    struct ofp_table_feature_prop_oxm *tfpo = ( struct ofp_table_feature_prop_oxm * )( ( char * ) ofp_table_feature->properties + properties_len );
    tfpo->type = OFPTFPT_WILDCARDS;
    uint32_t *oxm_id = ( uint32_t * ) &tfpo->oxm_ids;
    tfpo->length = ( uint16_t ) ( prop_hdr_len + assign_oxm_ids( oxm_id, &table_feature->wildcards ) );
    tfpo->length = ( uint16_t ) ( tfpo->length + PADLEN_TO_64( tfpo->length ) );
    properties_len += tfpo->length;
  }

  if ( is_any_table_feature_match_set( &table_feature->write_setfield ) ) {
    struct ofp_table_feature_prop_oxm *tfpo = ( struct ofp_table_feature_prop_oxm * )( ( char * ) ofp_table_feature->properties + properties_len );
    tfpo->type = OFPTFPT_WRITE_SETFIELD;
    uint32_t *oxm_id = ( uint32_t * ) &tfpo->oxm_ids;
    tfpo->length = ( uint16_t ) ( prop_hdr_len + assign_oxm_ids( oxm_id, &table_feature->write_setfield ) );
    tfpo->length = ( uint16_t ) ( tfpo->length + PADLEN_TO_64( tfpo->length ) );
    properties_len += tfpo->length;
  }

  if ( is_any_table_feature_match_set( &table_feature->write_setfield_miss ) ) {
    struct ofp_table_feature_prop_oxm *tfpo = ( struct ofp_table_feature_prop_oxm * )( ( char * ) ofp_table_feature->properties + properties_len );
    tfpo->type = OFPTFPT_WRITE_SETFIELD_MISS;
    uint32_t *oxm_id = ( uint32_t * ) &tfpo->oxm_ids;
    tfpo->length = ( uint16_t ) ( prop_hdr_len + assign_oxm_ids( oxm_id, &table_feature->write_setfield_miss ) );
    tfpo->length = ( uint16_t ) ( tfpo->length + PADLEN_TO_64( tfpo->length ) );
    properties_len += tfpo->length;
  }
  
  if ( is_any_table_feature_match_set( &table_feature->apply_setfield ) ) {
    struct ofp_table_feature_prop_oxm *tfpo = ( struct ofp_table_feature_prop_oxm * )( ( char * ) ofp_table_feature->properties + properties_len );
    tfpo->type = OFPTFPT_APPLY_SETFIELD;
    uint32_t *oxm_id = ( uint32_t * ) &tfpo->oxm_ids;
    tfpo->length = ( uint16_t ) ( prop_hdr_len + assign_oxm_ids( oxm_id, &table_feature->apply_setfield ) );
    tfpo->length = ( uint16_t ) ( tfpo->length + PADLEN_TO_64( tfpo->length ) );
    properties_len += tfpo->length;
  }

  if ( is_any_table_feature_match_set( &table_feature->apply_setfield_miss ) ) {
    struct ofp_table_feature_prop_oxm *tfpo = ( struct ofp_table_feature_prop_oxm * )( ( char * ) ofp_table_feature->properties + properties_len );
    tfpo->type = OFPTFPT_APPLY_SETFIELD_MISS;
    uint32_t *oxm_id = ( uint32_t * ) &tfpo->oxm_ids;
    tfpo->length = ( uint16_t ) ( prop_hdr_len + assign_oxm_ids( oxm_id, &table_feature->apply_setfield_miss ) );
    tfpo->length = ( uint16_t ) ( tfpo->length + PADLEN_TO_64( tfpo->length ) );
    properties_len += tfpo->length;
  }

  prop_hdr_len = ( uint16_t ) sizeof( struct ofp_table_feature_prop_next_tables );
  struct ofp_table_feature_prop_next_tables *tfpnt = ( struct ofp_table_feature_prop_next_tables * )( ( char * ) ofp_table_feature->properties + properties_len );
  tfpnt->type = OFPTFPT_NEXT_TABLES;
  tfpnt->length = prop_hdr_len;
  uint8_t *table_id = ( uint8_t * ) &tfpnt->next_table_ids;
  for ( uint8_t i = 0; i <= FLOW_TABLE_ID_MAX; i++ ) {
    if ( table_feature->next_table_ids[ i ] ) {
      tfpnt->length = ( uint16_t ) ( tfpnt->length + sizeof( uint8_t ) );
      *table_id = i;
      table_id++;
    }
  }
  tfpnt->length = ( uint16_t ) ( tfpnt->length + PADLEN_TO_64( tfpnt->length ) );
  properties_len += tfpnt->length;

  tfpnt = ( struct ofp_table_feature_prop_next_tables * )( ( char * ) ofp_table_feature->properties + properties_len );
  tfpnt->type = OFPTFPT_NEXT_TABLES_MISS;
  tfpnt->length = prop_hdr_len;
  table_id = ( uint8_t * ) &tfpnt->next_table_ids;
  for ( uint8_t i = 0; i <= FLOW_TABLE_ID_MAX; i++ ) {
    if ( table_feature->next_table_ids_miss[ i ] ) {
      tfpnt->length = ( uint16_t ) ( tfpnt->length + sizeof( uint8_t ) );
      *table_id = i;
      table_id++;
    }
  }
  tfpnt->length = ( uint16_t ) ( tfpnt->length + PADLEN_TO_64( tfpnt->length ) );
  properties_len += tfpnt->length;

  return ofp_table_feature;
}


/*
 * Set up information to request flow statistics from datapath. The function
 * returns a linked list of ofp_flow_stats objects. If no statistics found an
 * empty list is returned.
 */
static void
_request_send_flow_stats( const struct ofp_flow_stats_request *req, const uint32_t transaction_id ) {
  uint32_t nr_stats = 0;

  flow_stats *stats = retrieve_flow_stats( &nr_stats, req->table_id, req->out_port, req->out_group,
                                           req->cookie, req->cookie_mask, &req->match );

  list_element *list = new_list();

  if ( stats != NULL && nr_stats > 0 ) {
    oxm_matches *oxm_matches = create_oxm_matches();
    uint16_t flags = OFPMPF_REPLY_MORE;

    for ( uint32_t i = 0; i < nr_stats; i++ ) {
      construct_oxm( oxm_matches, &stats[ i ].match );
      uint16_t oxm_matches_len = get_oxm_matches_length( oxm_matches );
      struct ofp_flow_stats *fs = xmalloc( sizeof( *fs ) + oxm_matches_len );
      assign_ofp_flow_stats( fs, &stats[ i ] );

      // TODO this performs htons and the create_flow_multipart_reply does also htons.
      // We also don't have enough storage preallocated from the match.
      pack_ofp_match( &fs->match, oxm_matches );

      // finally update the length construct_ofp_match performs htons on the length and type
      fs->length = ( uint16_t ) ( sizeof( *fs ) +  ( uint16_t ) ( fs->match.length  - 4 ) );
      append_to_tail( &list, ( void * ) fs );
      if ( i == nr_stats - 1 ) {
        flags &= ( uint16_t ) ~OFPMPF_REPLY_MORE;
      }
      SEND_STATS( flow, transaction_id, flags, list )
      delete_element( &list, ( void * ) fs );
      xfree( fs );
    }
    xfree( stats );
    delete_oxm_matches( oxm_matches );
    delete_list( list );
  }
}
void ( *request_send_flow_stats)( const struct ofp_flow_stats_request *req, uint32_t transaction_id ) = _request_send_flow_stats;


static struct ofp_aggregate_stats_reply *
_request_aggregate_stats( const struct ofp_aggregate_stats_request *req ) {
  uint32_t nr_stats = 0;

  flow_stats *stats = retrieve_flow_stats( &nr_stats, req->table_id, req->out_port, req->out_group, req->cookie,
                                           req->cookie_mask, &req->match );
  if ( stats && nr_stats > 0 ) {
    struct ofp_aggregate_stats_reply *as_reply = xmalloc( sizeof( *as_reply ) );

    memset( as_reply, 0, sizeof( *as_reply ) );
    for( uint32_t i = 0; i < nr_stats; i++ ) {
      sum_ofp_aggregate_stats( as_reply, &stats[ i ] );
    }
    as_reply->flow_count = nr_stats;
    xfree( stats );
    return as_reply;
  }
  return NULL;
}
struct ofp_aggregate_stats_reply * (* request_aggregate_stats)( const struct ofp_aggregate_stats_request *req ) = _request_aggregate_stats;


/*
 * Request table statistics starting with lower table numbers terminate when an
 * invalid table number is found. Returns an array of ofp_table_stats.
 */
static void
_request_send_table_stats( const uint32_t transaction_id ) {
  table_stats *stats = NULL;
  list_element *list = new_list();
  uint8_t nr_tables = 0;

  get_table_stats( &stats, &nr_tables );

  table_stats *stat = stats;
  uint16_t flags = OFPMPF_REPLY_MORE;
  for ( uint8_t i = 0; i < nr_tables; i++ ) {
    append_to_tail( &list, ( void * ) stat );
    if ( i == nr_tables - 1 ) {
      flags &= ( uint16_t ) ~OFPMPF_REPLY_MORE;
    }
    SEND_STATS( table, transaction_id, flags, list );
    delete_element( &list, ( void * ) stat );
    stat++;
  }
  delete_list( list );
  if ( stats != NULL ) {
    xfree( stats );
  }
}
void ( *request_send_table_stats)( const uint32_t transaction_id ) = _request_send_table_stats;


static void
_request_send_port_stats( const struct ofp_port_stats_request *req, const uint32_t transaction_id ) {
  port_stats *stats = NULL;
  list_element *list = new_list();
  uint32_t nr_port_stats = 0;

  if ( get_port_stats( req->port_no, &stats, &nr_port_stats ) == OFDPE_SUCCESS ) {
    uint16_t flags = OFPMPF_REPLY_MORE;
    for ( uint32_t i = 0; i < nr_port_stats; i++ ) {
      append_to_tail( &list, ( void * ) &stats[ i ] );
      if ( i == nr_port_stats - 1 ) {
        flags &= ( uint16_t ) ~OFPMPF_REPLY_MORE;
      }
      SEND_STATS( port, transaction_id, flags, list )
      delete_element( &list, ( void * ) &stats[ i ] );
    }
    xfree( stats );
    delete_list( list );
  }
}
void ( *request_send_port_stats )( const struct ofp_port_stats_request *req, const uint32_t transaction_id ) = _request_send_port_stats;


static void
_request_send_group_stats( const struct ofp_group_stats_request *req, const uint32_t transaction_id ) {
  group_stats *stats = NULL;
  list_element *list = new_list();
  uint32_t nr_group_stats = 0;

  if ( get_group_stats( req->group_id, &stats, &nr_group_stats ) == OFDPE_SUCCESS ) {
    // FIXME: this may not work! group_stats is different type from struct ofp_group_stats.
    uint16_t flags = OFPMPF_REPLY_MORE;
    for ( uint32_t i = 0; i < nr_group_stats; i++ ) {
      append_to_tail( &list, ( void * ) &stats[ i ] );
      if ( i == nr_group_stats - 1 ) {
        flags &= ( uint16_t ) ~OFPMPF_REPLY_MORE;
      }
      SEND_STATS( group, transaction_id, flags, list )
      delete_element( &list, ( void * ) &stats[ i ] );
    }
    xfree( stats );
    delete_list( list );
  }
}
void ( *request_send_group_stats)( const struct ofp_group_stats_request *req, const uint32_t transaction_id ) = _request_send_group_stats;


static list_element *
_request_group_desc_stats( void ) {
  group_desc_stats *stats = NULL;
  list_element *list = new_list();
  uint16_t group_desc_stats_nr = 0;
  uint16_t length = 0;

  if ( get_group_desc_stats( &stats, &group_desc_stats_nr ) == OFDPE_SUCCESS ) {
    for ( uint16_t i = 0; i < group_desc_stats_nr; i++ ) {
      length = bucket_list_length( &stats[ i ].buckets );
      length = ( uint16_t ) ( length + sizeof( struct ofp_group_desc_stats ) );
      buffer *buffer = alloc_buffer_with_length( length );
      append_back_buffer( buffer, length );
      struct ofp_group_desc_stats *stat = buffer->data;
      stat->length = length;
      stat->group_id = stats[ i ].group_id;
      stat->type = stats[ i ].type;
      void *p = ( char * ) ( stat + offsetof( struct ofp_group_desc_stats, buckets ) );
      pack_bucket( p, &stats[ i ].buckets );
      append_to_tail( &list, ( void * ) stat );
    }
  }
  return list;
}
list_element * ( *request_group_desc_stats)( void ) = _request_group_desc_stats;


static void
_request_send_table_features_stats( uint32_t transaction_id ) {
  flow_table_features table_features;
  list_element *list = new_list();

  uint16_t flags = OFPMPF_REPLY_MORE;
  for ( uint8_t i = 0; i <= FLOW_TABLE_ID_MAX; i++ ) {
    if ( get_flow_table_features( i, &table_features ) == OFDPE_SUCCESS ) {
      struct ofp_table_features *table_features_reply = assign_table_features( &table_features );
      append_to_tail( &list, ( void * ) table_features_reply );
      if ( i == FLOW_TABLE_ID_MAX - 1 ) {
        flags &= ( uint16_t ) ~OFPMPF_REPLY_MORE;
      }
      SEND_STATS( table_features, transaction_id, flags, list )
      delete_element( &list, ( void * ) table_features_reply );
      xfree( table_features_reply );
    }
  }
  delete_list( list );
}
void ( *request_send_table_features_stats)( uint32_t transaction_id ) = _request_send_table_features_stats;


list_element *
_request_port_desc( void ) {
  struct ofp_port *ports;
  list_element *list = new_list();
  uint32_t nr_ports = 0;

  if ( get_port_description( OFPP_ALL, &ports, &nr_ports ) == OFDPE_SUCCESS ) {
    for ( uint32_t i = 0; i < nr_ports; i++ ) {
      append_to_tail( &list, ( void * ) &ports[ i ] );
    }
  }
  return list;
}
list_element * ( *request_port_desc )( void ) = _request_port_desc;


static struct ofp_group_features *
_request_group_features( void ) {
  group_table_features features;
  if ( get_group_features( &features ) == OFDPE_SUCCESS ) {
    struct ofp_group_features *reply = ( struct ofp_group_features * )xmalloc( sizeof( *reply ) );
    reply->types = features.types;
    reply->capabilities = features.capabilities;
    reply->max_groups[ 0 ] = features.max_groups[ 0 ];
    reply->max_groups[ 1 ] = features.max_groups[ 1 ];
    reply->max_groups[ 2 ] = features.max_groups[ 2 ];
    reply->max_groups[ 3 ] = features.max_groups[ 3 ];
    reply->actions[ 0 ] = features.actions[ 0 ] & UINT32_MAX;
    reply->actions[ 1 ] = features.actions[ 1 ] & UINT32_MAX;
    reply->actions[ 2 ] = features.actions[ 2 ] & UINT32_MAX;
    reply->actions[ 3 ] = features.actions[ 3 ] & UINT32_MAX;
    return reply;
  }
  return NULL;
}
struct ofp_group_features * ( *request_group_features )( void ) = _request_group_features;


static const char *
_mfr_desc( void ) {
  return "Trema project";
}
const char * ( *mfr_desc )( void ) = _mfr_desc;


static const char *
_serial_num( void ) {
   return "0";
}
const char * ( *serial_num )( void ) = _serial_num;


static char *
_hw_desc( void ) {
  struct utsname buf;

  char *hw_desc = ( char * ) xmalloc( sizeof( char ) * DESC_STR_LEN );
  if ( !uname( &buf ) ) {
    snprintf( hw_desc, DESC_STR_LEN, "%s %s %s %s %s",
      buf.sysname, buf.nodename, buf.release, buf.version, buf.machine );
    hw_desc[ DESC_STR_LEN - 1 ] = '\0';
  } else {
    hw_desc[ 0 ] = '\0';
  }
  return hw_desc;
}
char * ( *hw_desc )( void ) = _hw_desc;


static const char *
_dp_desc( void ) {
  return "Trema-based OpenFlow switch";
}
const char * ( *dp_desc )( void ) = _dp_desc;


/*
 * Local variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
