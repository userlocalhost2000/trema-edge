#
# A test example program to send a configurable number of OFPT_SET_CONFIG
# messages.
#
# Author: Nick Karanatsios <nickkaranatsios@gmail.com>
#
# Copyright (C) 2008-2011 NEC Corporation
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2, as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#


require "example"


class SetConfigController < Controller
  include Example


  class << self
    attr_accessor :count


    def usage
      puts "#{Example::exec_name} count"
      puts "Send count number of set-config messages."
      exit false
    end
  end


  def switch_ready datapath_id
    send_nr_msgs datapath_id, SetConfig
  end
end


SetConfigController.usage if ARGV.length < 2
SetConfigController.count = ARGV[ 1 ].to_i


### Local variables:
### mode: Ruby
### coding: utf-8-unix
### indent-tabs-mode: nil
### End: