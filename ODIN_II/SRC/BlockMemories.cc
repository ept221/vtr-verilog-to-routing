/**
 * Copyright (c) 2021 Seyed Alireza Damghani (sdamghann@gmail.com)
 * 
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <string.h>
#include <math.h>
#include "odin_globals.h"
#include "odin_types.h"
#include "odin_util.h"
#include "ast_util.h"

#include "netlist_utils.h"
#include "node_creation_library.h"
#include "hard_blocks.h"
#include "memories.h"
#include "BlockMemories.hh"
#include "partial_map.h"
#include "vtr_util.h"
#include "vtr_memory.h"

using vtr::t_linked_vptr;

t_linked_vptr* block_memory_list;
t_linked_vptr* read_only_memory_list;
block_memory_hashtable block_memories;
block_memory_hashtable read_only_memories;

static block_memory* init_block_memory(nnode_t* node, netlist_t* netlist);

void map_bram_to_mem_hardblocks(block_memory* bram, netlist_t* netlist);
void map_rom_to_mem_hardblocks(block_memory* rom, netlist_t* netlist);

static void create_r_single_port_ram(block_memory* rom, netlist_t* /* netlist */);
static void create_2r_dual_port_ram(block_memory* rom, netlist_t* netlist);
static void create_rw_single_port_ram(block_memory* bram, netlist_t* /* netlist */);
static void create_rw_dual_port_ram(block_memory* bram, netlist_t* /* netlist */);
static void create_r2w_dual_port_ram(block_memory* bram, netlist_t* netlist);
static void create_2rw_dual_port_ram(block_memory* bram, netlist_t* netlist);
static void create_2r2w_dual_port_ram(block_memory* bram, netlist_t* netlist);

static bool check_same_addrs(block_memory* bram);
static void perform_optimization(block_memory* memory);
static signal_list_t* merge_read_write_clks(signal_list_t* rd_clks, signal_list_t* wr_clks, nnode_t* node, netlist_t* netlist);
static signal_list_t* create_single_clk_pin(signal_list_t* clks, nnode_t* node, netlist_t* netlist);

static bool check_match_ports(signal_list_t* sig, signal_list_t* be_checked, netlist_t* netlist);
static void cleanup_block_memory_old_node(nnode_t* old_node);

static void free_block_memory_index(block_memory_hashtable to_free);
static void free_block_memory(block_memory* to_free);

/**
 * (function: init_block_memory_index)
 * 
 * @brief Initialises hashtables to lookup memories based on inputs and names.
 */
void init_block_memory_index() {
    block_memories = block_memory_hashtable();
    read_only_memories = block_memory_hashtable();
}

/**
 * (function: lookup_block_memory)
 * 
 * @brief Looks up an block memory by identifier name in the block memory lookup table.
 * 
 * @param instance_name_prefix memory node name (unique hard block node name)
 * @param identifier memory block id
 */
block_memory* lookup_block_memory(char* instance_name_prefix, char* identifier) {
    char* memory_string = make_full_ref_name(instance_name_prefix, NULL, NULL, identifier, -1);

    block_memory_hashtable::const_iterator mem_out = block_memories.find(std::string(memory_string));

    vtr::free(memory_string);

    if (mem_out == block_memories.end())
        return (NULL);
    else
        return (mem_out->second);
}

/**
 * (function: init_block_memory)
 * 
 * @brief: initialize bram signals
 * 
 * @param node pointing to a bram node
 * @param netlist pointer to the current netlist file
 */
static block_memory* init_block_memory(nnode_t* node, netlist_t* netlist) {
    int i, offset;
    block_memory* bram = (block_memory*)vtr::malloc(sizeof(block_memory));

    /** 
     * yosys memory block information 
     * 
     * RD_ADDR:    input port [0] 
     * RD_CLOCK:   input port [1] 
     * RD_DATA:    output port [0] 
     * RD_ENABLE:  input port [2] 
     * WR_ADDR:    input port [3] 
     * WR_CLOCK:   input port [4] 
     * WR_DATA:    input port [5] 
     * WR_ENABLE:  input port [6] 
    */

    int RD_ADDR_width = node->input_port_sizes[0];
    int RD_CLK_width = node->input_port_sizes[1];
    int RD_DATA_width = node->output_port_sizes[0];
    int RD_ENABLE_width = node->input_port_sizes[2];
    int WR_ADDR_width = node->input_port_sizes[3];
    int WR_CLK_width = node->input_port_sizes[4];
    int WR_DATA_width = node->input_port_sizes[5];
    int WR_ENABLE_width = node->input_port_sizes[6];

    /* INPUT */
    /* read address pins */
    offset = 0;
    bram->read_addr = init_signal_list();
    for (i = 0; i < RD_ADDR_width; i++) {
        add_pin_to_signal_list(bram->read_addr, node->input_pins[offset + i]);
    }

    /* read clk pins */
    offset += RD_ADDR_width;
    bram->read_clk = init_signal_list();
    for (i = 0; i < RD_CLK_width; i++) {
        add_pin_to_signal_list(bram->read_clk, node->input_pins[offset + i]);
    }

    /* read enable pins */
    offset += RD_CLK_width;
    bram->read_en = init_signal_list();
    for (i = 0; i < RD_ENABLE_width; i++) {
        add_pin_to_signal_list(bram->read_en, node->input_pins[offset + i]);
    }

    /* write addr pins */
    offset += RD_ENABLE_width;
    bram->write_addr = init_signal_list();
    for (i = 0; i < WR_ADDR_width; i++) {
        add_pin_to_signal_list(bram->write_addr, node->input_pins[offset + i]);
    }

    /* write clk pins */
    offset += WR_ADDR_width;
    bram->write_clk = init_signal_list();
    for (i = 0; i < WR_CLK_width; i++) {
        add_pin_to_signal_list(bram->write_clk, node->input_pins[offset + i]);
    }

    /* write data pins */
    offset += WR_CLK_width;
    bram->write_data = init_signal_list();
    for (i = 0; i < WR_DATA_width; i++) {
        add_pin_to_signal_list(bram->write_data, node->input_pins[offset + i]);
    }

    /* write enable clk pins */
    offset += WR_DATA_width;
    bram->write_en = init_signal_list();
    for (i = 0; i < WR_ENABLE_width; i++) {
        add_pin_to_signal_list(bram->write_en, node->input_pins[offset + i]);
    }

    /* OUTPUT */
    /* read clk pins */
    offset = 0;
    bram->read_data = init_signal_list();
    for (i = 0; i < RD_DATA_width; i++) {
        add_pin_to_signal_list(bram->read_data, node->output_pins[offset + i]);
    }

    /* creating new node since we need to reorder some input port for each inferenece mode */
    bram->node = node;

    /* CLK */
    /* handling multiple clock signals */
    /* merge all read and write clock to a single clock pin */
    bram->clk = merge_read_write_clks(bram->read_clk, bram->write_clk, bram->node, netlist);

    /* keep track of location since we need for further node creation */
    bram->loc = node->loc;

    bram->memory_id = vtr::strdup(node->attributes->memory_id);

    /* creating a unique name for the block memory */
    bram->name = make_full_ref_name(bram->node->name, NULL, NULL, bram->memory_id, -1);
    block_memories.emplace(bram->name, bram);

    return (bram);
}

