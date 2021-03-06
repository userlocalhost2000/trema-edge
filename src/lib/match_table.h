/*
 * OpenFlow flow matching library
 *
 * Author: Kazushi SUGYO
 *
 * Copyright (C) 2008-2012 NEC Corporation
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


#ifndef MATCH_TABLE_H
#define MATCH_TABLE_H


#include <openflow.h>
#include "linked_list.h"
#include "oxm_match.h"


void init_match_table( void );
void finalize_match_table( void );
bool insert_match_entry( oxm_matches *match, uint16_t priority, void *data );
void *lookup_match_strict_entry( oxm_matches *match, uint16_t priority );
void *lookup_match_entry( oxm_matches *match );
bool update_match_entry( oxm_matches *match, uint16_t priority, void *data );
void *delete_match_strict_entry( oxm_matches *match, uint16_t priority );
void foreach_match_table( void function( oxm_matches *match, uint16_t priority, void *data, void *user_data ), void *user_data );
void map_match_table( oxm_matches *match, void function( oxm_matches *match, uint16_t priority, void *data, void *user_data ), void *user_data );


#endif // MATCH_TABLE_H


/*
 * Local variables:
 * c-basic-offset: 2
 * indent-tabs-mode: nil
 * End:
 */