/**
 * (function: init_block_memory)
 * 
 * @brief: initialize rom signals
 * 
 * @param node pointing to a rom node
 * @param netlist pointer to the current netlist file
 */
static block_memory* init_read_only_memory(nnode_t* node, netlist_t* netlist) {
    int i, offset;
    block_memory* rom = (block_memory*)vtr::malloc(sizeof(block_memory));

    /** 
     * yosys single port ram information 
     * 
     * RD_ADDR:    input port [0] 
     * RD_CLOCK:   input port [1] 
     * RD_DATA:    output port [0] 
     * RD_ENABLE:  input port [2] 
    */

    int RD_ADDR_width = node->input_port_sizes[0];
    int RD_CLK_width = node->input_port_sizes[1];
    int RD_DATA_width = node->output_port_sizes[0];
    int RD_ENABLE_width = node->input_port_sizes[2];

    int WR_DATA_width = node->output_port_sizes[0];

    /* INPUT */
    /* read address pins */
    offset = 0;
    rom->read_addr = init_signal_list();
    for (i = 0; i < RD_ADDR_width; i++) {
        add_pin_to_signal_list(rom->read_addr, node->input_pins[offset + i]);
    }

    /* read clk pins */
    offset += RD_ADDR_width;
    rom->read_clk = init_signal_list();
    for (i = 0; i < RD_CLK_width; i++) {
        add_pin_to_signal_list(rom->read_clk, node->input_pins[offset + i]);
    }

    /* read enable pins */
    offset += RD_CLK_width;
    rom->read_en = init_signal_list();
    for (i = 0; i < RD_ENABLE_width; i++) {
        add_pin_to_signal_list(rom->read_en, node->input_pins[offset + i]);
    }

    /* OUTPUT */
    offset = 0;
    rom->read_data = init_signal_list();
    for (i = 0; i < RD_DATA_width; i++) {
        add_pin_to_signal_list(rom->read_data, node->output_pins[offset + i]);
    }

    /* PAD DATA IN */
    /* we pad the data_in port for rom using pad pins */
    rom->write_data = init_signal_list();
    for (i = 0; i < WR_DATA_width; i++) {
        add_pin_to_signal_list(rom->write_data, get_pad_pin(netlist));
    }

    /* create single clk pin */
    rom->clk = create_single_clk_pin(copy_input_signals(rom->read_clk), node, netlist);

    /* no need to these variables in rom */
    rom->write_addr = NULL;
    rom->write_clk = NULL;
    rom->write_en = NULL;

    /* creating new node since we need to reorder some input port for each inferenece mode */
    rom->node = node;

    /* keep track of location since we need for further node creation */
    rom->loc = node->loc;

    rom->memory_id = vtr::strdup(node->attributes->memory_id);

    /* creating a unique name for the block memory */
    rom->name = make_full_ref_name(rom->node->name, NULL, NULL, rom->memory_id, -1);
    read_only_memories.emplace(rom->name, rom);

    return (rom);
}

/**
 * (function: create_r_single_port_ram)
 * 
 * @brief read_only_memory will be considered as single port ram
 * this function reorders inputs and add data_in input to new node
 * which is connected to pad node
 * 
 * @param rom pointing to a rom node node
 * @param netlist pointer to the current netlist file
 */
static void create_r_single_port_ram(block_memory* rom, netlist_t* netlist) {
    nnode_t* old_node = rom->node;
    int num_rd_ports = old_node->attributes->RD_PORTS;
    int num_wr_ports = old_node->attributes->WR_PORTS;

    /* should have been resovled before this function */
    oassert(num_rd_ports == 1);
    oassert(num_wr_ports == 0);

    /* single port ram signals */
    sp_ram_signals* signals = (sp_ram_signals*)vtr::calloc(1, sizeof(dp_ram_signals));

    /* INPUTS */
    /* adding the read addr input port as address1 */
    signals->addr = rom->read_addr;

    /** 
     * handling multiple clock signals 
     * using a clk resulted from ORing all read clocks
    */
    if (rom->read_clk->count > 1) {
        rom->read_clk = make_chain(LOGICAL_OR, rom->read_clk, old_node);
    }
    signals->clk = rom->read_clk->pins[0];

    /**
     * we pad the data_in port using pad pins 
     * rom->write_data is already filled with pad pins
    */
    signals->data = rom->write_data;

    /* there is no write data to set any we, so it will be connected to GND */
    signals->we = get_zero_pin(netlist);
    delete_npin(rom->read_en->pins[0]);

    /* OUTPUT */
    /* adding the read data port as output1 */
    signals->out = rom->read_data;

    create_single_port_ram(signals, old_node);

    // CLEAN UP rom src node
    cleanup_block_memory_old_node(old_node);
    vtr::free(signals);
}

/**
 * (function: create_rw_single_port_ram)
 * 
 * @brief creates a single port ram for a block memory 
 * with one read and one write port that has the addr 
 * for both read and write
 * 
 * @param bram pointing to a bram node node
 * @param netlist pointer to the current netlist file
 */
static void create_rw_single_port_ram(block_memory* bram, netlist_t* /* netlist */) {
    int i;
    nnode_t* old_node = bram->node;
    int num_rd_ports = old_node->attributes->RD_PORTS;
    int num_wr_ports = old_node->attributes->WR_PORTS;

    /* should have been resovled before this function */
    oassert(num_rd_ports == 1);
    oassert(num_wr_ports == 1);

    /* single port ram signals */
    sp_ram_signals* signals = (sp_ram_signals*)vtr::calloc(1, sizeof(dp_ram_signals));

    /* the wr addr will be deleted since we do not need it anymore */
    for (i = 0; i < bram->write_addr->count; i++) {
        npin_t* wr_addr_pin = bram->write_addr->pins[i];
        /* delete pin */
        delete_npin(wr_addr_pin);
    }

    /* INPUTS */
    /* adding the read addr input port as address1 */
    signals->addr = bram->read_addr;

    /** 
     * handling multiple clock signals 
     * using a clk resulted fbram ORing all read clocks
    */
    /* handling multiple clock signals */
    /* merge all read and write clock to a single clock pin */
    signals->clk = bram->clk->pins[0];

    /**
     * we pad the data_in port using pad pins 
     * bram->write_data is already filled with pad pins
    */
    signals->data = bram->write_data;

    /* the rd enables will be deleted since we do not need it anymore */
    for (i = 0; i < bram->read_en->count; i++) {
        npin_t* rd_en_pin = bram->read_en->pins[i];
        /* delete pin */
        delete_npin(rd_en_pin);
    }

    /* merge all enables */
    if (bram->write_en->count > 1) {
        /* need to OR all write enable since we1 should be one bit in single port ram */
        // bram->write_en = make_chain(LOGICAL_OR, bram->write_en, old_node);
        for (i = 1; i < bram->write_en->count; i++) {
            delete_npin(bram->write_en->pins[i]);
        }
    }
    signals->we = bram->write_en->pins[0];

    /* OUTPUT */
    /* adding the read data port as output1 */
    signals->out = bram->read_data;

    create_single_port_ram(signals, old_node);

    // CLEAN UP bram src node
    cleanup_block_memory_old_node(old_node);
    vtr::free(signals);
}

/**
 * (function: create_rw_dual_port_ram)
 * 
 * @brief block_ram will be considered as dual port ram
 * this function reorders inputs and hook pad pins into datain2 
 * it also leaves the second output of the dual port ram unconnected
 * 
 * @param bram pointing to a block memory
 * @param netlist pointer to the current netlist file
 */
static void create_rw_dual_port_ram(block_memory* bram, netlist_t* netlist) {
    int i;
    nnode_t* old_node = bram->node;
    int num_rd_ports = old_node->attributes->RD_PORTS;
    int num_wr_ports = old_node->attributes->WR_PORTS;

    /* should have been resovled before this function */
    oassert(num_rd_ports == 1);
    oassert(num_wr_ports == 1);

    /* dual port ram signals */
    dp_ram_signals* signals = (dp_ram_signals*)vtr::calloc(1, sizeof(dp_ram_signals));

    /* equalize the addr width with max */
    int max_addr_width = std::max(bram->read_addr->count, bram->write_addr->count);

    /* INPUTS */
    /* adding the read addr input port as address1 */
    int read_addr_size = bram->read_addr->count;
    for (i = 0; i < max_addr_width; i++) {
        if (i < read_addr_size) {
            npin_t* pin = bram->read_addr->pins[i];
            /* in case of padding, pins have not been remapped, need to detach them from the BRAM node */
            pin->node->input_pins[pin->pin_node_idx] = NULL;
        } else {
            add_pin_to_signal_list(bram->read_addr, get_pad_pin(netlist));
        }
    }
    signals->addr1 = bram->read_addr;

    /* adding the write addr port as address2 */
    int write_addr_size = bram->write_addr->count;
    for (i = 0; i < max_addr_width; i++) {
        if (i < write_addr_size) {
            npin_t* pin = bram->write_addr->pins[i];
            /* in case of padding, pins have not been remapped, need to detach them from the BRAM node */
            pin->node->input_pins[pin->pin_node_idx] = NULL;
        } else {
            add_pin_to_signal_list(bram->write_addr, get_pad_pin(netlist));
        }
    }
    signals->addr2 = bram->write_addr;

    /** 
     * handling multiple clock signals 
     * using a clk resulted from merging all read and write clocks
    */
    signals->clk = bram->clk->pins[0];

    /* adding the write data port as data1 */
    // remap_input_port_to_block_memory(bram, bram->write_data, "data1");
    signals->data2 = bram->write_data;

    /* we pad the second data port using pad pins */
    signal_list_t* pad_signals = init_signal_list();
    for (i = 0; i < bram->write_data->count; i++) {
        add_pin_to_signal_list(pad_signals, get_pad_pin(netlist));
    }
    signals->data1 = pad_signals;

    for (i = 0; i < bram->read_en->count; i++) {
        /* delete all read enable pins, since no need to write from addr1 */
        delete_npin(bram->read_en->pins[0]);
    }
    signals->we1 = get_zero_pin(netlist);

    if (bram->write_en->count > 1) {
        /* need to OR all write enable since we2 should be one bit in dual port ram */
        // bram->write_en = make_chain(LOGICAL_OR, bram->write_en, old_node);
        for (i = 1; i < bram->write_en->count; i++) {
            delete_npin(bram->write_en->pins[i]);
        }
    }
    signals->we2 = bram->write_en->pins[0];

    /* OUTPUT */
    /* adding the read data port as output1 */
    signals->out1 = bram->read_data;

    /* leave second output port unconnected */
    int offset = bram->read_data->count;
    signal_list_t* out2_signals = init_signal_list();
    for (i = 0; i < bram->read_data->count; i++) {
        // specify the output pin
        npin_t* new_pin1 = allocate_npin();
        npin_t* new_pin2 = allocate_npin();
        nnet_t* new_net = allocate_nnet();
        new_net->name = make_full_ref_name(NULL, NULL, NULL, bram->name, offset + i);
        /* hook up new pin 1 into the new net */
        add_driver_pin_to_net(new_net, new_pin1);
        /* hook up the new pin 2 to this new net */
        add_fanout_pin_to_net(new_net, new_pin2);

        /* adding to signal list */
        add_pin_to_signal_list(out2_signals, new_pin1);
    }
    signals->out2 = out2_signals;

    create_dual_port_ram(signals, old_node);

    // CLEAN UP
    cleanup_block_memory_old_node(old_node);
    free_signal_list(pad_signals);
    free_signal_list(out2_signals);
    vtr::free(signals);
}

/**
 * (function: create_2r_dual_port_ram)
 * 
 * @brief in this case one write port MUST have the 
 * same address of the single read port.
 * Will be instantiated into a DPRAM.
 * 
 * @param bram pointer to the block memory
 * @param netlist pointer to the current netlist file
 */
static void create_2r_dual_port_ram(block_memory* rom, netlist_t* netlist) {
    nnode_t* old_node = rom->node;

    int i, offset;
    int data_width = rom->node->attributes->DBITS;
    int addr_width = rom->node->attributes->ABITS;
    int num_rd_ports = old_node->attributes->RD_PORTS;
    int num_wr_ports = old_node->attributes->WR_PORTS;

    /* should have been resovled before this function */
    oassert(num_rd_ports == 2);
    oassert(num_wr_ports == 0);
    oassert(rom->read_addr->count == 2 * addr_width);
    oassert(rom->read_data->count == 2 * data_width);

    /* create a list of dpram ram signals */
    dp_ram_signals* signals = (dp_ram_signals*)vtr::malloc(sizeof(dp_ram_signals));

    /* split read addr and add the first half to the addr1 */
    signals->addr1 = init_signal_list();
    for (i = 0; i < addr_width; i++) {
        add_pin_to_signal_list(signals->addr1, rom->read_addr->pins[i]);
    }

    /* add pad pins as data1 */
    signals->data1 = init_signal_list();
    for (i = 0; i < data_width; i++) {
        add_pin_to_signal_list(signals->data1, get_pad_pin(netlist));
    }

    /* there is no write data to set any we, so it will be connected to GND */
    signals->we1 = get_zero_pin(netlist);
    delete_npin(rom->read_en->pins[0]);

    /* add merged clk signal as dpram clk signal */
    signals->clk = rom->clk->pins[0];
    /* delete read clks since there is no need of them anymore */
    for (i = 0; i < rom->read_clk->count; i++) {
        delete_npin(rom->read_clk->pins[i]);
    }

    /* split read data and add the first half to the out1 */
    signals->out1 = init_signal_list();
    for (i = 0; i < data_width; i++) {
        add_pin_to_signal_list(signals->out1, rom->read_data->pins[i]);
    }

    /* add the second half of the read addr to addr2 */
    offset = addr_width;
    signals->addr2 = init_signal_list();
    for (i = 0; i < addr_width; i++) {
        add_pin_to_signal_list(signals->addr2, rom->read_addr->pins[offset + i]);
    }

    /* there is no write data to set any we, so it will be connected to GND */
    signals->we2 = get_zero_pin(netlist);
    delete_npin(rom->read_en->pins[1]);

    /* add the second half of the write data to data2 */
    signals->data2 = init_signal_list();
    for (i = 0; i < data_width; i++) {
        add_pin_to_signal_list(signals->data2, get_pad_pin(netlist));
    }

    /* add the second half of the read data to out2 */
    offset = data_width;
    signals->out2 = init_signal_list();
    for (i = 0; i < data_width; i++) {
        add_pin_to_signal_list(signals->out2, rom->read_data->pins[offset + i]);
    }

    /* create a new dual port ram */
    create_dual_port_ram(signals, old_node);

    // CLEAN UP
    cleanup_block_memory_old_node(old_node);
    free_dp_ram_signals(signals);
}

/**
 * (function: create_dual_port_ram_signals)
 * 
 * @brief in this case one write port MUST have the 
 * same address of the single read port.
 * Will be instantiated into a DPRAM.
 * 
 * @param bram pointer to the block memory
 * @param netlist pointer to the current netlist file
 */
static void create_r2w_dual_port_ram(block_memory* bram, netlist_t* netlist) {
    nnode_t* old_node = bram->node;

    int i, offset;
    int data_width = bram->node->attributes->DBITS;
    int addr_width = bram->node->attributes->ABITS;
    int num_rd_ports = old_node->attributes->RD_PORTS;
    int num_wr_ports = old_node->attributes->WR_PORTS;

    /* should have been resovled before this function */
    oassert(num_rd_ports == 1);
    oassert(num_wr_ports == 2);

    /* create a list of dpram ram signals */
    dp_ram_signals* signals = (dp_ram_signals*)vtr::malloc(sizeof(dp_ram_signals));

    /* add read address as addr1 to dpram signal lists */
    signals->addr1 = init_signal_list();
    for (i = 0; i < bram->read_addr->count; i++) {
        add_pin_to_signal_list(signals->addr1, bram->read_addr->pins[i]);
    }

    /* split wr_addr, wr_data and wr_en ports */
    offset = addr_width;
    signal_list_t* wr_addr1 = init_signal_list();
    signal_list_t* wr_addr2 = init_signal_list();
    for (i = 0; i < addr_width; i++) {
        add_pin_to_signal_list(wr_addr1, bram->write_addr->pins[i]);
        add_pin_to_signal_list(wr_addr2, bram->write_addr->pins[i + offset]);
    }

    offset = data_width;
    signal_list_t* wr_data1 = init_signal_list();
    signal_list_t* wr_data2 = init_signal_list();
    signal_list_t* wr_en1 = init_signal_list();
    signal_list_t* wr_en2 = init_signal_list();
    for (i = 0; i < data_width; i++) {
        add_pin_to_signal_list(wr_data1, bram->write_data->pins[i]);
        add_pin_to_signal_list(wr_data2, bram->write_data->pins[i + offset]);
        add_pin_to_signal_list(wr_en1, bram->write_en->pins[i]);
        add_pin_to_signal_list(wr_en2, bram->write_en->pins[i + offset]);
    }

    /**
     * [NOTE]:
     * currently, there is a limited support for yosys mem block in Odin.
     * we only assume that the multiple write or read ports are connected 
     * to memory block themselves OR at max by a multiplexer for checking enable
    */
    bool first_match = check_match_ports(wr_addr1, bram->read_addr, netlist);
    bool second_match = check_match_ports(wr_addr2, bram->read_addr, netlist);

    if (!first_match && !second_match) {
        first_match = check_match_ports(bram->read_addr, wr_addr1, netlist);
        second_match = check_match_ports(bram->read_addr, wr_addr2, netlist);
        if (!first_match && !second_match) {
            error_message(NETLIST, bram->loc,
                          "Cannot map memory block(%s) with more than two distinct ports!", old_node->name);
        }
    }

    /**
     * one write address is always equal to read addr.
     * As a result, the corresponding write data will be mapped to data1 
    */
    signals->data1 = (first_match) ? wr_data1 : wr_data2;

    /* free read en since we do not need in DPRAM model */
    delete_npin(bram->read_en->pins[0]);
    /* add write enable signals for matched wr port as we1 */
    signal_list_t* we1 = (first_match) ? wr_en1 : wr_en2;
    // we1 = make_chain(LOGICAL_OR, we1, old_node);
    signals->we1 = we1->pins[0];
    for (i = 1; i < we1->count; i++) {
        delete_npin(we1->pins[i]);
    }

    /* add merged clk signal as dpram clk signal */
    signals->clk = bram->clk->pins[0];

    /* map read data to the out1 */
    signals->out1 = init_signal_list();
    for (i = 0; i < bram->read_data->count; i++) {
        add_pin_to_signal_list(signals->out1, bram->read_data->pins[i]);
    }

    /* OTHER WR RELATED PORTS */
    /* addr2 for another write addr */
    signals->addr2 = (first_match) ? wr_addr2 : wr_addr1;

    /* add write enable signals for first wr port as we1 */
    signal_list_t* we2 = (first_match) ? wr_en2 : wr_en1;
    /* merged enables will be connected to we2 */
    // we2 = make_chain(LOGICAL_OR, we2, old_node);
    signals->we2 = we2->pins[0];
    for (i = 1; i < we2->count; i++) {
        delete_npin(we2->pins[i]);
    }

    /* the rest of write data pin is for data2 */
    signals->data2 = (first_match) ? wr_data2 : wr_data1;

    /* out2 will be unconnected */
    signals->out2 = init_signal_list();
    for (i = 0; i < signals->out1->count; i++) {
        /* create the clk node's output pin */
        npin_t* new_pin1 = allocate_npin();
        npin_t* new_pin2 = allocate_npin();
        nnet_t* new_net = allocate_nnet();
        /* hook up new pin 1 into the new net */
        add_driver_pin_to_net(new_net, new_pin1);
        /* hook up the new pin 2 to this new net */
        add_fanout_pin_to_net(new_net, new_pin2);

        /* hook the output pin into the node */
        add_pin_to_signal_list(signals->out2, new_pin1);
    }

    /* create a new dual port ram */
    create_dual_port_ram(signals, old_node);

    // CLEAN UP
    /* free matched wr addr pins since they are as the same as read addr */
    for (i = 0; i < addr_width; i++) {
        npin_t* pin = (first_match) ? wr_addr1->pins[i] : wr_addr2->pins[i];
        /* delete pin */
        delete_npin(pin);
    }
    free_signal_list((first_match) ? wr_addr1 : wr_addr2);
    free_signal_list(we1);
    free_signal_list(we2);

    cleanup_block_memory_old_node(old_node);
    free_dp_ram_signals(signals);
}

/**
 * (function: create_2rw_dual_port_ram)
 * 
 * @brief creates a dual port ram. The given bram should have
 * a pair of read addr and write addr with the same addrs
 * 
 * @param bram pointer to the block memory
 * @param netlist pointer to the current netlist file
 */
static void create_2rw_dual_port_ram(block_memory* bram, netlist_t* netlist) {
    nnode_t* old_node = bram->node;

    int i, offset;
    int data_width = bram->node->attributes->DBITS;
    int addr_width = bram->node->attributes->ABITS;
    int num_rd_ports = old_node->attributes->RD_PORTS;
    int num_wr_ports = old_node->attributes->WR_PORTS;

    /* should have been resovled before this function */
    oassert(num_rd_ports == 2);
    oassert(num_wr_ports == 1);

    /* create a list of dpram ram signals */
    dp_ram_signals* signals = (dp_ram_signals*)vtr::malloc(sizeof(dp_ram_signals));

    /* add write address as addr1 to dpram signal lists */
    signals->addr1 = init_signal_list();
    for (i = 0; i < bram->write_addr->count; i++) {
        add_pin_to_signal_list(signals->addr1, bram->write_addr->pins[i]);
    }

    /**
     * one write address is always equal to read addr.
     * As a result, the corresponding write data will be mapped to data1 
    */
    signals->data1 = init_signal_list();
    for (i = 0; i < data_width; i++) {
        add_pin_to_signal_list(signals->data1, bram->write_data->pins[i]);
    }

    /* add write enable signals for matched wr port as we1 */
    signal_list_t* we1 = init_signal_list();
    for (i = 0; i < data_width; i++) {
        add_pin_to_signal_list(we1, bram->write_en->pins[i]);
    }
    // we1 = make_chain(LOGICAL_OR, we1, old_node);
    signals->we1 = we1->pins[0];
    for (i = 1; i < we1->count; i++) {
        delete_npin(we1->pins[i]);
    }

    /* split rd_addr, rd_data ports */
    offset = addr_width;
    signal_list_t* rd_addr1 = init_signal_list();
    signal_list_t* rd_addr2 = init_signal_list();
    for (i = 0; i < addr_width; i++) {
        add_pin_to_signal_list(rd_addr1, bram->read_addr->pins[i]);
        add_pin_to_signal_list(rd_addr2, bram->read_addr->pins[i + offset]);
    }

    offset = data_width;
    signal_list_t* rd_data1 = init_signal_list();
    signal_list_t* rd_data2 = init_signal_list();
    for (i = 0; i < data_width; i++) {
        add_pin_to_signal_list(rd_data1, bram->read_data->pins[i]);
        add_pin_to_signal_list(rd_data2, bram->read_data->pins[i + offset]);
    }

    /* delete rd pins since we use corresponding wr_en and zero*/
    for (i = 0; i < bram->read_en->count; i++) {
        delete_npin(bram->read_en->pins[i]);
    }

    /**
     * [NOTE]:
     * currently, there is a limited support for yosys mem block in Odin.
     * we only assume that the multiple write or read ports are connected 
     * to memory block themselves OR at max by a multiplexer for checking enable
    */
    bool first_match = check_match_ports(bram->write_addr, rd_addr1, netlist);
    bool second_match = check_match_ports(bram->write_addr, rd_addr2, netlist);

    if (!first_match && !second_match) {
        first_match = check_match_ports(rd_addr1, bram->write_addr, netlist);
        second_match = check_match_ports(rd_addr2, bram->write_addr, netlist);
        if (!first_match && !second_match) {
            error_message(NETLIST, bram->loc,
                          "Cannot map memory block(%s) with more than two distinct ports!", old_node->name);
        }
    }

    /* map matched read data to the out1 */
    signals->out1 = (first_match) ? rd_data1 : rd_data2;

    /* add merged clk signal as dpram clk signal */
    signals->clk = bram->clk->pins[0];

    /* OTHER WR RELATED PORTS */
    /* addr2 for another write addr */
    signals->addr2 = (first_match) ? rd_addr2 : rd_addr1;

    /* en 2 should always be 0 since there is no second write port */
    signals->we2 = get_zero_pin(netlist);

    /* the rest of write data pin is for data2 */
    signals->data2 = init_signal_list();
    for (i = 0; i < data_width; i++) {
        add_pin_to_signal_list(signals->data2, get_pad_pin(netlist));
    }

    /* out2 map to other read data */
    signals->out2 = (first_match) ? rd_data2 : rd_data1;

    /* create a new dual port ram */
    create_dual_port_ram(signals, old_node);

    // CLEAN UP
    /* free matched rd addr pins since they are as the same as write addr */
    for (i = 0; i < addr_width; i++) {
        npin_t* pin = (first_match) ? rd_addr1->pins[i] : rd_addr2->pins[i];
        /* delete pin */
        delete_npin(pin);
    }
    free_signal_list((first_match) ? rd_addr1 : rd_addr2);
    free_signal_list(we1);

    cleanup_block_memory_old_node(old_node);
    free_dp_ram_signals(signals);
}

/**
 * (function: create_dual_port_ram_signals)
 * 
 * @brief in this case one write port MUST have the 
 * same address of the single read port.
 * Will be instantiated into a DPRAM.
 * 
 * @param bram pointer to the block memory
 * @param netlist pointer to the current netlist file
 */
static void create_2r2w_dual_port_ram(block_memory* bram, netlist_t* netlist) {
    nnode_t* old_node = bram->node;

    int i, offset;
    int data_width = bram->node->attributes->DBITS;
    int addr_width = bram->node->attributes->ABITS;
    int num_rd_ports = old_node->attributes->RD_PORTS;
    int num_wr_ports = old_node->attributes->WR_PORTS;

    /* should have been resovled before this function */
    oassert(num_rd_ports == 2);
    oassert(num_wr_ports == 2);
    oassert(bram->read_addr->count == 2 * addr_width);
    oassert(bram->read_data->count == 2 * data_width);

    /* split wr_addr, wr_data and wr_en ports */
    offset = addr_width;
    signal_list_t* wr_addr1 = init_signal_list();
    signal_list_t* wr_addr2 = init_signal_list();
    for (i = 0; i < addr_width; i++) {
        add_pin_to_signal_list(wr_addr1, bram->write_addr->pins[i]);
        add_pin_to_signal_list(wr_addr2, bram->write_addr->pins[i + offset]);
    }

    offset = data_width;
    signal_list_t* wr_data1 = init_signal_list();
    signal_list_t* wr_data2 = init_signal_list();
    signal_list_t* wr_en1 = init_signal_list();
    signal_list_t* wr_en2 = init_signal_list();
    for (i = 0; i < data_width; i++) {
        add_pin_to_signal_list(wr_data1, bram->write_data->pins[i]);
        add_pin_to_signal_list(wr_data2, bram->write_data->pins[i + offset]);
        add_pin_to_signal_list(wr_en1, bram->write_en->pins[i]);
        add_pin_to_signal_list(wr_en2, bram->write_en->pins[i + offset]);
    }

    /* split rd_addr, rd_data ports */
    offset = addr_width;
    signal_list_t* rd_addr1 = init_signal_list();
    signal_list_t* rd_addr2 = init_signal_list();
    for (i = 0; i < addr_width; i++) {
        add_pin_to_signal_list(rd_addr1, bram->read_addr->pins[i]);
        add_pin_to_signal_list(rd_addr2, bram->read_addr->pins[i + offset]);
    }

    offset = data_width;
    signal_list_t* rd_data1 = init_signal_list();
    signal_list_t* rd_data2 = init_signal_list();
    for (i = 0; i < data_width; i++) {
        add_pin_to_signal_list(rd_data1, bram->read_data->pins[i]);
        add_pin_to_signal_list(rd_data2, bram->read_data->pins[i + offset]);
    }

    /* delete rd pins since we use corresponding wr_en and zero*/
    for (i = 0; i < bram->read_en->count; i++) {
        delete_npin(bram->read_en->pins[i]);
    }

    /* create a list of dpram ram signals */
    dp_ram_signals* signals = (dp_ram_signals*)vtr::malloc(sizeof(dp_ram_signals));

    /* hook the first half os splitted addr into addr1 */
    signals->addr1 = rd_addr1;

    /* hook the second half os splitted addr into addr2 */
    signals->addr2 = rd_addr2;

    /* split read data and add the first half to the out1 */
    signals->out1 = rd_data1;

    /* add the second half of the read data to out2 */
    signals->out2 = rd_data2;

    /**
     * [NOTE]:
     * currently, there is a limited support for yosys mem block in Odin.
     * we only assume that the multiple write or read ports are connected 
     * to memory block themselves OR at max by a multiplexer for checking enable
    */
    bool first_match_read1 = check_match_ports(wr_addr1, rd_addr1, netlist);
    bool second_match_read1 = check_match_ports(wr_addr2, rd_addr1, netlist);
    if (!first_match_read1 && !second_match_read1) {
        first_match_read1 = check_match_ports(rd_addr1, wr_addr1, netlist);
        second_match_read1 = check_match_ports(rd_addr1, wr_addr2, netlist);
    }

    bool first_match_read2 = check_match_ports(wr_addr1, rd_addr2, netlist);
    bool second_match_read2 = check_match_ports(wr_addr2, rd_addr2, netlist);
    if (!first_match_read2 && !second_match_read2) {
        first_match_read2 = check_match_ports(rd_addr2, wr_addr1, netlist);
        second_match_read2 = check_match_ports(rd_addr2, wr_addr2, netlist);
    }

    if (!first_match_read1 && !second_match_read1 && !first_match_read2 && !second_match_read2) {
        error_message(NETLIST, bram->loc,
                      "Cannot map memory block(%s) with more than two distinct ports!", old_node->name);
    }

    /* validate the matching results */
    oassert((first_match_read1 != first_match_read2) || second_match_read1 != second_match_read2);

    /* split write data and add the first half to the data1 */
    signals->data1 = (first_match_read1) ? wr_data1 : wr_data2;

    /* split write data and add the second half to the data2 */
    signals->data2 = (first_match_read2) ? wr_data1 : wr_data2;

    /* add write enable signals for first wr port as we1 */
    // wr_en1 = make_chain(LOGICAL_OR, wr_en1, old_node);
    signals->we1 = wr_en1->pins[0];
    for (i = 1; i < wr_en1->count; i++) {
        delete_npin(wr_en1->pins[i]);
    }

    /* add merged clk signal as dpram clk signal */
    signals->clk = bram->clk->pins[0];

    /* add write enable signals for the second wr port as we2 */
    //wr_en2 = make_chain(LOGICAL_OR, wr_en2, old_node);
    signals->we2 = wr_en2->pins[0];
    for (i = 1; i < wr_en2->count; i++) {
        delete_npin(wr_en2->pins[i]);
    }
    

    /* create a new dual port ram */
    create_dual_port_ram(signals, old_node);

    // CLEAN UP
    /* at this point wr_addr and rd_addr must be the same */
    oassert(bram->read_addr->count == bram->write_addr->count);
    /* the wr addr will be deleted since we do not need it anymore */
    for (i = 0; i < bram->write_addr->count; i++) {
        npin_t* wr_addr_pin = bram->write_addr->pins[i];
        /* delete pin */
        delete_npin(wr_addr_pin);
    }
    free_signal_list(wr_addr1);
    free_signal_list(wr_addr2);
    free_signal_list(wr_en1);
    free_signal_list(wr_en2);

    cleanup_block_memory_old_node(old_node);
    free_dp_ram_signals(signals);
}

/**
 * (function: map_rom_to_mem_hardblocks)
 * 
 * @brief split the bram in width of input data if exceeded fromthe width of the output
 * 
 * @param rom pointer to the read only memory
 * @param netlist pointer to the current netlist file
 */
void map_rom_to_mem_hardblocks(block_memory* rom, netlist_t* netlist) {
    nnode_t* node = rom->node;

    int width = node->attributes->DBITS;
    int depth = shift_left_value_with_overflow_check(0X1, node->attributes->ABITS, rom->loc);

    int rd_ports = node->attributes->RD_PORTS;
    int wr_ports = node->attributes->WR_PORTS;

    /* Read Only Memory validateion */
    oassert(wr_ports == 0);

    int rom_relative_area = depth * width;
    t_model* lutram_model = find_hard_block(LUTRAM_string);

    if (lutram_model != NULL
        && (LUTRAM_MIN_THRESHOLD_AREA <= rom_relative_area)
        && (rom_relative_area <= LUTRAM_MAX_THRESHOLD_AREA)) {
        /* map to LUTRAM */
        // nnode_t* lutram = NULL;
        /* TODO */
    } else {
        /* need to split the rom from the data width */
        if (rd_ports == 1) {
            /* create the ROM and allocate ports according to the SPRAM hard block */
            create_r_single_port_ram(rom, netlist);

        } else if (rd_ports == 2) {
            /* create the ROM and allocate ports according to the DPRAM hard block */
            create_2r_dual_port_ram(rom, netlist);

        } else {
            /* shoudln't happen since this should be only for read only memories */
            error_message(NETLIST, rom->loc,
                          "Cannot hook multiple write ports into a Read-Only Memory (%s)", rom->name);
        }
    }
}

/**
 * (function: map_bram_to_mem_hardblocks)
 * 
 * @brief split the bram in width of input data if exceeded from the width of the output
 * 
 * @param bram pointer to the block memory
 * @param netlist pointer to the current netlist file
 */
void map_bram_to_mem_hardblocks(block_memory* bram, netlist_t* netlist) {
    nnode_t* node = bram->node;

    int width = node->attributes->DBITS;
    int depth = shift_left_value_with_overflow_check(0X1, node->attributes->ABITS, bram->loc);

    /* since the data1_w + data2_w == data_out for DPRAM */
    long rd_ports = node->attributes->RD_PORTS;
    long wr_ports = node->attributes->WR_PORTS;

    /**
     * Potential place for checking block ram if their relative 
     * size is less than a threshold, they could be mapped on LUTRAM
     *
     */

    int bram_relative_area = depth * width;
    t_model* lutram_model = find_hard_block(LUTRAM_string);

    if (lutram_model != NULL
        && (LUTRAM_MIN_THRESHOLD_AREA <= bram_relative_area)
        && (bram_relative_area <= LUTRAM_MAX_THRESHOLD_AREA)) {
        /* map to LUTRAM */
        // nnode_t* lutram = NULL;
        /* TODO */
    } else {
        if (wr_ports == (rd_ports == 1)) {
            if (check_same_addrs(bram)) {
                /* create a single port ram and allocate ports according to the SPRAM hard block */
                create_rw_single_port_ram(bram, netlist);

            } else {
                /* create a dual port ram and allocate ports according to the DPRAM hard block */
                create_rw_dual_port_ram(bram, netlist);
            }

        } else if (rd_ports == 1 && wr_ports == 2) {
            /* create a dual port ram and allocate ports according to the DPRAM hard block */
            create_r2w_dual_port_ram(bram, netlist);

        } else if (rd_ports == 2 && wr_ports == 1) {
            /* create a dual port ram and allocate ports according to the DPRAM hard block */
            create_2rw_dual_port_ram(bram, netlist);

        } else if (rd_ports == 2 && wr_ports == 2) {
            /* create a dual port ram and allocate ports according to the DPRAM hard block */
            create_2r2w_dual_port_ram(bram, netlist);

        } else {
            error_message(NETLIST, node->loc,
                          "Invalid memory block (%s)! Odin does not support Memories with more than two write/read ports\n", node->attributes->memory_id);
        }
    }
}
/**
 * (function: resolve_bram_node)
 * 
 * @brief create, verify and shrink the bram node
 * 
 * @param node pointing to a bram node
 * @param traverse_mark_number unique traversal mark for blif elaboration pass
 * @param netlist pointer to the current netlist file
 */
void resolve_bram_node(nnode_t* node, uintptr_t traverse_mark_number, netlist_t* netlist) {
    oassert(node->traverse_visited == traverse_mark_number);

    /* validate bram port sizes */
    oassert(node->num_input_port_sizes == 7);
    oassert(node->num_output_port_sizes == 1);

    /* initializing a new block ram */
    block_memory* bram = init_block_memory(node, netlist);

    /* perform optimization on memory inference */
    perform_optimization(bram);

    block_memory_list = insert_in_vptr_list(block_memory_list, bram);
}

/**
 * (function: resolve_rom_node)
 * 
 * @brief read_only_memory will be considered as single port ram
 * this function reorders inputs and add data_in input to new node
 * which is connected to pad node
 * 
 * @param node pointing to a rom node node
 * @param traverse_mark_number unique traversal mark for blif elaboration pass
 * @param netlist pointer to the current netlist file
 */
void resolve_rom_node(nnode_t* node, uintptr_t traverse_mark_number, netlist_t* netlist) {
    oassert(node->traverse_visited == traverse_mark_number);

    /* validate port sizes */
    oassert(node->num_input_port_sizes == 3);
    oassert(node->num_output_port_sizes == 1);

    /* create the BRAM and allocate ports according to the DPRAM hard block */
    block_memory* rom = init_read_only_memory(node, netlist);

    /* perform optimization on memory inference */
    perform_optimization(rom);

    read_only_memory_list = insert_in_vptr_list(read_only_memory_list, rom);
}

/**
 * (function: resolve_bram_node)
 * 
 * @brief create, verify and shrink the bram node
 * 
 * @param netlist pointer to the current netlist file
 */
void iterate_block_memories(netlist_t* netlist) {
    t_linked_vptr* ptr = block_memory_list;
    while (ptr != NULL) {
        block_memory* bram = (block_memory*)ptr->data_vptr;

        /* validation */
        oassert(bram != NULL);

        map_bram_to_mem_hardblocks(bram, netlist);
        ptr = ptr->next;
    }

    ptr = read_only_memory_list;
    while (ptr != NULL) {
        block_memory* rom = (block_memory*)ptr->data_vptr;

        /* validation */
        oassert(rom != NULL);

        map_rom_to_mem_hardblocks(rom, netlist);
        ptr = ptr->next;
    }
}

/**
 * (function: create_dual_port_ram_signals)
 * 
 * @brief this function is to check if the read addr
 *  and write addr is the same or not.
 * 
 * @param bram pointer to the block memory
 * 
 * @return read_addr == write_addr
 */
static bool check_same_addrs(block_memory* bram) {
    int read_addr_width = bram->read_addr->count;
    int write_addr_width = bram->write_addr->count;

    /* first check is the width, if they have not the equal width return false */
    if (read_addr_width != write_addr_width) {
        return (false);
    }
    /* if they have the equal width, here is to check their driver */
    else {
        int i;
        int width = read_addr_width;
        for (i = 0; i < width; i++) {
            npin_t* read_addr_pin = bram->read_addr->pins[i];
            npin_t* write_addr_pin = bram->write_addr->pins[i];

            if (read_addr_pin->net != write_addr_pin->net)
                return (false);
        }
    }

    return (true);
}

/**
 * (function: perform_optimization)
 * 
 * @brief this function is to perform optimization on block 
 * memories and roms. Optimization includes address width 
 * reduction base on the mem size
 * 
 * @param bram pointer to the block memory
 */
static void perform_optimization(block_memory* memory) {
    nnode_t* node = memory->node;
    int depth = node->attributes->size;
    int addr_width = node->attributes->ABITS;
    int rd_ports = node->attributes->RD_PORTS;
    int wr_ports = node->attributes->WR_PORTS;

    int needed_addr_width = 0;
    long shifted_value = 0;
    /* calculate the needed address width */
    while (shifted_value < depth) {
        needed_addr_width++;
        shifted_value = shift_left_value_with_overflow_check(0X1, needed_addr_width, node->loc);
    }

    /**
     * [NOTE]: At this point there is a need to take care of multiple read 
     * or write address pins (like dual port ram) since in the case of having
     * arithmetic operation in address pins (cause to extend addr to 32 bits), 
     * yosys would NOT handle it before creting the mem subcircuit.
     * e.g.:
     * 
     *      wire [3:0] read_addr;
     *      read_data <= mem[read_addr / 3] 
     *  
     * the signal representing the read addr signal in $mem subcircuit is 32 bits. 
     * However, we only need four bits
     */

    /* check if block memory has a read addr */
    if (memory->read_addr) {
        /* prune read address to reduce its width based on the needed addr width if needed */
        memory->read_addr = prune_signal(memory->read_addr, addr_width, needed_addr_width, rd_ports);
    }

    /* check if block memory has a write addr */
    if (memory->write_addr) {
        /* prune write address to reduce its width based on the needed addr width if needed */
        memory->write_addr = prune_signal(memory->write_addr, addr_width, needed_addr_width, wr_ports);
    }

    /* update new read addr width */
    node->attributes->ABITS = needed_addr_width;
}

/**
 * (function: create_single_clk_pin)
 * 
 * @brief ORing all given clks
 * 
 * @param clks signal list of clocks
 * @param node pointing to a bram node
 * @param netlist pointer to the current netlist file
 * 
 * @return single clock pin
 */
static signal_list_t* create_single_clk_pin(signal_list_t* clks, nnode_t* node, netlist_t* netlist) {
    signal_list_t* return_signal = NULL;

    /* or all clks */
    signal_list_t* clk_chain = make_chain(LOGICAL_OR, clks, node);

    /* create a new clk node */
    nnode_t* nor_gate = make_2port_gate(LOGICAL_NOR, 1, 1, 1, node, node->traverse_visited);
    /* add related inputs */
    add_input_pin_to_node(nor_gate, get_zero_pin(netlist), 0);
    add_input_pin_to_node(nor_gate, clk_chain->pins[0], 1);
    /* creating output signal */
    signal_list_t* not_clk = make_output_pins_for_existing_node(nor_gate, 1);

    /* creating a single input/output node */
    nnode_t* clk_node = make_not_gate(nor_gate, node->traverse_visited);
    connect_nodes(nor_gate, 0, clk_node, 0);
    return_signal = make_output_pins_for_existing_node(clk_node, 1);

    // CLEAN UP
    free_signal_list(clk_chain);
    free_signal_list(not_clk);

    /* adding the new clk node to netlist clocks */
    netlist->clocks = (nnode_t**)vtr::realloc(netlist->clocks, sizeof(nnode_t*) * (netlist->num_clocks + 1));
    netlist->clocks[netlist->num_clocks] = clk_node;
    netlist->num_clocks++;

    return (return_signal);
}

/**
 * (function: merge_read_write_clks)
 * 
 * @brief create a OR gate with read and write clocks as input
 * 
 * @param rd_clks signal list of read clocks
 * @param wr_clks signal list of write clocks
 * @param node pointing to a bram node
 * @param netlist pointer to the current netlist file
 * 
 * @return single clock pin
 */
static signal_list_t* merge_read_write_clks(signal_list_t* rd_clks, signal_list_t* wr_clks, nnode_t* node, netlist_t* /* netlist */) {
    int i;
    int rd_clks_width = rd_clks->count;
    int wr_clks_width = wr_clks->count;

    /* adding the OR of rd_clk and wr_clk as clock port */
    nnode_t* nor_gate = allocate_nnode(node->loc);
    nor_gate->traverse_visited = node->traverse_visited;
    nor_gate->type = LOGICAL_OR;
    nor_gate->name = node_name(nor_gate, node->name);

    add_input_port_information(nor_gate, rd_clks_width);
    allocate_more_input_pins(nor_gate, rd_clks_width);
    /* hook input pins */
    for (i = 0; i < rd_clks_width; i++) {
        npin_t* rd_clk = rd_clks->pins[i];
        remap_pin_to_new_node(rd_clk, nor_gate, i);
    }
    add_input_port_information(nor_gate, wr_clks_width);
    allocate_more_input_pins(nor_gate, wr_clks_width);
    /* hook input pins */
    for (i = 0; i < wr_clks_width; i++) {
        npin_t* wr_clk = wr_clks->pins[i];
        remap_pin_to_new_node(wr_clk, nor_gate, rd_clks_width + i);
    }

    /* add nor gate output information */
    add_output_port_information(nor_gate, 1);
    allocate_more_output_pins(nor_gate, 1);
    // specify the output pin
    npin_t* nor_gate_new_pin1 = allocate_npin();
    npin_t* nor_gate_new_pin2 = allocate_npin();
    nnet_t* nor_gate_new_net = allocate_nnet();
    nor_gate_new_net->name = make_full_ref_name(NULL, NULL, NULL, nor_gate->name, 0);
    /* hook the output pin into the node */
    add_output_pin_to_node(nor_gate, nor_gate_new_pin1, 0);
    /* hook up new pin 1 into the new net */
    add_driver_pin_to_net(nor_gate_new_net, nor_gate_new_pin1);
    /* hook up the new pin 2 to this new net */
    add_fanout_pin_to_net(nor_gate_new_net, nor_gate_new_pin2);

    /* create a clock node to specify the edge sensitivity */
    nnode_t* clk_node = make_not_gate(nor_gate, node->traverse_visited);
    connect_nodes(nor_gate, 0, clk_node, 0); //make_1port_gate(CLOCK_NODE, 1, 1, node, traverse_mark_number);
    /* create the clk node's output pin */
    npin_t* clk_node_new_pin1 = allocate_npin();
    npin_t* clk_node_new_pin2 = allocate_npin();
    nnet_t* clk_node_new_net = allocate_nnet();
    clk_node_new_net->name = make_full_ref_name(NULL, NULL, NULL, clk_node->name, 0);
    /* hook the output pin into the node */
    add_output_pin_to_node(clk_node, clk_node_new_pin1, 0);
    /* hook up new pin 1 into the new net */
    add_driver_pin_to_net(clk_node_new_net, clk_node_new_pin1);
    /* hook up the new pin 2 to this new net */
    add_fanout_pin_to_net(clk_node_new_net, clk_node_new_pin2);
    clk_node_new_pin2->mapping = vtr::strdup("clk");

    signal_list_t* return_signal = init_signal_list();
    add_pin_to_signal_list(return_signal, clk_node_new_pin2);

    return (return_signal);
}

/**
 * (function: check_match_ports)
 * 
 * @brief to check if sig1 is the same as be_checked 
 * or is connected to src source hierarchically
 * 
 * NOTE: this is a TEMPORARY solution, 
 * the code is not acceptible professionally
 * 
 * @param sig first signals
 * @param be_checked second signals
 * @param netlist to the netlist second signals
 */
static bool check_match_ports(signal_list_t* sig, signal_list_t* be_checked, netlist_t* netlist) {
    npin_t* possible_rd_pin1 = sig->pins[0];
    nnode_t* en_mux_node = sig->pins[0]->net->driver_pins[0]->node;
    npin_t* possible_rd_pin2 = (en_mux_node && en_mux_node->input_pins) ? en_mux_node->input_pins[1] : NULL;
    npin_t* possible_rd_pin3 = (en_mux_node && en_mux_node->input_pins) ? en_mux_node->input_pins[1 + en_mux_node->input_port_sizes[1]] : NULL;

    /* [TODO] CANNOT MAP MORE THAN TWO PORTS, DPRAM IS LIMITED */
    if (!strcmp(possible_rd_pin1->name, be_checked->pins[0]->name)) {
        return (true);
    } else {
        if (en_mux_node) {
            if (possible_rd_pin2 && !strcmp(possible_rd_pin2->name, be_checked->pins[0]->name)) {
                return (true);
            }
            if (possible_rd_pin3 && !strcmp(possible_rd_pin3->name, be_checked->pins[0]->name)) {
                return (true);
            }
        }
    }

    if (!strcmp(netlist->pad_node->name, be_checked->pins[0]->name))
        return(true);

    return (false);
}

/**
 * (function: detach_pins_from_old_node)
 * 
 * @brief Frees memory used for indexing block memories.
 * 
 * @param node pointer to the old node
 */
static void cleanup_block_memory_old_node(nnode_t* old_node) {
    int i;
    for (i = 0; i < old_node->num_input_pins; i++) {
        npin_t* pin = old_node->input_pins[i];

        if (pin)
            old_node->input_pins[i] = NULL;
    }

    for (i = 0; i < old_node->num_output_pins; i++) {
        npin_t* pin = old_node->output_pins[i];

        if (pin)
            old_node->output_pins[i] = NULL;
    }

    /* clean up */
    free_nnode(old_node);
}

/**
 * (function: free_block_memory_indices)
 * 
 * @brief Frees memory used for indexing block memories.
 */
void free_block_memories() {
    /* check if any block memory indexed */
    if (block_memory_list) {
        free_block_memory_index(block_memories);
        while (block_memory_list != NULL)
            block_memory_list = delete_in_vptr_list(block_memory_list);
    }
    /* check if any read only memory indexed */
    if (read_only_memory_list) {
        free_block_memory_index(read_only_memories);
        while (read_only_memory_list != NULL)
            read_only_memory_list = delete_in_vptr_list(read_only_memory_list);
    }
}

/**
 * (function: free_block_memory_index_and_finalize_memories)
 * 
 * @brief Frees memory used for indexing block memories. Finalises each
 * memory, making sure it has the right ports, and collapsing
 * the memory if possible.
 */
void free_block_memory_index(block_memory_hashtable to_free) {
    if (!to_free.empty()) {
        for (auto mem_it : to_free) {
            free_block_memory(mem_it.second);
        }
    }
    to_free.clear();
}

/**
 * (function: free_block_memory_index_and_finalize_memories)
 * 
 * @brief Frees memory used for indexing block memories. Finalises each
 * memory, making sure it has the right ports, and collapsing
 * the memory if possible.
 */
static void free_block_memory(block_memory* to_free) {
    free_signal_list(to_free->read_addr);
    free_signal_list(to_free->read_clk);
    free_signal_list(to_free->read_data);
    free_signal_list(to_free->read_en);
    free_signal_list(to_free->write_addr);
    free_signal_list(to_free->write_clk);
    free_signal_list(to_free->write_data);
    free_signal_list(to_free->write_en);
    free_signal_list(to_free->clk);

    vtr::free(to_free->name);
    vtr::free(to_free->memory_id);

    vtr::free(to_free);
}
